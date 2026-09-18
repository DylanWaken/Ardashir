/** @file ArdaRHIShaderTable.h
 * Declares ShaderTable definitions for the RHI shaders module.
 */

#pragma once

#include "RHI/Resources/ArdaRHIAccelerationStructures.h"
#include "RHI/Resources/ArdaRHIRef.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Shaders/ArdaRHIBindingSet.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

namespace arda
{
	/** Describes shader table desc. */
	struct FArdaRHIShaderTableDesc
	{
		/** Stores the cached. */
		bool mbCached = false;
		/** Stores the max entries. */
		uint32_t mMaxEntries = 0;
		/** Maximum bytes copied after the native shader identifier in a record. */
		uint32_t mMaxLocalArgumentBytes = 0;
		/** Table contents persist until explicitly replaced. */
		bool mbPersistent = false;
		/** Stores the debug name. */
		eastl::string mDebugName;
	};

	/** Category of a shader-binding-table record. */
	enum class EArdaRHIShaderTableRecordType : uint8_t
	{
		RayGeneration,
		Miss,
		HitGroup,
		Callable
	};

	/** Complete portable shader-binding-table record. */
	struct FArdaRHIShaderTableRecordDesc
	{
		EArdaRHIShaderTableRecordType mType = EArdaRHIShaderTableRecordType::RayGeneration;
		uint32_t mRecordIndex = 0;
		eastl::string mExportName;
		FArdaRHIBindingSetRef mBindings;
		eastl::vector<uint8_t> mLocalArguments;
		uint32_t mUserData = 0;
		FArdaRHIAccelStructRef mGeometry;
		uint32_t mGeometrySegment = 0;
	};

	/** Interface for shader table. */
	class IArdaRHIShaderTable : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIShaderTableDesc& GetDesc() const noexcept = 0;

		/**
         * Returns the entry count.
         * @return The requested numeric value.
         */
		[[nodiscard]] virtual uint32_t GetEntryCount() const noexcept = 0;
	};
}
