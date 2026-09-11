#pragma once

#include "RHI/ArdaRHITypes.h"

namespace arda
{
	struct FArdaTextureSubresource
	{
		uint32_t mMipLevel;
		uint32_t mArraySlice;
		uint32_t mPlane;
	};

	// Iterates a resolved range in native subresource order: mip, array slice, plane.
	class FArdaTextureSubresources
	{
	public:
		explicit FArdaTextureSubresources(const FArdaRHITextureSubresourceRange& ResolvedRange)
		    : mRange(ResolvedRange)
		{
		}

		struct FSentinel
		{
		};

		class FIterator
		{
		public:
			explicit FIterator(const FArdaRHITextureSubresourceRange& Range)
			    : mRange(Range),
			      mCurrent{Range.mBaseMipLevel, Range.mBaseArraySlice, Range.mBasePlane},
			      mbDone(!Range.mMipLevelCount || !Range.mArraySliceCount || !Range.mPlaneCount)
			{
			}

			FArdaTextureSubresource operator*() const noexcept
			{
				return mCurrent;
			}

			bool operator!=(FSentinel) const noexcept
			{
				return !mbDone;
			}

			FIterator& operator++() noexcept
			{
				if (++mCurrent.mMipLevel == mRange.mBaseMipLevel + mRange.mMipLevelCount)
				{
					mCurrent.mMipLevel = mRange.mBaseMipLevel;
					if (++mCurrent.mArraySlice == mRange.mBaseArraySlice + mRange.mArraySliceCount)
					{
						mCurrent.mArraySlice = mRange.mBaseArraySlice;
						mbDone = ++mCurrent.mPlane == mRange.mBasePlane + mRange.mPlaneCount;
					}
				}
				return *this;
			}

		private:
			const FArdaRHITextureSubresourceRange& mRange;
			FArdaTextureSubresource mCurrent;
			bool mbDone;
		};

		FIterator begin() const noexcept
		{
			return FIterator(mRange);
		}

		FSentinel end() const noexcept
		{
			return {};
		}

	private:
		FArdaRHITextureSubresourceRange mRange;
	};
}
