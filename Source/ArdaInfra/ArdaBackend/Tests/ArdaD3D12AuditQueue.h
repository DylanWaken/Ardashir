#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>

namespace arda
{
	/** Forwards real queue work while allowing tests to fail completion signals persistently. */
	class FArdaD3D12AuditQueue final : public ID3D12CommandQueue
	{
	public:
		explicit FArdaD3D12AuditQueue(ID3D12CommandQueue* Native)
		    : mNative(Native)
		{
		}

		std::atomic<bool> mbFailSignal{false};
		std::atomic<uint32_t> mSignalCalls{0};

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Interface, void** Object) override
		{
			if (!Object)
			{
				return E_POINTER;
			}
			if (Interface == __uuidof(IUnknown) || Interface == __uuidof(ID3D12Object) ||
			    Interface == __uuidof(ID3D12DeviceChild) || Interface == __uuidof(ID3D12Pageable) ||
			    Interface == __uuidof(ID3D12CommandQueue))
			{
				*Object = this;
				AddRef();
				return S_OK;
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

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID Interface, void** Device) override
		{
			return mNative->GetDevice(Interface, Device);
		}

		void STDMETHODCALLTYPE UpdateTileMappings(ID3D12Resource* Resource,
		    UINT RegionCount,
		    const D3D12_TILED_RESOURCE_COORDINATE* Coordinates,
		    const D3D12_TILE_REGION_SIZE* Sizes,
		    ID3D12Heap* Heap,
		    UINT RangeCount,
		    const D3D12_TILE_RANGE_FLAGS* RangeFlags,
		    const UINT* HeapOffsets,
		    const UINT* TileCounts,
		    D3D12_TILE_MAPPING_FLAGS Flags) override
		{
			mNative->UpdateTileMappings(Resource,
			    RegionCount,
			    Coordinates,
			    Sizes,
			    Heap,
			    RangeCount,
			    RangeFlags,
			    HeapOffsets,
			    TileCounts,
			    Flags);
		}

		void STDMETHODCALLTYPE CopyTileMappings(ID3D12Resource* Destination,
		    const D3D12_TILED_RESOURCE_COORDINATE* DestinationCoordinate,
		    ID3D12Resource* Source,
		    const D3D12_TILED_RESOURCE_COORDINATE* SourceCoordinate,
		    const D3D12_TILE_REGION_SIZE* Size,
		    D3D12_TILE_MAPPING_FLAGS Flags) override
		{
			mNative->CopyTileMappings(Destination, DestinationCoordinate, Source, SourceCoordinate, Size, Flags);
		}

		void STDMETHODCALLTYPE ExecuteCommandLists(UINT Count, ID3D12CommandList* const* Lists) override
		{
			mNative->ExecuteCommandLists(Count, Lists);
		}

		void STDMETHODCALLTYPE SetMarker(UINT Metadata, const void* Data, UINT Size) override
		{
			mNative->SetMarker(Metadata, Data, Size);
		}

		void STDMETHODCALLTYPE BeginEvent(UINT Metadata, const void* Data, UINT Size) override
		{
			mNative->BeginEvent(Metadata, Data, Size);
		}

		void STDMETHODCALLTYPE EndEvent() override
		{
			mNative->EndEvent();
		}

		HRESULT STDMETHODCALLTYPE Signal(ID3D12Fence* Fence, UINT64 Value) override
		{
			mSignalCalls.fetch_add(1, std::memory_order_relaxed);
			return mbFailSignal.load(std::memory_order_relaxed) ? E_OUTOFMEMORY : mNative->Signal(Fence, Value);
		}

		HRESULT STDMETHODCALLTYPE Wait(ID3D12Fence* Fence, UINT64 Value) override
		{
			return mNative->Wait(Fence, Value);
		}

		HRESULT STDMETHODCALLTYPE GetTimestampFrequency(UINT64* Frequency) override
		{
			return mNative->GetTimestampFrequency(Frequency);
		}

		HRESULT STDMETHODCALLTYPE GetClockCalibration(UINT64* GpuTimestamp, UINT64* CpuTimestamp) override
		{
			return mNative->GetClockCalibration(GpuTimestamp, CpuTimestamp);
		}

		D3D12_COMMAND_QUEUE_DESC STDMETHODCALLTYPE GetDesc() override
		{
			return mNative->GetDesc();
		}

	private:
		std::atomic<ULONG> mReferences{1};
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> mNative;
	};
}
