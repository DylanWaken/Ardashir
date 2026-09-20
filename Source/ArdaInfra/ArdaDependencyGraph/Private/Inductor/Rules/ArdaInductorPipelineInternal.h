#pragma once
#include "Inductor/Compiler/ArdaInductorPipelineCompiler.h"
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>

namespace arda::pipeline_detail
{
	inline FArdaRHIStatus Invalid(const char* Message)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
	}

	struct FArdaPipelineKeyWriter
	{
		eastl::vector<uint8_t> mBytes;
		bool mbValid = true;

		template <typename T>
		void Add(T Value)
		{
			const uint64_t Bits = static_cast<uint64_t>(Value);
			for (uint32_t Shift = 0; Shift < 64; Shift += 8)
			{
				mBytes.push_back(static_cast<uint8_t>(Bits >> Shift));
			}
		}

		void String(const eastl::string& Value)
		{
			Add(Value.size());
			mBytes.insert(mBytes.end(), Value.begin(), Value.end());
		}

		void Shader(const FArdaRHIShaderRef& Value)
		{
			Add(bool(Value));
			if (Value)
			{
				Add(Value->GetStage());
				Add(Value->GetPersistentCacheHash());
				mbValid &= Value->GetPersistentCacheHash() != 0;
			}
		}

		void Items(const eastl::vector<FArdaRHIBindingLayoutItem>& Values)
		{
			Add(Values.size());
			for (const auto& Value : Values)
			{
				Add(Value.mSlot);
				Add(Value.mArraySize);
				Add(Value.mType);
			}
		}

		void Layout(const FArdaRHIBindingLayoutRef& Value)
		{
			Add(bool(Value));
			if (!Value)
			{
				return;
			}
			const auto& Desc = Value->GetDesc();
			Add(Desc.mVisibility);
			Add(Desc.mRegisterSpace);
			Add(Desc.mbRegisterSpaceIsDescriptorSet);
			Items(Desc.mItems);
			const auto* Bindless = Value->GetBindlessDesc();
			Add(Bindless != nullptr);
			if (Bindless)
			{
				Bindless->VisitSemantics(
				    [this](uint64_t Value)
				    {
					    Add(Value);
				    });
			}
		}

		void Layouts(const eastl::vector<FArdaRHIBindingLayoutRef>& Values)
		{
			Add(Values.size());
			for (const auto& Value : Values)
			{
				Layout(Value);
			}
		}

		void Input(const FArdaRHIInputLayoutRef& Value)
		{
			Add(bool(Value));
			if (!Value)
			{
				return;
			}
			Add(Value->GetDesc().mAttributes.size());
			for (const auto& Attribute : Value->GetDesc().mAttributes)
			{
				String(Attribute.mSemanticName);
				Add(Attribute.mFormat);
				Add(Attribute.mArraySize);
				Add(Attribute.mBufferIndex);
				Add(Attribute.mOffset);
				Add(Attribute.mElementStride);
				Add(Attribute.mbInstanced);
			}
		}

		template <typename T>
		void Raster(const T& Desc)
		{
			Add(Desc.mTopology);
			Add(Desc.mBlendState.mbAlphaToCoverage);
			for (const auto& Target : Desc.mBlendState.mTargets)
			{
				Add(Target.mbEnable);
				Add(Target.mSourceColor);
				Add(Target.mDestinationColor);
				Add(Target.mSourceAlpha);
				Add(Target.mDestinationAlpha);
			}
			Add(Desc.mRasterState.mFillMode);
			Add(Desc.mRasterState.mCullMode);
			Add(Desc.mRasterState.mbFrontCounterClockwise);
			Add(Desc.mRasterState.mbDepthClip);
			Add(Desc.mRasterState.mbScissor);
			Add(Desc.mDepthStencilState.mbDepthTest);
			Add(Desc.mDepthStencilState.mbDepthWrite);
			Add(Desc.mDepthStencilState.mDepthFunc);
			Add(Desc.mColorFormats.size());
			for (const auto Format : Desc.mColorFormats)
			{
				Add(Format);
			}
			Add(Desc.mDepthFormat);
			Add(Desc.mSampleCount);
		}

		uint64_t Finish() const
		{
			uint64_t Hash = 14695981039346656037ull;
			for (uint8_t Byte : mBytes)
			{
				Hash = (Hash ^ Byte) * 1099511628211ull;
			}
			return Hash ? Hash : 1;
		}
	};

	FArdaPipelineKeyWriter ConfigurationKey(const FArdaInductorPipelineConfiguration&, bool bStages = true);
	bool SameShader(const FArdaRHIShaderRef&, const FArdaRHIShaderRef&);
	bool SameLayout(const FArdaRHIBindingLayoutRef&, const FArdaRHIBindingLayoutRef&);
	bool Matches(EArdaPipelineStateKind Kind, EArdaRHIShaderStage Stage);
	eastl::vector<FArdaRHIBindingLayoutRef>& GlobalLayouts(FArdaInductorPipelineConfiguration&);
	FArdaRHIStatus MergeLayouts(eastl::vector<FArdaRHIBindingLayoutRef>&,
	    const eastl::vector<FArdaRHIBindingLayoutRef>&);
	FArdaRHIStatus MergeLocalLayout(FArdaRHIBindingLayoutRef&, const FArdaRHIBindingLayoutRef&);
	FArdaRHIStatus AddShader(FArdaInductorPipelineConfiguration&, const FArdaInductorPipelineContribution&);
	FArdaRHIStatus MergeConfiguration(FArdaInductorPipelineConfiguration&,
	    bool& bHaveSettings,
	    const FArdaInductorPipelineConfiguration&);
	FArdaRHIStatus ValidatePattern(FArdaInductorPipelineConfiguration&);
}
