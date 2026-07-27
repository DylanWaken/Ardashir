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
        uint32_t mIdentifier = 0;
        uint32_t mFirstUse = 0;
        uint32_t mLastUse = 0;
        uint64_t mSize = 0;
        uint64_t mAlignment = 1;
    };

    /** Stores one virtual-resource placement selected by interval allocation. */
    struct FARDGTransientAllocation
    {
        uint32_t mIdentifier = 0;
        uint64_t mOffset = 0;
        uint64_t mSize = 0;
        bool mbReusedMemory = false;
    };

    /** Stores deterministic placements and the required heap capacity. */
    struct FARDGTransientHeapLayout
    {
        eastl::vector<FARDGTransientAllocation> mAllocations;
        uint64_t mCapacity = 0;
        bool mbContainsAliases = false;
    };

    /** Packs virtual resources by live interval with optional memory reuse. */
    class FARDGTransientHeapAllocator final
    {
    public:
        /**
         * Builds a deterministic heap layout.
         *
         * @param Requests Resource sizes, alignments, and inclusive lifetimes.
         * @param bAllowAliasing Whether expired ranges may be reused.
         * @return Placements in request identifier order.
         */
        [[nodiscard]] static FARDGTransientHeapLayout Allocate(
            const eastl::vector<FARDGTransientAllocationRequest>& Requests,
            bool bAllowAliasing);
    };

    class FARDGArena final
    {
    public:
        explicit FARDGArena(size_t DefaultBlockSize = 64u * 1024u);
        ~FARDGArena();

        FARDGArena(const FARDGArena&) = delete;
        FARDGArena& operator=(const FARDGArena&) = delete;
        FARDGArena(FARDGArena&&) = delete;
        FARDGArena& operator=(FARDGArena&&) = delete;

        [[nodiscard]] void* AllocateBytes(size_t Size, size_t Alignment);

        /**
         * Registers destruction for an object constructed in arena storage.
         *
         * @param Object The constructed object.
         * @param Destroy The function that destroys Object without releasing storage.
         */
        void RegisterDestructor(void* Object, void (*Destroy)(void*));

        template <typename ObjectType, typename... ArgumentTypes>
        [[nodiscard]] ObjectType* Allocate(ArgumentTypes&&... Arguments)
        {
            void* Storage = AllocateBytes(sizeof(ObjectType), alignof(ObjectType));
            ObjectType* Object = new (Storage) ObjectType(
                eastl::forward<ArgumentTypes>(Arguments)...);

            if constexpr (!eastl::is_trivially_destructible_v<ObjectType>)
            {
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

        void Reset() noexcept;

        [[nodiscard]] size_t GetObjectCount() const noexcept
        {
            return mObjectCount;
        }

        [[nodiscard]] size_t GetBlockCount() const noexcept
        {
            return mBlocks.size();
        }

    private:
        struct FARDGBlock
        {
            std::byte* mMemory = nullptr;
            size_t mCapacity = 0;
            size_t mOffset = 0;
            size_t mAlignment = 0;
        };

        struct FARDGDestructor
        {
            void* mObject = nullptr;
            void (*mDestroy)(void*) = nullptr;
        };

        [[nodiscard]] FARDGBlock& AddBlock(size_t MinimumSize, size_t Alignment);

        size_t mDefaultBlockSize = 0;
        size_t mObjectCount = 0;
        eastl::vector<FARDGBlock> mBlocks;
        eastl::vector<FARDGDestructor> mDestructors;
    };
}
