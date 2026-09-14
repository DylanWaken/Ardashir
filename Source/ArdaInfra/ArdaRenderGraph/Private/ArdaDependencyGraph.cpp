#include "ArdaDependencyGraphInternal.h"
#include "ArdaDependencyHazards.h"
#include <EASTL/sort.h>
#include <EASTL/unordered_set.h>
#include <atomic>
#include <cmath>
#include <string>

namespace arda
{
	FArdaRHIStatus InitializeArdaBuiltinNodes(FArdaNodeRegistry& Registry);

	namespace
	{
		FArdaRHIStatus Invalid(const char* Text)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Text);
		}

		FArdaRHIStatus NotEditing()
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Modify the dependency graph only between BeginGraphEdit and EndGraphEdit.");
		}
	}

	FArdaNodeRegistry& FArdaNodeRegistry::Get()
	{
		static FArdaNodeRegistry Registry;
		(void)InitializeArdaBuiltinNodes(Registry);
		return Registry;
	}

	FArdaRHIStatus FArdaNodeRegistry::RegisterExecutable(FArdaDependencyNodeExecutable D)
	{
		// The class base is the only producer of executable callbacks; validate authored metadata here.
		if (D.mName.empty() || !D.mVersion)
		{
			return Invalid("Node metadata requires a nonempty name and a positive implementation revision.");
		}

		std::lock_guard<std::mutex> Lock(mMutex);
		if (mDefinitions.find(D.mName) != mDefinitions.end())
		{
			return Invalid("A node definition with that name is already registered.");
		}
		const auto Name = D.mName;
		mDefinitions.emplace(Name,
		    eastl::shared_ptr<const FArdaDependencyNodeExecutable>(new FArdaDependencyNodeExecutable(eastl::move(D))));
		return {};
	}

	FArdaRHIStatus FArdaNodeRegistry::Unregister(const eastl::string& Name)
	{
		eastl::shared_ptr<const FArdaDependencyNodeExecutable> Retired;
		{
			std::lock_guard<std::mutex> Lock(mMutex);
			const auto Found = mDefinitions.find(Name);
			if (Found == mDefinitions.end())
			{
				return Invalid("No registered node definition has that name.");
			}
			Retired = eastl::move(Found->second);
			mDefinitions.erase(Found);
		}
		return {};
	}

	eastl::shared_ptr<const FArdaDependencyNodeExecutable> FArdaNodeRegistry::Find(const eastl::string& Name) const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		auto I = mDefinitions.find(Name);
		return I == mDefinitions.end() ? nullptr : I->second;
	}

	eastl::vector<eastl::string> FArdaNodeRegistry::GetNames() const
	{
		std::lock_guard<std::mutex> Lock(mMutex);
		eastl::vector<eastl::string> Names;
		for (const auto& D : mDefinitions)
		{
			Names.push_back(D.first);
		}
		eastl::sort(Names.begin(), Names.end());
		return Names;
	}

	FArdaDependencyGraph::FArdaDependencyGraph(FArdaRHIDeviceRef Device)
	    : mImpl(eastl::make_unique<FArdaImpl>())
	{
		static std::atomic<uint64_t> Next{1};
		mImpl->mIdentity = Next.fetch_add(1);
		mImpl->mDevice = eastl::move(Device);
		if (mImpl->mDevice)
		{
			mImpl->mPipelineCache = eastl::make_unique<FArdaPipelineStateCache>(mImpl->mDevice);
		}
	}

	FArdaDependencyGraph::~FArdaDependencyGraph()
	{
		CancelArdaInductorTimingOptimization(*mImpl);
	}

	bool FArdaDependencyGraph::IsEditing() const noexcept
	{
		return mImpl->mbEditing;
	}

	FArdaRHIDeviceRef FArdaDependencyGraph::GetDevice() const
	{
		return mImpl->mDevice;
	}

	const FArdaDependencyTopology& FArdaDependencyGraph::GetTopology() const
	{
		return mImpl->mTopology;
	}

	const FArdaInductorCompileResult& FArdaDependencyGraph::GetCompileResult() const
	{
		return mImpl->mCompile;
	}

	FArdaPipelineStateCacheStats FArdaDependencyGraph::GetPipelineCacheStats() const
	{
		return mImpl->mPipelineCache ? mImpl->mPipelineCache->GetStats() : FArdaPipelineStateCacheStats{};
	}

	FArdaRHIStatus FArdaDependencyGraph::BeginGraphEdit()
	{
		auto& G = *mImpl;
		if (G.mbEditing || G.mbExecuting)
		{
			return NotEditing();
		}
		CancelArdaInductorTimingOptimization(G);
		if (G.mRuntime)
		{
			if (auto S = G.mRuntime->WaitAll(); !S)
			{
				return S;
			}
		}
		G.mSavedTopology = eastl::make_unique<FArdaDependencyTopology>(G.mTopology.CloneSnapshot());
		G.mSavedResources = G.mResources;
		G.mSavedManualEdges = G.mManualEdges;
		G.mSavedOptions = G.mOptions;
		G.mSavedResourceGenerations = G.mResourceGenerations;
		G.mbEditing = true;
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::EndGraphEdit()
	{
		if (!mImpl->mbEditing)
		{
			return NotEditing();
		}
		auto S = FArdaInductor::Compile(*this);
		if (!S)
		{
			return S;
		}
		mImpl->mbEditing = false;
		mImpl->mSavedTopology.reset();
		mImpl->mSavedResources.clear();
		mImpl->mSavedManualEdges.clear();
		mImpl->mTiming->Clear();
		mImpl->mTiming->Configure(mImpl->mOptions.mGpuTimingEmaAlpha, mImpl->mOptions.mGpuTimingHistoryCapacity);
		mImpl->mAdaptiveSearch = {};
		mImpl->mAdaptiveStats = {};
		mImpl->mLastAdaptiveSequence = mImpl->mNextFrameSequence;
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::CancelGraphEdit()
	{
		auto& G = *mImpl;
		if (!G.mbEditing || !G.mSavedTopology)
		{
			return NotEditing();
		}
		// Native restoration may fail after the old pool was retired. Preserve the
		// rollback snapshot until the entire cancellation succeeds so another Cancel
		// can retry, even if the caller repairs or changes the current edit meanwhile.
		G.mTopology = G.mSavedTopology->CloneSnapshot();
		G.mResources = G.mSavedResources;
		G.mManualEdges = G.mSavedManualEdges;
		G.mOptions = G.mSavedOptions;
		G.mResourceGenerations = G.mSavedResourceGenerations;
		G.mNames.clear();
		for (auto H : G.mTopology.GetNodes())
		{
			G.mNames.emplace(G.mTopology.TryGetNode(H)->mPayload.mName, H);
		}
		G.mResourceNames.clear();
		for (uint32_t I = 0; I < G.mResources.size(); ++I)
		{
			G.mResourceNames.emplace(G.mResources[I].mName, I);
		}
		if (G.mDevice && !G.mRuntime && G.mCompile.mRevision)
		{
			const auto Revision = G.mCompile.mRevision;
			if (auto S = FArdaInductor::Compile(*this); !S)
			{
				return S;
			}
			G.mCompile.mRevision = Revision;
		}
		G.mSavedTopology.reset();
		G.mSavedResources.clear();
		G.mSavedManualEdges.clear();
		G.mSavedResourceGenerations.clear();
		G.mbEditing = false;
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::SetOptions(const FArdaInductorOptions& O)
	{
		if (!IsEditing())
		{
			return NotEditing();
		}
		if (!O.mMinimumAsyncChain || !O.mMaxSearchStates || !O.mFramesInFlight || !O.mGpuTimingSampleInterval ||
		    !O.mAdaptiveSchedulingInterval || !O.mAdaptiveSchedulingMinSamples || !O.mAdaptiveSchedulingSearchBudget)
		{
			return Invalid("Inductor chain, search and frame-slot limits must be positive.");
		}
		if (!std::isfinite(O.mGpuTimingEmaAlpha) || O.mGpuTimingEmaAlpha <= 0 || O.mGpuTimingEmaAlpha > 1 ||
		    !std::isfinite(O.mAdaptiveSchedulingMinImprovement) || O.mAdaptiveSchedulingMinImprovement < 0 ||
		    O.mAdaptiveSchedulingMinImprovement >= 1 || (O.mbEnableAdaptiveScheduling && !O.mbEnableGpuTiming))
		{
			return Invalid(
			    "Telemetry requires EMA alpha in (0,1]; adaptive scheduling requires telemetry and an improvement threshold in [0,1).");
		}
		if (O.mSearchMode > EArdaInductorSearchMode::Exhaustive || O.mObjective > EArdaInductorObjective::Memory ||
		    O.mCudaGraphMode > EArdaCudaGraphMode::Require)
		{
			return Invalid("Invalid Inductor scheduling or capture mode.");
		}
		mImpl->mOptions = O;
		return {};
	}

	TArdaRHIResult<FArdaDependencyResourceHandle> FArdaDependencyGraph::CreateResource(FArdaDependencyResourceDesc D)
	{
		auto& G = *mImpl;
		if (!G.mbEditing)
		{
			return {{}, NotEditing()};
		}
		if (D.mName.empty() || G.mResourceNames.find(D.mName) != G.mResourceNames.end())
		{
			return {{}, Invalid("Resource versions require unique nonempty names.")};
		}
		if ((D.mExternalBuffer && (D.mbTexture || D.mExternalTexture || D.mExternalAccelerationStructure)) ||
		    (D.mExternalAccelerationStructure && (D.mbTexture || D.mExternalTexture)))
		{
			return {{}, Invalid("External resource kind mismatch.")};
		}
		if (D.mExternalTexture && !D.mbTexture)
		{
			return {{}, Invalid("External texture kind mismatch.")};
		}
		if (D.mExternalBuffer)
		{
			D.mBuffer = D.mExternalBuffer->GetDesc();
		}
		if (D.mExternalTexture)
		{
			D.mTexture = D.mExternalTexture->GetDesc();
		}

		// One native allocation has one imported logical identity, so aliases cannot hide hazards.
		const auto PhysicalIdentity = [](const auto& Resource) -> const void*
		{
			if (!Resource)
			{
				return nullptr;
			}
			const void* Identity = Resource->GetPhysicalIdentity();
			return Identity ? Identity : Resource.Get();
		};
		for (const auto& Existing : G.mResources)
		{
			if ((D.mExternalBuffer &&
			        PhysicalIdentity(D.mExternalBuffer) == PhysicalIdentity(Existing.mExternalBuffer)) ||
			    (D.mExternalTexture &&
			        PhysicalIdentity(D.mExternalTexture) == PhysicalIdentity(Existing.mExternalTexture)) ||
			    (D.mExternalAccelerationStructure &&
			        PhysicalIdentity(D.mExternalAccelerationStructure) ==
			            PhysicalIdentity(Existing.mExternalAccelerationStructure)))
			{
				return {{},
				    Invalid(
				        "External storage is already imported. Reuse its logical handle so resource hazards cannot be hidden by another name.")};
			}
		}
		if (auto S = D.mExternalAccelerationStructure ? FArdaRHIStatus{}
		        : D.mbTexture                         ? Validate(D.mTexture)
		                                              : Validate(D.mBuffer);
		    !S)
		{
			return {{}, S};
		}
		if (G.mResources.size() >= UINT32_MAX)
		{
			return {{}, Invalid("Too many logical resources.")};
		}

		// Publish a fresh generation only after validating the descriptor and import identity.
		const uint32_t I = static_cast<uint32_t>(G.mResources.size());
		G.mResourceNames.emplace(D.mName, I);
		G.mResources.push_back(eastl::move(D));
		G.mResourceGenerations.push_back(G.mNextResourceGeneration++);
		return {{G.mIdentity, I, G.mResourceGenerations.back()}, {}};
	}

	TArdaRHIResult<FArdaDependencyResourceHandle> FArdaDependencyGraph::CreateBuffer(eastl::string Name,
	    FArdaRHIBufferDesc Desc)
	{
		FArdaDependencyResourceDesc D;
		D.mName = eastl::move(Name);
		D.mBuffer = eastl::move(Desc);
		return CreateResource(eastl::move(D));
	}

	TArdaRHIResult<FArdaDependencyResourceHandle> FArdaDependencyGraph::CreateTexture(eastl::string Name,
	    FArdaRHITextureDesc Desc)
	{
		FArdaDependencyResourceDesc D;
		D.mName = eastl::move(Name);
		D.mbTexture = true;
		D.mTexture = eastl::move(Desc);
		return CreateResource(eastl::move(D));
	}

	TArdaRHIResult<FArdaDependencyResourceHandle> FArdaDependencyGraph::ImportBuffer(eastl::string Name,
	    FArdaRHIBufferRef Buffer)
	{
		if (!Buffer)
		{
			return {{}, Invalid("Cannot import a null buffer.")};
		}
		FArdaDependencyResourceDesc D;
		D.mName = eastl::move(Name);
		D.mExternalBuffer = eastl::move(Buffer);
		return CreateResource(eastl::move(D));
	}

	TArdaRHIResult<FArdaDependencyResourceHandle> FArdaDependencyGraph::ImportTexture(eastl::string Name,
	    FArdaRHITextureRef Texture)
	{
		if (!Texture)
		{
			return {{}, Invalid("Cannot import a null texture.")};
		}
		FArdaDependencyResourceDesc D;
		D.mName = eastl::move(Name);
		D.mbTexture = true;
		D.mExternalTexture = eastl::move(Texture);
		return CreateResource(eastl::move(D));
	}

	TArdaRHIResult<FArdaDependencyResourceHandle> FArdaDependencyGraph::ImportAccelerationStructure(eastl::string Name,
	    FArdaRHIAccelStructRef AccelerationStructure)
	{
		if (!AccelerationStructure)
		{
			return {{}, Invalid("Cannot import a null acceleration structure.")};
		}
		FArdaDependencyResourceDesc Desc;
		Desc.mName = eastl::move(Name);
		Desc.mExternalAccelerationStructure = eastl::move(AccelerationStructure);
		return CreateResource(eastl::move(Desc));
	}

	FArdaRHIStatus FArdaDependencyGraph::MarkOutput(FArdaDependencyResourceHandle H, bool Output)
	{
		if (!IsEditing())
		{
			return NotEditing();
		}
		if (!mImpl->HasResource(H))
		{
			return Invalid("Unknown resource version.");
		}
		mImpl->mResources[H.mIndex].mbOutput = Output;
		return {};
	}

	const FArdaDependencyResourceDesc* FArdaDependencyGraph::FindResource(FArdaDependencyResourceHandle H) const
	{
		return mImpl->HasResource(H) ? &mImpl->mResources[H.mIndex] : nullptr;
	}

	FArdaGraphNodeHandle FArdaDependencyGraph::FindNode(const eastl::string& Name) const
	{
		auto I = mImpl->mNames.find(Name);
		return I == mImpl->mNames.end() ? FArdaGraphNodeHandle{} : I->second;
	}

	FArdaDependencyResourceHandle FArdaDependencyGraph::FindOutput(FArdaGraphNodeHandle Node,
	    const eastl::string& Name) const
	{
		const auto* N = mImpl->mTopology.TryGetNode(Node);
		if (N)
		{
			for (const auto& O : N->mPayload.mOutputs)
			{
				if (O.mName == Name)
				{
					return O.mResource;
				}
			}
		}
		return {};
	}

	FArdaDependencyResourceContext::FArdaDependencyResourceContext(FArdaDependencyGraph& Graph,
	    eastl::string Name,
	    eastl::vector<FArdaDependencyNodeOutput> Existing)
	    : mGraph(Graph),
	      mName(eastl::move(Name)),
	      mExisting(eastl::move(Existing))
	{
	}

	const FArdaDependencyResourceDesc* FArdaDependencyResourceContext::Find(FArdaDependencyResourceHandle Input) const
	{
		return mGraph.FindResource(Input);
	}

	FArdaRHIStatus FArdaDependencyResourceContext::Buffer(FArdaDependencyResourceHandle& Output,
	    eastl::string Name,
	    FArdaRHIBufferDesc Desc)
	{
		FArdaDependencyResourceDesc D;
		D.mBuffer = eastl::move(Desc);
		return Declare(Output, eastl::move(Name), eastl::move(D));
	}

	FArdaRHIStatus FArdaDependencyResourceContext::Texture(FArdaDependencyResourceHandle& Output,
	    eastl::string Name,
	    FArdaRHITextureDesc Desc)
	{
		FArdaDependencyResourceDesc D;
		D.mbTexture = true;
		D.mTexture = eastl::move(Desc);
		return Declare(Output, eastl::move(Name), eastl::move(D));
	}

	FArdaRHIStatus FArdaDependencyResourceContext::Declare(FArdaDependencyResourceHandle& Output,
	    eastl::string Name,
	    FArdaDependencyResourceDesc D)
	{
		if (Name.empty() ||
		    eastl::any_of(mOutputs.begin(),
		        mOutputs.end(),
		        [&](const auto& O)
		        {
			        return O.mName == Name;
		        }))
		{
			return Invalid("Node output names must be nonempty and unique.");
		}
		if (auto S = D.mbTexture ? Validate(D.mTexture) : Validate(D.mBuffer); !S)
		{
			return S;
		}
		if (!Output)
		{
			for (const auto& O : mExisting)
			{
				if (O.mName == Name)
				{
					Output = O.mResource;
					break;
				}
			}
		}
		if (Output)
		{
			const auto* E = Find(Output);
			if (!E || E->mbTexture != D.mbTexture || E->mExternalAccelerationStructure)
			{
				return Invalid("Supplied node output has an invalid resource kind or identity.");
			}
			if (D.mbTexture)
			{
				const auto& A = E->mTexture;
				const auto& B = D.mTexture;
				if (A.mDimension != B.mDimension || A.mWidth != B.mWidth || A.mHeight != B.mHeight ||
				    A.mDepth != B.mDepth || A.mArraySize != B.mArraySize || A.mMipLevels != B.mMipLevels ||
				    A.mFormat != B.mFormat || A.mSampleCount != B.mSampleCount || (A.mUsage & B.mUsage) != B.mUsage ||
				    (B.mbCudaInterop && !A.mbCudaInterop))
				{
					return Invalid("Supplied texture does not satisfy node output requirements.");
				}
			}
			else
			{
				const auto& A = E->mBuffer;
				const auto& B = D.mBuffer;
				if (A.mByteSize < B.mByteSize || (B.mStructureStride && A.mStructureStride != B.mStructureStride) ||
				    (B.mFormat != EArdaRHIFormat::Unknown && A.mFormat != B.mFormat) ||
				    (A.mUsage & B.mUsage) != B.mUsage || (B.mbCudaInterop && !A.mbCudaInterop))
				{
					return Invalid("Supplied buffer does not satisfy node output requirements.");
				}
			}
		}
		else
		{
			D.mName = "Node output " + mName + "/" + Name + "/" +
			    std::to_string(mGraph.mImpl->mNextResourceGeneration).c_str();
			auto Created = mGraph.CreateResource(eastl::move(D));
			if (!Created)
			{
				return Created.mStatus;
			}
			Output = Created.mValue;
		}
		mOutputs.push_back({eastl::move(Name), Output});
		return {};
	}

	TArdaRHIResult<FArdaGraphNodeHandle> FArdaDependencyGraph::AttachErased(eastl::string Name,
	    const eastl::string& Definition,
	    const void* Type,
	    eastl::shared_ptr<const void> Parameters)
	{
		auto& G = *mImpl;
		if (!G.mbEditing)
		{
			return {{}, NotEditing()};
		}
		auto D = FArdaNodeRegistry::Get().Find(Definition);
		if (Name.empty() || !D || D->mParameterType != Type || !Parameters)
		{
			return {{}, Invalid("Attach requires a known definition and matching typed parameters.")};
		}
		const auto AdmissionError = [&](FArdaRHIStatus Status)
		{
			Status.mMessage = "Node '" + Name + "' (" + Definition + "): " + Status.mMessage;
			return TArdaRHIResult<FArdaGraphNodeHandle>{{}, eastl::move(Status)};
		};
		// Admit explicit requirements before invoking any resource or device setup hook.
		auto Requirements = D->mGetRequirements(Parameters.get());
		// Device-less graphs support semantic analysis, but explicit requirements never assume support.
		if (G.mDevice && D->mKind == EArdaDependencyNodeKind::Cuda)
		{
			Requirements.mbRequireCuda = true;
		}
		if (auto S = Requirements.Check(G.mDevice.Get()); !S)
		{
			return AdmissionError(eastl::move(S));
		}

		// Roll back only allocations made by this attachment; never reuse abandoned generations.
		struct FArdaResourceRollback
		{
			FArdaImpl& G;
			size_t Count;
			bool mbCommitted = false;

			~FArdaResourceRollback()
			{
				if (mbCommitted)
				{
					return;
				}
				while (G.mResources.size() > Count)
				{
					G.mResourceNames.erase(G.mResources.back().mName);
					G.mResources.pop_back();
					G.mResourceGenerations.pop_back();
				}
			}
		} Rollback{G, G.mResources.size()};

		// Normalize logical outputs before comparing the existing instance's canonical identity.
		const auto ExistingHandle = FindNode(Name);
		FArdaDependencyResourceContext Resources(*this,
		    Name,
		    ExistingHandle ? G.mTopology.TryGetNode(ExistingHandle)->mPayload.mOutputs
		                   : eastl::vector<FArdaDependencyNodeOutput>{});
		{
			auto Resolved = D->mResolveResources(Resources, Parameters);
			if (!Resolved)
			{
				return {{}, eastl::move(Resolved.mStatus)};
			}
			if (!Resolved.mValue)
			{
				return {{}, Invalid("Resource declaration returned null parameters.")};
			}
			Parameters = eastl::move(Resolved.mValue);
		}
		auto Key = D->mCanonicalKey(Parameters.get());
		if (auto Existing = FindNode(Name))
		{
			const auto& N = G.mTopology.TryGetNode(Existing)->mPayload;
			if (N.mDefinition != D || N.mCanonicalKey != Key)
			{
				return {{},
				    Invalid(
				        "AttachOrFind found a different node under that name; remove it before replacing its semantics.")};
			}
			if (Resources.mOutputs.size() != N.mOutputs.size())
			{
				return {{}, Invalid("Node output declaration changed under an existing name.")};
			}
			for (size_t I = 0; I < N.mOutputs.size(); ++I)
			{
				if (Resources.mOutputs[I].mName != N.mOutputs[I].mName ||
				    Resources.mOutputs[I].mResource != N.mOutputs[I].mResource)
				{
					return {{}, Invalid("Node output declaration changed under an existing name.")};
				}
			}
			if (G.mDevice)
			{
				if (auto S = N.mDesc.mRequirements.Check(G.mDevice.Get()); !S)
				{
					return AdmissionError(eastl::move(S));
				}
			}
			return {Existing, {}};
		}

		// Prepare a new instance only after identity checks; retain its state for every replay.
		{
			auto Prepared = D->mPrepare(G.mDevice, Parameters);
			if (!Prepared)
			{
				return {{}, eastl::move(Prepared.mStatus)};
			}
			if (!Prepared.mValue)
			{
				return {{}, Invalid("Node preparation returned no retained execution parameters.")};
			}
			Parameters = eastl::move(Prepared.mValue);
		}

		// Infer descriptor requirements before automatic binding setup can create native objects.
		auto Desc = D->mDescribe(Parameters.get());
		Desc.mRequirements = eastl::move(Requirements);
		InferArdaNodeRequirements(G, D->mKind, Desc);
		if (G.mDevice)
		{
			if (auto S = Desc.mRequirements.Check(G.mDevice.Get()); !S)
			{
				return AdmissionError(eastl::move(S));
			}
		}
		if (auto S = DescribeArdaShaderBindings(G, Desc); !S)
		{
			return {{}, eastl::move(S)};
		}

		// Freeze pipeline values even when the author retains a mutable shared pointer.
		for (auto& Stage : Desc.mPipelineStages)
		{
			if (Stage.mConfiguration)
			{
				Stage.mConfiguration =
				    eastl::make_shared<const FArdaInductorPipelineConfiguration>(*Stage.mConfiguration);
			}
		}
		for (auto& Pipeline : Desc.mPipelines)
		{
			if (Pipeline.mConfiguration)
			{
				Pipeline.mConfiguration =
				    eastl::make_shared<const FArdaInductorPipelineConfiguration>(*Pipeline.mConfiguration);
			}
		}

		// Validate graph-visible effects before committing the node and its provisional outputs.
		if (!Desc.mEstimatedCost)
		{
			return {{}, Invalid("Node estimated cost must be positive.")};
		}
		for (const auto& A : Desc.mAccesses)
		{
			if (!G.HasResource(A.mResource))
			{
				return {{}, Invalid("Node parameters contain a foreign or stale resource version.")};
			}
		}
		for (const auto& Output : Resources.mOutputs)
		{
			if (!eastl::any_of(Desc.mAccesses.begin(),
			        Desc.mAccesses.end(),
			        [&](const auto& A)
			        {
				        return A.mResource == Output.mResource && A.mAccess != EArdaDependencyAccess::Read;
			        }))
			{
				return {{}, Invalid("A declared node output must have a write access.")};
			}
		}
		if (G.mNextAttachmentOrder == UINT64_MAX)
		{
			return {{}, Invalid("Node attachment order exhausted.")};
		}
		auto H = G.mTopology.AddNode({Name,
		    eastl::move(Key),
		    eastl::move(D),
		    eastl::move(Parameters),
		    eastl::move(Desc),
		    eastl::move(Resources.mOutputs),
		    G.mNextAttachmentOrder});
		if (!H)
		{
			return {{}, Invalid("Cannot allocate another graph node.")};
		}
		++G.mNextAttachmentOrder;
		G.mNames.emplace(eastl::move(Name), H);
		Rollback.mbCommitted = true;
		return {H, {}};
	}

	FArdaRHIStatus FArdaDependencyGraph::AddDependency(FArdaGraphNodeHandle P, FArdaGraphNodeHandle C)
	{
		auto& G = *mImpl;
		if (!G.mbEditing)
		{
			return NotEditing();
		}
		if (P == C || !G.mTopology.ContainsNode(P) || !G.mTopology.ContainsNode(C))
		{
			return Invalid("Manual dependencies require distinct nodes from this graph.");
		}
		for (const auto& E : G.mManualEdges)
		{
			if (E.first == P && E.second == C)
			{
				return {};
			}
		}
		G.mManualEdges.push_back({P, C});
		(void)G.mTopology.AddEdge(P, C, {});
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::RemoveDependency(FArdaGraphNodeHandle P, FArdaGraphNodeHandle C)
	{
		auto& G = *mImpl;
		if (!G.mbEditing)
		{
			return NotEditing();
		}
		auto I = eastl::find(G.mManualEdges.begin(), G.mManualEdges.end(), eastl::make_pair(P, C));
		if (I == G.mManualEdges.end())
		{
			return Invalid("No explicit dependency connects these nodes.");
		}
		G.mManualEdges.erase(I);
		if (auto E = G.mTopology.FindEdge(P, C))
		{
			auto* R = G.mTopology.TryGetEdge(E);
			if (!R->mPayload.mbResource && !R->mPayload.mbHazard)
			{
				G.mTopology.RemoveEdge(E);
			}
		}
		return {};
	}

	FArdaRHIStatus FArdaDependencyGraph::RemoveNode(FArdaGraphNodeHandle H)
	{
		auto& G = *mImpl;
		if (!G.mbEditing)
		{
			return NotEditing();
		}
		if (!G.mTopology.ContainsNode(H))
		{
			return Invalid("Unknown node.");
		}
		// Resolve the current edit, including uncompiled nodes, before removing any writer.
		// Overwrites and preceding readers constrain execution but do not consume the removed value.
		auto Dependencies = G.mTopology.CloneSnapshot();
		const auto Edges = Dependencies.GetEdges();
		for (auto Edge : Edges)
		{
			Dependencies.RemoveEdge(Edge);
		}
		(void)ResolveArdaDependencyResourceEdges(G, Dependencies, false);
		eastl::vector<FArdaGraphNodeHandle> Removed{H};
		eastl::unordered_set<uint32_t> Resources;
		eastl::unordered_set<uint32_t> RemovedIndices{H.GetIndex()};
		for (size_t Index = 0; Index < Removed.size(); ++Index)
		{
			for (const auto& Access : G.mTopology.TryGetNode(Removed[Index])->mPayload.mDesc.mAccesses)
			{
				if (Access.mAccess != EArdaDependencyAccess::Read && G.HasResource(Access.mResource))
				{
					Resources.insert(Access.mResource.mIndex);
				}
			}
			for (const auto Edge : Dependencies.GetOutgoingEdges(Removed[Index]))
			{
				const auto& Dependency = *Dependencies.TryGetEdge(Edge);
				if (Dependency.mPayload.mbResource && RemovedIndices.insert(Dependency.mTo.GetIndex()).second)
				{
					Removed.push_back(Dependency.mTo);
				}
			}
		}
		for (auto N : Removed)
		{
			G.mNames.erase(G.mTopology.TryGetNode(N)->mPayload.mName);
			G.mTopology.RemoveNode(N);
		}
		G.mManualEdges.erase(eastl::remove_if(G.mManualEdges.begin(),
		                         G.mManualEdges.end(),
		                         [&](auto E)
		                         {
			                         return !G.mTopology.ContainsNode(E.first) || !G.mTopology.ContainsNode(E.second);
		                         }),
		    G.mManualEdges.end());
		for (auto R : Resources)
		{
			// Preserve output roots still produced by an independent surviving range writer.
			bool Produced = false;
			for (auto N : G.mTopology.GetNodes())
			{
				for (const auto& A : G.mTopology.TryGetNode(N)->mPayload.mDesc.mAccesses)
				{
					Produced |= A.mResource.mIndex == R && A.mAccess != EArdaDependencyAccess::Read;
				}
			}
			if (!Produced)
			{
				G.mResources[R].mbOutput = false;
			}
		}
		return {};
	}

	FArdaGraphExecutionResult FArdaDependencyGraph::Execute(const FArdaGraphExecuteOptions& Options)
	{
		if (IsEditing() || !mImpl->mCompile.mRevision || !mImpl->mRuntime)
		{
			FArdaGraphExecutionResult R;
			R.mStatus = FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Execute requires a successful EndGraphEdit and a device-bound compiled plan.");
			return R;
		}
		return FArdaInductorRuntime::Execute(*mImpl, Options);
	}

	TArdaRHIResult<FArdaDependencyFrameTicket> FArdaDependencyGraph::Submit(const FArdaGraphExecuteOptions& Options)
	{
		return FArdaInductorRuntime::Submit(*mImpl, Options);
	}

	FArdaGraphExecutionResult FArdaDependencyGraph::Wait(const FArdaDependencyFrameTicket& Ticket)
	{
		return FArdaInductorRuntime::Wait(*mImpl, Ticket);
	}

	TArdaRHIResult<bool> FArdaDependencyGraph::IsComplete(const FArdaDependencyFrameTicket& Ticket) const
	{
		return FArdaInductorRuntime::IsComplete(*mImpl, Ticket);
	}

	eastl::vector<FArdaInductorTimingSample> FArdaDependencyGraph::GetTimingProfile() const
	{
		FArdaInductorRuntime::CollectTelemetry(*mImpl);
		return mImpl->mTiming->Snapshot();
	}

	eastl::vector<FArdaInductorNodeTiming> FArdaDependencyGraph::GetTimingHistory() const
	{
		FArdaInductorRuntime::CollectTelemetry(*mImpl);
		return mImpl->mTiming->History();
	}

	void FArdaDependencyGraph::CollectTelemetry()
	{
		AdvanceArdaInductorTelemetry(*mImpl);
	}

	FArdaInductorAdaptiveSchedulingStats FArdaDependencyGraph::GetAdaptiveSchedulingStats() const
	{
		return mImpl->mAdaptiveStats;
	}

	void FArdaDependencyGraph::ClearTimingProfile()
	{
		mImpl->mTiming->Clear();
	}

	FArdaCudaGraphStats FArdaDependencyGraph::GetCudaGraphStats() const
	{
		FArdaCudaGraphStats Result;
		if (!mImpl->mRuntime)
		{
			return Result;
		}
		for (const auto& Frame : mImpl->mRuntime->mFrames)
		{
			for (const auto& Entry : Frame->mCudaCaches)
			{
				const auto Stats = Entry.second->GetStats();
				Result.mCaptureCount += Stats.mCaptureCount;
				Result.mReplayCount += Stats.mReplayCount;
				Result.mRebuildCount += Stats.mRebuildCount;
				Result.mFallbackCount += Stats.mFallbackCount;
				Result.mCaptureFailureCount += Stats.mCaptureFailureCount;
				Result.mCacheHitCount += Stats.mCacheHitCount;
				Result.mEvictionCount += Stats.mEvictionCount;
				Result.mCachedVariantCount += Stats.mCachedVariantCount;
				if (!Stats.mLastFallbackReason.empty())
				{
					Result.mLastFallbackReason = Stats.mLastFallbackReason;
				}
			}
		}
		return Result;
	}
}
