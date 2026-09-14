#pragma once

#include "ArdaDependencyGraph.h"
#include <cstring>
#include <type_traits>

namespace arda
{
	/** Canonical byte encoding shared by node identities. Encode semantic fields explicitly;
	 * never hash structure padding or native resource addresses in place of graph handles.
	 * Values use their declared width in little-endian order. Strings and byte spans are length-prefixed.
	 */
	class FArdaDependencyKeyBuilder
	{
	public:
		template <class T>
		FArdaDependencyKeyBuilder& Value(T Input)
		{
			static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>, "Keys require scalar semantic values.");
			static_assert(sizeof(T) <= sizeof(uint64_t), "Encode wider values as separate semantic fields.");

			// Preserve floating-point and enum representations without depending on host byte order.
			uint8_t Representation[sizeof(T)];
			std::memcpy(Representation, &Input, sizeof(T));
			const uint16_t EndianProbe = 1;
			const bool LittleEndian = *reinterpret_cast<const uint8_t*>(&EndianProbe) == 1;
			for (size_t Index = 0; Index < sizeof(T); ++Index)
			{
				mBytes.push_back(static_cast<char>(Representation[LittleEndian ? Index : sizeof(T) - 1 - Index]));
			}

			return *this;
		}

		FArdaDependencyKeyBuilder& Resource(FArdaDependencyResourceHandle Handle)
		{
			return Value(Handle.mGraph).Value(Handle.mIndex).Value(Handle.mGeneration);
		}

		FArdaDependencyKeyBuilder& BufferRange(const FArdaRHIBufferRange& Range)
		{
			return Value(Range.mByteOffset).Value(Range.mByteSize);
		}

		FArdaDependencyKeyBuilder& TextureRange(const FArdaRHITextureSubresourceRange& Range)
		{
			return Value(Range.mBaseMipLevel)
			    .Value(Range.mMipLevelCount)
			    .Value(Range.mBaseArraySlice)
			    .Value(Range.mArraySliceCount)
			    .Value(Range.mBasePlane)
			    .Value(Range.mPlaneCount);
		}

		/** Data must point to Size readable bytes; an empty span may use nullptr. */
		FArdaDependencyKeyBuilder& Bytes(const void* Data, size_t Size)
		{
			Value(static_cast<uint64_t>(Size));
			if (Size)
			{
				mBytes.append(static_cast<const char*>(Data), Size);
			}

			return *this;
		}

		FArdaDependencyKeyBuilder& String(const eastl::string& Text)
		{
			return Bytes(Text.data(), Text.size());
		}

		/** Returns the complete identity bytes, not a collision-prone hash digest. */
		eastl::string Build() const
		{
			return mBytes;
		}

	private:
		eastl::string mBytes;
	};
}
