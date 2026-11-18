/** Typed tensor layouts over existing graph buffer identities. */
#pragma once

#include "Graph/Node/ArdaGraphArchive.h"
#include "Graph/Resources/ArdaGraphValue.h"
#include "RHI/Resources/ArdaRHITensor.h"

namespace arda
{
	class FArdaDependencyExecutionContext;

	/** Non-owning tensor value sharing its buffer's lifetime, aliases, and allocation.
	 * The Backend descriptor owns shape and dtype semantics; the graph adds only logical storage.
	 */
	struct FArdaGraphTensor
	{
		/** Logical storage in the same graph as the consuming operation. */
		FArdaGraphBuffer mBuffer;
		/** Concrete layout relative to the logical buffer origin. */
		FArdaRHITensorDesc mDesc;
	};

	/** Resolves a declared tensor through the normal execution resource lookup and checks storage bounds.
	 * bRequireNonOverlapping enforces independent writable elements. This performs no allocation or copy.
	 */
	[[nodiscard]] TArdaRHIResult<FArdaRHITensorView> ResolveArdaGraphTensor(
	    const FArdaDependencyExecutionContext& Context,
	    const FArdaGraphTensor& Tensor,
	    bool bRequireNonOverlapping = false);

	/** Portable tensor layout codec: dtype, rank, dimension/stride pairs, then byte origin.
	 * Scalar widths are explicit; there is no host padding, resource pointer, or device address.
	 */
	template <>
	struct TArdaGraphCodec<FArdaRHITensorDesc>
	{
		/** Tensor layouts have an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encodes only a validated concrete tensor descriptor. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHITensorDesc& Value);
		/** Validates decoded layout arithmetic before replacing the output descriptor. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHITensorDesc& Value);
	};

	/** Tensor graph fields use the same file-local buffer identities as untyped graph values. */
	template <>
	struct TArdaGraphCodec<FArdaGraphTensor>
	{
		/** Graph tensors have an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encodes a normal typed buffer reference followed by its shared Backend layout. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaGraphTensor& Value);
		/** Remaps and type-checks the buffer reference, preserving the output on malformed data. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaGraphTensor& Value);
	};
}
