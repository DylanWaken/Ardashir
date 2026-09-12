#include "Nodes/ArdaPixelSortSortNode.h"
#include "ArdaPixelSortNodeInternal.h"
#include "Nodes/ArdaPixelSortOperand.h"

namespace arda
{
	struct FArdaPixelSortSortNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		eastl::shared_ptr<FArdaPixelSortOperand> mOperand;

		FArdaRHIStatus Initialize()
		{
			try
			{
				auto* State = this;
				const auto Device = State->mDevice;
				State->mOperand = eastl::make_shared<FArdaPixelSortOperand>(Device);
				Check(State->mOperand->GetOperandSupport());
				return {};
			}
			catch (const std::exception& Error)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Error.what());
			}
		}
	};

	FArdaDependencyNodeMetadata FArdaPixelSortSortNode::GetMetadata()
	{
		return {"example.pixel-sort.radix", 1};
	}

	eastl::string FArdaPixelSortSortNode::GetCanonicalKey(const FParameters& P)
	{
		return MakePixelSortNodeKey(P);
	}

	FArdaRHIStatus FArdaPixelSortSortNode::Validate(const FParameters& P)
	{
		if (!P.mInput || !P.mWidth || !P.mHeight)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "PixelSort requires frame inputs and a nonempty extent.");
		}
		return {};
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaPixelSortSortNode::FState>> FArdaPixelSortSortNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}
		auto State = eastl::make_shared<FState>();
		State->mDevice = eastl::move(Device);
		auto Status = State->Initialize();
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaPixelSortSortNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mNoise, EArdaDependencyAccess::Read, EArdaRHIResourceState::UnorderedAccess},
		    {P.mSorted, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};

		return D;
	}

	FArdaRHIStatus FArdaPixelSortSortNode::PrepareCuda(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState,
	    FArdaCudaSequence& Sequence)
	{
		if (Prepared.mDevice != C.GetDevice())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::WrongDevice,
			    "PixelSort node library belongs to another device.");
		}
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
