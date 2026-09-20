#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Rules/ArdaInductorBindingArguments.h"
#include "Inductor/Runtime/ArdaInductorBindings.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	FArdaRHIStatus PrepareRayTable(const FArdaDependencyGraph::FArdaImpl& G,
	    FArdaInductorFrame& Frame,
	    uint32_t NodeId,
	    const FArdaDependencyNode& Node,
	    const eastl::string& Slot,
	    const FArdaRHIRayTracingPipelineRef& Pipeline)
	{
		eastl::vector<FArdaDependencyShaderTableRecord> Records;
		const auto T = eastl::find_if(Node.mDesc.mShaderTables.begin(),
		    Node.mDesc.mShaderTables.end(),
		    [&](const auto& Table)
		    {
			    return Table.mPipelineSlot == Slot;
		    });
		const auto& PD = Pipeline->GetDesc();
		if (T != Node.mDesc.mShaderTables.end())
		{
			Records = T->mRecords;
		}
		else
		{
			uint32_t Counts[4]{};
			for (const auto& S : PD.mShaders)
			{
				EArdaRHIShaderTableRecordType Type;
				switch (S.mShader->GetStage())
				{
				case EArdaRHIShaderStage::RayGeneration:
					Type = EArdaRHIShaderTableRecordType::RayGeneration;
					break;
				case EArdaRHIShaderStage::Miss:
					Type = EArdaRHIShaderTableRecordType::Miss;
					break;
				case EArdaRHIShaderStage::Callable:
					Type = EArdaRHIShaderTableRecordType::Callable;
					break;
				default:
					continue;
				}
				Records.push_back({Type, Counts[static_cast<uint32_t>(Type)]++, S.mExportName});
			}
			for (const auto& H : PD.mHitGroups)
			{
				Records.push_back({EArdaRHIShaderTableRecordType::HitGroup, Counts[2]++, H.mExportName});
			}
		}
		uint32_t Counts[4]{};
		FArdaRHIShaderTableDesc Desc;
		Desc.mbPersistent = true;
		Desc.mDebugName = Node.mName + "/" + Slot;
		Desc.mMaxEntries = static_cast<uint32_t>(Records.size());
		for (const auto& R : Records)
		{
			const auto Type = static_cast<uint32_t>(R.mType);
			if (Type >= 4 || R.mExportName.empty() || R.mLocalArguments.size() > UINT32_MAX)
			{
				return BindingError("Invalid shader table record.");
			}
			++Counts[Type];
			Desc.mMaxLocalArgumentBytes =
			    eastl::max(Desc.mMaxLocalArgumentBytes, static_cast<uint32_t>(R.mLocalArguments.size()));
		}
		if (Counts[0] != 1)
		{
			return BindingError(
			    "A shader table requires exactly one ray-generation record; declare an explicit selection.");
		}
		for (size_t I = 0; I < Records.size(); ++I)
		{
			const auto& R = Records[I];
			if (R.mRecordIndex >= Counts[static_cast<uint32_t>(R.mType)])
			{
				return BindingError("Shader table indices must be dense within each record category.");
			}
			for (size_t J = 0; J < I; ++J)
			{
				if (R.mType == Records[J].mType && R.mRecordIndex == Records[J].mRecordIndex)
				{
					return BindingError("Duplicate shader table record index.");
				}
			}
		}
		auto Created = G.mDevice->CreateShaderTable(Pipeline, Desc);
		if (!Created)
		{
			return Created.mStatus;
		}
		for (const auto& R : Records)
		{
			FArdaRHIShaderTableRecordDesc Native;
			Native.mType = R.mType;
			Native.mRecordIndex = R.mRecordIndex;
			Native.mExportName = R.mExportName;
			// Public node indices are category-relative; the RHI stores all categories in one record array.
			for (uint32_t Type = 0; Type < static_cast<uint32_t>(R.mType); ++Type)
			{
				Native.mRecordIndex += Counts[Type];
			}
			Native.mLocalArguments = R.mLocalArguments;
			Native.mUserData = R.mUserData;
			Native.mGeometrySegment = R.mGeometrySegment;
			if (R.mGeometry)
			{
				Native.mGeometry = Frame.mMemory.mAccelerationStructures[R.mGeometry.mIndex];
			}
			FArdaRHIBindingLayoutRef Local;
			bool Found = false;
			if (R.mType == EArdaRHIShaderTableRecordType::HitGroup)
			{
				for (const auto& H : PD.mHitGroups)
				{
					if (H.mExportName == R.mExportName)
					{
						Found = true;
						Local = H.mLocalBindingLayout;
					}
				}
			}
			else
			{
				const auto Stage = R.mType == EArdaRHIShaderTableRecordType::RayGeneration
				    ? EArdaRHIShaderStage::RayGeneration
				    : R.mType == EArdaRHIShaderTableRecordType::Miss ? EArdaRHIShaderStage::Miss
				                                                     : EArdaRHIShaderStage::Callable;
				for (const auto& S : PD.mShaders)
				{
					if (S.mExportName == R.mExportName && S.mShader->GetStage() == Stage)
					{
						Found = true;
						Local = S.mLocalBindingLayout;
					}
				}
			}
			if (!Found)
			{
				return BindingError("Shader table record names a missing export or the wrong shader stage.");
			}
			if (R.mLocalBindings.mMetadata)
			{
				if (!MatchesSchema(R.mLocalBindings, Local))
				{
					return BindingError("Ray local shader schema does not match its export's layout.");
				}
				auto Set = G.mDevice->CreateBindingSet(MakeSet(G, Frame, R.mLocalBindings, Local));
				if (!Set)
				{
					return Set.mStatus;
				}
				Native.mBindings = eastl::move(Set.mValue);
			}
			else if (Local && !Local->GetDesc().mItems.empty())
			{
				return BindingError("Shader table export needs declared local binding parameters.");
			}
			if (auto S = G.mDevice->SetShaderTableRecord(Created.mValue, Native); !S)
			{
				return S;
			}
		}
		if (auto S = G.mDevice->CommitShaderTable(Created.mValue); !S)
		{
			return S;
		}
		Frame.mNodeBindings[NodeId].mShaderTables[Slot] = eastl::move(Created.mValue);
		return {};
	}
}
