#pragma once

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <limits>

namespace arda
{
	// The provider selects a consistent unit (bytes or tiles) and domain (resource or subresource).
	// Copies form a proposed mapping transaction; publish only after the native bind is accepted.
	template <typename OwnerType>
	class TArdaSparseMappingSet
	{
	public:
		struct FArdaRange
		{
			uint64_t mDomain = 0;
			uint64_t mBegin = 0;
			uint64_t mEnd = 0;
			uint64_t mOwnerOffset = 0;
			OwnerType mOwner;
		};

		[[nodiscard]] const eastl::vector<FArdaRange>& GetRanges() const noexcept
		{
			return mRanges;
		}

		// Empty ownership unmaps the range. Invalid arithmetic leaves this set unchanged.
		[[nodiscard]] bool Replace(uint64_t Domain,
		    uint64_t Begin,
		    uint64_t Size,
		    const OwnerType& Owner,
		    uint64_t OwnerOffset = 0)
		{
			constexpr uint64_t Maximum = std::numeric_limits<uint64_t>::max();
			if (!Size || Size > Maximum - Begin || (Owner && Size > Maximum - OwnerOffset))
			{
				return false;
			}
			const uint64_t End = Begin + Size;
			eastl::vector<FArdaRange> Updated;
			Updated.reserve(mRanges.size() + 2);
			for (const auto& Range : mRanges)
			{
				if (Range.mDomain != Domain || Range.mEnd <= Begin || Range.mBegin >= End)
				{
					Updated.push_back(Range);
					continue;
				}
				if (Range.mBegin < Begin)
				{
					auto Left = Range;
					Left.mEnd = Begin;
					Updated.push_back(eastl::move(Left));
				}
				if (Range.mEnd > End)
				{
					auto Right = Range;
					Right.mOwnerOffset += End - Range.mBegin;
					Right.mBegin = End;
					Updated.push_back(eastl::move(Right));
				}
			}
			if (Owner)
			{
				Updated.push_back({Domain, Begin, End, OwnerOffset, Owner});
			}
			eastl::sort(Updated.begin(),
			    Updated.end(),
			    [](const FArdaRange& Left, const FArdaRange& Right)
			    {
				    return Left.mDomain < Right.mDomain ||
				        (Left.mDomain == Right.mDomain && Left.mBegin < Right.mBegin);
			    });
			eastl::vector<FArdaRange> Merged;
			Merged.reserve(Updated.size());
			for (auto& Range : Updated)
			{
				if (!Merged.empty())
				{
					auto& Previous = Merged.back();
					if (Previous.mDomain == Range.mDomain && Previous.mEnd == Range.mBegin &&
					    Previous.mOwner == Range.mOwner &&
					    Previous.mOwnerOffset + (Previous.mEnd - Previous.mBegin) == Range.mOwnerOffset)
					{
						Previous.mEnd = Range.mEnd;
						continue;
					}
				}
				Merged.push_back(eastl::move(Range));
			}
			mRanges = eastl::move(Merged);
			return true;
		}

		[[nodiscard]] uint64_t GetPrefixSize(uint64_t Domain = 0) const noexcept
		{
			uint64_t End = 0;
			for (const auto& Range : mRanges)
			{
				if (Range.mDomain == Domain)
				{
					if (Range.mBegin != End)
					{
						break;
					}
					End = Range.mEnd;
				}
			}
			return End;
		}

	private:
		eastl::vector<FArdaRange> mRanges;
	};

	// Capacity must already include the native allocation granularity. Clamp before rounding.
	[[nodiscard]] inline bool CalculateArdaSparsePrefixSize(uint64_t Requested,
	    uint64_t Capacity,
	    uint64_t Alignment,
	    uint64_t& Result) noexcept
	{
		if (!Alignment || Capacity % Alignment)
		{
			return false;
		}
		const uint64_t Clamped = eastl::min(Requested, Capacity);
		const uint64_t Remainder = Clamped % Alignment;
		Result = Remainder ? Clamped + (Alignment - Remainder) : Clamped;
		return true;
	}
}
