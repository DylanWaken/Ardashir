/** Versioned structural graph archives, using the shared backend file operations. */
#pragma once

#include "Graph/Node/ArdaDependencyGraph.h"
#include <filesystem>

namespace arda
{
	/** Shared archive service also exposed directly by FArdaDependencyGraph::Serialize/Deserialize/Save/Load.
	 * Saves named node types, public parameters, resources, connections and registered rule configuration.
	 * Preprocessed topology and culling annotations are retained. Native imports become named typed slots,
	 * left unbound when loaded. Bind the required slots before compilation on the destination device.
	 * Prepared execution objects, telemetry and device state are never persisted. Fields without an explicit
	 * codec return Unsupported. Named subgraphs retain their registered builder for structural preprocessing.
	 */
	class FArdaGraphSerialization final
	{
	public:
		/** Encode a closed construction as a deterministic binary string. */
		[[nodiscard]] static TArdaRHIResult<eastl::string> Serialize(const FArdaDependencyGraph& Graph);
		/** Restore into a new graph and queue its construction atomically. Native class definitions must be registered;
		 * Runtime kernel references and binding schemas are restored from the archive.
		 * incompatible versions and malformed references fail without publishing any partial graph.
		 */
		[[nodiscard]] static TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> Deserialize(
		    const eastl::string& Data,
		    FArdaRHIDeviceRef Device = {});
		/** Save the graph archive and stage available kernel libraries beside it. */
		[[nodiscard]] static FArdaRHIStatus Save(const FArdaDependencyGraph& Graph, const std::filesystem::path& Path);
		/** Read Path using backend FileOperations and restore its closed construction. */
		[[nodiscard]] static TArdaRHIResult<eastl::unique_ptr<FArdaDependencyGraph>> Load(
		    const std::filesystem::path& Path,
		    FArdaRHIDeviceRef Device = {});

	private:
		static TArdaRHIResult<eastl::string> Serialize(const FArdaDependencyGraph& Graph,
		    eastl::vector<eastl::string>* ReferencedLibraries);
	};
}
