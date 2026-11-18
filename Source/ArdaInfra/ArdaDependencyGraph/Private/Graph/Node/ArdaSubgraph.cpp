#include "Graph/Node/ArdaSubgraph.h"

#include <EASTL/algorithm.h>

namespace arda
{
	FArdaSubgraphContext::FArdaSubgraphContext(FArdaGraphEdit& Edit, eastl::vector<FArdaDependencyNodeOutput> Outputs)
	    : mEdit(Edit),
	      mOutputs(eastl::move(Outputs))
	{
	}

	FArdaGraphEdit& FArdaSubgraphContext::GetEdit() noexcept
	{
		return mEdit;
	}

	const FArdaRHIStatus& FArdaSubgraphContext::GetStatus() const noexcept
	{
		return mEdit.GetStatus();
	}

	FArdaDependencyResourceHandle FArdaSubgraphContext::FindInput(const eastl::string& Name)
	{
		for (const auto& Input : mInputs)
		{
			if (Input.mName == Name)
			{
				return Input.mResource;
			}
		}
		mEdit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
		    "Subgraph input must name a readable resource parameter."));
		return {};
	}

	FArdaDependencyResourceHandle FArdaSubgraphContext::FindOutput(const eastl::string& Name)
	{
		for (const auto& Output : mOutputs)
		{
			if (Output.mName == Name)
			{
				return Output.mResource;
			}
		}
		mEdit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
		    "Subgraph output must name a declared output slot."));
		return {};
	}

	FArdaGraphBuffer FArdaSubgraphContext::RequestBufferInput(const eastl::string& Name)
	{
		return mEdit.UseBuffer(FindInput(Name));
	}

	FArdaGraphTexture FArdaSubgraphContext::RequestTextureInput(const eastl::string& Name)
	{
		return mEdit.UseTexture(FindInput(Name));
	}

	FArdaGraphAccelerationStructure FArdaSubgraphContext::RequestAccelerationStructureInput(const eastl::string& Name)
	{
		return mEdit.UseAccelerationStructure(FindInput(Name));
	}

	FArdaGraphBuffer FArdaSubgraphContext::GetBufferOutput(const eastl::string& Name)
	{
		return mEdit.UseBuffer(FindOutput(Name));
	}

	FArdaGraphTexture FArdaSubgraphContext::GetTextureOutput(const eastl::string& Name)
	{
		return mEdit.UseTexture(FindOutput(Name));
	}

	FArdaRHIStatus FArdaSubgraphContext::DeclareOutput(const eastl::string& Name,
	    FArdaDependencyResourceHandle Resource)
	{
		const auto Expected = FindOutput(Name);
		if (!Resource || !mEdit.GetGraph().FindResource(Resource))
		{
			mEdit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Subgraph output must refer to a live content resource."));
		}
		if (eastl::find(mDeclaredOutputs.begin(), mDeclaredOutputs.end(), Name) != mDeclaredOutputs.end())
		{
			mEdit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Subgraph outputs must be declared exactly once."));
		}
		if (mEdit.GetStatus())
		{
			mDeclaredOutputs.push_back(Name);
			mOutputBindings.push_back({Expected, Resource});
		}
		return mEdit.GetStatus();
	}

	FArdaRHIStatus FArdaSubgraphContext::ValidateOutputs()
	{
		for (const auto& Output : mOutputs)
		{
			if (eastl::find(mDeclaredOutputs.begin(), mDeclaredOutputs.end(), Output.mName) == mDeclaredOutputs.end())
			{
				mEdit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "Subgraph Build must declare every parent output."));
			}
		}
		return mEdit.GetStatus();
	}
}
