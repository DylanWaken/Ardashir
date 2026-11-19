#include "ArdaNativeCompute.h"
#include <EASTL/algorithm.h>
#include <EASTL/unordered_set.h>
#include <exception>
#include <mutex>

namespace arda
{
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

		FArdaRHIStatus ValidateInvocation(const FArdaNativeOperandDesc& Operand,
		    const FArdaNativeInvocation& Invocation)
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
			return Operand.mValidate ? Operand.mValidate(Invocation) : FArdaRHIStatus{};
		}

		TArdaRHIResult<FArdaNativeWorkRef> TryVariant(const IArdaNativeComputeProvider* Provider,
		    const FArdaNativeComputeContext& Context,
		    const FArdaNativeOperandDesc& Operand,
		    const FArdaNativeKernelVariant& Variant,
		    const FArdaNativeInvocation* Invocation,
		    const FArdaNativeRouteCheck& CheckRoute)
		try
		{
			if (!Provider)
			{
				return {{}, Unsupported("Provider is not registered.")};
			}
			const auto& Desc = Provider->GetDesc();
			if (eastl::find(Desc.mGraphicsApis.begin(), Desc.mGraphicsApis.end(), Context.mDevice->GetBackendName()) ==
			    Desc.mGraphicsApis.end())
			{
				return {{}, Unsupported("Provider does not support this graphics API.")};
			}
			if (eastl::find(Desc.mSourceFormats.begin(), Desc.mSourceFormats.end(), Variant.mSource->GetFormat()) ==
			    Desc.mSourceFormats.end())
			{
				return {{}, Unsupported("Provider cannot load this source format.")};
			}
			if (auto Status = Provider->CheckSupport(Context, Operand, *Variant.mSource); !Status || !Invocation)
			{
				return {{}, eastl::move(Status)};
			}
			auto Launch = Variant.mSelect ? Variant.mSelect(Context, *Invocation)
			                              : TArdaRHIResult<FArdaNativeLaunch>{Invocation->mLaunch, {}};
			if (!Launch)
			{
				return {{}, eastl::move(Launch.mStatus)};
			}
			eastl::vector<EArdaRHIResourceState> RequiredStates;
			for (size_t I = 0; I < Operand.mResources.size(); ++I)
			{
				const auto Handoff = Provider->QueryHandoff(Context, Operand.mResources[I], Invocation->mResources[I]);
				if (!Handoff)
				{
					return {{}, Handoff.mStatus};
				}
				if (Handoff.mValue.mKind != EArdaNativeHandoff::Direct)
				{
					return {{},
					    {EArdaRHIResult::Unsupported,
					        Operand.mResources[I].mName + ": this view needs a conversion pass."}};
				}
				if (Handoff.mValue.mRequiredState == EArdaRHIResourceState::Unknown)
				{
					return {{}, Invalid("A direct handoff needs a graphics resource state.")};
				}
				if (CheckRoute)
				{
					RequiredStates.push_back(Handoff.mValue.mRequiredState);
				}
			}
			if (CheckRoute)
			{
				auto Status = CheckRoute(EArdaNativeComputeRoute::Native, Variant.mSource.get(), RequiredStates);
				if (!Status)
				{
					return {{}, eastl::move(Status)};
				}
			}
			return Provider->Prepare(Context, Operand, *Variant.mSource, *Invocation, Launch.mValue);
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
		if (Desc.mId.empty() || !HasNames(Desc.mGraphicsApis) || !HasNames(Desc.mSourceFormats))
		{
			return Invalid("A provider needs an ID, graphics APIs and source formats with distinct, nonempty names.");
		}
		std::unique_lock Lock(mMutex);
		return mProviders.emplace(Desc.mId, eastl::move(Provider)).second ? FArdaRHIStatus{}
		                                                                  : Invalid("Duplicate native provider ID.");
	}

	FArdaRHIStatus FArdaNativeComputeRegistry::RegisterOperand(FArdaNativeOperandDesc Operand)
	{
		if (Operand.mId.empty() || (Operand.mVariants.empty() && !Operand.mFallback.mPrepare))
		{
			return Invalid("An operand needs an ID and at least one implementation.");
		}
		if (Operand.mFallback.mPrepare && Operand.mFallback.mRoute != EArdaNativeComputeRoute::ComputeShader &&
		    Operand.mFallback.mRoute != EArdaNativeComputeRoute::GraphicsShader)
		{
			return Invalid("A fallback must specify a compute or graphics shader.");
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
			if (Variant.mName.empty() || !Names.insert(Variant.mName).second || !Variant.mSource ||
			    !Variant.mSource->GetProviderId() || !*Variant.mSource->GetProviderId() ||
			    !Variant.mSource->GetFormat() || !*Variant.mSource->GetFormat())
			{
				return Invalid("Each variant needs a unique name and a source with a provider and format.");
			}
		}
		auto Published = eastl::make_shared<const FArdaNativeOperandDesc>(eastl::move(Operand));
		std::unique_lock Lock(mMutex);
		return mOperands.emplace(Published->mId, Published).second ? FArdaRHIStatus{}
		                                                           : Invalid("Duplicate native operand ID.");
	}

	eastl::shared_ptr<const FArdaNativeOperandDesc> FArdaNativeComputeRegistry::FindOperand(const char* Id) const
	{
		std::shared_lock Lock(mMutex);
		const auto Found = mOperands.find(Id ? Id : "");
		return Found == mOperands.end() ? nullptr : Found->second;
	}

	eastl::shared_ptr<const IArdaNativeComputeProvider> FArdaNativeComputeRegistry::FindProvider(const char* Id) const
	{
		std::shared_lock Lock(mMutex);
		const auto Found = mProviders.find(Id ? Id : "");
		return Found == mProviders.end() ? nullptr : Found->second;
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
		const auto Operand = FindOperand(Id);
		if (!Operand || !Context.mDevice || Context.mPreference > EArdaNativeComputePreference::ShaderOnly)
		{
			return {{}, Invalid("Select a registered native operand, a device and a valid route preference.")};
		}
		if (Context.mQueue != EArdaRHIQueueType::Compute && Context.mQueue != EArdaRHIQueueType::Graphics)
		{
			return {{}, Unsupported("Native compute requires a compute or graphics command list.")};
		}
		const auto& Queues = Context.mDevice->GetCapabilities().mQueues;
		if ((Context.mQueue == EArdaRHIQueueType::Graphics && !Queues.mbGraphics) ||
		    (Context.mQueue == EArdaRHIQueueType::Compute && !Queues.mbCompute))
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
				auto Result = TryVariant(Provider.get(), Context, *Operand, Variant, Invocation, CheckRoute);
				if (Result)
				{
					if (Invocation && !Result.mValue)
					{
						return {{}, Invalid("Provider returned success without prepared work.")};
					}
					Plan.mWork = eastl::move(Result.mValue);
					Plan.mVariantName = Variant.mName;
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
				Failures.mMessage += " " + Variant.mName + ": " + Result.mStatus.mMessage;
			}
		}
		const auto& Fallback = Operand->mFallback;
		if (Context.mPreference != EArdaNativeComputePreference::NativeOnly && Fallback.mPrepare)
		{
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
			if (Status.mCode != EArdaRHIResult::Unsupported)
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
		return mWork->Record(Commands);
	}
	catch (const std::exception& Error)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
	}
	catch (...)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Native compute recording threw an exception.");
	}
}
