#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

// EASTL make_shared requests ordinary allocation even for 16-byte-aligned
// values. MSVC's max_align_t is only 8 bytes, so guarantee at least 16 here and
// advertise that same minimum to containers which select an allocation path.
#define EASTL_ALLOCATOR_MIN_ALIGNMENT ((alignof(std::max_align_t) > 16) ? alignof(std::max_align_t) : 16)

// EASTL's narrow string formatting uses the platform C runtime. This project
// does not link EAStdC, so provide EASTL's supported user formatting hook.
#define EASTL_EASTDC_VSNPRINTF 0

inline int Vsnprintf8(char* Destination, std::size_t Capacity, const char* Format, std::va_list Arguments)
{
	return std::vsnprintf(Destination, Capacity, Format, Arguments);
}

namespace eastl
{
	class FArdaEASTLAllocator
	{
	public:
		explicit FArdaEASTLAllocator(const char* = nullptr) noexcept
		{
		}

		FArdaEASTLAllocator(const FArdaEASTLAllocator&) noexcept = default;

		FArdaEASTLAllocator(const FArdaEASTLAllocator&, const char*) noexcept
		{
		}

		FArdaEASTLAllocator& operator=(const FArdaEASTLAllocator&) noexcept = default;

		[[nodiscard]] void* allocate(std::size_t Size, int = 0)
		{
			return AllocateAligned(Size, EASTL_ALLOCATOR_MIN_ALIGNMENT, 0);
		}

		[[nodiscard]] void* allocate(std::size_t Size, std::size_t Alignment, std::size_t AlignmentOffset, int = 0)
		{
			return AllocateAligned(Size, Alignment, AlignmentOffset);
		}

		void deallocate(void* Memory, std::size_t) noexcept
		{
			if (Memory != nullptr)
			{
				void* RawMemory = nullptr;
				std::memcpy(&RawMemory, static_cast<unsigned char*>(Memory) - sizeof(void*), sizeof(RawMemory));
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
		[[nodiscard]] static void* AllocateAligned(std::size_t Size, std::size_t Alignment, std::size_t AlignmentOffset)
		{
			if (Alignment < alignof(void*))
			{
				Alignment = alignof(void*);
			}

			if ((Alignment & (Alignment - 1)) != 0 ||
			    Size > std::numeric_limits<std::size_t>::max() - Alignment - sizeof(void*))
			{
				return nullptr;
			}

			AlignmentOffset %= Alignment;
			void* const RawMemory = std::malloc(Size + Alignment - 1 + sizeof(void*));
			if (RawMemory == nullptr)
			{
				return nullptr;
			}

			const std::uintptr_t Start = reinterpret_cast<std::uintptr_t>(RawMemory) + sizeof(void*);
			const std::uintptr_t Aligned =
			    ((Start - AlignmentOffset + Alignment - 1) & ~(static_cast<std::uintptr_t>(Alignment) - 1)) +
			    AlignmentOffset;
			void* const Memory = reinterpret_cast<void*>(Aligned);
			std::memcpy(static_cast<unsigned char*>(Memory) - sizeof(void*), &RawMemory, sizeof(RawMemory));
			return Memory;
		}
	};

	[[nodiscard]] inline FArdaEASTLAllocator* GetArdaEASTLAllocator() noexcept
	{
		static FArdaEASTLAllocator Allocator;
		return &Allocator;
	}

	inline bool operator==(const FArdaEASTLAllocator&, const FArdaEASTLAllocator&) noexcept
	{
		return true;
	}

	inline bool operator!=(const FArdaEASTLAllocator&, const FArdaEASTLAllocator&) noexcept
	{
		return false;
	}
}

#define EASTL_USER_DEFINED_ALLOCATOR 1
#define EASTLAllocatorType eastl::FArdaEASTLAllocator
#define EASTLAllocatorDefault eastl::GetArdaEASTLAllocator
