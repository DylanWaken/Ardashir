#pragma once

#include "CUDA/ArdaNativeCudaProvider.h"
#include "Graph/Node/ArdaRuntimeNodeDefinition.h"

namespace arda::graph_detail
{
	inline void ReadLegacyKernelArguments(FArdaGraphArchiveReader& Archive, FArdaRuntimeNodeDesc& Desc)
	{
		const auto Count = Archive.ReadCount();
		for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
		{
			FArdaRuntimeNodeArgumentDesc Argument;
			uint8_t Kind = 0;
			uint32_t Offset = 0, Size = 0, Alignment = 0;
			Archive.Fields(Argument.mName, Kind, Argument.mAccess, Offset, Size, Alignment);
			if (Kind > 1)
			{
				Archive.Fail("Legacy runtime arguments support scalar values and buffers.");
			}
			Desc.mArguments.push_back(eastl::move(Argument));
			Desc.mLayout.mArguments.push_back(
			    {Offset, Size, Alignment, Kind ? EArdaNativeArgumentKind::Buffer : EArdaNativeArgumentKind::Value});
		}
	}

	inline FArdaRHIStatus ImportLegacyRuntimeKernel(FArdaRuntimeNodeDesc& Desc,
	    const FArdaCudaModuleDesc& Module,
	    const eastl::string& EntryPoint)
	{
		FArdaCudaModuleKernelDesc Kernel;
		Kernel.mEntryPoint = EntryPoint;
		Kernel.mParameterSize = Desc.mLayout.mParameterSize;
		Kernel.mParameterAlignment = Desc.mLayout.mParameterAlignment;
		for (const auto& Layout : Desc.mLayout.mArguments)
		{
			const auto Kind = Layout.mKind == EArdaNativeArgumentKind::Buffer ? EArdaCudaModuleArgumentKind::Buffer
			                                                                  : EArdaCudaModuleArgumentKind::Value;
			Kernel.mArguments.push_back({Layout.mOffset, Layout.mSize, Layout.mAlignment, Kind});
		}
		auto Imported = ImportArdaLegacyCudaKernel(Module, Kernel);
		if (Imported)
		{
			Desc.mKernel = eastl::move(Imported.mValue);
		}
		return Imported.mStatus;
	}
}
