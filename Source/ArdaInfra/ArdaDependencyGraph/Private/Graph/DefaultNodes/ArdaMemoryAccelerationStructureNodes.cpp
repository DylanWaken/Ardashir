#include "Graph/DefaultNodes/ArdaMemoryAccelerationStructureNodes.h"

#include "Graph/Node/ArdaDependencyKey.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaMemoryCompactAccelerationStructureNode, CompactAccelerationStructure);
	ARDA_DEFINE_GRAPH_NODE(FArdaMemoryCopyAccelerationStructureNode, CopyAccelerationStructure);

	namespace
	{
		FArdaRHIStatus ValidateAccelerationStructures(FArdaDependencyResourceContext& Context,
		    FArdaDependencyResourceHandle SourceHandle,
		    FArdaDependencyResourceHandle DestinationHandle)
		{
			const auto* Source = Context.Find(SourceHandle);
			const auto* Destination = Context.Find(DestinationHandle);
			if (!Source || !Destination ||
			    Source->mExternalKind != EArdaGraphExternalResourceKind::AccelerationStructure ||
			    Destination->mExternalKind != EArdaGraphExternalResourceKind::AccelerationStructure ||
			    SourceHandle == DestinationHandle)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Acceleration-structure memory operations require two distinct imported BLAS or TLAS objects.");
			}
			const auto& SourceDesc = Source->mAccelerationStructure;
			const auto& DestinationDesc = Destination->mAccelerationStructure;
			if (SourceDesc.mbTopLevel != DestinationDesc.mbTopLevel ||
			    SourceDesc.mBuildFlags != DestinationDesc.mBuildFlags)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Acceleration-structure memory operations require matching BLAS/TLAS kinds and build flags.");
			}
			return {};
		}

	}

	FArdaDependencyNodeMetadata FArdaMemoryCopyAccelerationStructureNode::GetMetadata()
	{
		return {"arda.memory.copy-acceleration-structure", 1};
	}

	FArdaDependencyNodeRequirements FArdaMemoryCopyAccelerationStructureNode::GetRequirements(const FArdaParameters&)
	{
		FArdaDependencyNodeRequirements Requirements;
		Requirements.mFeatures.mbRequireAccelerationStructures = true;
		return Requirements;
	}

	FArdaRHIStatus FArdaMemoryCopyAccelerationStructureNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		return ValidateAccelerationStructures(Context, Parameters.mSource, Parameters.mDestination);
	}

	eastl::string FArdaMemoryCopyAccelerationStructureNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder().Resource(Parameters.mSource).Resource(Parameters.mDestination).Build();
	}

	FArdaRHIStatus FArdaMemoryCopyAccelerationStructureNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.GetCommands().CopyAccelStruct(*Context.GetAccelerationStructure(Parameters.mDestination),
		    *Context.GetAccelerationStructure(Parameters.mSource));
	}

	FArdaDependencyNodeMetadata FArdaMemoryCompactAccelerationStructureNode::GetMetadata()
	{
		return {"arda.memory.compact-acceleration-structure", 1};
	}

	FArdaDependencyNodeRequirements FArdaMemoryCompactAccelerationStructureNode::GetRequirements(const FArdaParameters&)
	{
		FArdaDependencyNodeRequirements Requirements;
		Requirements.mFeatures.mbRequireAccelerationStructures = true;
		Requirements.mFeatures.mbRequireAccelerationStructureCompaction = true;
		return Requirements;
	}

	FArdaRHIStatus FArdaMemoryCompactAccelerationStructureNode::DeclareResources(
	    FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		if (auto Status = ValidateAccelerationStructures(Context, Parameters.mSource, Parameters.mDestination); !Status)
		{
			return Status;
		}
		const auto& Source = *Context.Find(Parameters.mSource);
		const auto& Destination = *Context.Find(Parameters.mDestination);
		if (!HasAnyFlags(Source.mAccelerationStructure.mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowCompaction) ||
		    !Destination.mAccelerationStructure.mResultSizeOverride)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Compaction requires AllowCompaction and a destination with an explicit compact result size.");
		}
		if (Source.mExternalAccelerationStructure &&
		    Source.mExternalAccelerationStructure->GetBuildState() == EArdaRHIAccelStructBuildState::Unbuilt)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Compaction requires a previously submitted source build and completed-size query.");
		}
		return {};
	}

	eastl::string FArdaMemoryCompactAccelerationStructureNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder().Resource(Parameters.mSource).Resource(Parameters.mDestination).Build();
	}

	FArdaRHIStatus FArdaMemoryCompactAccelerationStructureNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		const auto Source = Context.GetAccelerationStructure(Parameters.mSource);
		const auto Destination = Context.GetAccelerationStructure(Parameters.mDestination);
		const auto Size = Context.GetDevice()->GetAccelStructCompactedSize(Source);
		if (!Size)
		{
			return Size.mStatus;
		}
		if (Destination->GetDesc().mResultSizeOverride < Size.mValue)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Acceleration-structure compaction destination is smaller than the queried compact size.");
		}
		return Context.GetCommands().CompactAccelStruct(*Destination, *Source);
	}

	FArdaRHIStatus RegisterArdaMemoryAccelerationStructureNodes()
	{
		if (auto Status = FArdaMemoryCopyAccelerationStructureNode::Register(); !Status)
		{
			return Status;
		}
		return FArdaMemoryCompactAccelerationStructureNode::Register();
	}
}
