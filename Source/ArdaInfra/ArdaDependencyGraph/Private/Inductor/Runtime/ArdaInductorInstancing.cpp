#include "Inductor/Runtime/ArdaInductorInstancing.h"

namespace arda
{
	namespace
	{
		bool SameAttachment(const FArdaRHIFramebufferTarget& A, const FArdaRHIFramebufferTarget& B)
		{
			return A.mTexture == B.mTexture && A.mAttachment.mSubresources == B.mAttachment.mSubresources &&
			    A.mAttachment.mFormat == B.mAttachment.mFormat && A.mAttachment.mbReadOnly == B.mAttachment.mbReadOnly;
		}

		bool SameFramebuffer(const FArdaRHIFramebufferRef& A, const FArdaRHIFramebufferRef& B)
		{
			if (A == B)
			{
				return true;
			}
			if (!A || !B)
			{
				return false;
			}
			const auto& Left = A->GetDesc();
			const auto& Right = B->GetDesc();
			if (Left.mColorAttachments.size() != Right.mColorAttachments.size() ||
			    !SameAttachment(Left.mDepthAttachment, Right.mDepthAttachment))
			{
				return false;
			}
			for (size_t Index = 0; Index < Left.mColorAttachments.size(); ++Index)
			{
				if (!SameAttachment(Left.mColorAttachments[Index], Right.mColorAttachments[Index]))
				{
					return false;
				}
			}
			return true;
		}

		bool SameBindingSet(const FArdaRHIBindingSetRef& A, const FArdaRHIBindingSetRef& B)
		{
			if (A == B)
			{
				return true;
			}
			if (!A || !B)
			{
				return false;
			}
			const auto& Left = A->GetDesc();
			const auto& Right = B->GetDesc();
			if (Left.mVariableDescriptorCount != Right.mVariableDescriptorCount ||
			    Left.mItems.size() != Right.mItems.size() ||
			    (Left.mLayout != Right.mLayout &&
			        (!Left.mLayout || !Right.mLayout || !(Left.mLayout->GetDesc() == Right.mLayout->GetDesc()))))
			{
				return false;
			}
			for (size_t Index = 0; Index < Left.mItems.size(); ++Index)
			{
				const auto& LeftItem = Left.mItems[Index];
				const auto& RightItem = Right.mItems[Index];
				if (LeftItem.mSlot != RightItem.mSlot || LeftItem.mArrayElement != RightItem.mArrayElement ||
				    LeftItem.mType != RightItem.mType || LeftItem.mResource != RightItem.mResource ||
				    !(LeftItem.mView == RightItem.mView))
				{
					return false;
				}
			}
			return true;
		}

		bool SameState(const FArdaRHIGraphicsState& A, const FArdaRHIGraphicsState& B)
		{
			if ((A.mPipeline != B.mPipeline &&
			        (!A.mPipeline || !B.mPipeline || !(A.mPipeline->GetDesc() == B.mPipeline->GetDesc()))) ||
			    !SameFramebuffer(A.mFramebuffer, B.mFramebuffer) || A.mBindings.size() != B.mBindings.size() ||
			    A.mIndexBuffer != B.mIndexBuffer || A.mIndexFormat != B.mIndexFormat ||
			    A.mIndexOffset != B.mIndexOffset || A.mVertexBuffers.size() != B.mVertexBuffers.size() ||
			    A.mViewports.size() != B.mViewports.size() || A.mScissors.size() != B.mScissors.size())
			{
				return false;
			}
			for (size_t Index = 0; Index < A.mBindings.size(); ++Index)
			{
				if (!SameBindingSet(A.mBindings[Index], B.mBindings[Index]))
				{
					return false;
				}
			}
			for (size_t Index = 0; Index < A.mVertexBuffers.size(); ++Index)
			{
				const auto& Left = A.mVertexBuffers[Index];
				const auto& Right = B.mVertexBuffers[Index];
				if (Left.mBuffer != Right.mBuffer || Left.mSlot != Right.mSlot || Left.mOffset != Right.mOffset)
				{
					return false;
				}
			}
			for (size_t Index = 0; Index < A.mViewports.size(); ++Index)
			{
				const auto& Left = A.mViewports[Index];
				const auto& Right = B.mViewports[Index];
				if (Left.mMinX != Right.mMinX || Left.mMaxX != Right.mMaxX || Left.mMinY != Right.mMinY ||
				    Left.mMaxY != Right.mMaxY || Left.mMinZ != Right.mMinZ || Left.mMaxZ != Right.mMaxZ)
				{
					return false;
				}
			}
			for (size_t Index = 0; Index < A.mScissors.size(); ++Index)
			{
				const auto& Left = A.mScissors[Index];
				const auto& Right = B.mScissors[Index];
				if (Left.mMinX != Right.mMinX || Left.mMaxX != Right.mMaxX || Left.mMinY != Right.mMinY ||
				    Left.mMaxY != Right.mMaxY)
				{
					return false;
				}
			}
			return true;
		}
	}

	bool TryMergeArdaGraphicsInstances(FArdaGraphicsWork& Accumulated, const FArdaGraphicsWork& Next) noexcept
	{
		const auto& Left = Accumulated.mDraw;
		const auto& Right = Next.mDraw;
		const uint64_t LeftEnd = uint64_t(Left.mStartInstance) + Left.mInstanceCount;
		const uint64_t RightEnd = uint64_t(Right.mStartInstance) + Right.mInstanceCount;
		const uint64_t CombinedCount = uint64_t(Left.mInstanceCount) + Right.mInstanceCount;
		if (Accumulated.mbIndexed != Next.mbIndexed || Left.mVertexCount == 0 ||
		    Left.mVertexCount != Right.mVertexCount || Left.mStartIndex != Right.mStartIndex ||
		    Left.mStartVertex != Right.mStartVertex || Left.mInstanceCount == 0 || Right.mInstanceCount == 0 ||
		    LeftEnd != Right.mStartInstance || RightEnd > uint64_t(UINT32_MAX) + 1 || CombinedCount > UINT32_MAX ||
		    !SameState(Accumulated.mState, Next.mState))
		{
			return false;
		}
		Accumulated.mDraw.mInstanceCount = static_cast<uint32_t>(CombinedCount);
		return true;
	}
}
