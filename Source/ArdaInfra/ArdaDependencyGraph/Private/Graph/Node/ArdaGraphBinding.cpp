#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphRevisionState.h"
#include "Graph/Node/ArdaGraphRecordingScope.h"
#include "Graph/Node/ArdaGraphClone.h"
#include "Graph/Node/ArdaGraphConstructionKey.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include "Graph/Node/ArdaGraphExternalValues.h"
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		template <class T>
		const void* ExternalStorageIdentity(const T& Resource)
		{
			if (!Resource)
			{
				return nullptr;
			}
			const auto Identity = Resource->GetPhysicalIdentity();
			return Identity ? Identity : Resource.Get();
		}
	}

	eastl::vector<FArdaGraphExternalValueSlot> FArdaDependencyGraph::GetExternalValueSlots(bool bOnlyUnbound) const
	{
		eastl::vector<FArdaGraphExternalValueSlot> Slots;
		for (const auto& Entry : GetReadImpl().mExternalValues)
		{
			if (!bOnlyUnbound || !Entry.second.mValue)
			{
				Slots.push_back({Entry.first, Entry.second.mTypeName, bool(Entry.second.mValue)});
			}
		}
		eastl::sort(Slots.begin(),
		    Slots.end(),
		    [](const auto& Left, const auto& Right)
		    {
			    return Left.mName < Right.mName;
		    });
		return Slots;
	}

	FArdaRHIStatus FArdaDependencyGraph::ApplyExternalValueBinding(FArdaGraphExternalValueBinding Binding)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbPreprocessing || mImpl->mbExecuting)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Bind external slots only on a closed graph outside preprocessing, compilation and execution.");
		}
		if (Binding.mName.empty())
		{
			return GraphInvalid("External value slots require an explicit nonempty name.");
		}
		const auto Found = mImpl->mExternalValues.find(Binding.mName);
		if (Found != mImpl->mExternalValues.end())
		{
			if (Binding.mTypeName.empty())
			{
				Binding.mTypeName = Found->second.mTypeName;
			}
			if (Binding.mTypeName != Found->second.mTypeName ||
			    (Found->second.mType && Binding.mType != Found->second.mType))
			{
				return GraphInvalid("External value binding does not match its declared type.");
			}
			if (!Binding.mValue)
			{
				return {}; // Redeclaring an existing slot retains its binding.
			}
		}
		if (Binding.mTypeName.empty())
		{
			return GraphInvalid("New external value slots require a portable type name.");
		}
		if (Binding.mIdentity)
		{
			for (const auto& Entry : mImpl->mExternalValues)
			{
				if (Entry.first != Binding.mName && Entry.second.mIdentity == Binding.mIdentity &&
				    Entry.second.mType == Binding.mType)
				{
					return GraphInvalid(
					    "The same external reference cannot have two slot names for one parameter type.");
				}
			}
		}
		for (const auto& Entry : mRevisions->mCache)
		{
			CancelArdaInductorTimingOptimization(*Entry.mGraph);
			if (Entry.mGraph->mCompile.mExecution->mRuntime)
			{
				if (auto Status = Entry.mGraph->mCompile.mExecution->mRuntime->WaitAll(); !Status)
				{
					return Status;
				}
			}
		}
		auto Bound = CloneArdaGraphConstruction(*mImpl);
		Bound->mCompilerSource.reset();
		const auto Captured = CaptureArdaGraphExternalParameters(*Bound);
		if (Found != mImpl->mExternalValues.end() && Found->second.mValue && !Captured)
		{
			return Captured;
		}
		Bound->mExternalValues[Binding.mName] = eastl::move(Binding);
		if (auto Status = RebindArdaGraphExternalParameters(*Bound); !Status)
		{
			return Status;
		}
		mRevisions->mCache.clear();
		mRevisions->mEditKeys.assign(1, BuildArdaGraphConstructionKey(*Bound));
		mRevisions->mbBuilding = true;
		mRevisions->mbLocked = false;
		mImpl = eastl::move(Bound);
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::ApplyExternalResourceBinding(FArdaDependencyResourceDesc Resource)
	{
		if (auto Status = CheckArdaGraphRecordingReentry(mRevisions.get()); !Status)
		{
			return Status;
		}
		std::lock_guard<std::recursive_mutex> Lock(mRevisions->mMutex);
		if (IsEditing() || mImpl->mbCompiling || mImpl->mbPreprocessing || mImpl->mbExecuting)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Bind external slots only on a closed graph outside preprocessing, compilation and execution.");
		}
		const auto Found = mImpl->mResourceNames.find(Resource.mName);
		if (Found == mImpl->mResourceNames.end() || !mImpl->mResources[Found->second].IsExternal())
		{
			return GraphInvalid("External binding must name an existing external-resource slot.");
		}
		for (size_t Index = 0; Index < mImpl->mResources.size(); ++Index)
		{
			if (Index == Found->second)
			{
				continue;
			}
			const auto& Other = mImpl->mResources[Index];
			if ((Resource.mExternalBuffer &&
			        ExternalStorageIdentity(Other.mExternalBuffer) ==
			            ExternalStorageIdentity(Resource.mExternalBuffer)) ||
			    (Resource.mExternalTexture &&
			        ExternalStorageIdentity(Other.mExternalTexture) ==
			            ExternalStorageIdentity(Resource.mExternalTexture)) ||
			    (Resource.mExternalAccelerationStructure &&
			        ExternalStorageIdentity(Other.mExternalAccelerationStructure) ==
			            ExternalStorageIdentity(Resource.mExternalAccelerationStructure)))
			{
				return GraphInvalid("The same native resource cannot bind two different graph slots.");
			}
		}
		for (const auto& Entry : mRevisions->mCache)
		{
			CancelArdaInductorTimingOptimization(*Entry.mGraph);
			if (Entry.mGraph->mCompile.mExecution->mRuntime)
			{
				if (auto Status = Entry.mGraph->mCompile.mExecution->mRuntime->WaitAll(); !Status)
				{
					return Status;
				}
			}
		}
		auto Bound = CloneArdaGraphConstruction(*mImpl);
		Bound->mCompilerSource.reset();
		Bound->mResources[Found->second] = eastl::move(Resource);
		Bound->mImportedBuffers.clear();
		Bound->mImportedTextures.clear();
		Bound->mImportedAccelerationStructures.clear();
		for (const auto& Value : Bound->mResources)
		{
			if (Value.mExternalBuffer)
			{
				Bound->mImportedBuffers.insert(ExternalStorageIdentity(Value.mExternalBuffer));
			}
			if (Value.mExternalTexture)
			{
				Bound->mImportedTextures.insert(ExternalStorageIdentity(Value.mExternalTexture));
			}
			if (Value.mExternalAccelerationStructure)
			{
				Bound->mImportedAccelerationStructures.insert(
				    ExternalStorageIdentity(Value.mExternalAccelerationStructure));
			}
		}
		mRevisions->mCache.clear();
		mRevisions->mEditKeys.assign(1, BuildArdaGraphConstructionKey(*Bound));
		mRevisions->mbBuilding = true;
		mRevisions->mbLocked = false;
		mImpl = eastl::move(Bound);
		return {};
	}
}
