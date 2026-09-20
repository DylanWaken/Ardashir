/** Typed resource operations that record their nodes into an open graph edit. */
#pragma once

#include "Graph/Node/ArdaGraphEdit.h"

#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace arda
{
	/** Storage policy for graph-owned variables across executions of a persistent revision. */
	enum class EArdaGraphResourceLifetime : uint8_t
	{
		/** Storage may alias other variables when their lifetimes do not overlap. */
		Transient,
		/** Retain dedicated storage and its contents across executions of the selected revision. */
		Persistent
	};

	/** Declare buffer storage without native allocation; name and storage lifetime are optional. */
	FArdaGraphBuffer AllocateBuffer(FArdaGraphEdit& Edit,
	    const FArdaRHIBufferDesc& Description,
	    eastl::string Name = {},
	    EArdaGraphResourceLifetime Lifetime = EArdaGraphResourceLifetime::Transient);
	/** Declare texture storage without native allocation; name and storage lifetime are optional. */
	FArdaGraphTexture AllocateTexture(FArdaGraphEdit& Edit,
	    const FArdaRHITextureDesc& Description,
	    eastl::string Name = {},
	    EArdaGraphResourceLifetime Lifetime = EArdaGraphResourceLifetime::Transient);

	/** Retain an existing native buffer as a graph variable. */
	FArdaGraphBuffer BindBuffer(FArdaGraphEdit& Edit, FArdaRHIBufferRef Buffer, eastl::string Name = {});
	/** Retain an existing native texture as a graph variable. */
	FArdaGraphTexture BindTexture(FArdaGraphEdit& Edit, FArdaRHITextureRef Texture, eastl::string Name = {});
	/** Retain an existing native BLAS or TLAS as a graph variable. */
	FArdaGraphAccelerationStructure BindAccelerationStructure(FArdaGraphEdit& Edit,
	    FArdaRHIAccelStructRef AccelerationStructure,
	    eastl::string Name = {});

	/** Record an upload of owned bytes and return the destination for further operations. */
	FArdaGraphBuffer Upload(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Destination,
	    eastl::vector<uint8_t> Bytes,
	    uint64_t DestinationOffset = 0);
	/** Upload owned, tightly packed texels to a texture region and return the destination. */
	FArdaGraphTexture Upload(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Destination,
	    eastl::vector<uint8_t> Bytes,
	    FArdaRHITextureSlice Slice = {});

	/** Copy a byte range through FArdaMemoryCopyBufferNode and return the destination.
	 * Both variables must belong to this graph.
	 */
	FArdaGraphBuffer CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Source,
	    FArdaGraphBuffer Destination,
	    uint64_t ByteSize = ArdaRHIWholeBuffer,
	    uint64_t SourceOffset = 0,
	    uint64_t DestinationOffset = 0);
	/** Copy texture regions through FArdaMemoryCopyTextureNode and return the destination. */
	FArdaGraphTexture CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Source,
	    FArdaGraphTexture Destination,
	    FArdaRHITextureSlice SourceSlice = {},
	    FArdaRHITextureSlice DestinationSlice = {});

	/** Copy buffer texel rows through FArdaGraphBufferToTextureNode and return the destination texture.
	 * SourceLayout selects the buffer byte offset and row pitch. Zero pitch infers the portable aligned
	 * row pitch for DestinationSlice; the byte offset is preserved and the buffer must fit that layout.
	 */
	FArdaGraphTexture CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Source,
	    FArdaGraphTexture Destination,
	    FArdaRHITextureSlice DestinationSlice = {},
	    FArdaRHITextureBufferLayout SourceLayout = {});
	/** Copy texture texels through FArdaGraphTextureToBufferNode and return the destination buffer.
	 * DestinationLayout selects the buffer byte offset and row pitch. Zero pitch infers the portable
	 * aligned row pitch for SourceSlice; the byte offset is preserved and the buffer must fit that layout.
	 */
	FArdaGraphBuffer CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Source,
	    FArdaGraphBuffer Destination,
	    FArdaRHITextureSlice SourceSlice = {},
	    FArdaRHITextureBufferLayout DestinationLayout = {});

	/** Clone imported BLAS/TLAS storage through FArdaMemoryCopyAccelerationStructureNode.
	 * Return the destination, whose kind/build flags must match and capacity must fit the source.
	 */
	FArdaGraphAccelerationStructure CopyTo(FArdaGraphEdit& Edit,
	    FArdaGraphAccelerationStructure Source,
	    FArdaGraphAccelerationStructure Destination);

	/** Fill a complete GPU buffer with a repeated 32-bit value and return the destination. */
	FArdaGraphBuffer Clear(FArdaGraphEdit& Edit, FArdaGraphBuffer Destination, uint32_t Value = 0);
	/** Clear selected color texture subresources and return the destination. */
	FArdaGraphTexture Clear(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Destination,
	    FArdaRHIColor Color = {},
	    FArdaRHITextureSubresourceRange Range = {});

	/** Infer a new GPU buffer from the source descriptor and record a complete copy into it. */
	FArdaGraphBuffer Clone(FArdaGraphEdit& Edit, FArdaGraphBuffer Source);

	/** Record readback and allocate its retained CPU output automatically. Consume after Execute/Wait
	 * succeeds; do not access while completion may publish or clear it. Serialize executions sharing it.
	 * Failure returns an empty pointer and is retained by Edit for Commit to report.
	 */
	eastl::shared_ptr<eastl::vector<uint8_t>> Readback(FArdaGraphEdit& Edit,
	    FArdaGraphBuffer Source,
	    uint64_t SourceOffset = 0,
	    uint64_t ByteSize = ArdaRHIWholeBuffer);
	/** Read texture texels into an automatically retained CPU vector, published after Execute/Wait.
	 * Do not access while completion may publish or clear it; serialize executions sharing the vector.
	 * Failure returns an empty pointer and is retained by Edit for Commit to report.
	 */
	eastl::shared_ptr<eastl::vector<uint8_t>> Readback(FArdaGraphEdit& Edit,
	    FArdaGraphTexture Source,
	    FArdaRHITextureSlice Slice = {});
}
