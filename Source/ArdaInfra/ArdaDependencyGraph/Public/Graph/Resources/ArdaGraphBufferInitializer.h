/** Immutable host bytes defining the initial prefix of graph-owned buffer storage. */
#pragma once

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Shares immutable initialization data across graph snapshots without copying checkpoint bytes.
	 * These are initial contents, not a per-frame upload or a promise that GPU storage remains constant.
	 */
	class FArdaGraphBufferInitializer final
	{
	public:
		/** Constructs an absent initializer. */
		FArdaGraphBufferInitializer() = default;

		/** Takes ownership of a byte sequence; subsequent copies share immutable storage. */
		explicit FArdaGraphBufferInitializer(eastl::vector<uint8_t> Bytes)
		{
			if (!Bytes.empty())
			{
				mBytes = eastl::make_shared<const eastl::vector<uint8_t>>(eastl::move(Bytes));
			}
		}

		/** Returns the immutable bytes, or null for an absent initializer. */
		[[nodiscard]] const uint8_t* GetData() const noexcept
		{
			return mBytes ? mBytes->data() : nullptr;
		}

		/** Returns the initialized prefix length in bytes. */
		[[nodiscard]] size_t GetSize() const noexcept
		{
			return mBytes ? mBytes->size() : 0;
		}

		/** Tests whether the resource has no declared initial contents. */
		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return !mBytes;
		}

		/** Compares content, allowing persistent storage reuse only for identical initial contents. */
		[[nodiscard]] bool operator==(const FArdaGraphBufferInitializer& Other) const noexcept
		{
			return mBytes == Other.mBytes || (mBytes && Other.mBytes && *mBytes == *Other.mBytes);
		}

		/** Tests whether initial contents differ. */
		[[nodiscard]] bool operator!=(const FArdaGraphBufferInitializer& Other) const noexcept
		{
			return !(*this == Other);
		}

	private:
		eastl::shared_ptr<const eastl::vector<uint8_t>> mBytes;
	};
}
