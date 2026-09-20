/** Typed logical values used by the operation-based graph authoring interface. */
#pragma once

#include "Graph/Resources/ArdaDependencyResource.h"

namespace arda
{
	class FArdaGraphEdit;

	/** Storage category checked when a logical handle enters the typed facade. */
	enum class EArdaGraphResourceKind : uint8_t
	{
		Buffer,
		Texture,
		AccelerationStructure
	};

	/** Non-owning logical value. Copies refer to the same resource, including in-place writes.
	 * The graph owns storage; canceled resources are rejected by their generation on subsequent use.
	 * A value may outlive an edit, but must only be used with its owning live graph.
	 */
	template <EArdaGraphResourceKind Kind>
	class TArdaGraphValue final
	{
	public:
		/** Constructs an empty value; operations reject it as an input or explicit destination. */
		TArdaGraphValue() = default;

		/** Exposes the logical identity for lower-level graph APIs. */
		FArdaDependencyResourceHandle GetHandle() const noexcept
		{
			return mHandle;
		}

		/** Allows typed values in existing custom-node parameter structs. */
		operator FArdaDependencyResourceHandle() const noexcept
		{
			return mHandle;
		}

		/** Tests whether an identity was assigned; graph operations also validate its current lifetime. */
		explicit operator bool() const noexcept
		{
			return bool(mHandle);
		}

	private:
		friend class FArdaGraphEdit;

		explicit TArdaGraphValue(FArdaDependencyResourceHandle Handle)
		    : mHandle(Handle)
		{
		}

		FArdaDependencyResourceHandle mHandle;
	};

	/** A typed graph buffer, passed by value between operations. */
	using FArdaGraphBuffer = TArdaGraphValue<EArdaGraphResourceKind::Buffer>;
	/** A typed graph texture; render targets use the same value type and descriptor usage flags. */
	using FArdaGraphTexture = TArdaGraphValue<EArdaGraphResourceKind::Texture>;
	/** A typed imported acceleration structure. */
	using FArdaGraphAccelerationStructure = TArdaGraphValue<EArdaGraphResourceKind::AccelerationStructure>;
}
