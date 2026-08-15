#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphAllocator.h"
#include "ArdaRenderGraphLog.h"

#include <EASTL/algorithm.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/sort.h>

namespace arda::render_graph
{
    namespace
    {
        /** Validates the alignment invariant shared by heap and arena allocation. */
        [[nodiscard]] bool IsPowerOfTwo(size_t Value) noexcept
        {
            return Value != 0 && (Value & (Value - 1u)) == 0;
        }

        /**
         * Rounds a heap offset up without silently wrapping uint64_t.
         *
         * Alignment is assumed to be a non-zero power of two, as validated by
         * the caller before this execution-allocation helper is used.
         */
        [[nodiscard]] uint64_t AlignUp(uint64_t Value, uint64_t Alignment)
        {
            if (Value > eastl::numeric_limits<uint64_t>::max() - (Alignment - 1u))
            {
                ARDA_CHECK_MSG("Render-graph memory allocation failed.");
            }
            return (Value + Alignment - 1u) & ~(Alignment - 1u);
        }
    }

    /**
     * Computes the ideal transient placement for compiled resource lifetimes.
     *
     * This execution-allocation model is deterministic: requests are visited
     * by first use and identifier, retired ranges are merged, and the first
     * aligned free range is selected. It does not create RHI heaps or
     * resources; the executor can evaluate the result independently of the
     * backend policy it ultimately uses.
     *
     * TODO(ArdaRenderGraph): Physical placed-resource aliasing is currently
     * disabled. Apply this layout only after the RHI exposes portable aliasing
     * barriers and heap-compatibility queries.
     */
    FARDGTransientHeapLayout FARDGTransientHeapAllocator::Allocate(
        const eastl::vector<FARDGTransientAllocationRequest>& Requests,
        bool bAllowAliasing)
    {
        struct FARDGActiveAllocation
        {
            /** Inclusive execution-order index after which this placement may retire. */
            uint32_t mLastUse = 0;
            /** Byte offset of the live placement from the ideal heap start. */
            uint64_t mOffset = 0;
            /** Size in bytes returned to the free-range set when the lifetime retires. */
            uint64_t mSize = 0;
        };
        struct FARDGFreeRange
        {
            /** Byte offset of the reusable range from the ideal heap start. */
            uint64_t mOffset = 0;
            /** Number of contiguous reusable bytes beginning at mOffset. */
            uint64_t mSize = 0;
        };

        eastl::vector<FARDGTransientAllocationRequest> Sorted = Requests;
        eastl::sort(
            Sorted.begin(),
            Sorted.end(),
            // Lifetime order makes retirement valid; the identifier tie-break
            // makes equal-first-use layouts reproducible.
            [](const auto& Left, const auto& Right)
            {
                if (Left.mFirstUse != Right.mFirstUse)
                {
                    return Left.mFirstUse < Right.mFirstUse;
                }
                return Left.mIdentifier < Right.mIdentifier;
            });

        eastl::vector<FARDGActiveAllocation> Active;
        eastl::vector<FARDGFreeRange> FreeRanges;
        FARDGTransientHeapLayout Layout;
        Layout.mAllocations.reserve(Requests.size());

        for (const FARDGTransientAllocationRequest& Request : Sorted)
        {
            if (Request.mSize == 0 ||
                !IsPowerOfTwo(static_cast<size_t>(Request.mAlignment)) ||
                Request.mFirstUse > Request.mLastUse)
            {
                ARDA_CHECK_MSG(
                    "Invalid render-graph transient allocation request.");
            }

            if (bAllowAliasing)
            {
                // Intervals are inclusive, so only allocations ending strictly
                // before this first use are no longer live.
                for (auto Iterator = Active.begin(); Iterator != Active.end();)
                {
                    if (Iterator->mLastUse < Request.mFirstUse)
                    {
                        FreeRanges.push_back(
                            {Iterator->mOffset, Iterator->mSize});
                        Iterator = Active.erase(Iterator);
                    }
                    else
                    {
                        ++Iterator;
                    }
                }

                eastl::sort(
                    FreeRanges.begin(),
                    FreeRanges.end(),
                    // Offset order enables the following linear adjacency merge.
                    [](const auto& Left, const auto& Right)
                    {
                        return Left.mOffset < Right.mOffset;
                    });
                eastl::vector<FARDGFreeRange> Merged;
                // Coalescing adjacent retired allocations can make a larger
                // request fit even when neither original range was sufficient.
                for (const FARDGFreeRange& Range : FreeRanges)
                {
                    if (!Merged.empty() &&
                        Merged.back().mOffset + Merged.back().mSize ==
                            Range.mOffset)
                    {
                        Merged.back().mSize += Range.mSize;
                    }
                    else
                    {
                        Merged.push_back(Range);
                    }
                }
                FreeRanges = eastl::move(Merged);
            }

            uint64_t Offset = 0;
            bool bReused = false;
            if (bAllowAliasing)
            {
                // Deterministic first-fit: preserve any alignment prefix and
                // unused suffix as independent ranges for later requests.
                for (size_t Index = 0; Index < FreeRanges.size(); ++Index)
                {
                    const FARDGFreeRange Range = FreeRanges[Index];
                    const uint64_t Candidate =
                        AlignUp(Range.mOffset, Request.mAlignment);
                    if (Candidate >= Range.mOffset &&
                        Candidate - Range.mOffset <= Range.mSize &&
                        Request.mSize <=
                            Range.mSize - (Candidate - Range.mOffset))
                    {
                        Offset = Candidate;
                        bReused = true;
                        FreeRanges.erase(FreeRanges.begin() + Index);
                        if (Candidate > Range.mOffset)
                        {
                            FreeRanges.push_back(
                                {Range.mOffset, Candidate - Range.mOffset});
                        }
                        const uint64_t End = Candidate + Request.mSize;
                        const uint64_t RangeEnd = Range.mOffset + Range.mSize;
                        if (End < RangeEnd)
                        {
                            FreeRanges.push_back({End, RangeEnd - End});
                        }
                        break;
                    }
                }
            }

            if (!bReused)
            {
                // Grow only at the aligned high-water mark when no retired
                // range can satisfy the request.
                Offset = AlignUp(Layout.mCapacity, Request.mAlignment);
            }
            if (Offset > eastl::numeric_limits<uint64_t>::max() - Request.mSize)
            {
                ARDA_CHECK_MSG("Render-graph memory allocation failed.");
            }
            Layout.mCapacity =
                eastl::max(Layout.mCapacity, Offset + Request.mSize);
            Layout.mbContainsAliases |= bReused;
            Layout.mAllocations.push_back(
                {Request.mIdentifier, Offset, Request.mSize, bReused});
            Active.push_back(
                {Request.mLastUse, Offset, Request.mSize});
        }

        eastl::sort(
            Layout.mAllocations.begin(),
            Layout.mAllocations.end(),
            // Consumers receive placements in stable request-identifier order,
            // independent of the lifetime order used by the algorithm.
            [](const auto& Left, const auto& Right)
            {
                return Left.mIdentifier < Right.mIdentifier;
            });
        return Layout;
    }

    /** Initializes build-time graph storage and rejects an unusable block size. */
    FARDGArena::FARDGArena(size_t DefaultBlockSize)
        : mDefaultBlockSize(DefaultBlockSize)
    {
        if (mDefaultBlockSize == 0)
        {
            ARDA_CHECK_MSG("A render-graph arena block size must be non-zero.");
        }
    }

    /** Ends graph storage lifetime by applying the same cleanup as Reset. */
    FARDGArena::~FARDGArena()
    {
        Reset();
    }

    /**
     * Allocates stable build-time storage from an existing block or a new one.
     *
     * Blocks are searched newest-first to favor the current allocation region.
     * The stored block alignment guarantees its base address can satisfy any
     * request no stricter than that alignment; offsets are then aligned within
     * the block. Successful allocation advances only that block's high-water
     * mark and never relocates earlier objects.
     */
    void* FARDGArena::AllocateBytes(size_t Size, size_t Alignment)
    {
        if (!IsPowerOfTwo(Alignment))
        {
            ARDA_CHECK_MSG("A render-graph arena alignment must be a non-zero power of two.");
        }

        Size = eastl::max<size_t>(Size, 1u);

        for (auto BlockIterator = mBlocks.rbegin(); BlockIterator != mBlocks.rend(); ++BlockIterator)
        {
            FARDGBlock& Block = *BlockIterator;
            if (Alignment > Block.mAlignment)
            {
                continue;
            }

            const uintptr_t BaseAddress = reinterpret_cast<uintptr_t>(Block.mMemory);
            const uintptr_t CurrentAddress = BaseAddress + Block.mOffset;
            if (CurrentAddress > eastl::numeric_limits<uintptr_t>::max() - (Alignment - 1u))
            {
                ARDA_CHECK_MSG("Render-graph memory allocation failed.");
            }

            const uintptr_t AlignedAddress =
                (CurrentAddress + Alignment - 1u) & ~(static_cast<uintptr_t>(Alignment) - 1u);
            const size_t AlignedOffset = static_cast<size_t>(AlignedAddress - BaseAddress);
            if (AlignedOffset <= Block.mCapacity && Size <= Block.mCapacity - AlignedOffset)
            {
                Block.mOffset = AlignedOffset + Size;
                return reinterpret_cast<void*>(AlignedAddress);
            }
        }

        FARDGBlock& Block = AddBlock(Size, Alignment);
        Block.mOffset = Size;
        return Block.mMemory;
    }

    /**
     * Adds a type-erased cleanup action for a constructed arena object.
     *
     * Registration order is construction order; Reset deliberately traverses
     * the list backward before releasing storage.
     */
    void FARDGArena::RegisterDestructor(void* Object, void (*Destroy)(void*))
    {
        if (Object == nullptr || Destroy == nullptr)
        {
            ARDA_CHECK_MSG("Cannot register an invalid render-graph destructor.");
        }
        mDestructors.push_back({Object, Destroy});
    }

    /**
     * Destroys graph-scoped objects and releases all arena blocks.
     *
     * Destruction precedes deallocation because callbacks dereference objects
     * in those blocks. Reverse traversal provides LIFO destruction, after
     * which counters and containers return to their initial empty state.
     */
    void FARDGArena::Reset() noexcept
    {
        for (auto DestructorIterator = mDestructors.rbegin();
             DestructorIterator != mDestructors.rend();
             ++DestructorIterator)
        {
            DestructorIterator->mDestroy(DestructorIterator->mObject);
        }
        mDestructors.clear();

        for (auto BlockIterator = mBlocks.rbegin(); BlockIterator != mBlocks.rend(); ++BlockIterator)
        {
            ::operator delete(
                BlockIterator->mMemory,
                std::align_val_t(BlockIterator->mAlignment));
        }
        mBlocks.clear();
        mObjectCount = 0;
    }

    /**
     * Appends one aligned block for the build-time monotonic allocator.
     *
     * The block grows beyond the preferred size for oversized requests and
     * uses at least max_align_t alignment. Its offset starts at zero; the
     * caller advances it after consuming the requested bytes.
     */
    FARDGArena::FARDGBlock& FARDGArena::AddBlock(size_t MinimumSize, size_t Alignment)
    {
        FARDGBlock Block;
        Block.mCapacity = eastl::max(mDefaultBlockSize, MinimumSize);
        Block.mAlignment = eastl::max(alignof(std::max_align_t), Alignment);
        Block.mMemory = static_cast<std::byte*>(
            ::operator new(Block.mCapacity, std::align_val_t(Block.mAlignment)));

        mBlocks.push_back(Block);

        return mBlocks.back();
    }
}
