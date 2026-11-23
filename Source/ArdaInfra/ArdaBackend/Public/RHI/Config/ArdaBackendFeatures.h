/** Arda feature names and support reported for an initialized graphics device. */
#pragma once

#include "RHI/Config/ArdaRHIStatus.h"
#include <EASTL/algorithm.h>
#include <EASTL/map.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace arda
{
	// ================================
	// Common feature names
	// ================================

	/** These string constants name Boolean capabilities. Each provider translates its own SDK into this vocabulary. */
	namespace ArdaBackendFeature
	{
		inline constexpr char ShaderFloat16[] = "shader.float16";
		inline constexpr char ShaderFloat64[] = "shader.float64";
		inline constexpr char ShaderInt16[] = "shader.int16";
		inline constexpr char ShaderInt64[] = "shader.int64";
		inline constexpr char GeometryShader[] = "shader.geometry";
		inline constexpr char TessellationShader[] = "shader.tessellation";
		inline constexpr char MeshShader[] = "shader.mesh";
		inline constexpr char TaskShader[] = "shader.task";
		inline constexpr char WaveOperations[] = "shader.wave-operations";
		inline constexpr char HardwareRayTracing[] = "ray-tracing.hardware";
		inline constexpr char RayTracingPipeline[] = "ray-tracing.pipeline";
		inline constexpr char InlineRayQuery[] = "ray-tracing.inline-query";
		inline constexpr char AccelerationStructure[] = "ray-tracing.acceleration-structure";
		inline constexpr char VariableRateShading[] = "rendering.variable-rate-shading";
		inline constexpr char ConservativeRasterization[] = "rendering.conservative-rasterization";
		inline constexpr char Multiview[] = "rendering.multiview";
		inline constexpr char SparseBinding[] = "resource.sparse-binding";
		inline constexpr char TimelineSemaphore[] = "synchronization.timeline-semaphore";
	}

	/** A name starts with a lowercase ASCII letter and has at least two dot-separated segments.
	 *  Segments contain lowercase letters and digits, with optional hyphens between words. */
	[[nodiscard]] inline bool IsArdaBackendFeatureName(eastl::string_view Name) noexcept
	{
		if (Name.empty() || Name.front() < 'a' || Name.front() > 'z')
		{
			return false;
		}
		bool HasDot = false;
		bool AfterSeparator = false;
		for (const char C : Name)
		{
			if ((C >= 'a' && C <= 'z') || (C >= '0' && C <= '9'))
			{
				AfterSeparator = false;
			}
			else if ((C == '.' || C == '-') && !AfterSeparator)
			{
				HasDot |= C == '.';
				AfterSeparator = true;
			}
			else
			{
				return false;
			}
		}
		return HasDot && !AfterSeparator;
	}

	// ================================
	// Device feature values and requirements
	// ================================

	/** Inclusive bounds and required flag bits for one named feature. Boolean requirements default to true. */
	struct FArdaBackendFeatureRequirement
	{
		eastl::string mName;
		uint64_t mMinimumValue = 1;
		uint64_t mRequiredBits = 0;
		uint64_t mMaximumValue = UINT64_MAX;

		/** True when the name is canonical and at least one integer can meet both bounds and the bit mask. */
		[[nodiscard]] bool IsValid() const noexcept
		{
			if (!IsArdaBackendFeatureName(mName) || mMinimumValue > mMaximumValue)
			{
				return false;
			}
			const uint64_t MissingBits = mRequiredBits & ~mMinimumValue;
			if (!MissingBits)
			{
				return true;
			}
			// At the highest missing bit the candidate already exceeds the minimum. Lower optional bits can be zero.
			uint64_t Bit = uint64_t{1} << 63;
			while (!(Bit & MissingBits))
			{
				Bit >>= 1;
			}
			const uint64_t FirstPossible = (mMinimumValue & ~(Bit - 1)) | mRequiredBits;
			return FirstPossible <= mMaximumValue;
		}
	};

	/** A conjunction of requirements shared by backend startup, operands and graph passes. */
	class FArdaRHIFeatureRequirements
	{
	public:
		FArdaRHIFeatureRequirements() = default;

		FArdaRHIFeatureRequirements(std::initializer_list<FArdaBackendFeatureRequirement> Features)
		{
			for (const auto& Feature : Features)
			{
				Require(Feature.mName, Feature.mMinimumValue, Feature.mRequiredBits, Feature.mMaximumValue);
			}
		}

		/** Repeated requests intersect bounds and accumulate required bits. */
		void Require(eastl::string_view Name,
		    uint64_t MinimumValue = 1,
		    uint64_t RequiredBits = 0,
		    uint64_t MaximumValue = UINT64_MAX)
		{
			const auto It = eastl::lower_bound(mEntries.begin(), mEntries.end(), Name, ByName);
			if (It != mEntries.end() && It->mName == Name)
			{
				It->mMinimumValue = eastl::max(It->mMinimumValue, MinimumValue);
				It->mRequiredBits |= RequiredBits;
				It->mMaximumValue = eastl::min(It->mMaximumValue, MaximumValue);
				return;
			}
			mEntries.insert(It, {eastl::string(Name), MinimumValue, RequiredBits, MaximumValue});
		}

		[[nodiscard]] const FArdaBackendFeatureRequirement* Find(const char* Name) const noexcept
		{
			if (!Name)
			{
				return nullptr;
			}
			const auto It = eastl::lower_bound(mEntries.begin(), mEntries.end(), eastl::string_view(Name), ByName);
			return It != mEntries.end() && It->mName == Name ? &*It : nullptr;
		}

		[[nodiscard]] bool Contains(const char* Name) const noexcept
		{
			return Find(Name) != nullptr;
		}

		/** Stored in canonical name order, including for graph serialization. */
		[[nodiscard]] const eastl::vector<FArdaBackendFeatureRequirement>& GetEntries() const noexcept
		{
			return mEntries;
		}

	private:
		static bool ByName(const FArdaBackendFeatureRequirement& Feature, eastl::string_view Name) noexcept
		{
			return eastl::string_view(Feature.mName) < Name;
		}

		eastl::vector<FArdaBackendFeatureRequirement> mEntries;
	};

	// ================================
	// Feature support reports
	// ================================

	/** Detailed result of evaluating a feature requirement set. */
	struct FArdaRHIFeatureSupportReport
	{
		/** All requested abilities absent from this device; an empty list means admission succeeded. */
		eastl::vector<eastl::string> mMissingAbilities;

		[[nodiscard]] bool IsSupported() const noexcept
		{
			return mMissingAbilities.empty();
		}

		[[nodiscard]] FArdaRHIStatus ToStatus() const
		{
			if (mMissingAbilities.empty())
			{
				return {};
			}
			eastl::string Message = "Missing RHI abilities: ";
			for (size_t Index = 0; Index < mMissingAbilities.size(); ++Index)
			{
				if (Index)
				{
					Message += ", ";
				}
				Message += mMissingAbilities[Index];
			}
			return {EArdaRHIResult::Unsupported, eastl::move(Message)};
		}
	};

	/** One device's feature values in the Arda vocabulary. Missing entries describe unreported support. */
	class FArdaBackendFeatureList
	{
	public:
		using FEntries = eastl::map<eastl::string, uint64_t, eastl::less<>>;

		/** Providers publish translated values, including known zeroes. Invalid names are rejected at registration. */
		void Add(eastl::string Name, uint64_t Value)
		{
			if (!IsArdaBackendFeatureName(Name))
			{
				throw std::invalid_argument((eastl::string("Invalid Arda feature name: ") + Name).c_str());
			}
			mEntries.insert_or_assign(eastl::move(Name), Value);
		}

		/** Returns the value in the feature's units, or null when the provider did not report it. */
		[[nodiscard]] const uint64_t* Find(const char* Name) const noexcept
		{
			if (!Name)
			{
				return nullptr;
			}
			const auto It = mEntries.find(Name);
			return It == mEntries.end() ? nullptr : &It->second;
		}

		/** Reads an optional numeric limit, using the caller's default for an unreported key. */
		[[nodiscard]] uint64_t GetValueOr(const char* Name, uint64_t DefaultValue) const noexcept
		{
			const auto* Value = Find(Name);
			return Value ? *Value : DefaultValue;
		}

		/** Checks all named requirements and reports each missing feature once. */
		[[nodiscard]] FArdaRHIFeatureSupportReport Evaluate(const FArdaRHIFeatureRequirements& Requirements) const;

		/** Checks an inclusive range and required flag bits. Missing names always fail. */
		[[nodiscard]] bool Supports(const char* Name,
		    uint64_t MinimumValue = 1,
		    uint64_t RequiredBits = 0,
		    uint64_t MaximumValue = UINT64_MAX) const noexcept
		{
			const uint64_t* Value = Find(Name);
			return Value && *Value >= MinimumValue && *Value <= MaximumValue && (*Value & RequiredBits) == RequiredBits;
		}

		/** Returns the device's feature names and values in name order. */
		[[nodiscard]] const FEntries& GetEntries() const noexcept
		{
			return mEntries;
		}

	private:
		FEntries mEntries;
	};

	/** Immutable snapshot that can be retained independently of the graphics device. */
	using FArdaBackendFeatureListRef = eastl::shared_ptr<const FArdaBackendFeatureList>;
}
