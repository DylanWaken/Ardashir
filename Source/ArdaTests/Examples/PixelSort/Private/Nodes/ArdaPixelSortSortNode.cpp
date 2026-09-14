#include "Nodes/ArdaPixelSortSortNode.h"
#include "ArdaPixelSortNodeInternal.h"
#include "Nodes/ArdaPixelSortOperand.h"

namespace arda
{
	struct FArdaPixelSortSortNode::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		eastl::shared_ptr<FArdaPixelSortOperand> mOperand;

		FArdaRHIStatus Initialize()
		{
			try
			{
				// The operand owns native variant registration and architecture qualification.
				mOperand = eastl::make_shared<FArdaPixelSortOperand>(mDevice);
				return mOperand->GetOperandSupport();
			}
			catch (const std::exception& Error)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Error.what());
			}
		}
	};

	FArdaRHIStatus FArdaPixelSortSortNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHITextureDesc D;
		D.mWidth = P.mWidth;
		D.mHeight = P.mHeight;
		D.mFormat = EArdaRHIFormat::RGBA8UInt;
		D.mbCudaInterop = true;
		D.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
		return C.Texture(P.mSorted, "Output", D);
	}

	FArdaDependencyNodeRequirements FArdaPixelSortSortNode::GetRequirements(const FArdaParameters&)
	{
		FArdaDependencyNodeRequirements R;
		R.mbRequireCuda = true;
		R.mbRequireCudaSurfaces = true;
		return R;
	}

	FArdaDependencyNodeMetadata FArdaPixelSortSortNode::GetMetadata()
	{
		return {"example.pixel-sort.radix", 1};
	}

	eastl::string FArdaPixelSortSortNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(P.mNoise)
		    .Resource(P.mSorted)
		    .Value(reinterpret_cast<uintptr_t>(P.mInput.get()))
		    .Value(P.mWidth)
		    .Value(P.mHeight)
		    .Build();
	}

	FArdaRHIStatus FArdaPixelSortSortNode::Validate(const FArdaParameters& P)
	{
		if (!P.mInput)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "PixelSort sorting requires dynamic sort inputs.");
		}
		return ValidatePixelSortExtent(P.mWidth, P.mHeight);
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaPixelSortSortNode::FArdaState>> FArdaPixelSortSortNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}

		auto State = eastl::make_shared<FArdaState>();
		State->mDevice = eastl::move(Device);

		// Publish prepared state only after all node-owned setup succeeds.
		auto Status = State->Initialize();
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}

		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaPixelSortSortNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mNoise, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess},
		    {P.mSorted, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};

		return D;
	}

	FArdaRHIStatus FArdaPixelSortSortNode::PrepareCuda(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState,
	    FArdaCudaSequence& Sequence)
	{
		if (Prepared.mDevice != C.GetDevice())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
			    "PixelSort node library belongs to another device.");
		}

		// Sample frame-varying values and resolve graph textures into the operand's typed arguments.
		FArdaPixelSortParameters Host;
		Host.mInput.mTexture = C.GetTexture(P.mNoise);
		Host.mOutput.mTexture = C.GetTexture(P.mSorted);
		Host.mWidth = P.mWidth;
		Host.mHeight = P.mHeight;
		Host.mChannel = P.mInput->mChannel;
		Host.mThreshold = P.mInput->mThreshold;
		return Sequence.Add(*Prepared.mOperand, Host);
	}
}
