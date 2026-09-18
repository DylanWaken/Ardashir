#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Shaders/ArdaRHIBindingLayout.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include "RHI/Resources/ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	namespace
	{
		template <typename T>
		void Combine(size_t& Seed, const T& Value) noexcept
		{
			ArdaHashCombine(Seed, Value);
		}

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}
	}

	size_t HashValue(const FArdaRHIBindingLayoutDesc& V) noexcept
	{
		size_t H = 0;
		Combine(H, static_cast<uint16_t>(V.mVisibility));
		Combine(H, V.mRegisterSpace);
		Combine(H, V.mbRegisterSpaceIsDescriptorSet);
		Combine(H, V.mItems.size());
		for (const auto& I : V.mItems)
		{
			Combine(H, I.mSlot);
			Combine(H, I.mArraySize);
			Combine(H, static_cast<uint8_t>(I.mType));
		}
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIBindingLayoutDesc& V) noexcept
	{
		if (V.mVisibility == EArdaRHIShaderStage::None || V.mItems.empty())
		{
			return Invalid("Binding layout visibility and items are required.");
		}
		for (size_t I = 0; I < V.mItems.size(); ++I)
		{
			if (V.mItems[I].mArraySize == 0 || V.mItems[I].mArraySize > 65535)
			{
				return Invalid("Binding array size must be between 1 and 65535.");
			}
			for (size_t J = I + 1; J < V.mItems.size(); ++J)
			{
				if (V.mItems[I].mSlot == V.mItems[J].mSlot && V.mItems[I].mType == V.mItems[J].mType)
				{
					return Invalid("Binding layout contains a duplicate slot and type.");
				}
			}
		}
		return {};
	}
}
