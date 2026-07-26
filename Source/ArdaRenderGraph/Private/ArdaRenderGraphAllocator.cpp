#include "ArdaRenderGraphPch.h"

#include "ArdaRenderGraphAllocator.h"
#include "ArdaRenderGraphLog.h"

#include <algorithm>
#include <limits>

namespace arda::render_graph
{
    namespace
    {
        [[nodiscard]] bool IsPowerOfTwo(size_t Value) noexcept
        {
            return Value != 0 && (Value & (Value - 1u)) == 0;
        }

        [[nodiscard]] uint64_t AlignUp(uint64_t Value, uint64_t Alignment)
        {
            if (Value > std::numeric_limits<uint64_t>::max() - (Alignment - 1u))
            {
                ARDA_CHECK_MSG("Render-graph memory allocation failed.");
            }
            return (Value + Alignment - 1u) & ~(Alignment - 1u);
        }
    }

    FARDGTransientHeapLayout FARDGTransientHeapAllocator::Allocate(
        const std::vector<FARDGTransientAllocationRequest>& Requests,
        bool bAllowAliasing)
    {
        struct FARDGActiveAllocation
        {
            uint32_t mLastUse = 0;
            uint64_t mOffset = 0;
            uint64_t mSize = 0;
        };
        struct FARDGFreeRange
        {
            uint64_t mOffset = 0;
            uint64_t mSize = 0;
        };

        std::vector<FARDGTransientAllocationRequest> Sorted = Requests;
        std::sort(
            Sorted.begin(),
            Sorted.end(),
            [](const auto& Left, const auto& Right)
            {
                if (Left.mFirstUse != Right.mFirstUse)
                {
                    return Left.mFirstUse < Right.mFirstUse;
                }
                return Left.mIdentifier < Right.mIdentifier;
            });

        std::vector<FARDGActiveAllocation> Active;
        std::vector<FARDGFreeRange> FreeRanges;
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

                std::sort(
                    FreeRanges.begin(),
                    FreeRanges.end(),
                    [](const auto& Left, const auto& Right)
                    {
                        return Left.mOffset < Right.mOffset;
                    });
                std::vector<FARDGFreeRange> Merged;
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
                FreeRanges = std::move(Merged);
            }

            uint64_t Offset = 0;
            bool bReused = false;
            if (bAllowAliasing)
            {
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
                Offset = AlignUp(Layout.mCapacity, Request.mAlignment);
            }
            if (Offset > std::numeric_limits<uint64_t>::max() - Request.mSize)
            {
                ARDA_CHECK_MSG("Render-graph memory allocation failed.");
            }
            Layout.mCapacity =
                std::max(Layout.mCapacity, Offset + Request.mSize);
            Layout.mbContainsAliases |= bReused;
            Layout.mAllocations.push_back(
                {Request.mIdentifier, Offset, Request.mSize, bReused});
            Active.push_back(
                {Request.mLastUse, Offset, Request.mSize});
        }

        std::sort(
            Layout.mAllocations.begin(),
            Layout.mAllocations.end(),
            [](const auto& Left, const auto& Right)
            {
                return Left.mIdentifier < Right.mIdentifier;
            });
        return Layout;
    }

    FARDGArena::FARDGArena(size_t DefaultBlockSize)
        : mDefaultBlockSize(DefaultBlockSize)
    {
        if (mDefaultBlockSize == 0)
        {
            ARDA_CHECK_MSG("A render-graph arena block size must be non-zero.");
        }
    }

    FARDGArena::~FARDGArena()
    {
        Reset();
    }

    void* FARDGArena::AllocateBytes(size_t Size, size_t Alignment)
    {
        if (!IsPowerOfTwo(Alignment))
        {
            ARDA_CHECK_MSG("A render-graph arena alignment must be a non-zero power of two.");
        }

        Size = std::max<size_t>(Size, 1u);

        for (auto BlockIterator = mBlocks.rbegin(); BlockIterator != mBlocks.rend(); ++BlockIterator)
        {
            FARDGBlock& Block = *BlockIterator;
            if (Alignment > Block.mAlignment)
            {
                continue;
            }

            const uintptr_t BaseAddress = reinterpret_cast<uintptr_t>(Block.mMemory);
            const uintptr_t CurrentAddress = BaseAddress + Block.mOffset;
            if (CurrentAddress > std::numeric_limits<uintptr_t>::max() - (Alignment - 1u))
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

    void FARDGArena::RegisterDestructor(void* Object, void (*Destroy)(void*))
    {
        if (Object == nullptr || Destroy == nullptr)
        {
            ARDA_CHECK_MSG("Cannot register an invalid render-graph destructor.");
        }
        mDestructors.push_back({Object, Destroy});
    }

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

    FARDGArena::FARDGBlock& FARDGArena::AddBlock(size_t MinimumSize, size_t Alignment)
    {
        FARDGBlock Block;
        Block.mCapacity = std::max(mDefaultBlockSize, MinimumSize);
        Block.mAlignment = std::max(alignof(std::max_align_t), Alignment);
        Block.mMemory = static_cast<std::byte*>(
            ::operator new(Block.mCapacity, std::align_val_t(Block.mAlignment)));

        mBlocks.push_back(Block);

        return mBlocks.back();
    }
}
