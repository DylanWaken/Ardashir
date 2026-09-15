#include "NodeLibrary/ArdaMemoryAccelerationStructureNodes.h"

#include "ArdaDependencyKey.h"

namespace arda
{
	namespace
	{
		FArdaRHIStatus ValidateAccelerationStructures(FArdaDependencyResourceContext& Context,
		    FArdaDependencyResourceHandle SourceHandle,
		    FArdaDependencyResourceHandle DestinationHandle)
		{
			const auto* Source = Context.Find(SourceHandle);
			const auto* Destination = Context.Find(DestinationHandle);
			if (!Source || !Destination || !Source->mExternalAccelerationStructure ||
			    !Destination->mExternalAccelerationStructure || SourceHandle == DestinationHandle)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Acceleration-structure memory operations require two distinct imported BLAS or TLAS objects.");
			}
			const auto& SourceDesc = Source->mExternalAccelerationStructure->GetDesc();
			const auto& DestinationDesc = Destination->mExternalAccelerationStructure->GetDesc();
			if (SourceDesc.mbTopLevel != DestinationDesc.mbTopLevel ||
			    SourceDesc.mBuildFlags != DestinationDesc.mBuildFlags)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Acceleration-structure memory operations require matching BLAS/TLAS kinds and build flags.");
			}
			return {};
		}

		FArdaDependencyNodeDesc DescribeAccelerationStructureCopy(FArdaDependencyResourceHandle Source,
		    FArdaDependencyResourceHandle Destination)
		{
			FArdaDependencyNodeDesc Description;
			Description.mAccesses = {{Source, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructRead},
			    {Destination, EArdaDependencyAccess::Write, EArdaRHIResourceState::AccelStructWrite}};
			return Description;
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

	FArdaDependencyNodeDesc FArdaMemoryCopyAccelerationStructureNode::Describe(const FArdaParameters& Parameters,
	    const FArdaState&)
	{
		return DescribeAccelerationStructureCopy(Parameters.mSource, Parameters.mDestination);
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
		const auto& Source = Context.Find(Parameters.mSource)->mExternalAccelerationStructure;
		const auto& Destination = Context.Find(Parameters.mDestination)->mExternalAccelerationStructure;
		if (!HasAnyFlags(Source->GetDesc().mBuildFlags, EArdaRHIAccelStructBuildFlags::AllowCompaction) ||
		    !Destination->GetDesc().mResultSizeOverride)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Compaction requires AllowCompaction and a destination with an explicit compact result size.");
		}
		if (Source->GetBuildState() == EArdaRHIAccelStructBuildState::Unbuilt)
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

	FArdaDependencyNodeDesc FArdaMemoryCompactAccelerationStructureNode::Describe(const FArdaParameters& Parameters,
	    const FArdaState&)
	{
		return DescribeAccelerationStructureCopy(Parameters.mSource, Parameters.mDestination);
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
