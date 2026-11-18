#pragma once

#include "CoreMinimal.h"
#include "RHIResources.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include <EASTL/functional.h>

class FRHICommandListImmediate;
class FRHIComputeShader;
class FRHITexture;
class FRHIBuffer;

namespace arda
{
	/** Registers the external-only unreal-rhi backend and its host device provider. */
	ARDAUNREALRUNTIME_API bool RegisterUnrealRHIBackend();

	/** Unregisters after Arda shutdown; fails while Arda still owns an active device. */
	ARDAUNREALRUNTIME_API bool UnregisterUnrealRHIBackend();

	/**
	 * Borrows the render thread's immediate graphics list. Arda calls append directly in order,
	 * so Unreal calls may be interleaved without a queue flush, context change, or CPU/GPU wait.
	 * Each Arda list must be opened, closed and executed inside this scope; do not cache lists.
	 * Deferred Arda lists replay at ExecuteCommandList; immediate lists append during recording.
	 * Nested scopes and async-compute lists are rejected. The host declares RDG resource
	 * accesses and resynchronizes imported resource states after any Unreal-side transitions.
	 * Compute pipeline/parameter bindings are caller-owned state: rebind them when switching users.
	 */
	class ARDAUNREALRUNTIME_API FArdaUnrealRHIScope final
	{
	public:
		/** Borrows CommandList for this render-thread scope; reports misuse through GetStatus. */
		explicit FArdaUnrealRHIScope(FRHICommandListImmediate& CommandList);
		/** Ends borrowing without submitting or waiting; all recording lists must have been closed. */
		~FArdaUnrealRHIScope();
		/** Scope ownership cannot be copied. */
		FArdaUnrealRHIScope(const FArdaUnrealRHIScope&) = delete;
		/** Scope ownership cannot be assigned. */
		FArdaUnrealRHIScope& operator=(const FArdaUnrealRHIScope&) = delete;
		/** Returns success only when this scope owns the current borrowed graphics list. */
		[[nodiscard]] const FArdaRHIStatus& GetStatus() const noexcept;

	private:
		FArdaRHIStatus mStatus;
		bool mbActive = false;
	};

	/**
	 * Imports an Unreal-compiled compute shader through Arda CreateShader. Retains its RHI reference.
	 * Shader binaries and reflection remain owned by Unreal's shader compiler and cook pipeline.
	 * Bindings use Unreal reflected resource indices, not assumed HLSL register numbers.
	 */
	[[nodiscard]] ARDAUNREALRUNTIME_API TArdaRHIResult<FArdaRHIShaderRef> ImportUnrealComputeShader(
	    FArdaRHIDeviceRef Device,
	    FRHIComputeShader* Shader);

	/** Imports a retained 2D single-sample color texture; the state must be explicit, not Common/Unknown. */
	[[nodiscard]] ARDAUNREALRUNTIME_API TArdaRHIResult<FArdaRHITextureRef> ImportUnrealTexture(FArdaRHIDeviceRef Device,
	    FRHITexture* Texture,
	    EArdaRHIResourceState InitialState);

	/** Imports a retained buffer; size/stride/usage must match, with an explicit state other than Common/Unknown. */
	[[nodiscard]] ARDAUNREALRUNTIME_API TArdaRHIResult<FArdaRHIBufferRef> ImportUnrealBuffer(FArdaRHIDeviceRef Device,
	    FRHIBuffer* Buffer,
	    const FArdaRHIBufferDesc& Desc);

	/**
	 * Records Unreal parameter binding or other host commands in the sole currently open Arda list.
	 * Deferred lists retain the callback until execution; capture owning RHI references and values.
	 * The callback may not open/execute Arda lists or outlive the containing Unreal RHI scope.
	 */
	[[nodiscard]] ARDAUNREALRUNTIME_API FArdaRHIStatus RecordUnrealCommands(
	    eastl::function<FArdaRHIStatus(FRHICommandListImmediate&)> Callback);

	/** Host RHI resources resolved from a normal Arda CUDA binding table. */
	struct FArdaUnrealCudaBinding
	{
		/** Retained texture, for surface bindings only. */
		FTextureRHIRef mTexture;
		/** Retained buffer, for linear bindings only. */
		FBufferRHIRef mBuffer;
		/** Declared resource access for the entire CUDA batch. */
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		/** Buffer or surface representation requested by Arda. */
		EArdaComputeBindingType mType = EArdaComputeBindingType::Buffer;
		/** Resolved byte range for buffer bindings. */
		FArdaRHIBufferRange mBufferRange;
		/** Texture mip to expose as a surface. */
		uint32 mMipLevel = 0;
	};

	/**
	 * Qualified host CUDA interop installed before Arda initializes. UE's portable RHI does not
	 * expose CUDA mappings or queue semaphore handles, so each native host bridge owns that policy.
	 * A bridge must validate all operations and parameter patches before handing resources to CUDA,
	 * launch the actual supplied compiled entries, order both directions, and retain mappings/code
	 * until the final graphics consumer retires. Unsupported external calls/capture must fail.
	 */
	class IArdaUnrealCudaInterop
	{
	public:
		/** Destroys mappings only after all CUDA and graphics consumers have completed. */
		virtual ~IArdaUnrealCudaInterop() = default;
		/** Reports the adapter-qualified mode and limits used by Arda kernel validation. */
		[[nodiscard]] virtual FArdaCudaCapabilities GetCapabilities() const = 0;
		/** Returns sharing information only for textures whose CUDA mapping already exists. */
		[[nodiscard]] virtual FArdaCudaResourceInfo DescribeTexture(FRHITexture* Texture) const = 0;
		/** Records the exact ordered kernels, patches resources, and performs an explicit host handoff. */
		virtual FArdaRHIStatus Dispatch(FRHICommandListImmediate& CommandList,
		    const eastl::vector<FArdaUnrealCudaBinding>& Bindings,
		    const eastl::vector<FArdaCudaKernel>& Kernels) = 0;
	};

	/** Installs or clears the retained host CUDA bridge before initialization; rejects active devices. */
	[[nodiscard]] ARDAUNREALRUNTIME_API bool SetUnrealCudaInterop(eastl::shared_ptr<IArdaUnrealCudaInterop> Interop);
}
