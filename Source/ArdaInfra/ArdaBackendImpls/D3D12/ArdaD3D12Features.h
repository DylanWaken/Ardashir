#pragma once

#include "RHI/Config/ArdaBackendFeatures.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3d12video.h>
#include <wrl/client.h>

#include <EASTL/shared_ptr.h>
#include <EASTL/vector.h>

#include <cstdio>
#include <initializer_list>
#include <iterator>
#include <type_traits>

namespace arda
{
	namespace
	{
		class FArdaD3D12FeatureProbe
		{
		public:
			explicit FArdaD3D12FeatureProbe(ID3D12Device& Device)
			    : mDevice(Device)
			{
				if (SUCCEEDED(Device.QueryInterface(IID_PPV_ARGS(&mDevice16))))
				{
					Microsoft::WRL::ComPtr<ID3D12DeviceConfiguration> Configuration;
					if (SUCCEEDED(Device.QueryInterface(IID_PPV_ARGS(&Configuration))))
					{
						const UINT RuntimeVersion = Configuration->GetDesc().SDKVersion;
						if (RuntimeVersion < mSDKVersion)
						{
							mSDKVersion = RuntimeVersion;
						}
					}
				}
			}

			template <class T>
			bool Query(D3D12_FEATURE Feature, T& Data) const
			{
				return SUCCEEDED(Query(Feature, &Data, sizeof(Data)));
			}

			HRESULT Query(D3D12_FEATURE Feature, void* Data, UINT Size) const
			{
				return mDevice16 ? mDevice16->CheckFeatureSupport1(Feature, Data, Size, mSDKVersion)
				                 : mDevice.CheckFeatureSupport(Feature, Data, Size);
			}

		private:
			ID3D12Device& mDevice;
			Microsoft::WRL::ComPtr<ID3D12Device16> mDevice16;
			UINT mSDKVersion = D3D12_SDK_VERSION;
		};

		template <class T>
		struct TD3D12NamedValue
		{
			T mValue;
			const char* mName;
		};

		template <class T>
		struct TD3D12VideoLevel
		{
			T mValue;
			uint64_t mLevel;
		};

		eastl::string D3D12FeatureName(std::initializer_list<const char*> Parts)
		{
			eastl::string Key;
			for (const char* Part : Parts)
			{
				if (!Key.empty())
				{
					Key += '.';
				}
				Key += Part;
			}
			return Key;
		}

		eastl::string D3D12FeatureNumber(const char* Prefix, UINT Number)
		{
			char Text[32];
			std::snprintf(Text, sizeof(Text), "%s%u", Prefix, Number);
			return Text;
		}

		template <class T>
		bool QueryD3D12VideoFeature(ID3D12VideoDevice& Device, D3D12_FEATURE_VIDEO Feature, T& Data)
		{
			return SUCCEEDED(Device.CheckFeatureSupport(Feature, &Data, sizeof(Data)));
		}

		template <class ProfilesType, class LevelsType, class LevelType, class BindProfile>
		void ReadD3D12VideoProfiles(FArdaBackendFeatureList& Features,
		    ID3D12VideoDevice& Video,
		    UINT Node,
		    const TD3D12NamedValue<D3D12_VIDEO_ENCODER_CODEC>& Codec,
		    const ProfilesType& Profiles,
		    const LevelsType& Levels,
		    LevelType,
		    BindProfile Bind)
		{
			const auto NodeName = D3D12FeatureNumber("node-", Node);
			for (const auto& Profile : Profiles)
			{
				auto ProfileValue = Profile.mValue;
				LevelType Minimum{}, Maximum{};
				D3D12_FEATURE_DATA_VIDEO_ENCODER_PROFILE_LEVEL Data{Node, Codec.mValue};
				Data.Profile.DataSize = sizeof(ProfileValue);
				Data.MinSupportedLevel.DataSize = sizeof(Minimum);
				Data.MaxSupportedLevel.DataSize = sizeof(Maximum);
				Bind(Data, ProfileValue, Minimum, Maximum);
				if (!QueryD3D12VideoFeature(Video, D3D12_FEATURE_VIDEO_ENCODER_PROFILE_LEVEL, Data))
				{
					continue;
				}
				const auto ProfileKey =
				    D3D12FeatureName({"video.encode", NodeName.c_str(), Codec.mName, "profile", Profile.mName});
				Features.Add(ProfileKey, Data.IsSupported != FALSE);
				if (Data.IsSupported == FALSE)
				{
					continue;
				}
				const auto ReadLevel = [&](const char* Suffix, const auto& Output)
				{
					for (const auto& Level : Levels)
					{
						if (Level.mValue == Output)
						{
							Features.Add(D3D12FeatureName({ProfileKey.c_str(), Suffix}), Level.mLevel);
							break;
						}
					}
				};
				// Level values use major * 100 + minor * 10, with 105 for H.264 level 1b.
				if constexpr (std::is_enum_v<LevelType>)
				{
					ReadLevel("min-level", Minimum);
					ReadLevel("max-level", Maximum);
				}
				else
				{
					ReadLevel("min-level", Minimum.Level);
					ReadLevel("max-level", Maximum.Level);
					// Both HEVC and AV1 define their High tier immediately after Main.
					Features.Add(D3D12FeatureName({ProfileKey.c_str(), "high-tier"}), Maximum.Tier != 0);
				}
			}
		}

		eastl::shared_ptr<FArdaBackendFeatureList> DiscoverD3D12Features(ID3D12Device& Device)
		{
			const FArdaD3D12FeatureProbe Probe(Device);
			auto Features = eastl::make_shared<FArdaBackendFeatureList>();
			Microsoft::WRL::ComPtr<ID3D12DebugDevice> Debug;
			Features->Add("diagnostics.validation-enabled", SUCCEEDED(Device.QueryInterface(IID_PPV_ARGS(&Debug))));

#include "ArdaD3D12Features.generated.inl"

			const auto Add = [&](std::initializer_list<const char*> Parts, uint64_t Value)
			{
				Features->Add(D3D12FeatureName(Parts), Value);
			};

			for (const auto Model : ShaderModels)
			{
				D3D12_FEATURE_DATA_SHADER_MODEL Data{Model};
				if (Probe.Query(D3D12_FEATURE_SHADER_MODEL, Data))
				{
					const unsigned Version = unsigned(Data.HighestShaderModel);
					Features->Add("shader.hlsl-model", ((Version >> 4) * 100) + (Version & 15));
					break;
				}
			}
			for (const auto Version : RootSignatures)
			{
				D3D12_FEATURE_DATA_ROOT_SIGNATURE Data{Version};
				if (Probe.Query(D3D12_FEATURE_ROOT_SIGNATURE, Data))
				{
					Features->Add("shader.root-signature-version", 99 + unsigned(Data.HighestVersion));
					break;
				}
			}
			D3D12_FEATURE_DATA_MLIR_COMPUTE_GRAPH_VERSION Mlir{};
			Mlir.HighestVersion.Version = UINT64_MAX;
			if (Probe.Query(D3D12_FEATURE_MLIR_COMPUTE_GRAPH_VERSION, Mlir))
			{
				Features->Add("compute-graph.mlir-program", Mlir.HighestVersion.Version != 0);
				Features->Add("compute-graph.mlir-ir-major-version", (Mlir.HighestVersion.Version >> 48) & 0xffff);
			}

			const UINT NodeCount = Device.GetNodeCount();
			Features->Add("device.node-count", NodeCount);
			Features->Add("rendering.sampler-anisotropy", 1);
			Features->Add("rendering.multi-draw-indirect", 1);
			for (UINT Node = 0; Node < NodeCount; ++Node)
			{
				const auto NodeName = D3D12FeatureNumber("node-", Node);
				D3D12_FEATURE_DATA_ARCHITECTURE1 Architecture{Node};
				if (Probe.Query(D3D12_FEATURE_ARCHITECTURE1, Architecture))
				{
					Add({"device", NodeName.c_str(), "tile-based-renderer"}, Architecture.TileBasedRenderer != FALSE);
					Add({"device", NodeName.c_str(), "unified-memory"}, Architecture.UMA != FALSE);
					Add({"device", NodeName.c_str(), "cache-coherent-unified-memory"},
					    Architecture.CacheCoherentUMA != FALSE);
					Add({"device", NodeName.c_str(), "isolated-mmu"}, Architecture.IsolatedMMU != FALSE);
				}
				D3D12_FEATURE_DATA_SERIALIZATION Serialization{Node};
				if (Probe.Query(D3D12_FEATURE_SERIALIZATION, Serialization))
				{
					Add({"memory", NodeName.c_str(), "heap-serialization"}, Serialization.HeapSerializationTier != 0);
				}
				D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT Protection{Node};
				if (Probe.Query(D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT, Protection))
				{
					Add({"memory", NodeName.c_str(), "protected-session"},
					    (Protection.Support & D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAG_SUPPORTED) != 0);
				}
				D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT TypeCount{Node};
				if (Probe.Query(D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT, TypeCount))
				{
					Add({"memory", NodeName.c_str(), "protected-session-type-count"}, TypeCount.Count);
					eastl::vector<GUID> Types(TypeCount.Count);
					D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES Sessions{Node, TypeCount.Count, Types.data()};
					if (Types.empty() || Probe.Query(D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES, Sessions))
					{
						Add({"memory", NodeName.c_str(), "hardware-protected-session"},
						    eastl::find(Types.begin(), Types.end(), HardwareProtectedSession) != Types.end());
					}
				}
				for (const auto& Type : QueueTypes)
				{
					D3D12_FEATURE_DATA_FENCE_BARRIERS Fence{Node, Type.mValue};
					if (Probe.Query(D3D12_FEATURE_FENCE_BARRIERS, Fence))
					{
						Add({"synchronization.fence-barrier", NodeName.c_str(), Type.mName},
						    Fence.FenceBarriersTier != 0);
						Add({"synchronization.shared-fence-barrier", NodeName.c_str(), Type.mName},
						    (Fence.SupportFlags & D3D12_FENCE_BARRIERS_SUPPORT_FLAG_SHARED_FENCES) != 0);
					}
				}
			}

			for (const auto& Type : QueueTypes)
			{
				for (const auto& Priority : QueuePriorities)
				{
					D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY Queue{Type.mValue, UINT(Priority.mValue)};
					if (Probe.Query(D3D12_FEATURE_COMMAND_QUEUE_PRIORITY, Queue))
					{
						Add({"queue.priority", Type.mName, Priority.mName}, Queue.PriorityForTypeIsSupported != FALSE);
					}
				}
				for (const auto& Layout : BarrierLayouts)
				{
					D3D12_FEATURE_DATA_BARRIER_LAYOUT Barrier{Type.mValue, Layout.mValue};
					if (Probe.Query(D3D12_FEATURE_BARRIER_LAYOUT, Barrier))
					{
						Add({"synchronization.barrier-layout", Type.mName, Layout.mName}, Barrier.Supported != FALSE);
					}
				}
			}

			for (const auto& Format : Formats)
			{
				D3D12_FEATURE_DATA_FORMAT_SUPPORT Support{Format.mValue};
				if (Probe.Query(D3D12_FEATURE_FORMAT_SUPPORT, Support))
				{
					const auto Bits = [&](const char* Capability, D3D12_FORMAT_SUPPORT1 Flags)
					{
						Add({"format", Format.mName, Capability}, (Support.Support1 & Flags) != 0);
					};
					const bool bTexture =
					    (Support.Support1 &
					        (D3D12_FORMAT_SUPPORT1_TEXTURE1D | D3D12_FORMAT_SUPPORT1_TEXTURE2D |
					            D3D12_FORMAT_SUPPORT1_TEXTURE3D | D3D12_FORMAT_SUPPORT1_TEXTURECUBE)) != 0;
					const bool bShaderLoad = (Support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0;
					const bool bShaderSample = (Support.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0;
					const bool bStorage = (Support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0;
					Add({"format", Format.mName, "sampled-image"}, bTexture && (bShaderLoad || bShaderSample));
					Add({"format", Format.mName, "linear-filter"}, bTexture && bShaderSample);
					Add({"format", Format.mName, "storage-image"}, bTexture && bStorage);
					Bits("color-attachment", D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
					Bits("depth-stencil-attachment", D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL);
					Bits("vertex-buffer", D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER);
					Add({"format", Format.mName, "buffer"},
					    (Support.Support1 & D3D12_FORMAT_SUPPORT1_BUFFER) != 0 && (bShaderLoad || bStorage));
					Bits("color-attachment-blend", D3D12_FORMAT_SUPPORT1_BLENDABLE);
					Bits("texture-1d", D3D12_FORMAT_SUPPORT1_TEXTURE1D);
					Bits("texture-2d", D3D12_FORMAT_SUPPORT1_TEXTURE2D);
					Bits("texture-3d", D3D12_FORMAT_SUPPORT1_TEXTURE3D);
					Bits("texture-cube", D3D12_FORMAT_SUPPORT1_TEXTURECUBE);
					Bits("index-buffer", D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER);
					Bits("comparison-sampling", D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON);
					Bits("multisample-resolve", D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE);
					Bits("gather", D3D12_FORMAT_SUPPORT1_SHADER_GATHER);
					const auto Storage = [&](const char* Capability, D3D12_FORMAT_SUPPORT2 Flags)
					{
						Add({"format", Format.mName, Capability}, (Support.Support2 & Flags) != 0);
					};
					Storage("storage-load", D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
					Storage("storage-store", D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE);
					Storage("storage-atomic-add", D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD);
					Storage("storage-atomic-bitwise", D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS);
					Storage("storage-atomic-compare-exchange",
					    D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE);
					Storage("storage-atomic-exchange", D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE);
					Storage("storage-atomic-minmax-signed", D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX);
					Storage("storage-atomic-minmax-unsigned", D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX);
					Storage("sparse-residency", D3D12_FORMAT_SUPPORT2_TILED);
				}
				D3D12_FEATURE_DATA_FORMAT_INFO Info{Format.mValue};
				if (Probe.Query(D3D12_FEATURE_FORMAT_INFO, Info))
				{
					Add({"format", Format.mName, "plane-count"}, Info.PlaneCount);
				}
				if ((Support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) == 0)
				{
					continue;
				}
				for (UINT Samples = 1; Samples <= D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT; Samples *= 2)
				{
					const auto SampleName = D3D12FeatureNumber("samples-", Samples);
					for (const auto& Flag : MultisampleFlags)
					{
						D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS Quality{Format.mValue, Samples, Flag.mValue};
						if (Probe.Query(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, Quality))
						{
							Add({"format", Format.mName, SampleName.c_str(), Flag.mName, "quality-levels"},
							    Quality.NumQualityLevels);
						}
					}
				}
			}

			Microsoft::WRL::ComPtr<ID3D12VideoDevice> Video;
			if (FAILED(Device.QueryInterface(IID_PPV_ARGS(&Video))))
			{
				return Features;
			}
			D3D12_FEATURE_DATA_VIDEO_ARCHITECTURE VideoArchitecture{};
			if (QueryD3D12VideoFeature(*Video.Get(), D3D12_FEATURE_VIDEO_ARCHITECTURE, VideoArchitecture))
			{
				Features->Add("video.io-coherent", VideoArchitecture.IOCoherent != FALSE);
			}
			bool HasDecode = false, HasProcess = false, HasEncode = false, HasArea = false;
			bool KnownDecodeProfiles = false;
			constexpr const char* DecodeCodecs[] = {"h264", "hevc", "av1", "vp9"};
			bool DecodeSupported[std::size(DecodeCodecs)]{};
			bool KnownEncodeCodec[std::size(VideoCodecs)]{};
			bool EncodeSupported[std::size(VideoCodecs)]{};
			for (UINT Node = 0; Node < NodeCount; ++Node)
			{
				const auto NodeName = D3D12FeatureNumber("node-", Node);
				const auto QueryVideo = [&](D3D12_FEATURE_VIDEO Feature, auto& Data)
				{
					return QueryD3D12VideoFeature(*Video.Get(), Feature, Data);
				};
				D3D12_FEATURE_DATA_VIDEO_FEATURE_AREA_SUPPORT Areas{Node};
				if (QueryVideo(D3D12_FEATURE_VIDEO_FEATURE_AREA_SUPPORT, Areas))
				{
					HasArea = true;
					HasDecode |= Areas.VideoDecodeSupport != FALSE;
					HasProcess |= Areas.VideoProcessSupport != FALSE;
					HasEncode |= Areas.VideoEncodeSupport != FALSE;
					Add({"video.decode", NodeName.c_str()}, Areas.VideoDecodeSupport != FALSE);
					Add({"video.process", NodeName.c_str()}, Areas.VideoProcessSupport != FALSE);
					Add({"video.encode", NodeName.c_str()}, Areas.VideoEncodeSupport != FALSE);
				}
				D3D12_FEATURE_DATA_VIDEO_PROCESS_MAX_INPUT_STREAMS Streams{Node};
				if (QueryVideo(D3D12_FEATURE_VIDEO_PROCESS_MAX_INPUT_STREAMS, Streams))
				{
					Add({"video.process", NodeName.c_str(), "max-input-streams"}, Streams.MaxInputStreams);
				}
				D3D12_FEATURE_DATA_VIDEO_PROCESS_PROTECTED_RESOURCES ProcessProtection{Node};
				if (QueryVideo(D3D12_FEATURE_VIDEO_PROCESS_PROTECTED_RESOURCES, ProcessProtection))
				{
					Add({"video.process", NodeName.c_str(), "protected-resources"},
					    ProcessProtection.SupportFlags != 0);
				}
				D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_PROTECTED_RESOURCES MotionProtection{Node};
				if (QueryVideo(D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR_PROTECTED_RESOURCES, MotionProtection))
				{
					Add({"video.motion-estimation", NodeName.c_str(), "protected-resources"},
					    MotionProtection.SupportFlags != 0);
				}
				D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILE_COUNT ProfileCount{Node};
				if (QueryVideo(D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT, ProfileCount))
				{
					Add({"video.decode", NodeName.c_str(), "profile-count"}, ProfileCount.ProfileCount);
					eastl::vector<GUID> Profiles(ProfileCount.ProfileCount);
					D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES Data{Node, ProfileCount.ProfileCount, Profiles.data()};
					if (!Profiles.empty() && QueryVideo(D3D12_FEATURE_VIDEO_DECODE_PROFILES, Data))
					{
						KnownDecodeProfiles = true;
						for (const auto& Known : VideoDecodeProfiles)
						{
							const bool Supported =
							    eastl::find(Profiles.begin(), Profiles.end(), Known.mValue) != Profiles.end();
							Add({"video.decode", NodeName.c_str(), "profile", Known.mName}, Supported);
							for (size_t Index = 0; Index < std::size(DecodeCodecs); ++Index)
							{
								DecodeSupported[Index] |=
								    Supported && eastl::string_view(Known.mName).starts_with(DecodeCodecs[Index]);
							}
						}
					}
				}
				for (size_t Index = 0; Index < std::size(VideoCodecs); ++Index)
				{
					const auto& Codec = VideoCodecs[Index];
					D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC Data{Node, Codec.mValue};
					if (!QueryVideo(D3D12_FEATURE_VIDEO_ENCODER_CODEC, Data))
					{
						continue;
					}
					KnownEncodeCodec[Index] = true;
					EncodeSupported[Index] |= Data.IsSupported != FALSE;
					Add({"video.encode", NodeName.c_str(), Codec.mName}, Data.IsSupported != FALSE);
					if (Data.IsSupported == FALSE)
					{
						continue;
					}
					for (const auto& Mode : VideoRateControls)
					{
						D3D12_FEATURE_DATA_VIDEO_ENCODER_RATE_CONTROL_MODE RateControl{Node, Codec.mValue, Mode.mValue};
						if (QueryVideo(D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE, RateControl))
						{
							Add({"video.encode", NodeName.c_str(), Codec.mName, "rate-control", Mode.mName},
							    RateControl.IsSupported != FALSE);
						}
					}
					switch (Codec.mValue)
					{
					case D3D12_VIDEO_ENCODER_CODEC_H264:
						ReadD3D12VideoProfiles(*Features,
						    *Video.Get(),
						    Node,
						    Codec,
						    H264Profiles,
						    H264Levels,
						    D3D12_VIDEO_ENCODER_LEVELS_H264{},
						    [](auto& Data, auto& Profile, auto& Min, auto& Max)
						    {
							    Data.Profile.pH264Profile = &Profile;
							    Data.MinSupportedLevel.pH264LevelSetting = &Min;
							    Data.MaxSupportedLevel.pH264LevelSetting = &Max;
						    });
						break;
					case D3D12_VIDEO_ENCODER_CODEC_HEVC:
						ReadD3D12VideoProfiles(*Features,
						    *Video.Get(),
						    Node,
						    Codec,
						    HevcProfiles,
						    HevcLevels,
						    D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC{},
						    [](auto& Data, auto& Profile, auto& Min, auto& Max)
						    {
							    Data.Profile.pHEVCProfile = &Profile;
							    Data.MinSupportedLevel.pHEVCLevelSetting = &Min;
							    Data.MaxSupportedLevel.pHEVCLevelSetting = &Max;
						    });
						break;
					case D3D12_VIDEO_ENCODER_CODEC_AV1:
						ReadD3D12VideoProfiles(*Features,
						    *Video.Get(),
						    Node,
						    Codec,
						    Av1Profiles,
						    Av1Levels,
						    D3D12_VIDEO_ENCODER_AV1_LEVEL_TIER_CONSTRAINTS{},
						    [](auto& Data, auto& Profile, auto& Min, auto& Max)
						    {
							    Data.Profile.pAV1Profile = &Profile;
							    Data.MinSupportedLevel.pAV1LevelSetting = &Min;
							    Data.MaxSupportedLevel.pAV1LevelSetting = &Max;
						    });
						break;
					}
				}
			}
			if (HasArea)
			{
				Features->Add("video.decode", HasDecode);
				Features->Add("video.process", HasProcess);
				Features->Add("video.encode", HasEncode);
			}
			if (KnownDecodeProfiles)
			{
				for (size_t Index = 0; Index < std::size(DecodeCodecs); ++Index)
				{
					Add({"video.decode", DecodeCodecs[Index]}, DecodeSupported[Index]);
				}
			}
			for (size_t Index = 0; Index < std::size(VideoCodecs); ++Index)
			{
				if (KnownEncodeCodec[Index])
				{
					Add({"video.encode", VideoCodecs[Index].mName}, EncodeSupported[Index]);
				}
			}
			return Features;
		}
	}
}
