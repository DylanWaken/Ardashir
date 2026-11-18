/** Portable configuration for ordered preprocessing and compilation rule programs. */
#pragma once

#include "Graph/Node/ArdaGraphArchive.h"
#include "Inductor/Config/ArdaInductorConfig.h"

namespace arda
{
	/** Register an explicit portable schema for a named application rule. Native pointers must never be encoded. */
	struct FArdaGraphRuleArchiveCodec
	{
		eastl::string mName;
		uint32_t mVersion = 1;
		const void* mType = nullptr;
		eastl::function<FArdaRHIStatus(FArdaGraphArchiveWriter&, const FArdaGraphRule&)> mWrite;
		eastl::function<TArdaRHIResult<eastl::shared_ptr<const FArdaGraphRule>>(FArdaGraphArchiveReader&)> mRead;
	};

	/** Built-in codecs are installed automatically; conflicting names or schema versions are rejected. */
	FArdaRHIStatus RegisterArdaGraphRuleArchive(FArdaGraphRuleArchiveCodec Codec);

	template <class Rule>
	inline const uint8_t ArdaGraphRuleArchiveType = 0;

	/** Write(Writer, const Rule&) returns Status; Read(Reader) returns Result<shared_ptr<const Rule>>.
	 * Name must match the rule's stable GetName(). Register the codec before loading its archives.
	 */
	template <class Rule, class Write, class Read>
	FArdaRHIStatus RegisterArdaGraphRuleArchive(eastl::string Name,
	    uint32_t Version,
	    Write WriteConfig,
	    Read ReadConfig)
	{
		static_assert(eastl::is_base_of_v<FArdaGraphRule, Rule>, "A portable rule must derive from FArdaGraphRule.");
		FArdaGraphRuleArchiveCodec Codec;
		Codec.mName = eastl::move(Name);
		Codec.mVersion = Version;
		Codec.mType = &ArdaGraphRuleArchiveType<Rule>;
		Codec.mWrite =
		    [WriteConfig = eastl::move(WriteConfig)](FArdaGraphArchiveWriter& Archive, const FArdaGraphRule& RuleValue)
		{
			const auto* Typed = dynamic_cast<const Rule*>(&RuleValue);
			return Typed ? WriteConfig(Archive, *Typed)
			             : FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			                   "A different rule implementation owns this archive name.");
		};
		Codec.mRead = [ReadConfig = eastl::move(ReadConfig)](
		                  FArdaGraphArchiveReader& Archive) -> TArdaRHIResult<eastl::shared_ptr<const FArdaGraphRule>>
		{
			auto Result = ReadConfig(Archive);
			return {eastl::move(Result.mValue), eastl::move(Result.mStatus)};
		};
		return RegisterArdaGraphRuleArchive(eastl::move(Codec));
	}

	/** Preserve all scalar options, both rule lists, their order, implementation versions and configuration. */
	FArdaRHIStatus WriteArdaGraphOptions(FArdaGraphArchiveWriter& Archive, const FArdaInductorOptions& Options);
	/** Reconstruct registered rule implementations; unknown or incompatible schemas return Unsupported. */
	FArdaRHIStatus ReadArdaGraphOptions(FArdaGraphArchiveReader& Archive, FArdaInductorOptions& Options);
}
