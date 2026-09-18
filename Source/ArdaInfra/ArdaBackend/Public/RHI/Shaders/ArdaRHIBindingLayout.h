/** @file ArdaRHIBindingLayout.h
 * Declares BindingLayout definitions for the RHI shaders module.
 */

#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIResource.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <EASTL/array.h>
#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Enumerates bindless layout type values. */
	enum class EArdaRHIBindlessLayoutType : uint8_t
	{
		Immutable,
		MutableSrvUavCbv,
		MutableCounters,
		MutableSampler
	};

	/** Enumerates binding type values. */
	enum class EArdaRHIBindingType : uint8_t
	{
		TextureSRV,
		TextureUAV,
		TypedBufferSRV,
		TypedBufferUAV,
		StructuredBufferSRV,
		StructuredBufferUAV,
		RawBufferSRV,
		RawBufferUAV,
		ConstantBuffer,
		VolatileConstantBuffer,
		Sampler,
		PushConstants,
		RayTracingAccelStruct,
		SamplerFeedbackTextureUAV
	};

	/** Describes binding layout item. */
	struct FArdaRHIBindingLayoutItem
	{
		/** Shader register slot. */
		uint32_t mSlot = 0;
		/** Number of array descriptors, or byte size for PushConstants. */
		uint32_t mArraySize = 1;
		/** Stores the type. */
		EArdaRHIBindingType mType = EArdaRHIBindingType::TextureSRV;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIBindingLayoutItem& O) const noexcept
		{
			return mSlot == O.mSlot && mArraySize == O.mArraySize && mType == O.mType;
		}
	};

	/** Describes binding layout desc. */
	struct FArdaRHIBindingLayoutDesc
	{
		/** Stores the visibility. */
		EArdaRHIShaderStage mVisibility = EArdaRHIShaderStage::None;
		/** Stores the register space. */
		uint32_t mRegisterSpace = 0;
		/** Stores the register space is descriptor set. */
		bool mbRegisterSpaceIsDescriptorSet = false;
		/** Stores the items. */
		eastl::vector<FArdaRHIBindingLayoutItem> mItems;
		/** Stores the debug name. */
		eastl::string mDebugName;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIBindingLayoutDesc& O) const noexcept
		{
			return mVisibility == O.mVisibility && mRegisterSpace == O.mRegisterSpace &&
			    mbRegisterSpaceIsDescriptorSet == O.mbRegisterSpaceIsDescriptorSet && mItems == O.mItems;
		}
	};

	/**
     * Tests for the requested h value.
     * @param Value The value.
     * @return The requested numeric value.
     */
	[[nodiscard]] size_t HashValue(const FArdaRHIBindingLayoutDesc& Value) noexcept;

	/**
     * Validates the descriptor.
     * @param Value The value.
     * @return A status describing whether the operation succeeded.
     */
	[[nodiscard]] FArdaRHIStatus Validate(const FArdaRHIBindingLayoutDesc& Value) noexcept;

	struct FArdaRHIBindlessLayoutDesc;

	/** Interface for binding layout. */
	class IArdaRHIBindingLayout : public virtual IArdaRHIResource
	{
	public:
		/**
         * Returns the desc.
         * @return A reference to the requested value.
         */
		[[nodiscard]] virtual const FArdaRHIBindingLayoutDesc& GetDesc() const noexcept = 0;

		/** Returns immutable bindless semantics, or null for a fixed layout. */
		[[nodiscard]] virtual const FArdaRHIBindlessLayoutDesc* GetBindlessDesc() const noexcept
		{
			return nullptr;
		}
	};

	/** Describes bindless layout desc. */
	struct FArdaRHIBindlessLayoutDesc
	{
		/** Stores the visibility. */
		EArdaRHIShaderStage mVisibility = EArdaRHIShaderStage::None;
		/** Stores the first slot. */
		uint32_t mFirstSlot = 0;
		/** Native descriptor-set/register-space selected for this table. */
		uint32_t mRegisterSpace = 0;
		/** Stores the max capacity. */
		uint32_t mMaxCapacity = 0;
		/** Zero-capacity layouts request the backend's maximum runtime array. */
		bool mbUnbounded = false;
		/** Descriptors may be changed after a table has been bound. */
		bool mbUpdateAfterBind = false;
		/** The last native binding uses the table's actual descriptor count. */
		bool mbVariableDescriptorCount = false;
		/** Shaders directly index the native resource/sampler heap. */
		bool mbDirectHeapIndexing = false;
		/** Uses native descriptor-buffer storage instead of descriptor sets. */
		bool mbDescriptorBuffer = false;
		/** Stores the layout type. */
		EArdaRHIBindlessLayoutType mLayoutType = EArdaRHIBindlessLayoutType::Immutable;
		/** Stores the register spaces. */
		eastl::vector<FArdaRHIBindingLayoutItem> mRegisterSpaces;
		/** Stores the debug name. */
		eastl::string mDebugName;

		/** Shared semantic projection for interning and PSO keys; excludes the diagnostic label. */
		eastl::array<uint64_t, 10> GetSemanticValues() const noexcept
		{
			return {{static_cast<uint64_t>(mVisibility),
			    mFirstSlot,
			    mRegisterSpace,
			    mMaxCapacity,
			    mbUnbounded,
			    mbUpdateAfterBind,
			    mbVariableDescriptorCount,
			    mbDirectHeapIndexing,
			    mbDescriptorBuffer,
			    static_cast<uint64_t>(mLayoutType)}};
		}

		/** Compares declarations exactly, independently of native handles and hash collisions. */
		bool operator==(const FArdaRHIBindlessLayoutDesc& Other) const noexcept
		{
			return GetSemanticValues() == Other.GetSemanticValues() && mRegisterSpaces == Other.mRegisterSpaces;
		}

		/** Visits fixed-width semantic fields and ordered bank declarations for a cache encoder. */
		template <class Visitor>
		void VisitSemantics(Visitor&& Visit) const
		{
			for (uint64_t Value : GetSemanticValues())
			{
				Visit(Value);
			}

			Visit(static_cast<uint64_t>(mRegisterSpaces.size()));
			for (const auto& Item : mRegisterSpaces)
			{
				Visit(static_cast<uint64_t>(Item.mSlot));
				Visit(static_cast<uint64_t>(Item.mArraySize));
				Visit(static_cast<uint64_t>(Item.mType));
			}
		}
	};
}
