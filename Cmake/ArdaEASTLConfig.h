#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace eastl
{
    class ArdaEASTLAllocator
    {
    public:
        explicit ArdaEASTLAllocator(const char* = nullptr) noexcept
        {
        }

        ArdaEASTLAllocator(const ArdaEASTLAllocator&) noexcept = default;

        ArdaEASTLAllocator(const ArdaEASTLAllocator&, const char*) noexcept
        {
        }

        ArdaEASTLAllocator& operator=(const ArdaEASTLAllocator&) noexcept = default;

        [[nodiscard]] void* allocate(std::size_t Size, int = 0)
        {
            return AllocateAligned(Size, alignof(std::max_align_t), 0);
        }

        [[nodiscard]] void* allocate(
            std::size_t Size,
            std::size_t Alignment,
            std::size_t AlignmentOffset,
            int = 0)
        {
            return AllocateAligned(Size, Alignment, AlignmentOffset);
        }

        void deallocate(void* Memory, std::size_t) noexcept
        {
            if (Memory != nullptr)
            {
                void* RawMemory = nullptr;
                std::memcpy(
                    &RawMemory,
                    static_cast<unsigned char*>(Memory) - sizeof(void*),
                    sizeof(RawMemory));
                std::free(RawMemory);
            }
        }

        [[nodiscard]] const char* get_name() const noexcept
        {
            return "Ardashir EASTL allocator";
        }

        void set_name(const char*) noexcept
        {
        }

    private:
        [[nodiscard]] static void* AllocateAligned(
            std::size_t Size,
            std::size_t Alignment,
            std::size_t AlignmentOffset)
        {
            if (Alignment < alignof(void*))
            {
                Alignment = alignof(void*);
            }

            if ((Alignment & (Alignment - 1)) != 0
                || Size > std::numeric_limits<std::size_t>::max()
                    - Alignment - sizeof(void*))
            {
                return nullptr;
            }

            AlignmentOffset %= Alignment;
            void* const RawMemory =
                std::malloc(Size + Alignment - 1 + sizeof(void*));
            if (RawMemory == nullptr)
            {
                return nullptr;
            }

            const std::uintptr_t Start =
                reinterpret_cast<std::uintptr_t>(RawMemory) + sizeof(void*);
            const std::uintptr_t Aligned =
                ((Start - AlignmentOffset + Alignment - 1)
                    & ~(static_cast<std::uintptr_t>(Alignment) - 1))
                + AlignmentOffset;
            void* const Memory = reinterpret_cast<void*>(Aligned);
            std::memcpy(
                static_cast<unsigned char*>(Memory) - sizeof(void*),
                &RawMemory,
                sizeof(RawMemory));
            return Memory;
        }
    };

    [[nodiscard]] inline ArdaEASTLAllocator* GetArdaEASTLAllocator() noexcept
    {
        static ArdaEASTLAllocator Allocator;
        return &Allocator;
    }

    inline bool operator==(
        const ArdaEASTLAllocator&,
        const ArdaEASTLAllocator&) noexcept
    {
        return true;
    }

    inline bool operator!=(
        const ArdaEASTLAllocator&,
        const ArdaEASTLAllocator&) noexcept
    {
        return false;
    }
}

#define EASTL_USER_DEFINED_ALLOCATOR 1
#define EASTLAllocatorType eastl::ArdaEASTLAllocator
#define EASTLAllocatorDefault eastl::GetArdaEASTLAllocator
