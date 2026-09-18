#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Resources/ArdaRHIFormat.h"
#include "RHI/Shaders/ArdaRHIInputLayout.h"

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

		void CombineString(size_t& Seed, const eastl::string& Value) noexcept
		{
			ArdaHashString(Seed, Value);
		}

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}
	}

	size_t HashValue(const FArdaRHIVertexAttributeDesc& V) noexcept
	{
		size_t H = 0;
		CombineString(H, V.mSemanticName);
		Combine(H, static_cast<uint8_t>(V.mFormat));
		Combine(H, V.mArraySize);
		Combine(H, V.mBufferIndex);
		Combine(H, V.mOffset);
		Combine(H, V.mElementStride);
		Combine(H, V.mbInstanced);
		return H;
	}

	size_t HashValue(const FArdaRHIInputLayoutDesc& V) noexcept
	{
		size_t H = 0;
		Combine(H, V.mAttributes.size());
		for (const auto& A : V.mAttributes)
		{
			Combine(H, HashValue(A));
		}
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIVertexAttributeDesc& V) noexcept
	{
		if (V.mSemanticName.empty() || !IsArdaRHIFormatKnown(V.mFormat) || V.mArraySize == 0 || V.mElementStride == 0)
		{
			return Invalid("Vertex attributes require a semantic, format, array size, and stride.");
		}
		const uint32_t ElementSize = GetArdaRHIFormatElementSize(V.mFormat);
		const uint32_t Alignment = GetArdaRHIVertexFormatAlignment(V.mFormat);
		if (!Alignment)
		{
			return Invalid("Vertex attributes require an uncompressed color format.");
		}
		if (V.mOffset % Alignment || V.mElementStride % Alignment)
		{
			return Invalid("Vertex attribute offset and stride must align to the format component size.");
		}
		const uint64_t ByteSize = uint64_t(ElementSize) * V.mArraySize;
		if (V.mOffset > V.mElementStride || ByteSize > V.mElementStride - V.mOffset)
		{
			return Invalid("Vertex attribute elements must fit within their declared stride.");
		}
		return {};
	}

	FArdaRHIStatus Validate(const FArdaRHIInputLayoutDesc& V)
	{
		if (V.mAttributes.empty())
		{
			return Invalid("Input layout requires attributes.");
		}
		for (const auto& A : V.mAttributes)
		{
			if (auto S = Validate(A); !S)
			{
				return S;
			}
			for (const auto& B : V.mAttributes)
			{
				if (A.mBufferIndex == B.mBufferIndex &&
				    (A.mElementStride != B.mElementStride || A.mbInstanced != B.mbInstanced))
				{
					return Invalid("Attributes sharing a vertex binding must agree on stride and instance rate.");
				}
			}
		}
		return {};
	}
}
