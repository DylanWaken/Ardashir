#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace arda::render_graph
{
    /** Describes one virtual-resource request for interval allocation. */
    struct FARDGTransientAllocationRequest
    {
        /** Stable request key used to restore identifier order; zero is a valid identifier. */
        uint32_t mIdentifier = 0;
        /** Inclusive first-use position in the caller's execution-order index domain. */
        uint32_t mFirstUse = 0;
        /** Inclusive last-use position in the caller's execution-order index domain. */
        uint32_t mLastUse = 0;
        /** Required allocation size in bytes; allocation rejects zero. */
        uint64_t mSize = 0;
        /** Required byte alignment; must be a non-zero power of two. */
        uint64_t mAlignment = 1;
    };

    /** Stores one virtual-resource placement selected by interval allocation. */
    struct FARDGTransientAllocation
    {
        /** Request identifier copied from the corresponding allocation request. */
        uint32_t mIdentifier = 0;
        /** Byte offset of the placement from the start of the ideal heap. */
        uint64_t mOffset = 0;
        /** Placement size in bytes, copied unchanged from its request. */
        uint64_t mSize = 0;
        /** True when this placement recycles memory retired by an earlier lifetime. */
        bool mbReusedMemory = false;
    };

    /** Stores deterministic placements and the required heap capacity. */
    struct FARDGTransientHeapLayout
    {
        /** Final placements, sorted by request identifier after allocation. */
        eastl::vector<FARDGTransientAllocation> mAllocations;
        /** Minimum ideal heap capacity in bytes spanning every placement. */
        uint64_t mCapacity = 0;
        /** True when at least one placement reuses a retired byte range. */
        bool mbContainsAliases = false;
    };

    /** Models the execution-allocation stage by packing virtual-resource intervals. */
    class FARDGTransientHeapAllocator final
    {
    public:
        /**
         * CPU side planning for the transient heap allocation on the GPU side.
         * 
         * Builds a deterministic ideal heap layout during execution-time
         * transient-allocation evaluation. Requests are treated as inclusive
         * live intervals; memory can be recycled only when an earlier
         * request's last use is strictly before the next request's first use.
         *
         * @todo Physical placed-resource aliasing is currently disabled. Wire
         * this layout into execution only after NVRHI provides portable aliasing
         * barriers and heap-compatibility queries.
         *
         * @param Requests Resource sizes, alignments, and inclusive lifetimes.
         * @param bAllowAliasing Whether expired ranges may be reused.
         * @return Placements in request identifier order.
         */
        [[nodiscard]] static FARDGTransientHeapLayout Allocate(
            const eastl::vector<FARDGTransientAllocationRequest>& Requests,
            bool bAllowAliasing);
    };

    /**
     * Owns monotonic CPU storage for one graph's build-time records.
     *
     * Allocation never moves existing objects, which keeps registry pointers
     * and frozen parameter addresses stable through build, compile, and
     * execution. Reset destroys non-trivial objects and releases every block.
     *
     * Memory grows forward inside each block:
     *
     * @code
     * mBlocks
     *   |
     *   +--> Block 0
     *   |    +-------------------------------------------------------+
     *   |    | Object A | padding | Object B | unused space          |
     *   |    +-------------------------------------------------------+
     *   |                              ^                              ^
     *   |                              mOffset                        mCapacity
     *   |
     *   +--> Block 1 (created when no existing block can fit a request)
     *        +-------------------------------------------------------+
     *        | Object C | unused space                               |
     *        +-------------------------------------------------------+
     *
     * Allocate<T>()
     *   -> align the next address
     *   -> construct T in place
     *   -> remember its destructor when T is non-trivial
     *
     * Reset()
     *   -> destroy C, B, A in reverse construction order
     *   -> release Block 1, then Block 0
     * @endcode
     */
    class FARDGArena final
    {
    public:
        /**
         * Creates an empty graph arena.
         *
         * @param DefaultBlockSize Preferred capacity of newly added blocks;
         *        oversized requests still receive a sufficiently large block.
         */
        explicit FARDGArena(size_t DefaultBlockSize = 64u * 1024u);

        /** Runs registered destructors and releases all graph-owned blocks. */
        ~FARDGArena();

        /** Arena ownership is unique because entries contain stable raw pointers. */
        FARDGArena(const FARDGArena&) = delete;
        /** Arena ownership is unique because entries contain stable raw pointers. */
        FARDGArena& operator=(const FARDGArena&) = delete;
        /** Moving is disabled so the arena's identity cannot change. */
        FARDGArena(FARDGArena&&) = delete;
        /** Moving is disabled so the arena's identity cannot change. */
        FARDGArena& operator=(FARDGArena&&) = delete;

        /**
         * Reserves suitably aligned, uninitialized graph-lifetime storage.
         *
         * The allocator first reuses unused space in an existing compatible
         * block and otherwise appends a block. A zero-byte request still
         * reserves one byte, and Alignment must be a non-zero power of two.
         */
        [[nodiscard]] void* AllocateBytes(size_t Size, size_t Alignment);

        /**
         * Registers destruction for an object constructed in arena storage.
         *
         * @param Object The constructed object.
         * @param Destroy The function that destroys Object without releasing storage.
         */
        void RegisterDestructor(void* Object, void (*Destroy)(void*));

        /**
         * Constructs one graph-scoped object in stable arena storage.
         *
         * Non-trivially destructible objects register a type-erased callback
         * so Reset can destroy them in reverse construction order. Storage is
         * retained until Reset; individual objects cannot be freed.
         */
        template <typename ObjectType, typename... ArgumentTypes>
        [[nodiscard]] ObjectType* Allocate(ArgumentTypes&&... Arguments)
        {
            void* Storage = AllocateBytes(sizeof(ObjectType), alignof(ObjectType));
            ObjectType* Object = new (Storage) ObjectType(
                eastl::forward<ArgumentTypes>(Arguments)...);

            if constexpr (!eastl::is_trivially_destructible_v<ObjectType>)
            {
                // Capture no state: Reset stores this lambda as a plain
                // function pointer and supplies the original object address.
                RegisterDestructor(
                    Object,
                    [](void* Address)
                    {
                        static_cast<ObjectType*>(Address)->~ObjectType();
                    });
            }

            ++mObjectCount;
            return Object;
        }

        /**
         * Ends the arena lifetime and returns it to an empty state.
         *
         * Destructors run last-in-first-out before backing blocks are freed,
         * preserving ordinary nested-lifetime expectations. The operation is
         * noexcept and leaves no registered objects or blocks.
         */
        void Reset() noexcept;

        /** Returns the number of objects constructed since the last Reset. */
        [[nodiscard]] size_t GetObjectCount() const noexcept
        {
            return mObjectCount;
        }

        /** Returns the number of currently owned backing blocks. */
        [[nodiscard]] size_t GetBlockCount() const noexcept
        {
            return mBlocks.size();
        }

    private:
        struct FARDGBlock
        {
            /** Owned aligned byte storage, released by Reset after object destruction. */
            std::byte* mMemory = nullptr;
            /** Total usable size of mMemory in bytes. */
            size_t mCapacity = 0;
            /** Monotonic byte offset of the first unconsumed location in this block. */
            size_t mOffset = 0;
            /** Alignment in bytes supplied when allocating mMemory; always a power of two. */
            size_t mAlignment = 0;
        };

        struct FARDGDestructor
        {
            /** Non-owning address of an arena object that remains valid until Reset. */
            void* mObject = nullptr;
            /** Type-erased destructor callback; never null for a registered entry. */
            void (*mDestroy)(void*) = nullptr;
        };

        /**
         * Appends a backing block able to satisfy an allocation request.
         *
         * Capacity is at least both MinimumSize and the preferred block size;
         * alignment is at least max_align_t. The returned reference remains
         * valid only until the block vector is modified again.
         */
        [[nodiscard]] FARDGBlock& AddBlock(size_t MinimumSize, size_t Alignment);

        /** Preferred capacity in bytes for new blocks; fixed and non-zero after construction. */
        size_t mDefaultBlockSize = 0;
        /** Number of objects constructed since construction or the latest Reset. */
        size_t mObjectCount = 0;
        /** Arena-owned backing blocks retained until Reset and searched newest-first. */
        eastl::vector<FARDGBlock> mBlocks;
        /** Non-trivial object cleanup records in construction order for reverse destruction. */
        eastl::vector<FARDGDestructor> mDestructors;
    };
}
