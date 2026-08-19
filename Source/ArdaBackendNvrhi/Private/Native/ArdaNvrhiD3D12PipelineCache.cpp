#include "ArdaNvrhiPch.h"

#include "Native/ArdaNvrhiPipelineCache.h"
#include "Native/ArdaNvrhiPipelineCacheCommon.h"

#if defined(_WIN32) && defined(ARDA_NVRHI_WITH_D3D12)

#include <atomic>
#include <vector>

namespace arda::rhi::private_impl
{
    namespace
    {
        class FD3D12PipelineCache;

        thread_local FD3D12PipelineCache* GActiveCache = nullptr;
        thread_local uint64_t GActiveKey = 0;

        class FD3D12DeviceProxy final : public ID3D12Device
        {
        public:
            FD3D12DeviceProxy(
                ID3D12Device* Device,
                FD3D12PipelineCache* Cache)
                : mDevice(Device), mCache(Cache)
            {
            }

            HRESULT STDMETHODCALLTYPE QueryInterface(
                REFIID InterfaceId, void** Object) override
            {
                if (!Object)
                    return E_POINTER;
                if (InterfaceId == __uuidof(IUnknown) ||
                    InterfaceId == __uuidof(ID3D12Object) ||
                    InterfaceId == __uuidof(ID3D12Device))
                {
                    *Object = static_cast<ID3D12Device*>(this);
                    AddRef();
                    return S_OK;
                }
                return mDevice->QueryInterface(InterfaceId, Object);
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return ++mReferences;
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                const ULONG References = --mReferences;
                if (!References)
                    delete this;
                return References;
            }

            HRESULT STDMETHODCALLTYPE GetPrivateData(
                REFGUID Guid, UINT* Size, void* Data) override
            { return mDevice->GetPrivateData(Guid, Size, Data); }
            HRESULT STDMETHODCALLTYPE SetPrivateData(
                REFGUID Guid, UINT Size, const void* Data) override
            { return mDevice->SetPrivateData(Guid, Size, Data); }
            HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
                REFGUID Guid, const IUnknown* Data) override
            { return mDevice->SetPrivateDataInterface(Guid, Data); }
            HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
            { return mDevice->SetName(Name); }
            UINT STDMETHODCALLTYPE GetNodeCount() override
            { return mDevice->GetNodeCount(); }
            HRESULT STDMETHODCALLTYPE CreateCommandQueue(
                const D3D12_COMMAND_QUEUE_DESC* Desc, REFIID Id, void** Value) override
            { return mDevice->CreateCommandQueue(Desc, Id, Value); }
            HRESULT STDMETHODCALLTYPE CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE Type, REFIID Id, void** Value) override
            { return mDevice->CreateCommandAllocator(Type, Id, Value); }
            HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(
                const D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc,
                REFIID Id, void** Value) override;
            HRESULT STDMETHODCALLTYPE CreateComputePipelineState(
                const D3D12_COMPUTE_PIPELINE_STATE_DESC* Desc,
                REFIID Id, void** Value) override;
            HRESULT STDMETHODCALLTYPE CreateCommandList(
                UINT NodeMask, D3D12_COMMAND_LIST_TYPE Type,
                ID3D12CommandAllocator* Allocator,
                ID3D12PipelineState* InitialState,
                REFIID Id, void** Value) override
            { return mDevice->CreateCommandList(NodeMask, Type, Allocator, InitialState, Id, Value); }
            HRESULT STDMETHODCALLTYPE CheckFeatureSupport(
                D3D12_FEATURE Feature, void* Data, UINT Size) override
            { return mDevice->CheckFeatureSupport(Feature, Data, Size); }
            HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(
                const D3D12_DESCRIPTOR_HEAP_DESC* Desc, REFIID Id, void** Value) override
            { return mDevice->CreateDescriptorHeap(Desc, Id, Value); }
            UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE Type) override
            { return mDevice->GetDescriptorHandleIncrementSize(Type); }
            HRESULT STDMETHODCALLTYPE CreateRootSignature(
                UINT NodeMask, const void* Blob, SIZE_T BlobSize,
                REFIID Id, void** Value) override
            { return mDevice->CreateRootSignature(NodeMask, Blob, BlobSize, Id, Value); }
            void STDMETHODCALLTYPE CreateConstantBufferView(
                const D3D12_CONSTANT_BUFFER_VIEW_DESC* Desc,
                D3D12_CPU_DESCRIPTOR_HANDLE Destination) override
            { mDevice->CreateConstantBufferView(Desc, Destination); }
            void STDMETHODCALLTYPE CreateShaderResourceView(
                ID3D12Resource* Resource,
                const D3D12_SHADER_RESOURCE_VIEW_DESC* Desc,
                D3D12_CPU_DESCRIPTOR_HANDLE Destination) override
            { mDevice->CreateShaderResourceView(Resource, Desc, Destination); }
            void STDMETHODCALLTYPE CreateUnorderedAccessView(
                ID3D12Resource* Resource, ID3D12Resource* CounterResource,
                const D3D12_UNORDERED_ACCESS_VIEW_DESC* Desc,
                D3D12_CPU_DESCRIPTOR_HANDLE Destination) override
            { mDevice->CreateUnorderedAccessView(Resource, CounterResource, Desc, Destination); }
            void STDMETHODCALLTYPE CreateRenderTargetView(
                ID3D12Resource* Resource,
                const D3D12_RENDER_TARGET_VIEW_DESC* Desc,
                D3D12_CPU_DESCRIPTOR_HANDLE Destination) override
            { mDevice->CreateRenderTargetView(Resource, Desc, Destination); }
            void STDMETHODCALLTYPE CreateDepthStencilView(
                ID3D12Resource* Resource,
                const D3D12_DEPTH_STENCIL_VIEW_DESC* Desc,
                D3D12_CPU_DESCRIPTOR_HANDLE Destination) override
            { mDevice->CreateDepthStencilView(Resource, Desc, Destination); }
            void STDMETHODCALLTYPE CreateSampler(
                const D3D12_SAMPLER_DESC* Desc,
                D3D12_CPU_DESCRIPTOR_HANDLE Destination) override
            { mDevice->CreateSampler(Desc, Destination); }
            void STDMETHODCALLTYPE CopyDescriptors(
                UINT DestinationCount,
                const D3D12_CPU_DESCRIPTOR_HANDLE* DestinationStarts,
                const UINT* DestinationSizes,
                UINT SourceCount,
                const D3D12_CPU_DESCRIPTOR_HANDLE* SourceStarts,
                const UINT* SourceSizes,
                D3D12_DESCRIPTOR_HEAP_TYPE Type) override
            { mDevice->CopyDescriptors(DestinationCount, DestinationStarts, DestinationSizes, SourceCount, SourceStarts, SourceSizes, Type); }
            void STDMETHODCALLTYPE CopyDescriptorsSimple(
                UINT Count, D3D12_CPU_DESCRIPTOR_HANDLE Destination,
                D3D12_CPU_DESCRIPTOR_HANDLE Source,
                D3D12_DESCRIPTOR_HEAP_TYPE Type) override
            { mDevice->CopyDescriptorsSimple(Count, Destination, Source, Type); }
            D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(
                UINT VisibleMask, UINT Count,
                const D3D12_RESOURCE_DESC* Descs) override
            { return mDevice->GetResourceAllocationInfo(VisibleMask, Count, Descs); }
            D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(
                UINT NodeMask, D3D12_HEAP_TYPE Type) override
            { return mDevice->GetCustomHeapProperties(NodeMask, Type); }
            HRESULT STDMETHODCALLTYPE CreateCommittedResource(
                const D3D12_HEAP_PROPERTIES* HeapProperties,
                D3D12_HEAP_FLAGS HeapFlags,
                const D3D12_RESOURCE_DESC* Desc,
                D3D12_RESOURCE_STATES InitialState,
                const D3D12_CLEAR_VALUE* ClearValue,
                REFIID Id, void** Value) override
            { return mDevice->CreateCommittedResource(HeapProperties, HeapFlags, Desc, InitialState, ClearValue, Id, Value); }
            HRESULT STDMETHODCALLTYPE CreateHeap(
                const D3D12_HEAP_DESC* Desc, REFIID Id, void** Value) override
            { return mDevice->CreateHeap(Desc, Id, Value); }
            HRESULT STDMETHODCALLTYPE CreatePlacedResource(
                ID3D12Heap* Heap, UINT64 Offset,
                const D3D12_RESOURCE_DESC* Desc,
                D3D12_RESOURCE_STATES InitialState,
                const D3D12_CLEAR_VALUE* ClearValue,
                REFIID Id, void** Value) override
            { return mDevice->CreatePlacedResource(Heap, Offset, Desc, InitialState, ClearValue, Id, Value); }
            HRESULT STDMETHODCALLTYPE CreateReservedResource(
                const D3D12_RESOURCE_DESC* Desc,
                D3D12_RESOURCE_STATES InitialState,
                const D3D12_CLEAR_VALUE* ClearValue,
                REFIID Id, void** Value) override
            { return mDevice->CreateReservedResource(Desc, InitialState, ClearValue, Id, Value); }
            HRESULT STDMETHODCALLTYPE CreateSharedHandle(
                ID3D12DeviceChild* Object,
                const SECURITY_ATTRIBUTES* Attributes,
                DWORD Access, LPCWSTR Name, HANDLE* Handle) override
            { return mDevice->CreateSharedHandle(Object, Attributes, Access, Name, Handle); }
            HRESULT STDMETHODCALLTYPE OpenSharedHandle(
                HANDLE Handle, REFIID Id, void** Value) override
            { return mDevice->OpenSharedHandle(Handle, Id, Value); }
            HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(
                LPCWSTR Name, DWORD Access, HANDLE* Handle) override
            { return mDevice->OpenSharedHandleByName(Name, Access, Handle); }
            HRESULT STDMETHODCALLTYPE MakeResident(
                UINT Count, ID3D12Pageable* const* Objects) override
            { return mDevice->MakeResident(Count, Objects); }
            HRESULT STDMETHODCALLTYPE Evict(
                UINT Count, ID3D12Pageable* const* Objects) override
            { return mDevice->Evict(Count, Objects); }
            HRESULT STDMETHODCALLTYPE CreateFence(
                UINT64 InitialValue, D3D12_FENCE_FLAGS Flags,
                REFIID Id, void** Value) override
            { return mDevice->CreateFence(InitialValue, Flags, Id, Value); }
            HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override
            { return mDevice->GetDeviceRemovedReason(); }
            void STDMETHODCALLTYPE GetCopyableFootprints(
                const D3D12_RESOURCE_DESC* Desc,
                UINT FirstSubresource, UINT Count, UINT64 BaseOffset,
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT* Layouts,
                UINT* RowCounts, UINT64* RowSizes, UINT64* TotalBytes) override
            { mDevice->GetCopyableFootprints(Desc, FirstSubresource, Count, BaseOffset, Layouts, RowCounts, RowSizes, TotalBytes); }
            HRESULT STDMETHODCALLTYPE CreateQueryHeap(
                const D3D12_QUERY_HEAP_DESC* Desc, REFIID Id, void** Value) override
            { return mDevice->CreateQueryHeap(Desc, Id, Value); }
            HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override
            { return mDevice->SetStablePowerState(Enable); }
            HRESULT STDMETHODCALLTYPE CreateCommandSignature(
                const D3D12_COMMAND_SIGNATURE_DESC* Desc,
                ID3D12RootSignature* RootSignature,
                REFIID Id, void** Value) override
            { return mDevice->CreateCommandSignature(Desc, RootSignature, Id, Value); }
            void STDMETHODCALLTYPE GetResourceTiling(
                ID3D12Resource* Resource, UINT* TileCount,
                D3D12_PACKED_MIP_INFO* PackedMip,
                D3D12_TILE_SHAPE* TileShape,
                UINT* SubresourceTilingCount,
                UINT FirstSubresource,
                D3D12_SUBRESOURCE_TILING* SubresourceTilings) override
            { mDevice->GetResourceTiling(Resource, TileCount, PackedMip, TileShape, SubresourceTilingCount, FirstSubresource, SubresourceTilings); }
            LUID STDMETHODCALLTYPE GetAdapterLuid() override
            { return mDevice->GetAdapterLuid(); }

        private:
            std::atomic_ulong mReferences{ 1 };
            Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
            FD3D12PipelineCache* mCache = nullptr;
        };

        class FD3D12PipelineCache final : public IArdaNvrhiPipelineCache
        {
        public:
            FD3D12PipelineCache(
                ID3D12Device* Device,
                eastl::string BackendName,
                std::filesystem::path Directory,
                backend::IArdaDiagnosticCallback* DiagnosticCallback)
                : mDevice(Device)
                , mBackendName(eastl::move(BackendName))
                , mDirectory(std::move(Directory))
                , mDiagnosticCallback(DiagnosticCallback)
            {
                Microsoft::WRL::ComPtr<ID3D12Device1> Device1;
                mbSupported = SUCCEEDED(mDevice.As(&Device1));
                if (!mbSupported || mDirectory.empty())
                    return;

                std::vector<uint8_t> Payload;
                const auto Path = pipeline_cache::MakePath(
                    mDirectory, mBackendName);
                std::error_code Error;
                const bool Exists = std::filesystem::exists(Path, Error);
                const bool Valid = Exists &&
                    pipeline_cache::ReadBlob(Path, mBackendName, Payload);
                if (Valid)
                    mSourceBlob = eastl::move(Payload);
                HRESULT Result = Device1->CreatePipelineLibrary(
                    mSourceBlob.empty() ? nullptr : mSourceBlob.data(),
                    mSourceBlob.size(),
                    IID_PPV_ARGS(&mLibrary));
                if (FAILED(Result) && Valid)
                {
                    pipeline_cache::Warn(
                        mDiagnosticCallback,
                        "D3D12 rejected persistent pipeline library data; using an empty library.");
                    mSourceBlob.clear();
                    Result = Device1->CreatePipelineLibrary(
                        nullptr, 0, IID_PPV_ARGS(&mLibrary));
                }
                else if (Exists && !Valid)
                {
                    pipeline_cache::Warn(
                        mDiagnosticCallback,
                        "Ignoring a corrupt, truncated, or wrong-backend pipeline cache blob.");
                }
                if (FAILED(Result) || !mLibrary)
                {
                    mbSupported = false;
                    pipeline_cache::Warn(
                        mDiagnosticCallback,
                        "ID3D12PipelineLibrary is unavailable for this device.");
                    return;
                }

                mProxy.Attach(new FD3D12DeviceProxy(mDevice.Get(), this));
                mbEnabled = true;
            }

            ~FD3D12PipelineCache() override
            {
                FlushAndDisable();
                mProxy.Reset();
            }

            bool IsSupported() const noexcept override
            {
                return mbSupported;
            }

            void SetPipelineKey(uint64_t Key) noexcept override
            {
                GActiveCache = this;
                GActiveKey = Key;
            }

            void ClearPipelineKey() noexcept override
            {
                if (GActiveCache == this)
                {
                    GActiveCache = nullptr;
                    GActiveKey = 0;
                }
            }

            void MarkDirty() noexcept override
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                if (mbEnabled)
                    mbDirty = true;
            }

            void FlushAndDisable() noexcept override
            {
                std::lock_guard<std::mutex> Lock(mMutex);
                if (!mbEnabled)
                    return;
                if (mbDirty && mLibrary)
                {
                    const SIZE_T Size = mLibrary->GetSerializedSize();
                    if (Size <= pipeline_cache::MaxPayloadSize)
                    {
                        std::vector<uint8_t> Payload(Size);
                        if (SUCCEEDED(mLibrary->Serialize(
                                Payload.data(), Payload.size())))
                        {
                            if (!pipeline_cache::WriteBlob(
                                    pipeline_cache::MakePath(
                                        mDirectory, mBackendName),
                                    mBackendName,
                                    Payload))
                            {
                                pipeline_cache::Warn(
                                    mDiagnosticCallback,
                                    "Failed to atomically save the D3D12 pipeline library.");
                            }
                        }
                    }
                }
                mLibrary.Reset();
                mSourceBlob.clear();
                mbEnabled = false;
                mbDirty = false;
                mDirectory.clear();
                mDiagnosticCallback = nullptr;
            }

            ID3D12Device* GetD3D12DeviceForNvrhi() noexcept override
            {
                return mbEnabled ? mProxy.Get() : mDevice.Get();
            }

            HRESULT CreateGraphicsPipeline(
                const D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc,
                REFIID Id,
                void** Value)
            {
                return CreatePipeline(
                    L"Graphics", Id, Value,
                    [this, Desc, &Id, Value](LPCWSTR Name)
                    { return mLibrary->LoadGraphicsPipeline(Name, Desc, Id, Value); },
                    [this, Desc, &Id, Value]()
                    { return mDevice->CreateGraphicsPipelineState(Desc, Id, Value); });
            }

            HRESULT CreateComputePipeline(
                const D3D12_COMPUTE_PIPELINE_STATE_DESC* Desc,
                REFIID Id,
                void** Value)
            {
                return CreatePipeline(
                    L"Compute", Id, Value,
                    [this, Desc, &Id, Value](LPCWSTR Name)
                    { return mLibrary->LoadComputePipeline(Name, Desc, Id, Value); },
                    [this, Desc, &Id, Value]()
                    { return mDevice->CreateComputePipelineState(Desc, Id, Value); });
            }

        private:
            template<typename TLoad, typename TCreate>
            HRESULT CreatePipeline(
                const wchar_t* Kind,
                REFIID,
                void** Value,
                TLoad&& Load,
                TCreate&& Create)
            {
                if (GActiveCache != this || !GActiveKey || !mLibrary)
                    return Create();

                wchar_t Name[48]{};
                _snwprintf_s(
                    Name, _countof(Name), _TRUNCATE,
                    Kind[0] == L'G' ? L"G_%016llX" : L"C_%016llX",
                    static_cast<unsigned long long>(GActiveKey));
                std::lock_guard<std::mutex> Lock(mMutex);
                HRESULT Result = Load(Name);
                if (SUCCEEDED(Result))
                {
                    if (mDiagnosticCallback)
                    {
                        const char* Message = Kind[0] == L'G'
                            ? "LoadGraphicsPipeline accepted a cached D3D12 PSO."
                            : "LoadComputePipeline accepted a cached D3D12 PSO.";
                        mDiagnosticCallback->Message(
                            backend::EArdaDiagnosticSeverity::Info, Message);
                    }
                    return Result;
                }

                // D3D12 load failures are allowed to leave the caller's output
                // unspecified. NVRHI's RefCountPtr expects a clean output slot
                // before the ordinary creation fallback.
                if (Value)
                    *Value = nullptr;
                Result = Create();
                if (SUCCEEDED(Result) && Value && *Value)
                {
                    Microsoft::WRL::ComPtr<ID3D12PipelineState> Pipeline;
                    if (SUCCEEDED(static_cast<IUnknown*>(*Value)->QueryInterface(
                            IID_PPV_ARGS(&Pipeline))))
                    {
                        const HRESULT StoreResult =
                            mLibrary->StorePipeline(Name, Pipeline.Get());
                        if (SUCCEEDED(StoreResult))
                            mbDirty = true;
                    }
                }
                return Result;
            }

            Microsoft::WRL::ComPtr<ID3D12Device> mDevice;
            Microsoft::WRL::ComPtr<ID3D12Device> mProxy;
            Microsoft::WRL::ComPtr<ID3D12PipelineLibrary> mLibrary;
            // D3D12 may retain references into the initialization blob.
            std::vector<uint8_t> mSourceBlob;
            eastl::string mBackendName;
            std::filesystem::path mDirectory;
            backend::IArdaDiagnosticCallback* mDiagnosticCallback = nullptr;
            std::mutex mMutex;
            bool mbSupported = false;
            bool mbEnabled = false;
            bool mbDirty = false;
        };

        HRESULT FD3D12DeviceProxy::CreateGraphicsPipelineState(
            const D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc,
            REFIID Id,
            void** Value)
        {
            return mCache->CreateGraphicsPipeline(Desc, Id, Value);
        }

        HRESULT FD3D12DeviceProxy::CreateComputePipelineState(
            const D3D12_COMPUTE_PIPELINE_STATE_DESC* Desc,
            REFIID Id,
            void** Value)
        {
            return mCache->CreateComputePipeline(Desc, Id, Value);
        }
    }

    eastl::shared_ptr<IArdaNvrhiPipelineCache>
    CreateArdaNvrhiD3D12PipelineCache(
        ID3D12Device* Device,
        const eastl::string& BackendName,
        const std::filesystem::path& Directory,
        backend::IArdaDiagnosticCallback* DiagnosticCallback)
    {
        if (!Device)
            return {};
        return eastl::make_shared<FD3D12PipelineCache>(
            Device, BackendName, Directory, DiagnosticCallback);
    }
}

#endif
