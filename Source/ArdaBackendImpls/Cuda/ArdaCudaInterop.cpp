/** @file ArdaCudaInterop.cpp
 * Loads the CUDA driver dynamically for D3D12 CiG or precompiled CUDA entry launches.
 * Shared D3D12/Vulkan imports, compiled entries and streams follow graphics-fence lifetime.
 */
#include "ArdaCudaInterop.h"
#include <exception>

#if defined(ARDA_ENABLE_CUDA)
#include <cuda.h>
#if CUDA_VERSION < 12000
#error Ardashir CUDA interop requires CUDA 12.0 or newer headers.
#endif
#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
#include <EASTL/algorithm.h>
#include <EASTL/shared_ptr.h>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <cmath>

namespace arda
{
	// The public cache owns only an opaque provider state and requires no CUDA SDK.
	struct FArdaCudaGraphCacheAccess
	{
		FArdaCudaGraphCache& mCache;
		std::unique_lock<std::mutex> mLock;

		explicit FArdaCudaGraphCacheAccess(FArdaCudaGraphCache& Cache)
		    : mCache(Cache),
		      mLock(Cache.mMutex)
		{
		}

		eastl::shared_ptr<void>& State()
		{
			return mCache.mNativeState;
		}

		FArdaCudaGraphStats& Stats()
		{
			return mCache.mStats;
		}
	};

	struct FArdaCudaTimingQueryAccess
	{
		static FArdaRHIStatus Attach(FArdaCudaTimingQuery& Query,
		    eastl::shared_ptr<void> State,
		    eastl::function<TArdaRHIResult<FArdaCudaTimingResult>()> Poll)
		{
			std::lock_guard<std::mutex> Lock(Query.mMutex);
			if (Query.mNativeState)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "Consume the previous CUDA timing sample before reusing its query.");
			}
			Query.mNativeState = eastl::move(State);
			Query.mPoll = eastl::move(Poll);
			return {};
		}

		static void Cancel(FArdaCudaTimingQuery& Query, const void* State)
		{
			std::lock_guard<std::mutex> Lock(Query.mMutex);
			if (Query.mNativeState.get() == State)
			{
				Query.mPoll = {};
				Query.mNativeState.reset();
			}
		}
	};

	namespace
	{
		// Resolve versioned driver entry points without creating a mandatory CUDA DLL import.
		struct FArdaCudaDriver
		{
#if defined(_WIN32)
			HMODULE mLibrary = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

			void* GetSymbol(const char* Name) const
			{
				return reinterpret_cast<void*>(GetProcAddress(mLibrary, Name));
			}
#else
			void* mLibrary = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);

			void* GetSymbol(const char* Name) const
			{
				return dlsym(mLibrary, Name);
			}
#endif
#define ARDA_CUDA_FUNCTION(Name, Symbol)                                                                               \
	decltype(&::Name) Name = mLibrary ? reinterpret_cast<decltype(Name)>(GetSymbol(Symbol)) : nullptr
			ARDA_CUDA_FUNCTION(cuInit, "cuInit");
			ARDA_CUDA_FUNCTION(cuDeviceGetCount, "cuDeviceGetCount");
			ARDA_CUDA_FUNCTION(cuDeviceGet, "cuDeviceGet");
			ARDA_CUDA_FUNCTION(cuDeviceGetLuid, "cuDeviceGetLuid");
			ARDA_CUDA_FUNCTION(cuDeviceGetUuid, "cuDeviceGetUuid_v2");
			ARDA_CUDA_FUNCTION(cuDeviceGetAttribute, "cuDeviceGetAttribute");
			using FArdaCudaCreateContext = CUresult(CUDAAPI*)(CUcontext*, unsigned int, CUdevice);
			FArdaCudaCreateContext mCreateContext =
			    mLibrary ? reinterpret_cast<FArdaCudaCreateContext>(GetSymbol("cuCtxCreate_v2")) : nullptr;
#if CUDA_VERSION >= 13030
			ARDA_CUDA_FUNCTION(cuCtxCreate, "cuCtxCreate_v4");
#endif
			ARDA_CUDA_FUNCTION(cuCtxDestroy, "cuCtxDestroy_v2");
			ARDA_CUDA_FUNCTION(cuCtxPushCurrent, "cuCtxPushCurrent_v2");
			ARDA_CUDA_FUNCTION(cuCtxPopCurrent, "cuCtxPopCurrent_v2");
			ARDA_CUDA_FUNCTION(cuCtxGetLimit, "cuCtxGetLimit");
			ARDA_CUDA_FUNCTION(cuStreamCreate, "cuStreamCreate");
			ARDA_CUDA_FUNCTION(cuStreamDestroy, "cuStreamDestroy_v2");
			ARDA_CUDA_FUNCTION(cuEventCreate, "cuEventCreate");
			ARDA_CUDA_FUNCTION(cuEventDestroy, "cuEventDestroy_v2");
			ARDA_CUDA_FUNCTION(cuEventRecord, "cuEventRecord");
			ARDA_CUDA_FUNCTION(cuEventRecordWithFlags, "cuEventRecordWithFlags");
			ARDA_CUDA_FUNCTION(cuEventQuery, "cuEventQuery");
			ARDA_CUDA_FUNCTION(cuEventElapsedTime, "cuEventElapsedTime");
			ARDA_CUDA_FUNCTION(cuStreamBeginCapture, "cuStreamBeginCapture_v2");
			ARDA_CUDA_FUNCTION(cuStreamEndCapture, "cuStreamEndCapture");
			ARDA_CUDA_FUNCTION(cuGraphInstantiate, "cuGraphInstantiateWithFlags");
			ARDA_CUDA_FUNCTION(cuGraphLaunch, "cuGraphLaunch");
			ARDA_CUDA_FUNCTION(cuGraphDestroy, "cuGraphDestroy");
			ARDA_CUDA_FUNCTION(cuGraphExecDestroy, "cuGraphExecDestroy");
			ARDA_CUDA_FUNCTION(cuGraphGetNodes, "cuGraphGetNodes");
			ARDA_CUDA_FUNCTION(cuGraphNodeGetType, "cuGraphNodeGetType");
			ARDA_CUDA_FUNCTION(cuGraphChildGraphNodeGetGraph, "cuGraphChildGraphNodeGetGraph");
			ARDA_CUDA_FUNCTION(cuGraphEventRecordNodeGetEvent, "cuGraphEventRecordNodeGetEvent");
#if CUDA_VERSION >= 13030
			ARDA_CUDA_FUNCTION(cuStreamBeginCaptureToCig, "cuStreamBeginCaptureToCig");
			ARDA_CUDA_FUNCTION(cuStreamEndCaptureToCig, "cuStreamEndCaptureToCig");
#endif
			ARDA_CUDA_FUNCTION(cuStreamSynchronize, "cuStreamSynchronize");
			ARDA_CUDA_FUNCTION(cuImportExternalSemaphore, "cuImportExternalSemaphore");
			ARDA_CUDA_FUNCTION(cuDestroyExternalSemaphore, "cuDestroyExternalSemaphore");
			ARDA_CUDA_FUNCTION(cuWaitExternalSemaphoresAsync, "cuWaitExternalSemaphoresAsync");
			ARDA_CUDA_FUNCTION(cuSignalExternalSemaphoresAsync, "cuSignalExternalSemaphoresAsync");
			ARDA_CUDA_FUNCTION(cuImportExternalMemory, "cuImportExternalMemory");
			ARDA_CUDA_FUNCTION(cuExternalMemoryGetMappedBuffer, "cuExternalMemoryGetMappedBuffer");
			ARDA_CUDA_FUNCTION(cuExternalMemoryGetMappedMipmappedArray, "cuExternalMemoryGetMappedMipmappedArray");
			ARDA_CUDA_FUNCTION(cuMipmappedArrayGetLevel, "cuMipmappedArrayGetLevel");
			ARDA_CUDA_FUNCTION(cuSurfObjectCreate, "cuSurfObjectCreate");
			ARDA_CUDA_FUNCTION(cuSurfObjectDestroy, "cuSurfObjectDestroy");
			ARDA_CUDA_FUNCTION(cuMemFree, "cuMemFree_v2");
			ARDA_CUDA_FUNCTION(cuMipmappedArrayDestroy, "cuMipmappedArrayDestroy");
			ARDA_CUDA_FUNCTION(cuDestroyExternalMemory, "cuDestroyExternalMemory");
			ARDA_CUDA_FUNCTION(cuGetErrorName, "cuGetErrorName");
#undef ARDA_CUDA_FUNCTION

			~FArdaCudaDriver()
			{
				if (!mLibrary)
				{
					return;
				}
#if defined(_WIN32)
				FreeLibrary(mLibrary);
#else
				dlclose(mLibrary);
#endif
			}

			bool IsComplete() const
			{
				return cuInit && cuDeviceGetCount && cuDeviceGet && cuDeviceGetUuid && cuDeviceGetAttribute &&
				    mCreateContext && cuCtxDestroy && cuCtxPushCurrent && cuCtxPopCurrent && cuCtxGetLimit &&
				    cuStreamCreate && cuStreamDestroy && cuStreamSynchronize && cuImportExternalSemaphore &&
				    cuDestroyExternalSemaphore && cuWaitExternalSemaphoresAsync && cuSignalExternalSemaphoresAsync &&
				    cuImportExternalMemory && cuExternalMemoryGetMappedBuffer &&
				    cuExternalMemoryGetMappedMipmappedArray && cuMipmappedArrayGetLevel && cuSurfObjectCreate &&
				    cuSurfObjectDestroy && cuMemFree && cuMipmappedArrayDestroy && cuDestroyExternalMemory &&
				    cuGetErrorName;
			}

			bool HasGraphs() const
			{
				return cuStreamBeginCapture && cuStreamEndCapture && cuGraphInstantiate && cuGraphLaunch &&
				    cuGraphDestroy && cuGraphExecDestroy && cuGraphGetNodes && cuGraphNodeGetType &&
				    cuGraphChildGraphNodeGetGraph;
			}

			bool HasTiming() const
			{
				return cuEventCreate && cuEventDestroy && cuEventRecord && cuEventRecordWithFlags && cuEventQuery &&
				    cuEventElapsedTime && cuGraphEventRecordNodeGetEvent;
			}

			FArdaRHIStatus Check(CUresult Result, const char* Operation) const
			{
				if (Result == CUDA_SUCCESS)
				{
					return {};
				}
				const char* Error = nullptr;
				if (cuGetErrorName)
				{
					cuGetErrorName(Result, &Error);
				}
				eastl::string Message = Operation;
				Message += ": ";
				Message += Error ? Error : "unknown CUDA driver error";
				return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Message.c_str());
			}
		};

		// CUDA context stacks are thread-local; restore the caller's context on every exit.
		struct FArdaCudaScope
		{
			FArdaCudaDriver& mDriver;
			CUresult mResult;

			FArdaCudaScope(FArdaCudaDriver& Driver, CUcontext Context)
			    : mDriver(Driver),
			      mResult(Driver.cuCtxPushCurrent(Context))
			{
			}

			~FArdaCudaScope()
			{
				if (mResult == CUDA_SUCCESS)
				{
					CUcontext Previous;
					mDriver.cuCtxPopCurrent(&Previous);
				}
			}
		};

		struct FArdaCudaContext;
		struct FArdaCudaGraphExecutable;
		struct FArdaCudaStream;
		struct FArdaCudaTimingEvents;
		struct FArdaCudaTimingLease;

		// The mapping retains its context; the native resource owns the mapping and outlives it.
		struct FArdaCudaMapping final : IArdaCudaMapping
		{
			eastl::shared_ptr<FArdaCudaContext> mContext;
			CUexternalMemory mMemory = nullptr;
			CUdeviceptr mBuffer = 0;
			CUmipmappedArray mArray = nullptr;
			eastl::vector<CUsurfObject> mSurfaces;
			~FArdaCudaMapping() override;

			uint64_t GetArgument(uint32_t Mip, uint64_t Offset) const override
			{
				return mBuffer ? mBuffer + Offset : mSurfaces.at(Mip);
			}
		};

		// Stream and entry references survive recording and are retired with the submitted list.
		struct FArdaCudaBatch final : IArdaCudaBatch
		{
			explicit FArdaCudaBatch(eastl::shared_ptr<FArdaCudaContext> Context)
			    : mContext(eastl::move(Context))
			{
			}

			~FArdaCudaBatch() override;

			const void* GetIdentity() const noexcept override
			{
				return this;
			}

			FArdaRHIStatus Record(void*,
			    const eastl::vector<FArdaCudaKernel>&,
			    const eastl::vector<uint64_t>&) override;
			FArdaRHIStatus ValidateSubmit() const override;
			void MarkSubmitted() override;
			FArdaRHIStatus Execute() override;

			void SetSynchronization(eastl::shared_ptr<IArdaCudaSemaphore> Wait,
			    uint64_t WaitValue,
			    eastl::shared_ptr<IArdaCudaSemaphore> Signal,
			    uint64_t SignalValue) override
			{
				mWait = eastl::move(Wait);
				mSignal = eastl::move(Signal);
				mWaitValue = WaitValue;
				mSignalValue = SignalValue;
			}

			eastl::shared_ptr<IArdaCudaSemaphore> mWait, mSignal;
			uint64_t mWaitValue = 0, mSignalValue = 0;

			// One resolved argument copy per recorded dispatch. Binding indices and patches
			// are consumed during recording and are not retained for submission.
			struct FArdaCudaLaunch
			{
				eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
				FArdaCudaLaunchConfig mConfig;
				eastl::vector<uint8_t> mParameters;
				eastl::shared_ptr<const IArdaCudaExternalCall> mExternalFactory;
				eastl::unique_ptr<IArdaCudaPreparedCall> mExternalCall;
				uint64_t mExternalRevision = 0;

				FArdaRHIStatus Enqueue(CUstream Stream) const
				{
					try
					{
						return mExternalCall ? mExternalCall->Enqueue(Stream)
						                     : mEntry->Launch(Stream, mConfig, mParameters.data(), mParameters.size());
					}
					catch (const std::exception& Error)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
					}
					catch (...)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
						    "CUDA operation enqueue threw an exception.");
					}
				}
			};

			struct FArdaCudaRecordedGroup
			{
				eastl::vector<FArdaCudaLaunch> mLaunches;
				eastl::shared_ptr<FArdaCudaGraphExecutable> mGraph;
				eastl::shared_ptr<FArdaCudaGraphCache> mCache;
				eastl::shared_ptr<FArdaCudaTimingLease> mTiming;
				FArdaRHIStatus Enqueue(CUstream Stream) const;
			};

			eastl::shared_ptr<FArdaCudaContext> mContext;
			eastl::vector<FArdaCudaRecordedGroup> mGroups;
			CUstream mStream = nullptr;
			eastl::shared_ptr<FArdaCudaStream> mStreamLifetime;
			bool mbSubmitted = false;
			bool mbFailed = false;
		};

		struct FArdaCudaContext final : IArdaCudaContext, eastl::enable_shared_from_this<FArdaCudaContext>
		{
			eastl::shared_ptr<FArdaCudaDriver> mDriver;
			eastl::shared_ptr<void> mNativeLifetime;
			CUcontext mContext = nullptr;
			FArdaCudaCapabilities mCapabilities;
			bool mbVulkan = false;
			mutable std::mutex mMutex;

			// CiG permits only one captured-but-unsubmitted batch per context in this backend.
			FArdaCudaBatch* mRecording = nullptr;
			std::unordered_map<const IArdaCudaKernelEntry*,
			    eastl::pair<eastl::shared_ptr<const IArdaCudaKernelEntry>, FArdaCudaKernelLimits>>
			    mKernelCache;

			struct FArdaExternalState
			{
				eastl::shared_ptr<const IArdaCudaExternalCall> mFactory;
				eastl::unique_ptr<IArdaCudaExternalCallState> mState;
			};

			std::unordered_map<const IArdaCudaExternalCall*, FArdaExternalState> mExternalStates;

			~FArdaCudaContext() override
			{
				if (mContext)
				{
					{
						FArdaCudaScope Scope(*mDriver, mContext);
						mExternalStates.clear();
						mKernelCache.clear();
					}
					mDriver->cuCtxDestroy(mContext);
				}
			}

			FArdaCudaCapabilities GetCapabilities() const override
			{
				return mCapabilities;
			}

			TArdaRHIResult<eastl::shared_ptr<IArdaCudaSemaphore>> ImportSemaphore(void*) override;
			TArdaRHIResult<eastl::shared_ptr<IArdaCudaMapping>> ImportMemory(void*,
			    uint64_t,
			    uint64_t,
			    const FArdaRHITextureDesc*) override;

			eastl::shared_ptr<IArdaCudaBatch> CreateBatch() override
			{
				return eastl::make_shared<FArdaCudaBatch>(shared_from_this());
			}
		};

		struct FArdaCudaStream
		{
			eastl::shared_ptr<FArdaCudaContext> mContext;
			CUstream mStream = nullptr;

			~FArdaCudaStream()
			{
				FArdaCudaScope Scope(*mContext->mDriver, mContext->mContext);
				if (mStream && Scope.mResult == CUDA_SUCCESS)
				{
					mContext->mDriver->cuStreamDestroy(mStream);
				}
			}
		};

		struct FArdaCudaTimingEvents
		{
			eastl::shared_ptr<FArdaCudaContext> mContext;
			eastl::vector<FArdaCudaTimingRegion> mRegions;
			eastl::vector<CUevent> mEvents;
			std::atomic<bool> mbReserved{false};

			~FArdaCudaTimingEvents()
			{
				FArdaCudaScope Scope(*mContext->mDriver, mContext->mContext);
				if (Scope.mResult == CUDA_SUCCESS)
				{
					for (const auto Event : mEvents)
					{
						if (Event)
						{
							mContext->mDriver->cuEventDestroy(Event);
						}
					}
				}
			}

			FArdaRHIStatus Initialize()
			{
				auto& D = *mContext->mDriver;
				if (!D.HasTiming())
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
					    "The CUDA driver does not expose region timing events.");
				}
				mEvents.resize(mRegions.size() * 2, nullptr);
				for (auto& Event : mEvents)
				{
					if (auto Status =
					        D.Check(D.cuEventCreate(&Event, CU_EVENT_DEFAULT), "Create CUDA region timing event");
					    !Status)
					{
						return Status;
					}
				}
				return {};
			}

			FArdaRHIStatus RecordBoundary(size_t Operation, CUstream Stream, bool Capture) const
			{
				auto& D = *mContext->mDriver;
				for (size_t I = 0; I < mRegions.size(); ++I)
				{
					const auto& Region = mRegions[I];
					CUevent Event = Region.mEndOperation == Operation ? mEvents[I * 2 + 1] : nullptr;
					if (Region.mFirstOperation == Operation)
					{
						Event = mEvents[I * 2];
					}
					if (Event)
					{
						const auto Result = Capture ? D.cuEventRecordWithFlags(Event, Stream, CU_EVENT_RECORD_EXTERNAL)
						                            : D.cuEventRecord(Event, Stream);
						if (auto Status = D.Check(Result, "Record CUDA region timing event"); !Status)
						{
							return Status;
						}
					}
				}
				return {};
			}

			bool Matches(const eastl::vector<FArdaCudaTimingRegion>& Regions) const
			{
				if (mbReserved.load() || mRegions.size() != Regions.size())
				{
					return false;
				}
				for (size_t I = 0; I < Regions.size(); ++I)
				{
					const auto& A = mRegions[I];
					const auto& B = Regions[I];
					if (A.mRegionId != B.mRegionId || A.mFirstOperation != B.mFirstOperation ||
					    A.mEndOperation != B.mEndOperation)
					{
						return false;
					}
				}
				return true;
			}
		};

		struct FArdaCudaTimingSample
		{
			eastl::shared_ptr<FArdaCudaContext> mContext;
			eastl::shared_ptr<FArdaCudaTimingEvents> mEvents;
			FArdaRHIStatus mStatus;
			std::atomic<bool> mbSubmitted{false};
			bool mbConsumed = false;

			~FArdaCudaTimingSample()
			{
				ReleaseEvents();
			}

			void ReleaseEvents()
			{
				if (mEvents && !mbConsumed)
				{
					mEvents->mbReserved.store(false);
					mbConsumed = true;
				}
			}

			TArdaRHIResult<FArdaCudaTimingResult> Poll()
			{
				if (!mbSubmitted.load())
				{
					return {{}, {}};
				}
				std::unique_lock<std::mutex> Lock(mContext->mMutex, std::try_to_lock);
				if (!Lock.owns_lock())
				{
					return {{}, {}};
				}
				if (!mStatus)
				{
					ReleaseEvents();
					return {{}, mStatus};
				}
				auto& D = *mContext->mDriver;
				FArdaCudaScope Scope(D, mContext->mContext);
				if (auto Status = D.Check(Scope.mResult, "Push CUDA timing context"); !Status)
				{
					ReleaseEvents();
					return {{}, Status};
				}
				for (const auto Event : mEvents->mEvents)
				{
					const auto Native = D.cuEventQuery(Event);
					if (Native == CUDA_ERROR_NOT_READY)
					{
						return {{}, {}};
					}
					if (auto Status = D.Check(Native, "Query CUDA region timing event"); !Status)
					{
						ReleaseEvents();
						return {{}, Status};
					}
				}
				FArdaCudaTimingResult Result;
				for (size_t I = 0; I < mEvents->mRegions.size(); ++I)
				{
					float Milliseconds = 0;
					const auto Native =
					    D.cuEventElapsedTime(&Milliseconds, mEvents->mEvents[I * 2], mEvents->mEvents[I * 2 + 1]);
					if (Native == CUDA_ERROR_NOT_READY)
					{
						return {{}, {}};
					}
					if (auto Status = D.Check(Native, "Read CUDA region elapsed time"); !Status)
					{
						ReleaseEvents();
						return {{}, Status};
					}
					if (!std::isfinite(Milliseconds) || Milliseconds < 0)
					{
						ReleaseEvents();
						return {{},
						    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
						        "CUDA returned an invalid elapsed duration.")};
					}
					Result.mRegions.push_back(
					    {mEvents->mRegions[I].mRegionId, static_cast<double>(Milliseconds) * 0.001});
				}
				Result.mbReady = true;
				ReleaseEvents();
				return {eastl::move(Result), {}};
			}
		};

		struct FArdaCudaTimingLease
		{
			eastl::shared_ptr<FArdaCudaTimingQuery> mQuery;
			eastl::shared_ptr<FArdaCudaTimingSample> mSample;

			~FArdaCudaTimingLease()
			{
				if (mSample && !mSample->mbSubmitted.load())
				{
					FArdaCudaTimingQueryAccess::Cancel(*mQuery, mSample.get());
				}
			}
		};

		// Executables retain their capture stream, library state, modules and resource identities.
		// Each submitted batch holds its own reference, so replacement never retires in-flight work.
		struct FArdaCudaGraphExecutable
		{
			eastl::shared_ptr<FArdaCudaContext> mContext;
			eastl::vector<FArdaCudaBatch::FArdaCudaLaunch> mLaunches;
			eastl::vector<FArdaRHIResourceRef> mResources;
			CUgraphExec mExecutable = nullptr;
			eastl::shared_ptr<FArdaCudaStream> mCaptureStream;
			eastl::shared_ptr<FArdaCudaTimingEvents> mTimingEvents;
			bool mbLaunched = false;

			~FArdaCudaGraphExecutable()
			{
				FArdaCudaScope Scope(*mContext->mDriver, mContext->mContext);
				if (Scope.mResult == CUDA_SUCCESS)
				{
					if (mExecutable)
					{
						mContext->mDriver->cuGraphExecDestroy(mExecutable);
					}
					mLaunches.clear();
					mCaptureStream.reset();
				}
			}

			bool Matches(const FArdaCudaContext* Context,
			    const eastl::vector<FArdaCudaBatch::FArdaCudaLaunch>& Launches,
			    const eastl::vector<FArdaRHIResourceRef>& Resources,
			    const eastl::vector<FArdaCudaTimingRegion>& Regions) const
			{
				if (mTimingEvents ? !mTimingEvents->Matches(Regions) : !Regions.empty())
				{
					return false;
				}
				if (mContext.get() != Context || mResources != Resources || mLaunches.size() != Launches.size())
				{
					return false;
				}
				for (size_t I = 0; I < Launches.size(); ++I)
				{
					const auto& A = mLaunches[I];
					const auto& B = Launches[I];
					if (A.mEntry != B.mEntry || A.mExternalFactory != B.mExternalFactory ||
					    A.mExternalRevision != B.mExternalRevision || A.mParameters != B.mParameters ||
					    A.mConfig.mSharedMemoryBytes != B.mConfig.mSharedMemoryBytes)
					{
						return false;
					}
					for (size_t D = 0; D < 3; ++D)
					{
						if (A.mConfig.mGridSize[D] != B.mConfig.mGridSize[D] ||
						    A.mConfig.mBlockSize[D] != B.mConfig.mBlockSize[D])
						{
							return false;
						}
					}
				}
				return true;
			}
		};

		struct FArdaCudaGraphVariants
		{
			eastl::vector<eastl::shared_ptr<FArdaCudaGraphExecutable>> mEntries;
		};

		FArdaRHIStatus FArdaCudaBatch::FArdaCudaRecordedGroup::Enqueue(CUstream Stream) const
		{
			if (mGraph && mGraph->mExecutable)
			{
				auto Status = mGraph->mContext->mDriver->Check(
				    mGraph->mContext->mDriver->cuGraphLaunch(mGraph->mExecutable, Stream),
				    "Launch retained CUDA Graph");
				if (Status)
				{
					FArdaCudaGraphCacheAccess Cache(*mCache);
					if (mGraph->mbLaunched)
					{
						++Cache.Stats().mReplayCount;
					}
					mGraph->mbLaunched = true;
				}
				return Status;
			}
			for (size_t I = 0; I <= mLaunches.size(); ++I)
			{
				if (mTiming && mTiming->mSample->mStatus && mTiming->mSample->mEvents)
				{
					// Ordinary streams and direct CiG capture preserve these events without graph-external flags.
					mTiming->mSample->mStatus = mTiming->mSample->mEvents->RecordBoundary(I, Stream, false);
				}
				if (I == mLaunches.size())
				{
					break;
				}
				if (auto Status = mLaunches[I].Enqueue(Stream); !Status)
				{
					return Status;
				}
			}
			return {};
		}

		FArdaRHIStatus ValidateGraphNodes(FArdaCudaDriver& D, CUgraph Graph, const FArdaCudaTimingEvents* Timing)
		{
			size_t Count = 0;
			if (auto S = D.Check(D.cuGraphGetNodes(Graph, nullptr, &Count), "Inspect captured CUDA Graph"); !S)
			{
				return S;
			}
			eastl::vector<CUgraphNode> Nodes(Count);
			if (auto S = D.Check(D.cuGraphGetNodes(Graph, Nodes.data(), &Count), "Inspect captured CUDA Graph nodes");
			    !S)
			{
				return S;
			}
			for (auto Node : Nodes)
			{
				CUgraphNodeType Type;
				if (auto S = D.Check(D.cuGraphNodeGetType(Node, &Type), "Inspect captured CUDA Graph node type"); !S)
				{
					return S;
				}
				if (Type == CU_GRAPH_NODE_TYPE_GRAPH)
				{
					CUgraph Child = nullptr;
					if (auto S = D.Check(D.cuGraphChildGraphNodeGetGraph(Node, &Child), "Inspect nested CUDA Graph");
					    !S)
					{
						return S;
					}
					if (auto S = ValidateGraphNodes(D, Child, Timing); !S)
					{
						return S;
					}
				}
				else if (Type == CU_GRAPH_NODE_TYPE_EVENT_RECORD && Timing)
				{
					CUevent Event = nullptr;
					if (auto S =
					        D.Check(D.cuGraphEventRecordNodeGetEvent(Node, &Event), "Inspect CUDA timing event node");
					    !S)
					{
						return S;
					}
					if (eastl::find(Timing->mEvents.begin(), Timing->mEvents.end(), Event) == Timing->mEvents.end())
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
						    "Captured event nodes must belong to the explicit CUDA timing regions.");
					}
				}
				else if (Type != CU_GRAPH_NODE_TYPE_KERNEL && Type != CU_GRAPH_NODE_TYPE_MEMCPY &&
				    Type != CU_GRAPH_NODE_TYPE_MEMSET && Type != CU_GRAPH_NODE_TYPE_EMPTY)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
					    "Retained CUDA Graphs admit kernels, copies, memset, explicit timing events and empty/child nodes; other synchronization, allocation and host nodes remain outside capture.");
				}
			}
			return {};
		}

		struct FArdaCudaSemaphore final : IArdaCudaSemaphore
		{
			eastl::shared_ptr<FArdaCudaContext> mContext;
			CUexternalSemaphore mSemaphore = nullptr;

			void* GetNativeHandle() const override
			{
				return mSemaphore;
			}

			~FArdaCudaSemaphore() override
			{
				FArdaCudaScope Scope(*mContext->mDriver, mContext->mContext);
				if (mSemaphore && Scope.mResult == CUDA_SUCCESS)
				{
					mContext->mDriver->cuDestroyExternalSemaphore(mSemaphore);
				}
			}
		};

		TArdaRHIResult<eastl::shared_ptr<IArdaCudaSemaphore>> FArdaCudaContext::ImportSemaphore(void* Handle)
		{
			auto Semaphore = eastl::make_shared<FArdaCudaSemaphore>();
			Semaphore->mContext = shared_from_this();
			auto& D = *mDriver;
			FArdaCudaScope Scope(D, mContext);
			CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC Desc{};
#if defined(_WIN32)
			Desc.type = mbVulkan ? CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32
			                     : CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;
			Desc.handle.win32.handle = Handle;
#else
			Desc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD;
			Desc.handle.fd = static_cast<int>(reinterpret_cast<intptr_t>(Handle));
#endif
			auto Result = Scope.mResult == CUDA_SUCCESS ? D.cuImportExternalSemaphore(&Semaphore->mSemaphore, &Desc)
			                                            : Scope.mResult;
#if !defined(_WIN32)
			if (Result != CUDA_SUCCESS)
			{
				close(Desc.handle.fd);
			}
#endif
			if (auto S = D.Check(Result, "Import graphics semaphore"); !S)
			{
				return {{}, S};
			}
			return {Semaphore, {}};
		}

		FArdaCudaMapping::~FArdaCudaMapping()
		{
			auto& D = *mContext->mDriver;
			FArdaCudaScope Scope(D, mContext->mContext);
			if (Scope.mResult != CUDA_SUCCESS)
			{
				return;
			}

			// Views depend on arrays/memory: destroy in reverse construction order.
			for (auto Surface : mSurfaces)
			{
				D.cuSurfObjectDestroy(Surface);
			}
			if (mArray)
			{
				D.cuMipmappedArrayDestroy(mArray);
			}
			if (mBuffer)
			{
				D.cuMemFree(mBuffer);
			}
			if (mMemory)
			{
				D.cuDestroyExternalMemory(mMemory);
			}
		}

		TArdaRHIResult<eastl::shared_ptr<IArdaCudaMapping>> FArdaCudaContext::ImportMemory(void* Handle,
		    uint64_t AllocationSize,
		    uint64_t BufferSize,
		    const FArdaRHITextureDesc* Texture)
		{
			auto Mapping = eastl::make_shared<FArdaCudaMapping>();
			Mapping->mContext = shared_from_this();
			auto& D = *mDriver;
			FArdaCudaScope Scope(D, mContext);
			if (auto S = D.Check(Scope.mResult, "Push CUDA context"); !S)
			{
#if !defined(_WIN32)
				close(static_cast<int>(reinterpret_cast<intptr_t>(Handle)));
#endif
				return {{}, S};
			}
			CUDA_EXTERNAL_MEMORY_HANDLE_DESC Import{};
#if defined(_WIN32)
			Import.type =
			    mbVulkan ? CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 : CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
			Import.handle.win32.handle = Handle;
#else
			Import.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
			Import.handle.fd = static_cast<int>(reinterpret_cast<intptr_t>(Handle));
#endif
			Import.size = AllocationSize;
			Import.flags = CUDA_EXTERNAL_MEMORY_DEDICATED;
			if (auto S = D.Check(D.cuImportExternalMemory(&Mapping->mMemory, &Import), "Import graphics allocation");
			    !S)
			{
#if !defined(_WIN32)
				close(Import.handle.fd); // CUDA consumes the FD only after a successful import.
#endif
				return {{}, S};
			}
			if (!Texture)
			{
				CUDA_EXTERNAL_MEMORY_BUFFER_DESC Range{};
				Range.size = BufferSize;
				if (auto S = D.Check(D.cuExternalMemoryGetMappedBuffer(&Mapping->mBuffer, Mapping->mMemory, &Range),
				        "Map CUDA buffer");
				    !S)
				{
					return {{}, S};
				}
			}
			else
			{
				const auto Format = GetArdaCudaFormatInfo(Texture->mFormat);
				CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC Array{};
				Array.numLevels = Texture->mMipLevels;
				auto& A = Array.arrayDesc;
				A.Width = Texture->mWidth;
				A.Height = Texture->mDimension == EArdaRHITextureDimension::Texture1D ||
				        Texture->mDimension == EArdaRHITextureDimension::Texture1DArray
				    ? 0
				    : Texture->mHeight;
				const bool Layered = Texture->mDimension == EArdaRHITextureDimension::Texture1DArray ||
				    Texture->mDimension == EArdaRHITextureDimension::Texture2DArray;
				A.Depth = Layered ? Texture->mArraySize
				                  : (Texture->mDimension == EArdaRHITextureDimension::Texture3D ? Texture->mDepth : 0);
				A.NumChannels = Format.mChannels;
				A.Format = Format.mScalarType == EArdaCudaScalarType::Float
				    ? (Format.mBits == 16 ? CU_AD_FORMAT_HALF : CU_AD_FORMAT_FLOAT)
				    : (Format.mScalarType == EArdaCudaScalarType::UInt
				              ? (Format.mBits == 8           ? CU_AD_FORMAT_UNSIGNED_INT8
				                        : Format.mBits == 16 ? CU_AD_FORMAT_UNSIGNED_INT16
				                                             : CU_AD_FORMAT_UNSIGNED_INT32)
				              : (Format.mBits == 8           ? CU_AD_FORMAT_SIGNED_INT8
				                        : Format.mBits == 16 ? CU_AD_FORMAT_SIGNED_INT16
				                                             : CU_AD_FORMAT_SIGNED_INT32));
				A.Flags = CUDA_ARRAY3D_SURFACE_LDST | (Layered ? CUDA_ARRAY3D_LAYERED : 0);
				if (HasAnyFlags(Texture->mUsage, EArdaRHITextureUsage::RenderTarget))
				{
					A.Flags |= CUDA_ARRAY3D_COLOR_ATTACHMENT;
				}
				if (auto S =
				        D.Check(D.cuExternalMemoryGetMappedMipmappedArray(&Mapping->mArray, Mapping->mMemory, &Array),
				            "Map CUDA image");
				    !S)
				{
					return {{}, S};
				}
				for (uint32_t Mip = 0; Mip < Texture->mMipLevels; ++Mip)
				{
					CUDA_RESOURCE_DESC Resource{};
					Resource.resType = CU_RESOURCE_TYPE_ARRAY;
					if (auto S = D.Check(D.cuMipmappedArrayGetLevel(&Resource.res.array.hArray, Mapping->mArray, Mip),
					        "Resolve CUDA mip");
					    !S)
					{
						return {{}, S};
					}
					CUsurfObject Surface = 0;
					if (auto S = D.Check(D.cuSurfObjectCreate(&Surface, &Resource), "Create CUDA surface"); !S)
					{
						return {{}, S};
					}
					Mapping->mSurfaces.push_back(Surface);
				}
			}
			return {Mapping, {}};
		}

		FArdaCudaBatch::~FArdaCudaBatch()
		{
			std::lock_guard<std::mutex> Lock(mContext->mMutex);
			if (!mbSubmitted && mContext->mCapabilities.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG)
			{
				for (const auto& Group : mGroups)
				{
					if (!Group.mGraph || !Group.mCache)
					{
						continue;
					}
					// Launches of one CUDA graph executable are serialized, including launches
					// captured into CiG. A discarded graphics list leaves that launch unexecuted;
					// retire its executable so the next recording cannot wait on the canceled one.
					FArdaCudaGraphCacheAccess Cache(*Group.mCache);
					if (Cache.State())
					{
						auto Variants = eastl::static_pointer_cast<FArdaCudaGraphVariants>(Cache.State());
						Variants->mEntries.erase(
						    eastl::remove(Variants->mEntries.begin(), Variants->mEntries.end(), Group.mGraph),
						    Variants->mEntries.end());
						Cache.Stats().mCachedVariantCount = static_cast<uint32_t>(Variants->mEntries.size());
					}
				}
			}
			if (mContext->mRecording == this)
			{
				mContext->mRecording = nullptr;
			}
			FArdaCudaScope Scope(*mContext->mDriver, mContext->mContext);
			if (mStream && Scope.mResult == CUDA_SUCCESS)
			{
				// Normally retired after the graphics completion fence; also protect failed submissions.
				if (mbSubmitted && mWait)
				{
					mContext->mDriver->cuStreamSynchronize(mStream);
				}
				// Library handles must retire while their context is current and stream is alive.
				mGroups.clear();
				mStreamLifetime.reset();
			}
		}

		FArdaRHIStatus FArdaCudaBatch::ValidateSubmit() const
		{
			std::lock_guard<std::mutex> Lock(mContext->mMutex);
			if (mbSubmitted || mbFailed || (mContext->mRecording && mContext->mRecording != this))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "CiG command lists are single-use and must submit in capture order; a failed capture must be discarded.");
			}
			return {};
		}

		void FArdaCudaBatch::MarkSubmitted()
		{
			std::lock_guard<std::mutex> Lock(mContext->mMutex);
			mbSubmitted = true;
			for (const auto& Group : mGroups)
			{
				if (Group.mTiming)
				{
					Group.mTiming->mSample->mbSubmitted.store(true);
				}
			}
			if (mContext->mRecording == this)
			{
				mContext->mRecording = nullptr;
			}
		}

		FArdaRHIStatus FArdaCudaBatch::Record(void* CommandList,
		    const eastl::vector<FArdaCudaKernel>& Kernels,
		    const eastl::vector<uint64_t>& Bindings)
		{
			std::lock_guard<std::mutex> Lock(mContext->mMutex);
			if (mbSubmitted || mbFailed || (mContext->mRecording && mContext->mRecording != this))
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Submit or discard the previous CiG command list before recording another one on this context.");
			}
			auto& D = *mContext->mDriver;
			FArdaCudaScope Scope(D, mContext->mContext);
			if (auto S = D.Check(Scope.mResult, "Push CUDA context"); !S)
			{
				return S;
			}
			if (!mStream)
			{
				mStreamLifetime = eastl::make_shared<FArdaCudaStream>();
				mStreamLifetime->mContext = mContext;
				if (auto S = D.Check(D.cuStreamCreate(&mStream, CU_STREAM_NON_BLOCKING), "Create CUDA stream"); !S)
				{
					return S;
				}
				mStreamLifetime->mStream = mStream;
			}
			if (auto S = ValidateArdaCudaKernelBatch(Kernels, Bindings.size(), mContext->mCapabilities); !S)
			{
				return S;
			}
			eastl::vector<FArdaCudaLaunch> Prepared;
			Prepared.reserve(Kernels.size());
			for (const auto& K : Kernels)
			{
				if (K.mEntry)
				{
					auto Found = mContext->mKernelCache.find(K.mEntry.get());
					if (Found == mContext->mKernelCache.end())
					{
						auto Limits = K.mEntry->GetLimits();
						if (!Limits)
						{
							return Limits.mStatus;
						}
						if (mContext->mKernelCache.size() >= 256)
						{
							mContext->mKernelCache.clear();
						}
						Found =
						    mContext->mKernelCache.emplace(K.mEntry.get(), eastl::make_pair(K.mEntry, Limits.mValue))
						        .first;
					}
					const auto& Limits = Found->second.second;
					if (uint64_t(K.mBlockSize[0]) * K.mBlockSize[1] * K.mBlockSize[2] > Limits.mMaxThreadsPerBlock ||
					    K.mSharedMemoryBytes > Limits.mMaxDynamicSharedMemoryBytes ||
					    uint64_t(Limits.mStaticSharedMemoryBytes) + K.mSharedMemoryBytes >
					        mContext->mCapabilities.mMaxSharedMemoryBytes)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "Compiled CUDA kernel exceeds its thread/shared-memory limit.");
					}
				}
				FArdaCudaLaunch Launch{K.mEntry,
				    K,
				    K.mParameters,
				    K.mExternalCall,
				    {},
				    K.mExternalCall ? K.mExternalCall->GetGraphCaptureRevision() : 0};
				for (const auto& P : K.mPatches)
				{
					const auto Address = Bindings[P.mBindingIndex];
					if (!Address || Address % P.mAlignment)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "Resolved CUDA resource is null or misaligned.");
					}
					std::memcpy(Launch.mParameters.data() + P.mOffset, &Address, sizeof(Address));
				}

				Prepared.push_back(eastl::move(Launch));
			}
			FArdaCudaRecordedGroup Group;
			const auto Batch = Kernels.empty() ? nullptr : Kernels.front().mGraphBatch;
			for (const auto& Kernel : Kernels)
			{
				if (Kernel.mGraphBatch != Batch)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "CUDA Graph metadata must identify one common sequence batch.");
				}
			}
			Group.mCache = Batch ? Batch->mCache : nullptr;
			eastl::vector<FArdaCudaTimingRegion> TimingRegions;
			if (Batch && (!Batch->mTimingRegions.empty() || Batch->mTimingQuery))
			{
				if (!Batch->mTimingQuery || Batch->mTimingRegions.empty())
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "CUDA timing requires a query and nonempty regions.");
				}
				uint32_t PreviousEnd = 0;
				for (size_t I = 0; I < Batch->mTimingRegions.size(); ++I)
				{
					const auto& Region = Batch->mTimingRegions[I];
					if (Region.mFirstOperation < PreviousEnd || Region.mFirstOperation >= Region.mEndOperation ||
					    Region.mEndOperation > Kernels.size())
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "CUDA timing regions must be ordered, disjoint and within the operation batch.");
					}
					for (size_t J = 0; J < I; ++J)
					{
						if (Batch->mTimingRegions[J].mRegionId == Region.mRegionId)
						{
							return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
							    "CUDA timing region IDs must be unique.");
						}
					}
					PreviousEnd = Region.mEndOperation;
				}
				Group.mTiming = eastl::make_shared<FArdaCudaTimingLease>();
				Group.mTiming->mQuery = Batch->mTimingQuery;
				auto Sample = eastl::make_shared<FArdaCudaTimingSample>();
				Sample->mContext = mContext;
				Group.mTiming->mSample = Sample;
				if (auto Status = FArdaCudaTimingQueryAccess::Attach(*Batch->mTimingQuery,
				        Sample,
				        [Sample]
				        {
					        return Sample->Poll();
				        });
				    !Status)
				{
					return Status;
				}
				if (D.HasTiming())
				{
					TimingRegions = Batch->mTimingRegions;
				}
				else
				{
					Sample->mStatus = FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
					    "CUDA region timing events are unavailable on this driver.");
				}
			}
			const auto Mode = Group.mCache ? Group.mCache->GetMode() : EArdaCudaGraphMode::Disabled;
			if (Mode > EArdaCudaGraphMode::Require)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Invalid CUDA Graph capture mode.");
			}
			if (Group.mCache && !Group.mCache->GetMaximumCachedVariants())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "CUDA Graph cache capacity must be positive.");
			}
			bool bCapture = Mode != EArdaCudaGraphMode::Disabled;
			if (bCapture)
			{
				const char* Reason = D.HasGraphs() ? nullptr : "CUDA driver does not expose the required graph APIs.";
				for (const auto& Launch : Prepared)
				{
					if (Launch.mExternalFactory &&
					    !Launch.mExternalFactory->SupportsGraphCapture(mContext->mCapabilities))
					{
						Reason = "External CUDA call did not opt into graph capture and repeated replay.";
					}
				}
				FArdaCudaGraphCacheAccess Cache(*Group.mCache);
				if (Reason)
				{
					Cache.Stats().mLastFallbackReason = Reason;
					if (Mode == EArdaCudaGraphMode::Require)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Reason);
					}
					++Cache.Stats().mFallbackCount;
					bCapture = false;
				}
				else if (Cache.State())
				{
					auto Variants = eastl::static_pointer_cast<FArdaCudaGraphVariants>(Cache.State());
					for (auto It = Variants->mEntries.begin(); It != Variants->mEntries.end(); ++It)
					{
						if ((*It)->Matches(mContext.get(), Prepared, Batch->mResources, TimingRegions))
						{
							Group.mGraph = *It;
							Variants->mEntries.erase(It);
							Variants->mEntries.push_back(Group.mGraph);
							++Cache.Stats().mCacheHitCount;
							bCapture = false;
							break;
						}
					}
				}
			}
			if (!TimingRegions.empty())
			{
				auto Events = Group.mGraph ? Group.mGraph->mTimingEvents : eastl::make_shared<FArdaCudaTimingEvents>();
				if (!Group.mGraph)
				{
					Events->mContext = mContext;
					Events->mRegions = TimingRegions;
					Group.mTiming->mSample->mStatus = Events->Initialize();
				}
				if (Group.mTiming->mSample->mStatus)
				{
					Events->mbReserved.store(true);
					Group.mTiming->mSample->mEvents = eastl::move(Events);
				}
				else
				{
					TimingRegions.clear();
				}
			}
			const FArdaCudaExternalCallContext BaseCallContext{mContext->mContext,
			    mStream,
			    mContext->mCapabilities.mLaunchMode,
			    mContext->mCapabilities.mComputeCapability};
			for (auto& Launch : Prepared)
			{
				if (Group.mGraph || !Launch.mExternalFactory)
				{
					continue;
				}
				try
				{
					auto CallContext = BaseCallContext;
					auto Found = mContext->mExternalStates.find(Launch.mExternalFactory.get());
					if (Found == mContext->mExternalStates.end())
					{
						auto State = Launch.mExternalFactory->CreateContextState(CallContext);
						if (!State)
						{
							return State.mStatus;
						}
						if (State.mValue)
						{
							Found = mContext->mExternalStates
							            .emplace(Launch.mExternalFactory.get(),
							                FArdaCudaContext::FArdaExternalState{Launch.mExternalFactory,
							                    eastl::move(State.mValue)})
							            .first;
						}
					}
					if (Found != mContext->mExternalStates.end())
					{
						CallContext.mState = Found->second.mState.get();
					}
					auto Call = Launch.mExternalFactory->Prepare(CallContext,
					    Launch.mParameters.data(),
					    Launch.mParameters.size());
					if (!Call)
					{
						return Call.mStatus;
					}
					if (!Call.mValue)
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "External CUDA preparation returned no callable state.");
					}
					Launch.mExternalCall = eastl::move(Call.mValue);
				}
				catch (const std::exception& Error)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what());
				}
				catch (...)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
					    "External CUDA preparation threw an exception.");
				}
			}

			if (bCapture)
			{
				auto Executable = eastl::make_shared<FArdaCudaGraphExecutable>();
				Executable->mContext = mContext;
				Executable->mCaptureStream = mStreamLifetime;
				Executable->mResources = Batch->mResources;
				Executable->mLaunches = eastl::move(Prepared);
				Executable->mTimingEvents = Group.mTiming ? Group.mTiming->mSample->mEvents : nullptr;
				CUgraph Graph = nullptr;
				bool bOperationFailed = false;
				FArdaRHIStatus Status;
				for (uint32_t Attempt = 0; Attempt < 2; ++Attempt)
				{
					Status = D.Check(D.cuStreamBeginCapture(mStream, CU_STREAM_CAPTURE_MODE_THREAD_LOCAL),
					    "Begin retained CUDA Graph capture");
					if (Status)
					{
						for (size_t I = 0; I <= Executable->mLaunches.size(); ++I)
						{
							if (Executable->mTimingEvents)
							{
								Status = Executable->mTimingEvents->RecordBoundary(I, mStream, true);
								if (!Status)
								{
									break;
								}
							}
							if (I == Executable->mLaunches.size())
							{
								break;
							}
							Status = Executable->mLaunches[I].Enqueue(mStream);
							if (!Status)
							{
								bOperationFailed = true;
								break;
							}
						}
						// Always leave capture mode, including invalidated captures and adapter failures.
						auto End = D.Check(D.cuStreamEndCapture(mStream, &Graph), "End retained CUDA Graph capture");
						if (Status && !End)
						{
							Status = End;
						}
					}
					if (Status)
					{
						Status = ValidateGraphNodes(D, Graph, Executable->mTimingEvents.get());
					}
					if (Status)
					{
						Status = D.Check(D.cuGraphInstantiate(&Executable->mExecutable, Graph, 0),
						    "Instantiate retained CUDA Graph");
					}
					if (Status || !Executable->mTimingEvents || bOperationFailed)
					{
						break;
					}
					// No captured work has executed. Retry optional instrumentation/capture infrastructure
					// failures without markers; actual kernel/library operation errors are never retried.
					if (Graph)
					{
						D.cuGraphDestroy(Graph);
						Graph = nullptr;
					}
					if (Executable->mExecutable)
					{
						D.cuGraphExecDestroy(Executable->mExecutable);
						Executable->mExecutable = nullptr;
					}
					Group.mTiming->mSample->mStatus = Status;
					Group.mTiming->mSample->ReleaseEvents();
					Group.mTiming->mSample->mEvents.reset();
					Executable->mTimingEvents.reset();
				}
				if (Graph)
				{
					D.cuGraphDestroy(Graph);
				}
				eastl::shared_ptr<FArdaCudaGraphExecutable> Retired;
				{
					FArdaCudaGraphCacheAccess Cache(*Group.mCache);
					if (Status)
					{
						++Cache.Stats().mCaptureCount;
						if (Cache.State())
						{
							++Cache.Stats().mRebuildCount;
						}
						else
						{
							Cache.State() = eastl::make_shared<FArdaCudaGraphVariants>();
						}
						auto Variants = eastl::static_pointer_cast<FArdaCudaGraphVariants>(Cache.State());
						if (Variants->mEntries.size() >= Group.mCache->GetMaximumCachedVariants())
						{
							Retired = Variants->mEntries.front();
							Variants->mEntries.erase(Variants->mEntries.begin());
							++Cache.Stats().mEvictionCount;
						}
						Variants->mEntries.push_back(Executable);
						Cache.Stats().mCachedVariantCount = static_cast<uint32_t>(Variants->mEntries.size());
						Cache.Stats().mLastFallbackReason.clear();
						Group.mGraph = eastl::move(Executable);
					}
					else
					{
						++Cache.Stats().mCaptureFailureCount;
						Cache.Stats().mLastFallbackReason = Status.mMessage;
						if (Mode == EArdaCudaGraphMode::Require || bOperationFailed)
						{
							return Status;
						}
						++Cache.Stats().mFallbackCount;
						Prepared = eastl::move(Executable->mLaunches);
					}
				}
			}
			if (!Group.mGraph)
			{
				Group.mLaunches = eastl::move(Prepared);
			}
			mGroups.push_back(eastl::move(Group));
			if (mContext->mCapabilities.mLaunchMode != EArdaCudaLaunchMode::D3D12CiG)
			{
				return {};
			}
#if CUDA_VERSION >= 13030
			CUstreamCigParam Native{STREAM_CIG_DATA_TYPE_D3D12_COMMAND_LIST, CommandList};
			CUstreamCigCaptureParams Capture{&Native};
			if (auto S = D.Check(D.cuStreamBeginCaptureToCig(mStream, &Capture), "Begin CiG capture"); !S)
			{
				mGroups.pop_back();
				return S;
			}
			mContext->mRecording = this;
			FArdaRHIStatus Status;
			Status = mGroups.back().Enqueue(mStream);
			auto End = D.Check(D.cuStreamEndCaptureToCig(mStream), "End CiG capture");
			if (Status && !End)
			{
				Status = End;
			}
			mbFailed = !Status;
			return Status;
#else
			mGroups.pop_back();
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "This SDK does not declare CiG stream capture.");
#endif
		}

		FArdaRHIStatus FArdaCudaBatch::Execute()
		{
			std::lock_guard<std::mutex> Lock(mContext->mMutex);
			if (mbSubmitted || mbFailed || mContext->mCapabilities.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "CUDA batch cannot be executed or replayed.");
			}
			auto& D = *mContext->mDriver;
			FArdaCudaScope Scope(D, mContext->mContext);
			if (auto S = D.Check(Scope.mResult, "Push CUDA context"); !S)
			{
				return S;
			}
			FArdaRHIStatus Status;
			if (mWait)
			{
				auto Semaphore = static_cast<CUexternalSemaphore>(mWait->GetNativeHandle());
				CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS Params{};
				Params.params.fence.value = mWaitValue;
				Status =
				    D.Check(D.cuWaitExternalSemaphoresAsync(&Semaphore, &Params, 1, mStream), "CUDA wait for graphics");
			}
			for (const auto& LaunchInfo : mGroups)
			{
				if (!Status)
				{
					break;
				}
				Status = LaunchInfo.Enqueue(mStream);
			}
			if (Status && mSignal)
			{
				auto Semaphore = static_cast<CUexternalSemaphore>(mSignal->GetNativeHandle());
				CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS Params{};
				Params.params.fence.value = mSignalValue;
				Status = D.Check(D.cuSignalExternalSemaphoresAsync(&Semaphore, &Params, 1, mStream),
				    "CUDA signal to graphics");
			}

			// Drain partially failed work before releasing its inputs; success stays asynchronous.
			if (!Status || !mSignal)
			{
				auto Completion = D.Check(D.cuStreamSynchronize(mStream), "Complete CUDA segment");
				if (Status && !Completion)
				{
					Status = Completion;
				}
			}
			mbSubmitted = true;
			mbFailed = !Status;
			for (const auto& Group : mGroups)
			{
				if (Group.mTiming)
				{
					if (!Status)
					{
						Group.mTiming->mSample->mStatus = Status;
					}
					Group.mTiming->mSample->mbSubmitted.store(true);
				}
			}
			return Status;
		}
	}

	static TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateContext(void* Queue,
	    const void* Identifier,
	    eastl::shared_ptr<void> Lifetime,
	    EArdaCudaExecutionMode Mode,
	    bool bVulkan)
	{
		const auto Unsupported = [](const char* Reason) -> TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>>
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Reason)};
		};
		auto D = eastl::make_shared<FArdaCudaDriver>();
		if (!D->IsComplete())
		{
			return Unsupported("CUDA driver or required external-memory entry points are unavailable.");
		}
		if (auto S = D->Check(D->cuInit(0), "Initialize CUDA driver"); !S)
		{
			return {{}, S};
		}
		int Count = 0;
		if (auto S = D->Check(D->cuDeviceGetCount(&Count), "Enumerate CUDA devices"); !S)
		{
			return {{}, S};
		}
		CUdevice Device = -1;
		for (int I = 0; I < Count; ++I)
		{
			CUdevice CandidateDevice;
			if (D->cuDeviceGet(&CandidateDevice, I) != CUDA_SUCCESS)
			{
				continue;
			}
			if (bVulkan)
			{
				CUuuid Uuid{};
				if (D->cuDeviceGetUuid(&Uuid, CandidateDevice) == CUDA_SUCCESS &&
				    !std::memcmp(Uuid.bytes, Identifier, sizeof(Uuid.bytes)))
				{
					Device = CandidateDevice;
					break;
				}
			}
			else
			{
				char Candidate[8]{};
				unsigned int Nodes = 0;
				if (D->cuDeviceGetLuid && D->cuDeviceGetLuid(Candidate, &Nodes, CandidateDevice) == CUDA_SUCCESS &&
				    Nodes == 1 && !std::memcmp(Candidate, Identifier, 8))
				{
					Device = CandidateDevice;
					break;
				}
			}
		}
		if (Device < 0)
		{
			return Unsupported("No CUDA adapter matches the graphics device identity.");
		}
		const auto Attribute = [&](CUdevice_attribute A)
		{
			int Value = 0;
			D->cuDeviceGetAttribute(&Value, A, Device);
			return uint32_t(Value);
		};
		auto Context = eastl::make_shared<FArdaCudaContext>();
		Context->mDriver = D;
		Context->mbVulkan = bVulkan;
		Context->mNativeLifetime = eastl::move(Lifetime);
		auto& C = Context->mCapabilities;
		C.mLaunchMode = EArdaCudaLaunchMode::ContextSwitch;
		if (Mode != EArdaCudaExecutionMode::ContextSwitch)
		{
			C.mFallbackReason = bVulkan ? "Vulkan CiG external queue is unavailable in this SDK, driver, or device."
			                            : "D3D12 CiG stream capture is unavailable in this SDK, driver, or adapter.";
#if CUDA_VERSION >= 13030
			if (D->cuCtxCreate && Queue &&
			    (bVulkan ? Attribute(CU_DEVICE_ATTRIBUTE_VULKAN_CIG_SUPPORTED)
			             : D->cuStreamBeginCaptureToCig && D->cuStreamEndCaptureToCig &&
			                Attribute(CU_DEVICE_ATTRIBUTE_D3D12_CIG_SUPPORTED) &&
			                Attribute(CU_DEVICE_ATTRIBUTE_D3D12_CIG_STREAMS_SUPPORTED)))
			{
				CUctxCigParam Cig{bVulkan ? CIG_DATA_TYPE_NV_BLOB : CIG_DATA_TYPE_D3D12_COMMAND_QUEUE, Queue};
				CUctxCreateParams Params{};
				Params.cigParams = &Cig;
				auto Status =
				    D->Check(D->cuCtxCreate(&Context->mContext, &Params, 0, Device), "Create CUDA CiG context");
				if (Status)
				{
					C.mLaunchMode = bVulkan ? EArdaCudaLaunchMode::VulkanCiG : EArdaCudaLaunchMode::D3D12CiG;
					C.mFallbackReason.clear();
				}
				else
				{
					C.mFallbackReason = Status.mMessage;
				}
			}
#endif
			if (!Context->mContext && Mode == EArdaCudaExecutionMode::GraphicsQueue)
			{
				return Unsupported(C.mFallbackReason.c_str());
			}
		}
		if (!Context->mContext)
		{
			if (auto Status =
			        D->Check(D->mCreateContext(&Context->mContext, 0, Device), "Create ordinary CUDA context");
			    !Status)
			{
				return {{}, Status};
			}
		}
		CUcontext Popped = nullptr;
		if (auto Status = D->Check(D->cuCtxPopCurrent(&Popped), "Restore caller CUDA context"); !Status)
		{
			return {{}, Status};
		}
		C.mComputeCapability = Attribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR) * 10 +
		    Attribute(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR);
		C.mMaxThreadsPerBlock = Attribute(CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK);
		for (uint32_t I = 0; I < 3; ++I)
		{
			C.mMaxBlockSize[I] = Attribute(static_cast<CUdevice_attribute>(CU_DEVICE_ATTRIBUTE_MAX_BLOCK_DIM_X + I));
			C.mMaxGridSize[I] = Attribute(static_cast<CUdevice_attribute>(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X + I));
		}
		C.mMaxSharedMemoryBytes = Attribute(CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK);
#if CUDA_VERSION >= 13030
		if (C.mLaunchMode == EArdaCudaLaunchMode::D3D12CiG)
		{
			FArdaCudaScope Scope(*D, Context->mContext);
			if (auto Status = D->Check(Scope.mResult, "Push CiG context"); !Status)
			{
				return {{}, Status};
			}
			size_t Shared = 0;
			if (auto Status = D->Check(D->cuCtxGetLimit(&Shared, CU_LIMIT_SHMEM_SIZE), "Query CiG shared-memory limit");
			    !Status)
			{
				return {{}, Status};
			}
			C.mMaxSharedMemoryBytes = static_cast<uint32_t>(eastl::min<size_t>(Shared, C.mMaxSharedMemoryBytes));
		}
#endif
		C.mbSurfaceAccess = true;

		// Opaque Vulkan layered images have not passed graphics/CUDA layer-stride
		// qualification. Do not expose a successfully mapped but incompatible layout.
		C.mbLayeredSurfaceAccess = !bVulkan;
		C.mUnavailableReason.clear();
		return {Context, {}};
	}

	TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaD3D12CudaContext(void* Queue,
	    const void* Luid,
	    eastl::shared_ptr<void> Lifetime,
	    EArdaCudaExecutionMode Mode)
	{
		return CreateContext(Queue, Luid, eastl::move(Lifetime), Mode, false);
	}

	TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaVulkanCudaContext(const void* DeviceUuid,
	    void* ExternalQueueData,
	    EArdaCudaExecutionMode Mode)
	{
		return CreateContext(ExternalQueueData, DeviceUuid, {}, Mode, true);
	}
}
#else
namespace arda
{
	TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaD3D12CudaContext(void*,
	    const void*,
	    eastl::shared_ptr<void>,
	    EArdaCudaExecutionMode)
	{
		return {{},
		    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "This build excludes the CUDA driver provider.")};
	}

	TArdaRHIResult<eastl::shared_ptr<IArdaCudaContext>> CreateArdaVulkanCudaContext(const void*,
	    void*,
	    EArdaCudaExecutionMode)
	{
		return {{},
		    FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "This build excludes the CUDA driver provider.")};
	}
}
#endif
