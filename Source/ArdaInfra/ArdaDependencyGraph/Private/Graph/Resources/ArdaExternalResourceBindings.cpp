#include "Graph/Node/ArdaDependencyGraph.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphArchive.h"
#include "Graph/Resources/ArdaGraphResourceValidation.h"

namespace arda
{
	namespace
	{
		FArdaRHIStatus ExternalBindingError(const eastl::string& Name, const char* Reason)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    ("External resource '" + Name + "': " + Reason).c_str());
		}
	}

	eastl::vector<FArdaDependencyResourceDesc> FArdaDependencyGraph::GetExternalResourceSlots(bool bOnlyUnbound) const
	{
		eastl::vector<FArdaDependencyResourceDesc> Slots;
		for (const auto& Resource : GetReadImpl().mResources)
		{
			if (Resource.IsExternal() && (!bOnlyUnbound || !Resource.HasExternalBinding()))
			{
				Slots.push_back(Resource);
			}
		}
		return Slots;
	}

	FArdaRHIStatus FArdaDependencyGraph::BindExternalResource(const eastl::string& Name, FArdaRHIBufferRef Buffer)
	{
		const auto& Graph = GetReadImpl();
		const auto Found = Graph.mResourceNames.find(Name);
		if (Name.empty() || Found == Graph.mResourceNames.end() || !Buffer)
		{
			return ExternalBindingError(Name, "a declared slot and non-null buffer are required.");
		}
		auto Slot = Graph.mResources[Found->second];
		auto Shape = Buffer->GetDesc();
		Shape.mDebugName = Slot.mBuffer.mDebugName;
		if (Slot.mExternalKind != EArdaGraphExternalResourceKind::Buffer || !(Shape == Slot.mBuffer))
		{
			return ExternalBindingError(Name, "buffer kind or descriptor does not match the declared slot.");
		}
		Slot.mExternalBuffer = eastl::move(Buffer);
		return ApplyExternalResourceBinding(eastl::move(Slot));
	}

	FArdaRHIStatus FArdaDependencyGraph::BindExternalResource(const eastl::string& Name, FArdaRHITextureRef Texture)
	{
		const auto& Graph = GetReadImpl();
		const auto Found = Graph.mResourceNames.find(Name);
		if (Name.empty() || Found == Graph.mResourceNames.end() || !Texture)
		{
			return ExternalBindingError(Name, "a declared slot and non-null texture are required.");
		}
		auto Slot = Graph.mResources[Found->second];
		auto Shape = Texture->GetDesc();
		Shape.mDebugName = Slot.mTexture.mDebugName;
		if (Slot.mExternalKind != EArdaGraphExternalResourceKind::Texture || !(Shape == Slot.mTexture))
		{
			return ExternalBindingError(Name, "texture kind or descriptor does not match the declared slot.");
		}
		Slot.mExternalTexture = eastl::move(Texture);
		return ApplyExternalResourceBinding(eastl::move(Slot));
	}

	FArdaRHIStatus FArdaDependencyGraph::BindExternalResource(const eastl::string& Name,
	    FArdaRHIAccelStructRef Structure)
	{
		const auto& Graph = GetReadImpl();
		const auto Found = Graph.mResourceNames.find(Name);
		if (Name.empty() || Found == Graph.mResourceNames.end() || !Structure)
		{
			return ExternalBindingError(Name, "a declared slot and non-null acceleration structure are required.");
		}
		auto Slot = Graph.mResources[Found->second];
		auto Shape = ArdaGraphAccelerationStructureShape(Structure->GetDesc());
		Shape.mDebugName = Slot.mAccelerationStructure.mDebugName;
		FArdaGraphArchiveWriter Expected;
		FArdaGraphArchiveWriter Actual;
		Expected.Write(Slot.mAccelerationStructure);
		Actual.Write(Shape);
		if (Slot.mExternalKind != EArdaGraphExternalResourceKind::AccelerationStructure || !Expected.GetStatus() ||
		    !Actual.GetStatus() || Expected.GetData() != Actual.GetData())
		{
			return ExternalBindingError(Name, "acceleration-structure kind or shape does not match the declared slot.");
		}
		Slot.mExternalAccelerationStructure = eastl::move(Structure);
		return ApplyExternalResourceBinding(eastl::move(Slot));
	}
}
