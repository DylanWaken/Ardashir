#include "Graph/Resources/ArdaDependencyResourceContext.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include "Graph/Resources/ArdaGraphResourceValidation.h"
#include "Graph/Resources/ArdaGraphResourceIdentity.h"
#include <EASTL/algorithm.h>

namespace arda
{
	TArdaRHIResult<FArdaDependencyResourceHandle> FArdaDependencyGraph::CreateResource(FArdaDependencyResourceDesc D)
	{
		auto& G = *mImpl;
		if (G.mbCompiling || (G.mbPreprocessing && !G.mbPreprocessEditing))
		{
			return {{}, GraphCompiling()};
		}
		if (!G.mbEditing)
		{
			return {{}, GraphNotEditing()};
		}
		if (D.mName.empty() || G.mResourceNames.find(D.mName) != G.mResourceNames.end())
		{
			return {{}, GraphInvalid("Resource versions require unique nonempty names.")};
		}
		const auto BoundKind = D.mExternalBuffer ? EArdaGraphExternalResourceKind::Buffer
		    : D.mExternalTexture                 ? EArdaGraphExternalResourceKind::Texture
		    : D.mExternalAccelerationStructure   ? EArdaGraphExternalResourceKind::AccelerationStructure
		                                         : EArdaGraphExternalResourceKind::None;
		if (D.mExternalKind == EArdaGraphExternalResourceKind::None)
		{
			D.mExternalKind = BoundKind;
		}
		if (D.mExternalKind > EArdaGraphExternalResourceKind::AccelerationStructure ||
		    (BoundKind != EArdaGraphExternalResourceKind::None && BoundKind != D.mExternalKind) ||
		    (D.IsExternal() && D.mbTexture != (D.mExternalKind == EArdaGraphExternalResourceKind::Texture)))
		{
			return {{}, GraphInvalid("External resource slot kind does not match its declaration or binding.")};
		}
		if (D.mExternalAccelerationStructure)
		{
			D.mAccelerationStructure = ArdaGraphAccelerationStructureShape(D.mExternalAccelerationStructure->GetDesc());
		}
		if ((D.mExternalBuffer && (D.mbTexture || D.mExternalTexture || D.mExternalAccelerationStructure)) ||
		    (D.mExternalAccelerationStructure && (D.mbTexture || D.mExternalTexture)))
		{
			return {{}, GraphInvalid("External resource kind mismatch.")};
		}
		if (D.mExternalTexture && !D.mbTexture)
		{
			return {{}, GraphInvalid("External texture kind mismatch.")};
		}
		// Only the resource's active descriptor participates in usage deduction.
		if (D.mbTexture || D.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure)
		{
			D.mBuffer.mUsage = EArdaRHIBufferUsage::None;
		}
		if (!D.mbTexture || D.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure)
		{
			D.mTexture.mUsage = EArdaRHITextureUsage::None;
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
		const auto BufferIdentity = GetArdaGraphPhysicalIdentity(D.mExternalBuffer);
		const auto TextureIdentity = GetArdaGraphPhysicalIdentity(D.mExternalTexture);
		const auto StructureIdentity = GetArdaGraphPhysicalIdentity(D.mExternalAccelerationStructure);
		if ((BufferIdentity && G.mImportedBuffers.count(BufferIdentity)) ||
		    (TextureIdentity && G.mImportedTextures.count(TextureIdentity)) ||
		    (StructureIdentity && G.mImportedAccelerationStructures.count(StructureIdentity)))
		{
			return {{},
			    GraphInvalid(
			        "External storage is already imported. Reuse its logical handle so resource hazards cannot be hidden by another name.")};
		}
		if (auto S = ValidateArdaGraphResourceDescription(D); !S)
		{
			return {{}, S};
		}
		if (G.mResources.size() >= UINT32_MAX)
		{
			return {{}, GraphInvalid("Too many logical resources.")};
		}

		// Publish a fresh generation only after validating the descriptor and import identity.
		const uint32_t I = static_cast<uint32_t>(G.mResources.size());
		G.mResourceNames.emplace(D.mName, I);
		G.mResources.push_back(eastl::move(D));
		if (BufferIdentity)
		{
			G.mImportedBuffers.insert(BufferIdentity);
		}
		if (TextureIdentity)
		{
			G.mImportedTextures.insert(TextureIdentity);
		}
		if (StructureIdentity)
		{
			G.mImportedAccelerationStructures.insert(StructureIdentity);
		}
		G.mResourceGenerations.push_back(G.mResourceGenerationSource
		        ? G.mResourceGenerationSource->fetch_add(1, std::memory_order_relaxed)
		        : G.mNextResourceGeneration++);
		G.mNextResourceGeneration = eastl::max(G.mNextResourceGeneration, G.mResourceGenerations.back() + 1);
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
			return {{}, GraphInvalid("Cannot import a null buffer.")};
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
			return {{}, GraphInvalid("Cannot import a null texture.")};
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
			return {{}, GraphInvalid("Cannot import a null acceleration structure.")};
		}
		FArdaDependencyResourceDesc Desc;
		Desc.mName = eastl::move(Name);
		Desc.mExternalAccelerationStructure = eastl::move(AccelerationStructure);
		return CreateResource(eastl::move(Desc));
	}

	FArdaRHIStatus FArdaDependencyGraph::MarkOutput(FArdaDependencyResourceHandle H, bool Output)
	{
		if (mImpl->mbCompiling || (mImpl->mbPreprocessing && !mImpl->mbPreprocessEditing))
		{
			return GraphCompiling();
		}
		if (!IsEditing())
		{
			return GraphNotEditing();
		}
		if (!mImpl->HasResource(H))
		{
			return GraphInvalid("Unknown resource version.");
		}
		H = mImpl->ResolveResource(H);
		mImpl->mResources[H.mIndex].mbOutput = Output;
		return {};
	}

	FArdaDependencyResourceHandle FArdaDependencyGraph::FindResource(const eastl::string& Name) const
	{
		const auto& Graph = GetReadImpl();
		const auto Found = Graph.mResourceNames.find(Name);
		if (Found == Graph.mResourceNames.end())
		{
			return {};
		}
		const auto Index = Found->second;
		return Graph.ResolveResource({Graph.mIdentity, Index, Graph.mResourceGenerations[Index]});
	}

	const FArdaDependencyResourceDesc* FArdaDependencyGraph::FindResource(FArdaDependencyResourceHandle H) const
	{
		const auto& Graph = GetReadImpl();
		H = Graph.ResolveResource(H);
		return Graph.HasResource(H) ? &Graph.mResources[H.mIndex] : nullptr;
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

	const IArdaRHIDevice* FArdaDependencyResourceContext::GetDevice() const
	{
		return mGraph.GetDevice().Get();
	}

	FArdaRHIStatus FArdaDependencyResourceContext::Buffer(FArdaDependencyResourceHandle& Output,
	    eastl::string Name,
	    FArdaRHIBufferDesc Desc)
	{
		FArdaDependencyResourceDesc D;
		D.mBuffer = eastl::move(Desc);
		return Declare(Output, eastl::move(Name), eastl::move(D));
	}

	FArdaRHIStatus FArdaDependencyResourceContext::Buffer(FArdaGraphBuffer& Output,
	    eastl::string Name,
	    FArdaRHIBufferDesc Desc)
	{
		auto Handle = Output.GetHandle();
		auto Status = Buffer(Handle, eastl::move(Name), eastl::move(Desc));
		if (Status)
		{
			Output = FArdaGraphBuffer(Handle);
		}
		return Status;
	}

	FArdaRHIStatus FArdaDependencyResourceContext::Texture(FArdaGraphTexture& Output,
	    eastl::string Name,
	    FArdaRHITextureDesc Desc)
	{
		auto Handle = Output.GetHandle();
		auto Status = Texture(Handle, eastl::move(Name), eastl::move(Desc));
		if (Status)
		{
			Output = FArdaGraphTexture(Handle);
		}
		return Status;
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
			return GraphInvalid("Node output names must be nonempty and unique.");
		}
		if (auto S = ValidateArdaGraphResourceDescription(D); !S)
		{
			return S;
		}
		const auto BufferUsage = D.mbTexture ? EArdaRHIBufferUsage::None : ArdaExplicitResourceUsage(D.mBuffer.mUsage);
		const auto TextureUsage =
		    D.mbTexture ? ArdaExplicitResourceUsage(D.mTexture.mUsage) : EArdaRHITextureUsage::None;
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
			Output = mGraph.mImpl->ResolveResource(Output);
			const auto* E = Find(Output);
			if (!E || E->mbTexture != D.mbTexture ||
			    E->mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure)
			{
				return GraphInvalid("Supplied node output has an invalid resource kind or identity.");
			}
			if (D.mbTexture)
			{
				const auto& A = E->mTexture;
				const auto& B = D.mTexture;
				if (A.mDimension != B.mDimension || A.mWidth != B.mWidth || A.mHeight != B.mHeight ||
				    A.mDepth != B.mDepth || A.mArraySize != B.mArraySize || A.mMipLevels != B.mMipLevels ||
				    A.mFormat != B.mFormat || A.mSampleCount != B.mSampleCount ||
				    (!HasAnyFlags(A.mUsage, EArdaRHITextureUsage::AutoDeduction) &&
				        (A.mUsage & TextureUsage) != TextureUsage) ||
				    (B.mbCudaInterop && !A.mbCudaInterop))
				{
					return GraphInvalid("Supplied texture does not satisfy node output requirements.");
				}
			}
			else
			{
				const auto& A = E->mBuffer;
				const auto& B = D.mBuffer;
				if (A.mByteSize < B.mByteSize || (B.mStructureStride && A.mStructureStride != B.mStructureStride) ||
				    (B.mFormat != EArdaRHIFormat::Unknown && A.mFormat != B.mFormat) ||
				    (!HasAnyFlags(A.mUsage, EArdaRHIBufferUsage::AutoDeduction) &&
				        (A.mUsage & BufferUsage) != BufferUsage) ||
				    (B.mbCudaInterop && !A.mbCudaInterop))
				{
					return GraphInvalid("Supplied buffer does not satisfy node output requirements.");
				}
			}
		}
		else
		{
			D.mName =
			    "Node output " + mName + "/" + Name + "/" + eastl::to_string(mGraph.mImpl->mResources.size()).c_str();
			auto Created = mGraph.CreateResource(eastl::move(D));
			if (!Created)
			{
				return Created.mStatus;
			}
			Output = Created.mValue;
		}
		mOutputs.push_back({eastl::move(Name), Output, BufferUsage, TextureUsage});
		return {};
	}
}
