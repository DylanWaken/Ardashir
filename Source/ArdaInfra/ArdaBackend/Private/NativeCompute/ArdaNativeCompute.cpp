#include "NativeCompute/ArdaNativeCompute.h"
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_set.h>
#include <cstring>
#include <exception>
#include <mutex>

namespace arda
{
#define ARDA_LINKED_NATIVE_COMPUTE_PROVIDER(Function) FArdaRHIStatus Function(FArdaNativeComputeRegistry&);
#include "ArdaLinkedNativeComputeProviders.inl"
#undef ARDA_LINKED_NATIVE_COMPUTE_PROVIDER

	struct FArdaNativeDeviceProviders
	{
		struct FArdaEntry
		{
			eastl::shared_ptr<const IArdaNativeComputeProvider> mProvider;
			std::once_flag mProbed;
			TArdaRHIResult<FArdaNativeProviderSupport> mSupport;
		};
		eastl::unordered_map<const IArdaNativeComputeProvider*, eastl::shared_ptr<FArdaEntry>> mEntries;
	};

	namespace
	{
		FArdaRHIStatus Invalid(const char* Reason)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Reason);
		}

		FArdaRHIStatus Unsupported(const char* Reason)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Reason);
		}

		bool HasNames(const eastl::vector<eastl::string>& Names)
		{
			eastl::unordered_set<eastl::string> Seen;
			return !Names.empty() &&
			    eastl::all_of(Names.begin(),
			        Names.end(),
			        [&](const auto& Name)
			        {
				        return !Name.empty() && Seen.insert(Name).second;
			        });
		}

		bool SameOperandContract(const FArdaNativeOperand& A, const FArdaNativeOperand& B)
		{
			if (A.mSignature != B.mSignature || A.mParameterSize != B.mParameterSize ||
			    A.mResources.size() != B.mResources.size()) return false;
			for (size_t I = 0; I < A.mResources.size(); ++I)
			{
				const auto& First = A.mResources[I];
				const auto& Second = B.mResources[I];
				if (First.mName != Second.mName || First.mType != Second.mType ||
				    First.mAccess != Second.mAccess || First.mFormat != Second.mFormat) return false;
			}
			return true;
		}

		FArdaRHIStatus ValidateInvocation(const FArdaNativeOperand& Operand,
		    const FArdaNativeInvocation& Invocation, const char* NativeProvider = nullptr)
		{
			if (Invocation.mResources.size() != Operand.mResources.size() ||
			    Invocation.mParameters.size() != Operand.mParameterSize)
			{
				return Invalid("The resource count or parameter byte count differs from the registered operand.");
			}
			for (size_t I = 0; I < Operand.mResources.size(); ++I)
			{
				const auto& Slot = Operand.mResources[I];
				const auto& View = Invocation.mResources[I];
				if (NativeProvider && Slot.mType == EArdaRHIResourceType::Count)
				{
					const auto& Resource = View.mNativeResource;
					if (!Resource || !Resource->GetProviderId() || !Resource->GetCategoryId() ||
					    std::strcmp(NativeProvider, Resource->GetProviderId()) || Slot.mCategory != Resource->GetCategoryId())
						return Invalid("Converted native resource does not match its provider and category.");
					continue;
				}
				if (!View.mResource || Slot.mType != View.mResource->GetResourceType())
				{
					return {EArdaRHIResult::InvalidArgument, Slot.mName + ": missing resource or incorrect kind."};
				}
				if (Slot.mType == EArdaRHIResourceType::Buffer)
				{
					const auto* Buffer = dynamic_cast<const IArdaRHIBuffer*>(View.mResource.Get());
					const auto& Range = View.mBufferRange;
					if (!Buffer || Range.mByteOffset >= Buffer->GetDesc().mByteSize || !Range.mByteSize ||
					    (Range.mByteSize != ArdaRHIWholeBuffer &&
					        Range.mByteSize > Buffer->GetDesc().mByteSize - Range.mByteOffset))
					{
						return {EArdaRHIResult::InvalidArgument, Slot.mName + ": buffer range is out of bounds."};
					}
				}
				if (Slot.mType == EArdaRHIResourceType::Texture)
				{
					const auto* Texture = dynamic_cast<const IArdaRHITexture*>(View.mResource.Get());
					if (!Texture || View.mMipLevel >= Texture->GetDesc().mMipLevels ||
					    (Slot.mFormat != EArdaRHIFormat::Unknown && Slot.mFormat != Texture->GetDesc().mFormat))
					{
						return {EArdaRHIResult::InvalidArgument,
						    Slot.mName + ": texture format or mip does not match the slot."};
					}
				}
			}
			return !NativeProvider && Operand.mValidate ? Operand.mValidate(Invocation) : FArdaRHIStatus{};
		}

		class FArdaPackagedKernel final : public IArdaNativeKernelSource
		{
		public:
			FArdaPackagedKernel(FArdaNativeKernelKey Key, FArdaNativeKernelLayout Layout)
			    : mKey(eastl::move(Key)), mLayout(eastl::move(Layout)) {}
			const char* GetProviderId() const noexcept override { return mKey.mProvider.c_str(); }
			const char* GetFormat() const noexcept override { return "native-kernel-library"; }
			const FArdaNativeKernelKey& GetKey() const { return mKey; }

			TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> Resolve(
			    const eastl::shared_ptr<const IArdaNativeComputeProvider>& Provider) const
			{
				std::lock_guard Lock(mMutex);
				if (mProvider.lock() == Provider && mSource) return {mSource, {}};
				auto Binary = FindArdaNativeKernelBinary(mKey);
				if (!Binary)
				{
					if (Binary.mStatus.mCode == EArdaRHIResult::InvalidArgument) Binary.mStatus.mCode = EArdaRHIResult::BackendFailure;
					return {{}, eastl::move(Binary.mStatus)};
				}
				auto Loaded = Provider->LoadKernel(*Binary.mValue, mLayout);
				if (!Loaded && Loaded.mStatus.mCode == EArdaRHIResult::InvalidArgument)
					Loaded.mStatus.mCode = EArdaRHIResult::BackendFailure;
				if (Loaded && !Loaded.mValue) return {{}, Invalid("Provider returned an empty kernel.")};
				if (Loaded)
				{
					mProvider = Provider;
					mSource = Loaded.mValue;
				}
				return Loaded;
			}
		private:
			FArdaNativeKernelKey mKey;
			FArdaNativeKernelLayout mLayout;
			mutable std::mutex mMutex;
			mutable eastl::weak_ptr<const IArdaNativeComputeProvider> mProvider;
			mutable eastl::shared_ptr<const IArdaNativeKernelSource> mSource;
		};

		struct FArdaDirectResourceConversion
		{
			FArdaRHIStatus operator()(const FArdaNativeConversionContext&, FArdaNativeConversion&) const { return {}; }
		};

		void RestoreConversions(FArdaNativeOperand& Local, const FArdaNativeOperand& Registered)
		{
			if (Registered.mGraphicsToNative && !HasArdaNativeResourceConversions(Local))
			{
				Local.mGraphicsToNative = Registered.mGraphicsToNative;
				Local.mNativeToGraphics = Registered.mNativeToGraphics;
			}
			for (auto& Variant : Local.mVariants)
			{
				const auto* Archived = dynamic_cast<const FArdaPackagedKernel*>(Variant.mSource.get());
				if (!Archived) continue;
				const auto& Key = Archived->GetKey();
				for (const auto& Other : Registered.mVariants)
				{
					const auto Provider = Other.mProviderId.empty() ? Other.mSource->GetProviderId() : Other.mProviderId.c_str();
					if (Key.mProvider != Provider) continue;
					const auto* Packaged = dynamic_cast<const FArdaPackagedKernel*>(Other.mSource.get());
					if (Packaged ? (Packaged->GetKey().mName != Key.mName || Packaged->GetKey().mLibrary != Key.mLibrary ||
					                Packaged->GetKey().mSignature != Key.mSignature) : Other.mName != Key.mName)
						continue;
					Variant.mNativeResources = Other.mNativeResources;
					Variant.mGraphicsStates = Other.mGraphicsStates;
					break;
				}
			}
		}

		FArdaRHIStatus RecordBoundary(const eastl::vector<FArdaNativeWorkRef>& Work, IArdaRHICommandList& Commands)
		{
			for (const auto& Item : Work)
				if (auto Status = Item->Record(Commands); !Status) return Status;
			return {};
		}

		TArdaRHIResult<FArdaNativeWorkRef> TryVariant(const eastl::shared_ptr<const IArdaNativeComputeProvider>& Provider,
		    const FArdaNativeComputeContext& Context,
		    const FArdaNativeOperand& Operand,
		    const FArdaNativeKernelVariant& Variant,
		    const FArdaNativeInvocation* Invocation,
		    const FArdaNativeRouteCheck& CheckRoute, FArdaNativeConversion& Conversion)
		try
		{
			auto Source = Variant.mSource;
			if (auto* Packaged = dynamic_cast<const FArdaPackagedKernel*>(Source.get()))
			{
				auto Loaded = Packaged->Resolve(Provider);
				if (!Loaded) return {{}, eastl::move(Loaded.mStatus)};
				Source = eastl::move(Loaded.mValue);
			}
			const auto& Desc = Provider->GetDesc();
			if (eastl::find(Desc.mSourceFormats.begin(), Desc.mSourceFormats.end(), Source->GetFormat()) ==
			    Desc.mSourceFormats.end())
			{
				return {{}, Unsupported("Provider cannot load this source format.")};
			}
			if (!Operand.mGraphicsToNative || !Operand.mNativeToGraphics ||
			    (Operand.mbRequiresRegisteredConversions && !HasArdaNativeResourceConversions(Operand)))
				return {{}, Unsupported("The registered operand's resource conversions are unavailable.")};
			FArdaNativeOperand NativeContract;
			if (!Variant.mNativeResources.empty())
			{
				NativeContract = Operand;
				NativeContract.mResources = Variant.mNativeResources;
			}
			const auto& NativeOperand = Variant.mNativeResources.empty() ? Operand : NativeContract;
			for (const auto& Slot : NativeOperand.mResources)
			{
				if (!Slot.mCategory.empty() && !eastl::any_of(Desc.mResourceCategories.begin(), Desc.mResourceCategories.end(),
				        [&](const auto& Category) { return Category.mId == Slot.mCategory && Category.mType == Slot.mType; }))
					return {{}, Unsupported("Provider does not support the requested native resource category.")};
			}
			if (auto Status = Provider->CheckSupport(Context, NativeOperand, *Source); !Status)
				return {{}, eastl::move(Status)};
			Conversion.mGraphicsStates = Variant.mGraphicsStates;
			if (Conversion.mGraphicsStates.empty())
				for (const auto& Slot : NativeOperand.mResources) Conversion.mGraphicsStates.push_back(Provider->GetRequiredState(Slot));
			if (!Invocation)
				return {{}, CheckRoute ? CheckRoute(EArdaNativeComputeRoute::Native, Source.get(), Conversion.mGraphicsStates) : FArdaRHIStatus{}};
			auto Launch = Variant.mSelect ? Variant.mSelect(Context, *Invocation)
			                              : TArdaRHIResult<FArdaNativeLaunch>{Invocation->mLaunch, {}};
			if (!Launch) return {{}, eastl::move(Launch.mStatus)};

			const FArdaNativeConversionContext Boundary{Context, *Provider, Operand.mResources, NativeOperand.mResources, *Invocation};
			Conversion.mNative = *Invocation;
			Conversion.mNative.mLaunch = Launch.mValue;
			if (auto Status = Operand.mGraphicsToNative(Boundary, Conversion); !Status) return {{}, eastl::move(Status)};
			if (auto Status = ValidateInvocation(NativeOperand, Conversion.mNative, Desc.mId.c_str()); !Status) return {{}, eastl::move(Status)};
			if (Conversion.mGraphicsStates.size() != Operand.mResources.size())
				return {{}, Invalid("A conversion must declare one graphics boundary state per resource.")};
			bool NeedsStateRestore = false;
			for (size_t I = 0; I < NativeOperand.mResources.size(); ++I)
			{
				const auto& Slot = NativeOperand.mResources[I];
				const auto& View = Conversion.mNative.mResources[I];
				auto Handoff = Provider->QueryHandoff(Context, Slot, View);
				if (!Handoff) return {{}, eastl::move(Handoff.mStatus)};
				if (Handoff.mValue.mKind != EArdaNativeHandoff::Direct)
					return {{}, {EArdaRHIResult::Unsupported, Slot.mName + ": converted view cannot be bound by this provider."}};
				if (Conversion.mGraphicsStates[I] == EArdaRHIResourceState::Unknown)
					Conversion.mGraphicsStates[I] = Handoff.mValue.mRequiredState;
				if (Handoff.mValue.mRequiredState == EArdaRHIResourceState::Unknown ||
				    Conversion.mGraphicsStates[I] == EArdaRHIResourceState::Unknown)
					return {{}, Invalid("A native handoff requires an explicit graphics state.")};
				if (View.mResource == Invocation->mResources[I].mResource &&
				    Conversion.mGraphicsStates[I] != Handoff.mValue.mRequiredState)
					NeedsStateRestore = true;
			}
			if (CheckRoute)
				if (auto Status = CheckRoute(EArdaNativeComputeRoute::Native, Source.get(), Conversion.mGraphicsStates); !Status)
					return {{}, eastl::move(Status)};
			auto Prepared = Provider->Prepare(Context, NativeOperand, *Source, Conversion.mNative, Conversion.mNative.mLaunch);
			if (!Prepared || !Prepared.mValue) return Prepared;
			if (auto Status = Operand.mNativeToGraphics(Boundary, Conversion); !Status) return {{}, eastl::move(Status)};
			if (NeedsStateRestore && (Conversion.mBeforeNative.empty() || Conversion.mAfterNative.empty()))
				return {{}, Unsupported("A direct binding with different graphics and native states needs both boundary transitions.")};
			if (eastl::find(Conversion.mBeforeNative.begin(), Conversion.mBeforeNative.end(), nullptr) != Conversion.mBeforeNative.end() ||
			    eastl::find(Conversion.mAfterNative.begin(), Conversion.mAfterNative.end(), nullptr) != Conversion.mAfterNative.end())
				return {{}, Invalid("A resource conversion returned empty recording work.")};
			return Prepared;
		}
		catch (const std::exception& Error)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what())};
		}
		catch (...)
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Native variant preparation threw an exception.")};
		}

		FArdaRHIStatus CheckFallback(const FArdaNativeShaderFallback& Fallback,
		    const FArdaNativeComputeContext& Context)
		{
			if (Fallback.mRoute == EArdaNativeComputeRoute::GraphicsShader &&
			    Context.mQueue != EArdaRHIQueueType::Graphics)
			{
				return Unsupported("The graphics shader fallback needs a graphics command list.");
			}
			return Context.mDevice->RequireFeatures(Fallback.mRequiredFeatures);
		}

		class FArdaCallbackWork final : public IArdaNativePreparedWork
		{
		public:
			explicit FArdaCallbackWork(eastl::function<FArdaRHIStatus(IArdaRHICommandList&)> Callback)
			    : mCallback(eastl::move(Callback))
			{
			}

			FArdaRHIStatus Record(IArdaRHICommandList& Commands) const override
			{
				return mCallback(Commands);
			}

		private:
			eastl::function<FArdaRHIStatus(IArdaRHICommandList&)> mCallback;
		};
	}

	void UseArdaNativeDirectResources(FArdaNativeOperand& Operand)
	{
		Operand.mGraphicsToNative = FArdaDirectResourceConversion{};
		Operand.mNativeToGraphics = FArdaDirectResourceConversion{};
	}

	bool HasArdaNativeResourceConversions(const FArdaNativeOperand& Operand)
	{
		return Operand.mGraphicsToNative && Operand.mNativeToGraphics &&
		    (!Operand.mGraphicsToNative.target<FArdaDirectResourceConversion>() ||
		        !Operand.mNativeToGraphics.target<FArdaDirectResourceConversion>());
	}

	FArdaNativeWorkRef MakeArdaNativeWork(eastl::function<FArdaRHIStatus(IArdaRHICommandList&)> Record)
	{
		return Record ? eastl::make_shared<FArdaCallbackWork>(eastl::move(Record)) : FArdaNativeWorkRef{};
	}

	FArdaRHIStatus FArdaNativeComputeRegistry::RegisterProvider(
	    eastl::shared_ptr<const IArdaNativeComputeProvider> Provider)
	{
		if (!Provider)
		{
			return Invalid("RegisterProvider requires a provider.");
		}
		const auto& Desc = Provider->GetDesc();
		if (Desc.mInterfaceVersion != ArdaNativeComputeProviderInterfaceVersion) return Invalid("Unsupported native provider interface version.");
		if (Desc.mId.empty() || !HasNames(Desc.mGraphicsApis) || !HasNames(Desc.mSourceFormats))
		{
			return Invalid("A provider needs an ID, graphics APIs and source formats with distinct, nonempty names.");
		}
		eastl::unordered_set<eastl::string> Categories;
		for (const auto& Category : Desc.mResourceCategories)
		{
			if (Category.mId.empty() || !Categories.insert(Category.mId).second || Category.mType > EArdaRHIResourceType::Count)
				return Invalid("A provider's native resource categories must have unique names and valid resource kinds.");
		}
		std::unique_lock Lock(mMutex);
		return mProviders.emplace(Desc.mId, eastl::move(Provider)).second ? FArdaRHIStatus{}
		                                                                  : Invalid("Duplicate native provider ID.");
	}

	FArdaRHIStatus FArdaNativeComputeRegistry::RegisterOperand(FArdaNativeOperand Operand)
	{
		if (Operand.mId.empty() || (Operand.mVariants.empty() && Operand.mShaderFallbacks.empty() && !Operand.mFallback.mPrepare))
		{
			return Invalid("An operand needs an ID and at least one implementation.");
		}
		if (bool(Operand.mGraphicsToNative) != bool(Operand.mNativeToGraphics) ||
		    (!Operand.mVariants.empty() && !Operand.mGraphicsToNative &&
		        !(mbUseGlobalProviders && Operand.mbRequiresRegisteredConversions)))
			return Invalid("A native operand must define both graphics-to-native and native-to-graphics conversions.");
		if (Operand.mFallback.mPrepare && Operand.mFallback.mRoute != EArdaNativeComputeRoute::ComputeShader &&
		    Operand.mFallback.mRoute != EArdaNativeComputeRoute::GraphicsShader)
		{
			return Invalid("A fallback must specify a compute or graphics shader.");
		}
		for (const auto& Fallback : Operand.mShaderFallbacks)
		{
			if (!Fallback.mPrepare || (Fallback.mRoute != EArdaNativeComputeRoute::ComputeShader &&
			    Fallback.mRoute != EArdaNativeComputeRoute::GraphicsShader))
				return Invalid("A shader fallback requires a compute or graphics route and preparation callback.");
		}
		eastl::unordered_set<eastl::string> Names;
		for (const auto& Slot : Operand.mResources)
		{
			if (Slot.mName.empty() || !Names.insert(Slot.mName).second || Slot.mType >= EArdaRHIResourceType::Count ||
			    Slot.mAccess > EArdaComputeAccess::ReadWrite || Slot.mFormat >= EArdaRHIFormat::Count ||
			    (Slot.mFormat != EArdaRHIFormat::Unknown && Slot.mType != EArdaRHIResourceType::Texture))
			{
				return Invalid("Invalid or duplicate native resource slot.");
			}
		}
		Names.clear();
		for (const auto& Variant : Operand.mVariants)
		{
			if ((!Variant.mNativeResources.empty() && Variant.mNativeResources.size() != Operand.mResources.size()) ||
			    (!Variant.mGraphicsStates.empty() && Variant.mGraphicsStates.size() != Operand.mResources.size()))
				return Invalid("Native and graphics resource declarations must have matching slot counts.");
			for (size_t Index = 0; Index < Variant.mNativeResources.size(); ++Index)
			{
				const auto& Slot = Variant.mNativeResources[Index];
				if (Slot.mAccess != Operand.mResources[Index].mAccess || Slot.mType > EArdaRHIResourceType::Count ||
				    (Slot.mType == EArdaRHIResourceType::Count && Slot.mCategory.empty()) ||
				    Slot.mFormat >= EArdaRHIFormat::Count || (Slot.mType != EArdaRHIResourceType::Texture && Slot.mFormat != EArdaRHIFormat::Unknown))
					return Invalid("A native resource slot must preserve access and declare a valid representation.");
			}
			if (Variant.mName.empty() || !Names.insert(Variant.mName).second || !Variant.mSource ||
			    !Variant.mSource->GetProviderId() || !*Variant.mSource->GetProviderId() ||
			    !Variant.mSource->GetFormat() || !*Variant.mSource->GetFormat())
			{
				return Invalid("Each variant needs a unique name and a source with a provider and format.");
			}
		}
		auto Published = eastl::make_shared<const FArdaNativeOperand>(eastl::move(Operand));
		std::unique_lock Lock(mMutex);
		return mOperands.emplace(Published->mId, Published).second ? FArdaRHIStatus{}
		                                                           : Invalid("Duplicate native operand ID.");
	}

	eastl::shared_ptr<const FArdaNativeOperand> FArdaNativeComputeRegistry::FindOperand(const char* Id) const
	{
		std::shared_lock Lock(mMutex);
		const auto Found = mOperands.find(Id ? Id : "");
		return Found == mOperands.end() ? nullptr : Found->second;
	}

	eastl::shared_ptr<const IArdaNativeComputeProvider> FArdaNativeComputeRegistry::FindProvider(const char* Id) const
	{
		std::shared_lock Lock(mMutex);
		const auto Found = mProviders.find(Id ? Id : "");
		if (Found != mProviders.end()) return Found->second;
		Lock.unlock();
		return mbUseGlobalProviders ? GetArdaNativeComputeRegistry().FindProvider(Id) : nullptr;
	}

	TArdaRHIResult<FArdaNativeResourceCategory> FArdaNativeComputeRegistry::FindResourceCategory(
	    const char* ProviderId, const char* CategoryId) const
	{
		if (const auto Provider = FindProvider(ProviderId); Provider && CategoryId)
			for (const auto& Category : Provider->GetDesc().mResourceCategories)
				if (Category.mId == CategoryId) return {Category, {}};
		return {{}, Unsupported("Native resource category is not registered by this provider.")};
	}

	FArdaRHIStatus FArdaNativeComputeRegistry::CheckSupport(const char* Id,
	    const FArdaNativeComputeContext& Context) const
	{
		return Resolve(Id, Context, nullptr, {}).mStatus;
	}

	TArdaRHIResult<FArdaNativeComputePlan> FArdaNativeComputeRegistry::Prepare(const char* Id,
	    const FArdaNativeComputeContext& Context,
	    const FArdaNativeInvocation& Invocation,
	    const FArdaNativeRouteCheck& CheckRoute) const
	{
		return Resolve(Id, Context, &Invocation, CheckRoute);
	}

	TArdaRHIResult<FArdaNativeComputePlan> FArdaNativeComputeRegistry::Resolve(const char* Id,
	    const FArdaNativeComputeContext& Context,
	    const FArdaNativeInvocation* Invocation,
	    const FArdaNativeRouteCheck& CheckRoute) const
	try
	{
		auto Operand = FindOperand(Id);
		if (!Operand || !Context.mDevice || Context.mPreference > EArdaNativeComputePreference::ShaderOnly)
		{
			return {{}, Invalid("Select a registered native operand, a device and a valid route preference.")};
		}
		if (mbUseGlobalProviders && Operand->mbRequiresRegisteredConversions && !HasArdaNativeResourceConversions(*Operand))
		{
			auto Shared = GetArdaNativeComputeRegistry().FindOperand(Id);
			if (Shared && SameOperandContract(*Shared, *Operand) && HasArdaNativeResourceConversions(*Shared))
			{
				auto Restored = eastl::make_shared<FArdaNativeOperand>(*Operand);
				RestoreConversions(*Restored, *Shared);
				Operand = eastl::move(Restored);
			}
		}
		if (Context.mQueue != EArdaRHIQueueType::Compute && Context.mQueue != EArdaRHIQueueType::Graphics)
		{
			return {{}, Unsupported("Native compute requires a compute or graphics command list.")};
		}
		if (!Context.mDevice->IsQueueSupported(Context.mQueue))
		{
			return {{}, Unsupported("Device does not expose the requested queue.")};
		}
		if (Invocation)
		{
			if (auto Status = ValidateInvocation(*Operand, *Invocation); !Status)
			{
				return {{}, eastl::move(Status)};
			}
		}
		FArdaNativeComputePlan Plan;
		Plan.mContext = Context;
		FArdaRHIStatus Failures{EArdaRHIResult::Unsupported, Operand->mId + ": no eligible implementation."};
		if (Context.mPreference != EArdaNativeComputePreference::ShaderOnly)
		{
			for (const auto& Variant : Operand->mVariants)
			{
				const char* ProviderId =
				    Variant.mProviderId.empty() ? Variant.mSource->GetProviderId() : Variant.mProviderId.c_str();
				const auto& Allowed = Context.mAllowedProviderIds;
				if ((!Context.mRequiredProviderId.empty() && Context.mRequiredProviderId != ProviderId) ||
				    (!Allowed.empty() && eastl::find(Allowed.begin(), Allowed.end(), ProviderId) == Allowed.end()))
				{
					continue;
				}
				auto Provider = FindProvider(ProviderId);
				auto Support = ProbeProvider(Provider, Context);
				auto Selection = Context;
				Selection.mProviderContext = Support.mValue.mContext;
				FArdaNativeConversion Conversion;
				auto Result = Support ? TryVariant(Provider, Selection, *Operand, Variant, Invocation, CheckRoute, Conversion)
				                      : TArdaRHIResult<FArdaNativeWorkRef>{{}, Support.mStatus};
				if (Result)
				{
					if (Invocation && !Result.mValue)
					{
						return {{}, Invalid("Provider returned success without prepared work.")};
					}
					Plan.mWork = eastl::move(Result.mValue);
					Plan.mBeforeNative = eastl::move(Conversion.mBeforeNative);
					Plan.mAfterNative = eastl::move(Conversion.mAfterNative);
					Plan.mVariantName = Variant.mName;
					Plan.mMode = Support.mValue.mMode;
					Plan.mProviderId = ProviderId;
					Plan.mProvider = eastl::move(Provider);
					Plan.mContext.mProviderContext = eastl::move(Selection.mProviderContext);
					return {eastl::move(Plan), {}};
				}
				if (Result.mStatus.mCode != EArdaRHIResult::Unsupported &&
				    Result.mStatus.mCode != EArdaRHIResult::BackendFailure)
				{
					return {{}, eastl::move(Result.mStatus)};
				}
				if (Result.mStatus.mCode == EArdaRHIResult::BackendFailure)
				{
					Failures.mCode = Result.mStatus.mCode;
				}
				Failures.mMessage += " " + Variant.mName + " (" + ProviderId + "): " + Result.mStatus.mMessage;
			}
		}
		auto FallbackOperand = Operand;
		if (mbUseGlobalProviders && !Operand->mSignature.empty() && !Operand->mFallback.mPrepare &&
		    Operand->mShaderFallbacks.empty() && Context.mPreference != EArdaNativeComputePreference::NativeOnly)
		{
			// An archive can be opened before the application installs its fallback implementation.
			auto Shared = GetArdaNativeComputeRegistry().FindOperand(Id);
			if (Shared && SameOperandContract(*Shared, *Operand))
			{
				if (Invocation && Shared->mValidate)
				{
					if (auto Status = Shared->mValidate(*Invocation); !Status) return {{}, eastl::move(Status)};
				}
				FallbackOperand = eastl::move(Shared);
			}
		}
		for (size_t Index = 0; Context.mPreference != EArdaNativeComputePreference::NativeOnly &&
		    Index <= FallbackOperand->mShaderFallbacks.size(); ++Index)
		{
			const auto& Fallback = Index < FallbackOperand->mShaderFallbacks.size()
			    ? FallbackOperand->mShaderFallbacks[Index] : FallbackOperand->mFallback;
			if (!Fallback.mPrepare) continue;
			auto Status = CheckFallback(Fallback, Context);
			if (Status && CheckRoute)
			{
				Status = CheckRoute(Fallback.mRoute, nullptr, {});
			}
			if (Status && Invocation)
			{
				auto Result = Fallback.mPrepare(Context, *Invocation);
				Status = eastl::move(Result.mStatus);
				Plan.mWork = eastl::move(Result.mValue);
				if (Status && !Plan.mWork)
				{
					return {{}, Invalid("Shader fallback returned success without prepared work.")};
				}
			}
			if (Status)
			{
				Plan.mRoute = Fallback.mRoute;
				return {eastl::move(Plan), {}};
			}
			if (Status.mCode != EArdaRHIResult::Unsupported && Status.mCode != EArdaRHIResult::BackendFailure)
			{
				return {{}, eastl::move(Status)};
			}
			Failures.mMessage += " Shader fallback: " + Status.mMessage;
		}
		return {{}, eastl::move(Failures)};
	}
	catch (const std::exception& Error)
	{
		return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what())};
	}
	catch (...)
	{
		return {{}, FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Native operand preparation threw an exception.")};
	}

	FArdaRHIStatus FArdaNativeComputePlan::RecordOn(IArdaRHICommandList& Commands) const
	try
	{
		if (!mWork)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Prepare a native compute plan before recording.");
		}
		if (Commands.GetDevice() != mContext.mDevice.Get())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice, "Command list belongs to another device.");
		}
		if (Commands.GetQueueType() != mContext.mQueue)
		{
			return Invalid("Command list queue differs from the prepared queue.");
		}
		if (auto Status = RecordBoundary(mBeforeNative, Commands); !Status) return Status;
		if (auto Status = mWork->Record(Commands); !Status) return Status;
		return RecordBoundary(mAfterNative, Commands);
	}
	catch (const std::exception& Error)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
	}
	catch (...)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Native compute recording threw an exception.");
	}
	TArdaRHIResult<EArdaNativeExecutionMode> FArdaNativeComputeRegistry::QueryExecutionMode(const char* Id,
	    const FArdaNativeComputeContext& Context, eastl::string* OutProviderId, const FArdaNativeRouteCheck& CheckRoute) const
	{
		auto Plan = Resolve(Id, Context, nullptr, CheckRoute);
		if (OutProviderId) *OutProviderId = Plan.mValue.GetProviderId();
		return {Plan.mValue.GetExecutionMode(), eastl::move(Plan.mStatus)};
	}

	TArdaRHIResult<FArdaNativeProviderSupport> FArdaNativeComputeRegistry::QueryProviderSupport(
	    const char* Id, const FArdaNativeComputeContext& Context) const
	{
		return ProbeProvider(FindProvider(Id), Context);
	}

	TArdaRHIResult<FArdaNativeProviderSupport> FArdaNativeComputeRegistry::ProbeProvider(
	    const eastl::shared_ptr<const IArdaNativeComputeProvider>& Provider, const FArdaNativeComputeContext& Context) const
	{
		if (!Context.mDevice) return {{}, Invalid("Provider probing requires a device.")};
		if (!Provider) return {{}, Unsupported("Native compute provider is not registered.")};
		auto& Device = *Context.mDevice;
		eastl::shared_ptr<FArdaNativeDeviceProviders::FArdaEntry> Entry;
		{
			std::lock_guard Lock(Device.mNativeProviderMutex);
			if (!Device.mNativeProviders) Device.mNativeProviders = eastl::make_shared<FArdaNativeDeviceProviders>();
			auto& Slot = Device.mNativeProviders->mEntries[Provider.get()];
			if (!Slot)
			{
				Slot = eastl::make_shared<FArdaNativeDeviceProviders::FArdaEntry>();
				Slot->mProvider = Provider;
			}
			Entry = Slot;
		}
		std::call_once(Entry->mProbed, [&]
		{
			try
			{
				const auto& Apis = Provider->GetDesc().mGraphicsApis;
				if (eastl::find(Apis.begin(), Apis.end(), "*") == Apis.end() &&
				    eastl::find(Apis.begin(), Apis.end(), Device.GetBackendName()) == Apis.end())
				{
					Entry->mSupport.mStatus = Unsupported("Provider does not support this graphics API.");
					return;
				}
				Entry->mSupport = Provider->ProbeDevice(Device);
				if (!Entry->mSupport)
					Entry->mSupport.mStatus.mCode = EArdaRHIResult::Unsupported;
				else if (Entry->mSupport.mValue.mMode > EArdaNativeExecutionMode::ContextSwitching)
					Entry->mSupport.mStatus = Unsupported("Provider returned an unknown execution mode.");
			}
			catch (...) { Entry->mSupport.mStatus = Unsupported("Provider initialization failed on this device."); }
		});
		return Entry->mSupport;
	}

	eastl::vector<eastl::string> FArdaNativeComputeRegistry::GetProviderIds() const
	{
		auto Names = mbUseGlobalProviders ? GetArdaNativeComputeRegistry().GetProviderIds() : eastl::vector<eastl::string>{};
		std::shared_lock Lock(mMutex);
		for (const auto& Entry : mProviders)
		{
			if (eastl::find(Names.begin(), Names.end(), Entry.first) == Names.end()) Names.push_back(Entry.first);
		}
		eastl::sort(Names.begin(), Names.end());
		return Names;
	}

	void FArdaNativeComputeRegistry::ProbeDeviceProviders(const FArdaRHIDeviceRef& Device) const
	{
		for (const auto& Id : GetProviderIds()) (void)QueryProviderSupport(Id.c_str(), {Device});
	}

	FArdaRHIStatus RegisterLinkedArdaNativeComputeProviders(FArdaNativeComputeRegistry& Registry)
	{
		FArdaNativeComputeRegistry Staged;
#define ARDA_LINKED_NATIVE_COMPUTE_PROVIDER(Function) if (auto Status = Function(Staged); !Status) return Status;
#include "ArdaLinkedNativeComputeProviders.inl"
#undef ARDA_LINKED_NATIVE_COMPUTE_PROVIDER
		std::unique_lock Lock(Registry.mMutex);
		for (const auto& Entry : Staged.mProviders)
			if (Registry.mProviders.count(Entry.first)) return Invalid("Duplicate linked native compute provider.");
		for (const auto& Entry : Staged.mOperands)
			if (Registry.mOperands.count(Entry.first)) return Invalid("Duplicate linked native operand.");
		Registry.mProviders.insert(Staged.mProviders.begin(), Staged.mProviders.end());
		Registry.mOperands.insert(Staged.mOperands.begin(), Staged.mOperands.end());
		return {};
	}

	namespace
	{
		struct FArdaLinkedComputeRegistry
		{
			FArdaNativeComputeRegistry mRegistry;
			FArdaRHIStatus mStatus = RegisterLinkedArdaNativeComputeProviders(mRegistry);
		};
		FArdaLinkedComputeRegistry& LinkedRegistry()
		{
			static FArdaLinkedComputeRegistry Instance;
			return Instance;
		}
	}

	FArdaNativeComputeRegistry& GetArdaNativeComputeRegistry() { return LinkedRegistry().mRegistry; }
	FArdaRHIStatus GetArdaNativeComputeModuleStatus() { return LinkedRegistry().mStatus; }

	TArdaRHIResult<FArdaNativeOperand> MakeArdaPackagedNativeOperand(const FArdaNativeKernelKey& Key,
	    const FArdaNativeKernelLayout& Layout, FArdaNativeOperand Operand)
	{
		if (!IsValidArdaNativeKernelKey(Key) ||
		    Operand.mParameterSize != Layout.mParameterSize || Operand.mId.empty() ||
		    (!Operand.mSignature.empty() && Operand.mSignature != Key.mSignature))
			return {{}, Invalid("Packaged native operand key or parameter schema is invalid.")};
		const auto Aligned = [](size_t Value) { return Value && !(Value & (Value - 1)); };
		if (!Aligned(Layout.mParameterAlignment)) return {{}, Invalid("Invalid native parameter alignment.")};
		size_t End = 0;
		for (const auto& Arg : Layout.mArguments)
		{
			if (Arg.mKind > EArdaNativeArgumentKind::Surface || !Arg.mSize || !Aligned(Arg.mAlignment) ||
			    Arg.mAlignment > Layout.mParameterAlignment || Arg.mOffset % Arg.mAlignment || Arg.mOffset < End ||
			    Arg.mOffset > Layout.mParameterSize || Arg.mSize > Layout.mParameterSize - Arg.mOffset)
				return {{}, Invalid("Invalid packed native argument layout.")};
			End = Arg.mOffset + Arg.mSize;
		}
		Operand.mSignature = Key.mSignature;
		Operand.mVariants.push_back({Key.mName, eastl::make_shared<FArdaPackagedKernel>(Key, Layout), {}, Key.mProvider});
		if (auto Shared = GetArdaNativeComputeRegistry().FindOperand(Operand.mId.c_str());
		    Shared && Shared->mSignature == Key.mSignature)
		{
			if (!SameOperandContract(*Shared, Operand))
				return {{}, Invalid("Registered operand signature has a different resource or parameter contract.")};
			if (!Operand.mFallback.mPrepare && Operand.mShaderFallbacks.empty())
			{
				Operand.mFallback = Shared->mFallback;
				Operand.mShaderFallbacks = Shared->mShaderFallbacks;
			}
			if (!Operand.mValidate) Operand.mValidate = Shared->mValidate;
			RestoreConversions(Operand, *Shared);
		}
		return {eastl::move(Operand), {}};
	}

	FArdaRHIStatus FArdaNativeComputeSession::Append(FArdaNativeComputePlan Plan, IArdaRHICommandList& Commands)
	{
		if (mCommands && mCommands != &Commands)
			return Invalid("Finish the native session on its original command list.");
		if (!Plan.mWork || Commands.GetDevice() != Plan.mContext.mDevice.Get() || Commands.GetQueueType() != Plan.mContext.mQueue)
			return Invalid("Native plan and recording command list do not match.");
		if (Plan.mMode == EArdaNativeExecutionMode::Direct || Plan.mProvider != mProvider || !Plan.mBeforeNative.empty())
		{
			if (auto Status = Flush(Commands); !Status) return Status;
		}
		if (Plan.mMode == EArdaNativeExecutionMode::Direct) return Plan.Record(Commands);
		mCommands = &Commands;
		mProvider = eastl::move(Plan.mProvider);
		if (mWork.empty()) mBeforeNative = eastl::move(Plan.mBeforeNative);
		mWork.push_back(eastl::move(Plan.mWork));
		if (Plan.mAfterNative.empty()) return {};
		mAfterNative = eastl::move(Plan.mAfterNative);
		return Flush(Commands);
	}

	FArdaRHIStatus FArdaNativeComputeSession::Flush(IArdaRHICommandList& Commands)
	{
		if (!mProvider) return {};
		if (mCommands != &Commands) return Invalid("Finish the native session on its original command list.");
		auto Provider = eastl::move(mProvider);
		auto Work = eastl::move(mWork);
		auto Before = eastl::move(mBeforeNative);
		auto After = eastl::move(mAfterNative);
		mCommands = nullptr;
		struct FArdaBatchCommand final : IArdaRHIComputeCommand
		{
			const IArdaNativeComputeProvider& mProvider;
			const eastl::vector<FArdaNativeWorkRef>& mWork;
			const eastl::vector<FArdaNativeWorkRef>& mBefore;
			const eastl::vector<FArdaNativeWorkRef>& mAfter;
			FArdaBatchCommand(const IArdaNativeComputeProvider& Provider, const eastl::vector<FArdaNativeWorkRef>& Work,
			    const eastl::vector<FArdaNativeWorkRef>& Before, const eastl::vector<FArdaNativeWorkRef>& After)
			    : mProvider(Provider), mWork(Work), mBefore(Before), mAfter(After) {}
			FArdaRHIStatus RecordOn(IArdaRHICommandList& List) const override
			{
				try
				{
					if (auto Status = RecordBoundary(mBefore, List); !Status) return Status;
					if (auto Status = mProvider.RecordBatch(List, mWork); !Status) return Status;
					return RecordBoundary(mAfter, List);
				}
				catch (...) { return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Native session recording failed."); }
			}
		};
		try { return Commands.RecordNativeCompute(FArdaBatchCommand(*Provider, Work, Before, After)); }
		catch (...) { return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Native session recording failed."); }
	}

}
