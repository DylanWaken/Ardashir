#include "CUDA/ArdaCudaModule.h"
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace arda
{
	namespace
	{
		FArdaRHIStatus InvalidModule(const char* Reason)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Reason);
		}

		bool IsAlignment(size_t Alignment)
		{
			return Alignment && !(Alignment & (Alignment - 1));
		}

		struct FArdaCudaModuleParameters
		{
		};

		struct FArdaCudaModuleCache
		{
			std::mutex mMutex;
			std::unordered_multimap<uint64_t, eastl::weak_ptr<const FArdaCudaModule>> mModules;
			uint64_t mRegistrations = 0;
		};

		uint64_t HashModuleImage(const FArdaCudaModuleDesc& Desc)
		{
			uint64_t Hash = 14695981039346656037ull;
			for (const auto Byte : Desc.mImage)
			{
				Hash = (Hash ^ Byte) * 1099511628211ull;
			}
			return Hash;
		}

		bool AreModuleDescriptionsEqual(const FArdaCudaModuleDesc& Left, const FArdaCudaModuleDesc& Right)
		{
			if (Left.mFormat != Right.mFormat || Left.mImage != Right.mImage ||
			    Left.mBuild.mIdentity != Right.mBuild.mIdentity || Left.mBuild.mName != Right.mBuild.mName ||
			    Left.mBuild.mbFastMath != Right.mBuild.mbFastMath ||
			    Left.mBuild.mArchitectures.size() != Right.mBuild.mArchitectures.size())
			{
				return false;
			}
			for (size_t Index = 0; Index < Left.mBuild.mArchitectures.size(); ++Index)
			{
				const auto& A = Left.mBuild.mArchitectures[Index];
				const auto& B = Right.mBuild.mArchitectures[Index];
				if (A.mComputeCapability != B.mComputeCapability || A.mbExact != B.mbExact)
				{
					return false;
				}
			}
			return true;
		}

		class FArdaCudaModuleEntry final : public IArdaCudaKernelEntry
		{
		public:
			FArdaCudaModuleEntry(eastl::shared_ptr<const FArdaCudaModule> Module, FArdaCudaModuleKernelDesc Desc)
			    : mModule(eastl::move(Module)),
			      mDesc(eastl::move(Desc))
			{
			}

			FArdaCudaKernelSignature GetSignature() const noexcept override
			{
				return {&typeid(FArdaCudaModuleParameters), mDesc.mParameterSize, mDesc.mParameterAlignment, true};
			}

			const FArdaCudaBuildInfo& GetBuildInfo() const noexcept override
			{
				return mModule->GetDesc().mBuild;
			}

			const FArdaCudaModule* GetModule() const noexcept override
			{
				return mModule.get();
			}

			const FArdaCudaModuleKernelDesc* GetModuleKernel() const noexcept override
			{
				return &mDesc;
			}

			TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const override
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				        "CUDA module entry must be resolved by its execution provider.")};
			}

			FArdaRHIStatus Launch(void*, const FArdaCudaLaunchConfig&, const void*, size_t) const override
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "CUDA module entry must be resolved by its execution provider.");
			}

		private:
			const eastl::shared_ptr<const FArdaCudaModule> mModule;
			const FArdaCudaModuleKernelDesc mDesc;
		};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCudaModule>> CreateArdaCudaModule(FArdaCudaModuleDesc Desc)
	{
		if (Desc.mImage.empty() || Desc.mBuild.mArchitectures.empty() || Desc.mBuild.mIdentity.empty())
		{
			return {{}, InvalidModule("CUDA module requires image bytes, a build identity and architecture coverage.")};
		}
		for (const auto& Architecture : Desc.mBuild.mArchitectures)
		{
			if (!Architecture.mComputeCapability)
			{
				return {{}, InvalidModule("CUDA module architecture must be nonzero.")};
			}
		}
		if (Desc.mFormat == EArdaCudaModuleFormat::Ptx)
		{
			const auto* Terminator = std::memchr(Desc.mImage.data(), 0, Desc.mImage.size());
			if (Terminator && Terminator != Desc.mImage.data() + Desc.mImage.size() - 1)
			{
				return {{}, InvalidModule("CUDA PTX contains an embedded null byte.")};
			}
			if (!Terminator)
			{
				Desc.mImage.push_back(0);
			}
		}
		else if (Desc.mFormat != EArdaCudaModuleFormat::Cubin || Desc.mImage.size() < 64 ||
		    std::memcmp(Desc.mImage.data(),
		        "\x7f"
		        "ELF",
		        4) != 0)
		{
			return {{}, InvalidModule("CUDA cubin requires a native ELF image; the driver validates its contents.")};
		}
		// Runtime definitions deserialize independently. Canonicalize immutable host code so
		// hundreds of entries from one artifact also share one native module per context.
		const auto Hash = HashModuleImage(Desc);
		static FArdaCudaModuleCache Cache;
		std::lock_guard<std::mutex> Lock(Cache.mMutex);
		if (Cache.mRegistrations++ % 256 == 0)
		{
			for (auto It = Cache.mModules.begin(); It != Cache.mModules.end();)
			{
				if (It->second.expired())
				{
					It = Cache.mModules.erase(It);
				}
				else
				{
					++It;
				}
			}
		}
		const auto Range = Cache.mModules.equal_range(Hash);
		for (auto It = Range.first; It != Range.second; ++It)
		{
			if (auto Module = It->second.lock(); Module && AreModuleDescriptionsEqual(Desc, Module->GetDesc()))
			{
				return {eastl::move(Module), {}};
			}
		}
		auto Module = eastl::shared_ptr<const FArdaCudaModule>(new FArdaCudaModule(eastl::move(Desc)));
		Cache.mModules.emplace(Hash, Module);
		return {eastl::move(Module), {}};
	}

	FArdaRHIStatus ValidateArdaCudaModuleKernelDesc(const FArdaCudaModuleKernelDesc& Desc)
	{
		if (Desc.mEntryPoint.empty() || Desc.mEntryPoint.find('\0') != eastl::string::npos ||
		    !Desc.mParameterSize || !IsAlignment(Desc.mParameterAlignment) || Desc.mArguments.empty())
		{
			return InvalidModule("CUDA module kernel requires a symbol and a nonempty parameter ABI.");
		}
		size_t End = 0;
		for (const auto& Argument : Desc.mArguments)
		{
			if (!Argument.mSize || !IsAlignment(Argument.mAlignment) ||
			    Argument.mAlignment > Desc.mParameterAlignment || Argument.mOffset % Argument.mAlignment ||
			    Argument.mOffset < End || Argument.mOffset > Desc.mParameterSize ||
			    Argument.mSize > Desc.mParameterSize - Argument.mOffset ||
			    (Argument.mKind != EArdaCudaModuleArgumentKind::Value &&
			        Argument.mKind != EArdaCudaModuleArgumentKind::Buffer &&
			        Argument.mKind != EArdaCudaModuleArgumentKind::Surface) ||
			    (Argument.mKind != EArdaCudaModuleArgumentKind::Value && Argument.mSize != sizeof(uint64_t)))
			{
				return InvalidModule(
				    "CUDA module arguments must be ordered, aligned, nonoverlapping and within their ABI storage.");
			}
			End = Argument.mOffset + Argument.mSize;
		}
		return {};
	}

	TArdaRHIResult<eastl::shared_ptr<const IArdaCudaKernelEntry>> CreateArdaCudaModuleKernel(
	    eastl::shared_ptr<const FArdaCudaModule> Module,
	    FArdaCudaModuleKernelDesc Desc)
	{
		if (!Module)
		{
			return {{}, InvalidModule("CUDA module kernel requires a module image.")};
		}
		if (auto Status = ValidateArdaCudaModuleKernelDesc(Desc); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::make_shared<FArdaCudaModuleEntry>(eastl::move(Module), eastl::move(Desc)), {}};
	}

	FArdaRHIStatus ValidateArdaCudaModuleParameters(const FArdaCudaModuleKernelDesc& Desc,
	    const FArdaCudaKernel& Kernel)
	{
		for (const auto& Argument : Desc.mArguments)
		{
			size_t Patches = 0;
			for (const auto& Patch : Kernel.mPatches)
			{
				if (Patch.mOffset == Argument.mOffset && Argument.mKind != EArdaCudaModuleArgumentKind::Value)
				{
					++Patches;
				}
			}
			if (Argument.mKind != EArdaCudaModuleArgumentKind::Value && Patches != 1)
			{
				return InvalidModule("CUDA module resource arguments require exactly one declared resource patch.");
			}
		}
		for (const auto& Patch : Kernel.mPatches)
		{
			bool bResource = false;
			for (const auto& Argument : Desc.mArguments)
			{
				bResource |= Argument.mOffset == Patch.mOffset && Argument.mKind != EArdaCudaModuleArgumentKind::Value;
			}
			if (!bResource)
			{
				return InvalidModule(
				    "CUDA module patches must address complete resource arguments, never scalar data or padding.");
			}
		}
		return {};
	}
}
