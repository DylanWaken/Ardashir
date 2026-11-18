#include "Graph/Node/ArdaGraphSemanticFields.h"
#include "Inductor/Compiler/ArdaGraphRuleArchive.h"
#include <EASTL/unordered_map.h>
#include <mutex>
#include <exception>

namespace arda
{
	namespace
	{
		struct FArdaRuleCodecs
		{
			std::mutex mMutex;
			eastl::unordered_map<eastl::string, FArdaGraphRuleArchiveCodec> mEntries;
		};

		FArdaRuleCodecs& Codecs()
		{
			static FArdaRuleCodecs Value;
			return Value;
		}

		FArdaRHIStatus RegisterCodec(FArdaGraphRuleArchiveCodec Codec)
		{
			if (Codec.mName.empty() || !Codec.mVersion || !Codec.mType || !Codec.mWrite || !Codec.mRead)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "A rule archive codec requires a name, version, type and two callbacks.");
			}
			auto& Registry = Codecs();
			std::lock_guard<std::mutex> Lock(Registry.mMutex);
			const auto Found = Registry.mEntries.find(Codec.mName);
			if (Found != Registry.mEntries.end())
			{
				return Found->second.mType == Codec.mType && Found->second.mVersion == Codec.mVersion
				    ? FArdaRHIStatus{}
				    : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				          "Another rule archive schema owns this name.");
			}
			const auto Name = Codec.mName;
			Registry.mEntries.emplace(Name, eastl::move(Codec));
			return {};
		}

		template <class Rule, class Write, class Read>
		void RegisterBuiltin(Write WriteConfig, Read ReadConfig)
		{
			FArdaGraphRuleArchiveCodec Codec;
			Codec.mName = Rule{}.GetName();
			Codec.mType = &ArdaGraphRuleArchiveType<Rule>;
			Codec.mWrite = [WriteConfig](FArdaGraphArchiveWriter& Archive, const FArdaGraphRule& Value)
			{
				const auto* Typed = dynamic_cast<const Rule*>(&Value);
				if (!Typed)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
					    "An unregistered implementation uses a built-in rule name.");
				}
				WriteConfig(Archive, *Typed);
				return Archive.GetStatus();
			};
			Codec.mRead =
			    [ReadConfig](
			        FArdaGraphArchiveReader& Archive) -> TArdaRHIResult<eastl::shared_ptr<const FArdaGraphRule>>
			{
				auto RuleValue = eastl::make_shared<Rule>();
				ReadConfig(Archive, *RuleValue);
				return {eastl::move(RuleValue), Archive.GetStatus()};
			};
			(void)RegisterCodec(eastl::move(Codec));
		}

		template <class Rule>
		void RegisterStateless()
		{
			RegisterBuiltin<Rule>(
			    [](FArdaGraphArchiveWriter&, const Rule&)
			    {
			    },
			    [](FArdaGraphArchiveReader&, Rule&)
			    {
			    });
		}

		void EnsureBuiltins()
		{
			static std::once_flag Once;
			std::call_once(Once,
			    []
			    {
				    RegisterStateless<FArdaExpandSubgraphsRule>();
				    RegisterStateless<FArdaAssembleGraphicsStagesRule>();
				    RegisterStateless<FArdaAssembleRayTracingStagesRule>();
				    RegisterStateless<FArdaDeduceResourceUsageRule>();
				    RegisterStateless<FArdaValidateResourceAccessesRule>();
				    RegisterStateless<FArdaResolveDependenciesRule>();
				    RegisterStateless<FArdaValidateAcyclicRule>();
				    RegisterStateless<FArdaCullUnreachableRule>();
				    RegisterStateless<FArdaKeepAllNodesRule>();
				    RegisterStateless<FArdaFlattenGraphRule>();
				    RegisterStateless<FArdaCheckRequiredFeaturesRule>();
				    RegisterStateless<FArdaInferPipelinesRule>();
				    RegisterStateless<FArdaSearchOrderRule>();
				    RegisterStateless<FArdaStableOrderRule>();
				    RegisterStateless<FArdaGraphicsQueueRule>();
				    RegisterStateless<FArdaBatchCudaRule>();
				    RegisterStateless<FArdaBatchPipelinesRule>();
				    RegisterStateless<FArdaMergeDrawInstancesRule>();
				    RegisterStateless<FArdaFlattenDispatchesRule>();
				    RegisterStateless<FArdaResolveFencesRule>();
				    RegisterBuiltin<FArdaMinimizeMemoryRule>(
				        [](auto& Archive, const auto& Rule)
				        {
					        Archive.Write(Rule.mSearchBudget);
				        },
				        [](auto& Archive, auto& Rule)
				        {
					        Archive.Read(Rule.mSearchBudget);
				        });
				    RegisterBuiltin<FArdaPlanMemoryRule>(
				        [](auto& Archive, const auto& Rule)
				        {
					        Archive.Write(Rule.mbAllowSerialization);
				        },
				        [](auto& Archive, auto& Rule)
				        {
					        Archive.Read(Rule.mbAllowSerialization);
				        });
				    RegisterBuiltin<FArdaAssignQueuesRule>(
				        [](auto& Archive, const auto& Rule)
				        {
					        Archive.Fields(Rule.mbEnableCopy, Rule.mbEnableCompute);
				        },
				        [](auto& Archive, auto& Rule)
				        {
					        Archive.Fields(Rule.mbEnableCopy, Rule.mbEnableCompute);
				        });
			    });
		}

		bool FindCodec(const eastl::string& Name, FArdaGraphRuleArchiveCodec& Codec)
		{
			EnsureBuiltins();
			auto& Registry = Codecs();
			std::lock_guard<std::mutex> Lock(Registry.mMutex);
			const auto Found = Registry.mEntries.find(Name);
			if (Found == Registry.mEntries.end())
			{
				return false;
			}
			Codec = Found->second;
			return true;
		}

		template <class Rule>
		void WriteRules(FArdaGraphArchiveWriter& Archive, const eastl::vector<eastl::shared_ptr<const Rule>>& Rules)
		{
			Archive.WriteInteger(Rules.size(), 8);
			for (const auto& RuleValue : Rules)
			{
				FArdaGraphRuleArchiveCodec Codec;
				if (!RuleValue || !RuleValue->GetName() || !FindCodec(RuleValue->GetName(), Codec))
				{
					Archive.Fail("An ordered graph rule has no registered portable configuration codec.",
					    EArdaRHIResult::Unsupported);
					return;
				}
				FArdaGraphArchiveWriter Configuration;
				const auto Status = Codec.mWrite(Configuration, *RuleValue);
				if (!Status || !Configuration.GetStatus())
				{
					const auto& Error = Status ? Configuration.GetStatus() : Status;
					Archive.Fail(Error.mMessage.c_str(), Error.mCode);
					return;
				}
				Archive.Fields(Codec.mName, Codec.mVersion, Configuration.GetData());
			}
		}

		template <class Rule>
		void ReadRules(FArdaGraphArchiveReader& Archive, eastl::vector<eastl::shared_ptr<const Rule>>& Rules)
		{
			Rules.clear();
			const auto Count = Archive.ReadCount();
			for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
			{
				eastl::string Name, Bytes;
				uint32_t Version = 0;
				Archive.Fields(Name, Version, Bytes);
				if (!Archive.GetStatus())
				{
					return;
				}
				FArdaGraphRuleArchiveCodec Codec;
				if (!FindCodec(Name, Codec) || Version != Codec.mVersion)
				{
					Archive.Fail(
					    "A graph rule archive requires a registered implementation with the same configuration version.",
					    EArdaRHIResult::Unsupported);
					return;
				}
				FArdaGraphArchiveReader Configuration(Bytes);
				auto Loaded = Codec.mRead(Configuration);
				const auto Status = Configuration.Finish();
				if (!Loaded || !Status)
				{
					const auto& Error = Loaded ? Status : Loaded.mStatus;
					Archive.Fail(Error.mMessage.c_str(), Error.mCode);
					return;
				}
				const auto Typed = eastl::dynamic_pointer_cast<const Rule>(Loaded.mValue);
				if (!Typed || !Typed->GetName() || Name != Typed->GetName())
				{
					Archive.Fail("A graph rule archive factory returned the wrong rule name or phase.");
					return;
				}
				Rules.push_back(Typed);
			}
		}

	}

	FArdaRHIStatus RegisterArdaGraphRuleArchive(FArdaGraphRuleArchiveCodec Codec)
	{
		EnsureBuiltins();
		return RegisterCodec(eastl::move(Codec));
	}

	FArdaRHIStatus WriteArdaGraphOptions(FArdaGraphArchiveWriter& Archive, const FArdaInductorOptions& Options)
	{
		try
		{
			Archive.Write(uint32_t{1});
			InductorOptionFields(Archive, Options);
			WriteRules(Archive, Options.mRules.mPreprocessRules);
			WriteRules(Archive, Options.mRules.mCompileRules);
		}
		catch (const std::exception& Error)
		{
			Archive.Fail(Error.what(), EArdaRHIResult::BackendFailure);
		}
		catch (...)
		{
			Archive.Fail("Graph rule configuration encoding threw.", EArdaRHIResult::BackendFailure);
		}
		return Archive.GetStatus();
	}

	FArdaRHIStatus ReadArdaGraphOptions(FArdaGraphArchiveReader& Archive, FArdaInductorOptions& Options)
	{
		try
		{
			uint32_t Version = 0;
			Archive.Read(Version);
			if (Version != 1)
			{
				Archive.Fail("Unsupported graph options archive version.", EArdaRHIResult::Unsupported);
			}
			FArdaInductorOptions Loaded;
			InductorOptionFields(Archive, Loaded);
			ReadRules(Archive, Loaded.mRules.mPreprocessRules);
			ReadRules(Archive, Loaded.mRules.mCompileRules);
			if (Archive.GetStatus())
			{
				Options = eastl::move(Loaded);
			}
		}
		catch (const std::exception& Error)
		{
			Archive.Fail(Error.what(), EArdaRHIResult::BackendFailure);
		}
		catch (...)
		{
			Archive.Fail("Graph rule configuration decoding threw.", EArdaRHIResult::BackendFailure);
		}
		return Archive.GetStatus();
	}
}
