#include "Graph/Node/ArdaDependencyNodeRegistry.h"
#include "Graph/Node/ArdaDependencyGraphStatus.h"
#include <EASTL/sort.h>

namespace arda
{
	FArdaRHIStatus InitializeArdaBuiltinNodes(FArdaNodeRegistry& Registry);

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
			return GraphInvalid("Node metadata requires a nonempty name and a positive implementation revision.");
		}

		std::lock_guard<std::mutex> Lock(mMutex);
		if (mDefinitions.find(D.mName) != mDefinitions.end())
		{
			return GraphInvalid("A node definition with that name is already registered.");
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
				return GraphInvalid("No registered node definition has that name.");
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
}
