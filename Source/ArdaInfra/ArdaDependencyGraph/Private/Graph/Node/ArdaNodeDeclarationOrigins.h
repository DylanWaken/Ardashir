#pragma once

#include "Graph/Node/ArdaDependencyNodeDesc.h"

namespace arda
{
	inline void StampArdaNodeDeclarationOrigins(FArdaDependencyNodeDesc& Desc, FArdaGraphNodeHandle Node)
	{
		const auto Stamp = [Node](auto& Values)
		{
			for (uint32_t Index = 0; Index < Values.size(); ++Index)
			{
				Values[Index].mDeclaration = {Node, Index};
			}
		};
		Stamp(Desc.mShaderBindings);
		Stamp(Desc.mBindlessTables);
		Stamp(Desc.mShaderTables);
		Stamp(Desc.mPipelineStages);
		Stamp(Desc.mPipelines);
		for (uint32_t Table = 0; Table < Desc.mShaderTables.size(); ++Table)
		{
			for (uint32_t Record = 0; Record < Desc.mShaderTables[Table].mRecords.size(); ++Record)
			{
				Desc.mShaderTables[Table].mRecords[Record].mLocalBindings.mDeclaration = {Node, Record, Table};
			}
		}
	}
}
