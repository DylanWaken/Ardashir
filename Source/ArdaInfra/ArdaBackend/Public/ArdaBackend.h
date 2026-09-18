/** @file ArdaBackend.h
 * Complete public entry point for backend configuration, devices and RHI calls.
 * Individual modules may be included directly to limit dependencies.
 */
#pragma once

#include "RHI/Config/ArdaAssert.h"
#include "RHI/Config/ArdaLog.h"
#include "RHI/Device/ArdaBackendDevice.h"
#include "RHI/Interop/ArdaExternalInterop.h"
#include "RHI/Scheduling/ArdaSwapChain.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Pipelines/ArdaRHIProviderPipelineCache.h"
#include "RHI/Pipelines/ArdaPipelineStateCache.h"
#include "RHI/Shaders/ArdaShaderCompiler.h"
#include "RHI/Shaders/ArdaShaderDirectories.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"
#include "RHI/Shaders/ArdaComputeOperand.h"
#include "RHI/CUDA/ArdaCudaExternalCall.h"
#include "RHI/CUDA/ArdaCudaKernelVariants.h"
#include "RHI/CUDA/ArdaCudaParameters.h"
#include "RHI/Context/ArdaCudaContext.h"
#include "RHI/Context/ArdaCudaSelectionContext.h"
#include "RHI/Scheduling/ArdaCudaSequence.h"
#include "RHI/Scheduling/ArdaCudaSubmission.h"
#include "RHI/Resources/ArdaCudaTextureBuffer.h"
#include "RHI/Resources/ArdaRHISubresources.h"
#include "RHI/Memory/ArdaMemoryPlanner.h"
#include "FileOperations/ArdaFileOperations.h"
#include "RHI/Providers/ArdaExternalDeviceProvider.h"
#include "RHI/Providers/ArdaExternalResourceProvider.h"
#include "RHI/Providers/ArdaCudaContextProvider.h"
