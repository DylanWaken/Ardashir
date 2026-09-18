#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <cstring>
#include "RHI/Device/ArdaRHIDiagnostics.h"

namespace arda
{
	class FArdaD3D12AuditDred final : public ID3D12DeviceRemovedExtendedData1
	{
	public:
		FArdaD3D12AuditDred()
		{
			std::memset(mName, 'B', sizeof(mName) - 1);
			for (size_t Index = 0; Index < mNodes.size(); ++Index)
			{
				auto& Node = mNodes[Index];
				Node.pCommandListDebugNameA = mName;
				Node.pCommandQueueDebugNameA = "Injected queue";
				Node.BreadcrumbCount = 4;
				Node.pLastBreadcrumbValue = &mCompleted;
				Node.pNext = Index + 1 < mNodes.size() ? &mNodes[Index + 1] : nullptr;
			}
			mAllocation.ObjectNameA = "Injected live buffer";
			mAllocation.AllocationType = D3D12_DRED_ALLOCATION_TYPE_RESOURCE;
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Interface, void** Object) override
		{
			if (!Object)
			{
				return E_POINTER;
			}
			*Object = nullptr;
			if (Interface != __uuidof(IUnknown) && Interface != __uuidof(ID3D12DeviceRemovedExtendedData) &&
			    Interface != __uuidof(ID3D12DeviceRemovedExtendedData1))
			{
				return E_NOINTERFACE;
			}
			*Object = this;
			AddRef();
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++mReferences;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG Remaining = --mReferences;
			if (!Remaining)
			{
				delete this;
			}
			return Remaining;
		}

		HRESULT STDMETHODCALLTYPE GetAutoBreadcrumbsOutput(D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT*) override
		{
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetPageFaultAllocationOutput(D3D12_DRED_PAGE_FAULT_OUTPUT*) override
		{
			return E_NOTIMPL;
		}

		HRESULT STDMETHODCALLTYPE GetAutoBreadcrumbsOutput1(D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1* Output) override
		{
			Output->pHeadAutoBreadcrumbNode = mNodes.data();
			return S_OK;
		}

		HRESULT STDMETHODCALLTYPE GetPageFaultAllocationOutput1(D3D12_DRED_PAGE_FAULT_OUTPUT1* Output) override
		{
			*Output = {0xABC000, &mAllocation, nullptr};
			return S_OK;
		}

	private:
		std::atomic<ULONG> mReferences{1};
		std::array<D3D12_AUTO_BREADCRUMB_NODE1, ArdaRHIMaxDiagnosticEntries + 1> mNodes{};
		D3D12_DRED_ALLOCATION_NODE1 mAllocation{};
		UINT mCompleted = 2;
		char mName[1100]{};
	};

	// A real native device with scoped fault injection and descriptor observation.
	class FArdaD3D12AuditDevice final : public ID3D12Device
	{
	public:
		explicit FArdaD3D12AuditDevice(ID3D12Device* Native)
		    : mNative(Native)
		{
		}

		uint32_t mFailFenceCall = 0;
		uint32_t mFenceCalls = 0;
		HRESULT mRemovedReason = S_OK;
		Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> mDredOverride;
		D3D12_RENDER_TARGET_VIEW_DESC mLastRtv{};
		D3D12_DEPTH_STENCIL_VIEW_DESC mLastDsv{};
		uint32_t mRtvCount = 0;
		uint32_t mDsvCount = 0;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Interface, void** Object) override
		{
			if (!Object)
			{
				return E_POINTER;
			}
			if (Interface == __uuidof(IUnknown) || Interface == __uuidof(ID3D12Object) ||
			    Interface == __uuidof(ID3D12Device))
			{
				*Object = this;
				AddRef();
				return S_OK;
			}
			if (mDredOverride && Interface == __uuidof(ID3D12DeviceRemovedExtendedData1))
			{
				return mDredOverride->QueryInterface(Interface, Object);
			}
			return mNative->QueryInterface(Interface, Object);
		}

		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++mReferences;
		}

		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG Remaining = --mReferences;
			if (!Remaining)
			{
				delete this;
			}
			return Remaining;
		}

		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Id, UINT* Size, void* Data) override
		{
			return mNative->GetPrivateData(Id, Size, Data);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Id, UINT Size, const void* Data) override
		{
			return mNative->SetPrivateData(Id, Size, Data);
		}

		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Id, const IUnknown* Data) override
		{
			return mNative->SetPrivateDataInterface(Id, Data);
		}

		HRESULT STDMETHODCALLTYPE SetName(LPCWSTR Name) override
		{
			return mNative->SetName(Name);
		}

		UINT STDMETHODCALLTYPE GetNodeCount() override
		{
			return mNative->GetNodeCount();
		}

		HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC* Desc,
		    REFIID Riid,
		    void** CommandQueue) override
		{
			return mNative->CreateCommandQueue(Desc, Riid, CommandQueue);
		}

		HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE Type,
		    REFIID Riid,
		    void** CommandAllocator) override
		{
			return mNative->CreateCommandAllocator(Type, Riid, CommandAllocator);
		}

		HRESULT STDMETHODCALLTYPE CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc,
		    REFIID Riid,
		    void** PipelineState) override
		{
			return mNative->CreateGraphicsPipelineState(Desc, Riid, PipelineState);
		}

		HRESULT STDMETHODCALLTYPE CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* Desc,
		    REFIID Riid,
		    void** PipelineState) override
		{
			return mNative->CreateComputePipelineState(Desc, Riid, PipelineState);
		}

		HRESULT STDMETHODCALLTYPE CreateCommandList(UINT NodeMask,
		    D3D12_COMMAND_LIST_TYPE Type,
		    ID3D12CommandAllocator* CommandAllocator,
		    ID3D12PipelineState* InitialState,
		    REFIID Riid,
		    void** CommandList) override
		{
			return mNative->CreateCommandList(NodeMask, Type, CommandAllocator, InitialState, Riid, CommandList);
		}

		HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE Feature,
		    void* FeatureSupportData,
		    UINT FeatureSupportDataSize) override
		{
			return mNative->CheckFeatureSupport(Feature, FeatureSupportData, FeatureSupportDataSize);
		}

		HRESULT STDMETHODCALLTYPE CreateDescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC* DescriptorHeapDesc,
		    REFIID Riid,
		    void** vHeap) override
		{
			return mNative->CreateDescriptorHeap(DescriptorHeapDesc, Riid, vHeap);
		}

		UINT STDMETHODCALLTYPE GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType) override
		{
			return mNative->GetDescriptorHandleIncrementSize(DescriptorHeapType);
		}

		HRESULT STDMETHODCALLTYPE CreateRootSignature(UINT NodeMask,
		    const void* BlobWithRootSignature,
		    SIZE_T BlobLengthInBytes,
		    REFIID Riid,
		    void** vRootSignature) override
		{
			return mNative->CreateRootSignature(NodeMask,
			    BlobWithRootSignature,
			    BlobLengthInBytes,
			    Riid,
			    vRootSignature);
		}

		void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC* Desc,
		    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			return mNative->CreateConstantBufferView(Desc, DestDescriptor);
		}

		void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource* Resource,
		    const D3D12_SHADER_RESOURCE_VIEW_DESC* Desc,
		    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			return mNative->CreateShaderResourceView(Resource, Desc, DestDescriptor);
		}

		void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource* Resource,
		    ID3D12Resource* CounterResource,
		    const D3D12_UNORDERED_ACCESS_VIEW_DESC* Desc,
		    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			return mNative->CreateUnorderedAccessView(Resource, CounterResource, Desc, DestDescriptor);
		}

		void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource* Resource,
		    const D3D12_RENDER_TARGET_VIEW_DESC* Desc,
		    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			++mRtvCount;
			mLastRtv = Desc ? *Desc : D3D12_RENDER_TARGET_VIEW_DESC{};
			return mNative->CreateRenderTargetView(Resource, Desc, DestDescriptor);
		}

		void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource* Resource,
		    const D3D12_DEPTH_STENCIL_VIEW_DESC* Desc,
		    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			++mDsvCount;
			mLastDsv = Desc ? *Desc : D3D12_DEPTH_STENCIL_VIEW_DESC{};
			return mNative->CreateDepthStencilView(Resource, Desc, DestDescriptor);
		}

		void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC* Desc,
		    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor) override
		{
			return mNative->CreateSampler(Desc, DestDescriptor);
		}

		void STDMETHODCALLTYPE CopyDescriptors(UINT NumDestDescriptorRanges,
		    const D3D12_CPU_DESCRIPTOR_HANDLE* DestDescriptorRangeStarts,
		    const UINT* DestDescriptorRangeSizes,
		    UINT NumSrcDescriptorRanges,
		    const D3D12_CPU_DESCRIPTOR_HANDLE* SrcDescriptorRangeStarts,
		    const UINT* SrcDescriptorRangeSizes,
		    D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override
		{
			return mNative->CopyDescriptors(NumDestDescriptorRanges,
			    DestDescriptorRangeStarts,
			    DestDescriptorRangeSizes,
			    NumSrcDescriptorRanges,
			    SrcDescriptorRangeStarts,
			    SrcDescriptorRangeSizes,
			    DescriptorHeapsType);
		}

		void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT NumDescriptors,
		    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
		    D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
		    D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType) override
		{
			return mNative->CopyDescriptorsSimple(NumDescriptors,
			    DestDescriptorRangeStart,
			    SrcDescriptorRangeStart,
			    DescriptorHeapsType);
		}

		D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT VisibleMask,
		    UINT NumResourceDescs,
		    const D3D12_RESOURCE_DESC* ResourceDescs) override
		{
			return mNative->GetResourceAllocationInfo(VisibleMask, NumResourceDescs, ResourceDescs);
		}

		D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT NodeMask,
		    D3D12_HEAP_TYPE HeapType) override
		{
			return mNative->GetCustomHeapProperties(NodeMask, HeapType);
		}

		HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES* HeapProperties,
		    D3D12_HEAP_FLAGS HeapFlags,
		    const D3D12_RESOURCE_DESC* Desc,
		    D3D12_RESOURCE_STATES InitialResourceState,
		    const D3D12_CLEAR_VALUE* OptimizedClearValue,
		    REFIID RiidResource,
		    void** vResource) override
		{
			return mNative->CreateCommittedResource(HeapProperties,
			    HeapFlags,
			    Desc,
			    InitialResourceState,
			    OptimizedClearValue,
			    RiidResource,
			    vResource);
		}

		HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC* Desc, REFIID Riid, void** vHeap) override
		{
			return mNative->CreateHeap(Desc, Riid, vHeap);
		}

		HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap* Heap,
		    UINT64 HeapOffset,
		    const D3D12_RESOURCE_DESC* Desc,
		    D3D12_RESOURCE_STATES InitialState,
		    const D3D12_CLEAR_VALUE* OptimizedClearValue,
		    REFIID Riid,
		    void** vResource) override
		{
			return mNative
			    ->CreatePlacedResource(Heap, HeapOffset, Desc, InitialState, OptimizedClearValue, Riid, vResource);
		}

		HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC* Desc,
		    D3D12_RESOURCE_STATES InitialState,
		    const D3D12_CLEAR_VALUE* OptimizedClearValue,
		    REFIID Riid,
		    void** vResource) override
		{
			return mNative->CreateReservedResource(Desc, InitialState, OptimizedClearValue, Riid, vResource);
		}

		HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild* Object,
		    const SECURITY_ATTRIBUTES* Attributes,
		    DWORD Access,
		    LPCWSTR Name,
		    HANDLE* Handle) override
		{
			return mNative->CreateSharedHandle(Object, Attributes, Access, Name, Handle);
		}

		HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE NTHandle, REFIID Riid, void** vObj) override
		{
			return mNative->OpenSharedHandle(NTHandle, Riid, vObj);
		}

		HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR Name, DWORD Access, HANDLE* NTHandle) override
		{
			return mNative->OpenSharedHandleByName(Name, Access, NTHandle);
		}

		HRESULT STDMETHODCALLTYPE MakeResident(UINT NumObjects, ID3D12Pageable* const* Objects) override
		{
			return mNative->MakeResident(NumObjects, Objects);
		}

		HRESULT STDMETHODCALLTYPE Evict(UINT NumObjects, ID3D12Pageable* const* Objects) override
		{
			return mNative->Evict(NumObjects, Objects);
		}

		HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue,
		    D3D12_FENCE_FLAGS Flags,
		    REFIID Riid,
		    void** Fence) override
		{
			if (++mFenceCalls == mFailFenceCall)
			{
				*Fence = nullptr;
				return E_OUTOFMEMORY;
			}
			return mNative->CreateFence(InitialValue, Flags, Riid, Fence);
		}

		HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override
		{
			return FAILED(mRemovedReason) ? mRemovedReason : mNative->GetDeviceRemovedReason();
		}

		void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC* ResourceDesc,
		    UINT FirstSubresource,
		    UINT NumSubresources,
		    UINT64 BaseOffset,
		    D3D12_PLACED_SUBRESOURCE_FOOTPRINT* Layouts,
		    UINT* NumRows,
		    UINT64* RowSizeInBytes,
		    UINT64* TotalBytes) override
		{
			return mNative->GetCopyableFootprints(ResourceDesc,
			    FirstSubresource,
			    NumSubresources,
			    BaseOffset,
			    Layouts,
			    NumRows,
			    RowSizeInBytes,
			    TotalBytes);
		}

		HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC* Desc, REFIID Riid, void** vHeap) override
		{
			return mNative->CreateQueryHeap(Desc, Riid, vHeap);
		}

		HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL Enable) override
		{
			return mNative->SetStablePowerState(Enable);
		}

		HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC* Desc,
		    ID3D12RootSignature* RootSignature,
		    REFIID Riid,
		    void** vCommandSignature) override
		{
			return mNative->CreateCommandSignature(Desc, RootSignature, Riid, vCommandSignature);
		}

		void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource* TiledResource,
		    UINT* NumTilesForEntireResource,
		    D3D12_PACKED_MIP_INFO* PackedMipDesc,
		    D3D12_TILE_SHAPE* StandardTileShapeForNonPackedMips,
		    UINT* NumSubresourceTilings,
		    UINT FirstSubresourceTilingToGet,
		    D3D12_SUBRESOURCE_TILING* SubresourceTilingsForNonPackedMips) override
		{
			return mNative->GetResourceTiling(TiledResource,
			    NumTilesForEntireResource,
			    PackedMipDesc,
			    StandardTileShapeForNonPackedMips,
			    NumSubresourceTilings,
			    FirstSubresourceTilingToGet,
			    SubresourceTilingsForNonPackedMips);
		}

		LUID STDMETHODCALLTYPE GetAdapterLuid() override
		{
			return mNative->GetAdapterLuid();
		}

	private:
		Microsoft::WRL::ComPtr<ID3D12Device> mNative;
		std::atomic<ULONG> mReferences{1};
	};
}
