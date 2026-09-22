#pragma once

#include "Graph/Resources/ArdaDependencyResource.h"
#include "Graph/Resources/ArdaGraphicsState.h"
#include <cstring>
#include <EASTL/type_traits.h>

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
			static_assert(eastl::is_arithmetic_v<T> || eastl::is_enum_v<T>, "Keys require scalar semantic values.");
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
			// Construction identity follows logical slots across fresh frame descriptions.
			// Ordinary keys retain generations so attachment and stale-handle checks stay strict.
			if (mConstructionContext)
			{
				const bool bValid = Handle.mGraph == mConstructionContext->mGraphIdentity &&
				    mConstructionContext->mValidate(mConstructionContext->mGraph, Handle);
				// Keep invalid serialized handles disjoint from a normalized valid resource.
				Value(bValid);
				if (bValid)
				{
					return Value(uint64_t{0}).Value(Handle.mIndex).Value(uint64_t{0});
				}
			}
			return Value(Handle.mGraph).Value(Handle.mIndex).Value(Handle.mGeneration);
		}

		FArdaDependencyKeyBuilder& BufferRange(const FArdaRHIBufferRange& Range)
		{
			return Value(Range.mByteOffset).Value(Range.mByteSize);
		}

		/** Encodes a logical stage connection, normalizing valid producer identities for revision reuse. */
		FArdaDependencyKeyBuilder& Stage(FArdaGraphStageValue Input)
		{
			const auto Handle = Input.GetProducer();
			Value(Input.GetKind());
			if (mConstructionContext)
			{
				const bool bValid = mConstructionContext->mValidateStage(mConstructionContext->mGraph, Handle);
				Value(bValid);
				if (bValid)
				{
					return Value(uint64_t{0}).Value(Handle.GetIndex()).Value(uint32_t{0});
				}
			}
			return Value(Handle.GetGraphIdentity()).Value(Handle.GetIndex()).Value(Handle.GetGeneration());
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
		friend class FArdaDependencyGraph;

		struct FArdaConstructionResourceContext
		{
			uint64_t mGraphIdentity;
			const void* mGraph;
			bool (*mValidate)(const void*, FArdaDependencyResourceHandle);
			bool (*mValidateStage)(const void*, FArdaGraphNodeHandle);
		};

		/** Scoped by attachment while asking a node for its construction identity. */
		inline static thread_local const FArdaConstructionResourceContext* mConstructionContext = nullptr;
		eastl::string mBytes;
	};
}
