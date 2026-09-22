/** Frozen logical shader bindings, bindless tables and shader-table declarations. */
#pragma once

#include "Graph/Resources/ArdaDependencyResource.h"
#include "RHI/Resources/ArdaRHISampler.h"
#include "RHI/Shaders/ArdaShaderParameters.h"
#include "RHI/Shaders/ArdaRHIShaderTable.h"

namespace arda
{
	/** Logical argument for a named shader-struct leaf. Register information comes from its metadata. */
	struct FArdaDependencyShaderResource
	{
		eastl::string mMember;
		FArdaDependencyResourceHandle mResource;
		uint32_t mArrayElement = 0;
		/** Inherits the shader schema by default. Explicit access must cover its requirements. */
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::AutoDeduction;
		FArdaRHIViewDesc mView;
		/** Samplers have no graph allocation or data dependency. */
		FArdaRHISamplerRef mSampler;
	};

	/** Writes a populated table entry's index into a uint32_t field of frozen shader parameter bytes. */
	struct FArdaDependencyDescriptorIndex
	{
		eastl::string mTable;
		uint32_t mSlot = 0;
		uint32_t mArrayElement = 0;
		EArdaRHIBindingType mType = EArdaRHIBindingType::StructuredBufferSRV;
		uint32_t mByteOffset = 0;
		/** Native heap address, supported only for direct-heap tables with one descriptor bank. */
		bool mbAbsoluteHeapIndex = false;
	};

	/** Bytes copied from node parameters at attachment; named push-constant leaf in the shader schema. */
	struct FArdaDependencyShaderValue
	{
		eastl::string mMember;
		eastl::vector<uint8_t> mBytes;
		eastl::vector<FArdaDependencyDescriptorIndex> mDescriptorIndices;
	};

	/** Frozen shader schema and logical arguments for executor-owned binding preparation. */
	struct FArdaDependencyShaderBindings
	{
		const FArdaShaderParameterMetadata* mMetadata = nullptr;
		eastl::string mPipelineSlot = "default";
		eastl::vector<FArdaDependencyShaderResource> mResources;
		eastl::vector<FArdaDependencyShaderValue> mValues;
	};

	/** A populated descriptor and its complete possible data access. Indices do not establish hazards. */
	struct FArdaDependencyBindlessEntry
	{
		uint32_t mSlot = 0;
		uint32_t mArrayElement = 0;
		EArdaRHIBindingType mType = EArdaRHIBindingType::StructuredBufferSRV;
		FArdaDependencyResourceHandle mResource;
		FArdaRHIViewDesc mView;
		EArdaDependencyAccess mAccess = EArdaDependencyAccess::Read;
		FArdaRHISamplerRef mSampler;
	};

	/** Immutable node-local table declaration. The graph creates its layout and per-frame descriptor tables.
     * List every entry the shader might access, including GPU-selected indices. Different nodes may declare
     * different entries of the same layout. Capacity is bounded; holes must never be indexed by the shader.
     */
	struct FArdaDependencyBindlessTable
	{
		eastl::string mName;
		eastl::string mPipelineSlot = "default";
		FArdaRHIBindlessLayoutDesc mLayout;
		/** Zero selects one past the greatest supplied index. Must fit mLayout.mMaxCapacity. */
		uint32_t mCapacity = 0;
		eastl::vector<FArdaDependencyBindlessEntry> mEntries;
	};

	/** Declarative ray record; local shader resources are resolved against the export's local layout. */
	struct FArdaDependencyShaderTableRecord
	{
		EArdaRHIShaderTableRecordType mType = EArdaRHIShaderTableRecordType::RayGeneration;
		/** Dense zero-based index within mType, not the native table's flat record index. */
		uint32_t mRecordIndex = 0;
		eastl::string mExportName;
		FArdaDependencyShaderBindings mLocalBindings;
		eastl::vector<uint8_t> mLocalArguments;
		uint32_t mUserData = 0;
		FArdaDependencyResourceHandle mGeometry;
		uint32_t mGeometrySegment = 0;
	};

	/** Optional explicit record order. Without a declaration, a ray slot receives its unique raygen,
     * then every miss/hit-group/callable in the resolved pipeline's deterministic export order.
     */
	struct FArdaDependencyShaderTable
	{
		eastl::string mPipelineSlot = "default";
		eastl::vector<FArdaDependencyShaderTableRecord> mRecords;
	};
}
