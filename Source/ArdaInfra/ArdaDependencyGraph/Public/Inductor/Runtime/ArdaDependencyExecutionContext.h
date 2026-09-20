/** Declared resource and pipeline access inside a recorded dependency graph node. */
#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"
#include "Inductor/Rules/ArdaInductorPipeline.h"

namespace arda
{
	class FArdaInductorPassContext;

	/** Typed callbacks resolve only their declared resources and automatically assigned pipeline slots. */
	class FArdaDependencyExecutionContext
	{
	public:
		IArdaRHICommandList& GetCommands() const;
		FArdaRHIDeviceRef GetDevice() const;
		FArdaRHIBufferRef GetBuffer(FArdaDependencyResourceHandle Resource) const;
		FArdaRHIBufferRef GetWorkspaceBuffer() const;
		uint32_t GetFrameIndex() const;
		uint64_t GetFrameSequence() const;
		/** Records an asynchronous readback. Execute waits after graph submission and
		 * publishes bytes only if every submission/completion succeeds. Failed frames
		 * clear registered destinations; unsubmitted command lists cancel their waits.
		 */
		FArdaRHIStatus ReadbackBuffer(FArdaDependencyResourceHandle Resource,
		    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
		    uint64_t SourceOffset = 0,
		    uint64_t Size = ArdaRHIWholeBuffer) const;
		/** Records a graphics-queue texture readback with completion-safe publication.
		 * Resource must declare the selected subresource in CopySource state. Supports typed,
		 * single-sample, uncompressed color regions. Output contains tightly packed rows in increasing
		 * Y order, followed by increasing Z slices; native row padding is removed after completion.
		 * The retained destination follows ReadbackBuffer's successful-frame and failure semantics.
		 * Declare mTransientWorkspaceBytes at least GetArdaRHITextureBufferFootprint(...).mValue.mByteSize.
		 * The graph budgets and allocates this workspace per frame; its UAV state is restored after recording.
		 */
		FArdaRHIStatus ReadbackTexture(FArdaDependencyResourceHandle Resource,
		    eastl::shared_ptr<eastl::vector<uint8_t>> Destination,
		    const FArdaRHITextureSlice& Slice = {}) const;
		FArdaRHITextureRef GetTexture(FArdaDependencyResourceHandle Resource) const;
		/** Returns a declared acceleration structure access for the current node. */
		FArdaRHIAccelStructRef GetAccelerationStructure(FArdaDependencyResourceHandle Resource) const;
		const FArdaInductorResolvedPipeline* GetPipeline(const eastl::string& Slot = "default") const;
		/** Retained sets prepared for this node/frame slot; lookup performs no binding creation. */
		const eastl::vector<FArdaRHIBindingSetRef>& GetBindings(const eastl::string& Slot = "default") const;
		/** Bind the compiled compute pipeline and its automatically prepared shader arguments. */
		/** Prepared ray table and descriptor tables remain stable throughout a compiled frame slot. */
		const FArdaRHIShaderTableRef& GetShaderTable(const eastl::string& Slot = "default") const;
		const FArdaRHIDescriptorTableRef& GetDescriptorTable(const eastl::string& Name) const;
		FArdaRHIStatus SetRayTracingState(const eastl::string& Slot = "default") const;
		FArdaRHIStatus DispatchWorkGraph(const void* Records,
		    uint32_t RecordCount,
		    uint32_t RecordStride,
		    const eastl::string& Slot = "default") const;
		/** Apply the frozen push-constant bytes declared by BindShader; state helpers call this automatically. */
		FArdaRHIStatus ApplyShaderParameters(const eastl::string& Slot = "default") const;
		FArdaRHIStatus SetComputeState(const eastl::string& Slot = "default") const;
		/** Complete draw state with the compiled pipeline, framebuffer and shader bindings. */
		FArdaRHIStatus SetGraphicsState(FArdaRHIGraphicsState State, const eastl::string& Slot = "default") const;
		/** Complete mesh state with the compiled pipeline, framebuffer and shader bindings. */
		FArdaRHIStatus SetMeshletState(FArdaRHIMeshletState State, const eastl::string& Slot = "default") const;
		FArdaRHIFramebufferRef GetFramebuffer() const;
		FArdaGraphNodeHandle GetNode() const;

	private:
		friend class FArdaInductor;
		friend class FArdaDependencyGraph;
		friend struct FArdaInductorRuntime;
		FArdaDependencyGraph::FArdaImpl* mGraph = nullptr;
		FArdaInductorFrame* mFrame = nullptr;
		FArdaInductorPassContext* mPass = nullptr;
		FArdaGraphNodeHandle mNode;
	};
}
