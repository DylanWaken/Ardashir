/** Compiled model subgraphs with separate, replaceable immutable state dictionaries. */
#pragma once

#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/Resources/ArdaGraphTensor.h"
#include <filesystem>

namespace arda
{
	/** A named tensor at a compiled package boundary. */
	struct FArdaGraphPackageBinding
	{
		/** Interface name published by the offline compiler. */
		eastl::string mName;
		/** Tensor view backed by this edit's ordinary graph buffer. */
		FArdaGraphTensor mTensor;
	};

	/** Reflected static tensor contract. */
	struct FArdaGraphPackageTensor
	{
		/** Interface or diagnostic tensor name. */
		eastl::string mName;
		/** Package-local backing buffer index. */
		uint32_t mBuffer = 0;
		/** Bit zero denotes input, bit one denotes output; zero is internal. */
		uint8_t mRole = 0;
		/** Backend tensor dimensions, dtype, element strides and storage origin. */
		FArdaRHITensorDesc mDesc;
		/** Required backing storage extent, including other views and compiler padding in this buffer. */
		uint64_t mStorageByteSize = 0;
	};

	/** Named model state expected by a version-two descriptor. Each slot owns a complete dense buffer. */
	struct FArdaGraphPackageStateSlot
	{
		/** Stable parameter or persistent-buffer key shared with checkpoint files. */
		eastl::string mName;
		/** Package-local immutable backing buffer index. */
		uint32_t mBuffer = 0;
		/** Exact dtype, dimensions and contiguous storage layout required by the compiled graph. */
		FArdaRHITensorDesc mDesc;
		/** Exact checkpoint payload size, equal to the complete backing buffer capacity. */
		uint64_t mStorageByteSize = 0;
	};

	/** Results remain ordinary tensor views in the caller's graph. */
	struct FArdaGraphPackageInvocation
	{
		/** Named input/output views, including automatically allocated outputs. */
		eastl::vector<FArdaGraphPackageBinding> mBindings;
		/** Find a named interface; returns null if absent. */
		[[nodiscard]] const FArdaGraphTensor* Find(const eastl::string& Name) const;
	};

	/** Validated immutable model descriptor and optional state snapshot. Invocation expands its subgraph
	 * into the caller's graph, including a typed subgraph's Build edit. Version two separates named state
	 * from compiled code, tensor contracts and ordered dispatch/fill/copy recipes. Version-one packages
	 * with embedded weights remain readable. Embedded code is trusted executable content.
	 */
	class FArdaGraphPackage final
	{
	public:
		/** Parse a bounded complete package and validate every reference before registering its definitions. */
		[[nodiscard]] static TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> Deserialize(
		    const eastl::string& Bytes);
		/** Load an .ardagpu package using the shared Backend file service. */
		[[nodiscard]] static TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> Load(
		    const std::filesystem::path& Path);
		/** Validate a complete .ardastate checkpoint against every state slot and return a new snapshot.
		 * Keys, dtype, dimensions and byte counts must match exactly. This descriptor and previous
		 * invocations remain unchanged on success or failure. Legacy packages have no replaceable slots.
		 */
		[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> DeserializeStateDict(
		    const eastl::string& Bytes) const;
		/** Load a separate .ardastate file through the Backend file service and return a new snapshot. */
		[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const FArdaGraphPackage>> LoadStateDict(
		    const std::filesystem::path& Path) const;
		/** Inspect the immutable package name. */
		[[nodiscard]] const eastl::string& GetName() const noexcept;
		/** Inspect tensor interface contracts before allocating or binding buffers. */
		[[nodiscard]] const eastl::vector<FArdaGraphPackageTensor>& GetTensors() const noexcept;
		/** Inspect required checkpoint keys and their static tensor contracts. */
		[[nodiscard]] const eastl::vector<FArdaGraphPackageStateSlot>& GetStateSlots() const noexcept;
		/** Whether every declared state slot is loaded; true when the descriptor requires no state. */
		[[nodiscard]] bool HasStateDict() const noexcept;
		/** Bind required inputs and optional outputs; allocate storage and merge this model's operations.
		 * Required state must be loaded before invocation. Its immutable bytes initialize graph storage once.
		 * All dependencies and execution use the caller's edit. Failure is retained for Edit.Commit.
		 */
		[[nodiscard]] TArdaRHIResult<FArdaGraphPackageInvocation> Invoke(FArdaGraphEdit& Edit,
		    const eastl::vector<FArdaGraphPackageBinding>& Bindings,
		    eastl::string Name = {}) const;

	private:
		struct FArdaImpl;
		explicit FArdaGraphPackage(eastl::shared_ptr<const FArdaImpl> Impl);
		eastl::shared_ptr<const FArdaImpl> mImpl;
	};
}
