#include "ArdaInductorPipeline.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <unordered_map>

namespace arda
{
	namespace
	{
		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		// Explicit little-endian fields avoid pointer values, padding, native size_t widths,
		// and debug labels. Bytes also permit collision-independent compatibility checks.
		struct FPipelineKeyWriter
		{
			eastl::vector<uint8_t> mBytes;
			bool mbValid = true;

			template <typename T>
			void Add(T Value)
			{
				const uint64_t Bits = static_cast<uint64_t>(Value);
				for (uint32_t Shift = 0; Shift < 64; Shift += 8)
				{
					mBytes.push_back(static_cast<uint8_t>(Bits >> Shift));
				}
			}

			void String(const eastl::string& Value)
			{
				Add(Value.size());
				mBytes.insert(mBytes.end(), Value.begin(), Value.end());
			}

			void Shader(const FArdaRHIShaderRef& Value)
			{
				Add(bool(Value));
				if (Value)
				{
					Add(Value->GetStage());
					Add(Value->GetPersistentCacheHash());
					mbValid &= Value->GetPersistentCacheHash() != 0;
				}
			}

			void Items(const eastl::vector<FArdaRHIBindingLayoutItem>& Values)
			{
				Add(Values.size());
				for (const auto& Value : Values)
				{
					Add(Value.mSlot);
					Add(Value.mArraySize);
					Add(Value.mType);
				}
			}

			void Layout(const FArdaRHIBindingLayoutRef& Value)
			{
				Add(bool(Value));
				if (!Value)
				{
					return;
				}
				const auto& Desc = Value->GetDesc();
				Add(Desc.mVisibility);
				Add(Desc.mRegisterSpace);
				Add(Desc.mbRegisterSpaceIsDescriptorSet);
				Items(Desc.mItems);
				const auto* Bindless = Value->GetBindlessDesc();
				Add(Bindless != nullptr);
				if (Bindless)
				{
					Add(Bindless->mVisibility);
					Add(Bindless->mFirstSlot);
					Add(Bindless->mRegisterSpace);
					Add(Bindless->mMaxCapacity);
					Add(Bindless->mbUnbounded);
					Add(Bindless->mbUpdateAfterBind);
					Add(Bindless->mbVariableDescriptorCount);
					Add(Bindless->mbDirectHeapIndexing);
					Add(Bindless->mbDescriptorBuffer);
					Add(Bindless->mLayoutType);
					Items(Bindless->mRegisterSpaces);
				}
			}

			void Layouts(const eastl::vector<FArdaRHIBindingLayoutRef>& Values)
			{
				Add(Values.size());
				for (const auto& Value : Values)
				{
					Layout(Value);
				}
			}

			void Input(const FArdaRHIInputLayoutRef& Value)
			{
				Add(bool(Value));
				if (!Value)
				{
					return;
				}
				Add(Value->GetDesc().mAttributes.size());
				for (const auto& Attribute : Value->GetDesc().mAttributes)
				{
					String(Attribute.mSemanticName);
					Add(Attribute.mFormat);
					Add(Attribute.mArraySize);
					Add(Attribute.mBufferIndex);
					Add(Attribute.mOffset);
					Add(Attribute.mElementStride);
					Add(Attribute.mbInstanced);
				}
			}

			template <typename T>
			void Raster(const T& Desc)
			{
				Add(Desc.mTopology);
				Add(Desc.mBlendState.mbAlphaToCoverage);
				for (const auto& Target : Desc.mBlendState.mTargets)
				{
					Add(Target.mbEnable);
					Add(Target.mSourceColor);
					Add(Target.mDestinationColor);
					Add(Target.mSourceAlpha);
					Add(Target.mDestinationAlpha);
				}
				Add(Desc.mRasterState.mFillMode);
				Add(Desc.mRasterState.mCullMode);
				Add(Desc.mRasterState.mbFrontCounterClockwise);
				Add(Desc.mRasterState.mbDepthClip);
				Add(Desc.mRasterState.mbScissor);
				Add(Desc.mDepthStencilState.mbDepthTest);
				Add(Desc.mDepthStencilState.mbDepthWrite);
				Add(Desc.mDepthStencilState.mDepthFunc);
				Add(Desc.mColorFormats.size());
				for (const auto Format : Desc.mColorFormats)
				{
					Add(Format);
				}
				Add(Desc.mDepthFormat);
				Add(Desc.mSampleCount);
			}

			uint64_t Finish() const
			{
				uint64_t Hash = 14695981039346656037ull;
				for (uint8_t Byte : mBytes)
				{
					Hash = (Hash ^ Byte) * 1099511628211ull;
				}
				return Hash ? Hash : 1;
			}
		};

		FPipelineKeyWriter ConfigurationKey(const FArdaInductorPipelineConfiguration& C, bool bStages = true)
		{
			FPipelineKeyWriter W;
			W.Add(0x4152444150534f01ull); // Semantic schema version.
			W.Add(C.mKind);
			switch (C.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				if (bStages)
				{
					W.Shader(C.mCompute.mDesc.mComputeShader);
					W.Layouts(C.mCompute.mDesc.mBindingLayouts);
				}
				break;
			case EArdaPipelineStateKind::Graphics:
			{
				const auto& D = C.mGraphics.mDesc;
				W.Raster(D);
				W.Add(D.mPatchControlPoints);
				W.Input(D.mInputLayout);
				if (bStages)
				{
					W.Shader(D.mVertexShader);
					W.Shader(D.mHullShader);
					W.Shader(D.mDomainShader);
					W.Shader(D.mGeometryShader);
					W.Shader(D.mPixelShader);
					W.Layouts(D.mBindingLayouts);
				}
				break;
			}
			case EArdaPipelineStateKind::Meshlet:
			{
				const auto& D = C.mMeshlet.mDesc;
				W.Raster(D);
				if (bStages)
				{
					W.Shader(D.mAmplificationShader);
					W.Shader(D.mMeshShader);
					W.Shader(D.mPixelShader);
					W.Layouts(D.mBindingLayouts);
				}
				break;
			}
			case EArdaPipelineStateKind::RayTracing:
			{
				const auto& D = C.mRayTracing.mDesc;
				W.Add(D.mMaxPayloadSize);
				W.Add(D.mMaxAttributeSize);
				W.Add(D.mMaxRecursionDepth);
				W.Add(D.mbAllowOpacityMicromaps);
				if (bStages)
				{
					W.Layouts(D.mGlobalBindingLayouts);
					W.Add(D.mShaders.size());
					for (const auto& S : D.mShaders)
					{
						W.String(S.mExportName);
						W.Shader(S.mShader);
						W.Layout(S.mLocalBindingLayout);
					}
					W.Add(D.mHitGroups.size());
					for (const auto& G : D.mHitGroups)
					{
						W.String(G.mExportName);
						W.Shader(G.mClosestHitShader);
						W.Shader(G.mAnyHitShader);
						W.Shader(G.mIntersectionShader);
						W.Layout(G.mLocalBindingLayout);
						W.Add(G.mbProceduralPrimitive);
					}
				}
				break;
			}
			case EArdaPipelineStateKind::WorkGraph:
			{
				const auto& D = C.mWorkGraph.mDesc;
				W.String(D.mProgramName);
				W.String(D.mEntryPoint);
				W.Add(D.mMaxInputRecords);
				if (bStages)
				{
					W.Layouts(D.mGlobalBindingLayouts);
					W.Add(D.mShaders.size());
					for (const auto& S : D.mShaders)
					{
						W.Shader(S);
					}
				}
				break;
			}
			default:
				W.mbValid = false;
				break;
			}
			return W;
		}

		bool SameShader(const FArdaRHIShaderRef& A, const FArdaRHIShaderRef& B)
		{
			return A == B ||
			    (A && B && A->GetStage() == B->GetStage() && A->GetPersistentCacheHash() != 0 &&
			        A->GetPersistentCacheHash() == B->GetPersistentCacheHash());
		}

		bool SameLayout(const FArdaRHIBindingLayoutRef& A, const FArdaRHIBindingLayoutRef& B)
		{
			FPipelineKeyWriter Left, Right;
			Left.Layout(A);
			Right.Layout(B);
			return Left.mBytes == Right.mBytes;
		}

		FArdaRHIStatus AssignShader(FArdaRHIShaderRef& Destination,
		    const FArdaRHIShaderRef& Source,
		    EArdaRHIShaderStage Expected)
		{
			if (!Source)
			{
				return {};
			}
			if (Source->GetStage() != Expected || !Source->GetPersistentCacheHash())
			{
				return Invalid("Pipeline shaders require the declared stage and a deterministic content hash.");
			}
			if (Destination && !SameShader(Destination, Source))
			{
				return Invalid(
				    "Pipeline inference found conflicting shaders for one stage or export; select a unique group.");
			}
			Destination = Source;
			return {};
		}

		bool Matches(EArdaPipelineStateKind Kind, EArdaRHIShaderStage Stage)
		{
			switch (Kind)
			{
			case EArdaPipelineStateKind::Compute:
				return Stage == EArdaRHIShaderStage::Compute;
			case EArdaPipelineStateKind::Graphics:
				return Stage == EArdaRHIShaderStage::Vertex || Stage == EArdaRHIShaderStage::Hull ||
				    Stage == EArdaRHIShaderStage::Domain || Stage == EArdaRHIShaderStage::Geometry ||
				    Stage == EArdaRHIShaderStage::Pixel;
			case EArdaPipelineStateKind::Meshlet:
				return Stage == EArdaRHIShaderStage::Amplification || Stage == EArdaRHIShaderStage::Mesh ||
				    Stage == EArdaRHIShaderStage::Pixel;
			case EArdaPipelineStateKind::RayTracing:
				return IsArdaRHIRayTracingShaderStage(Stage);
			case EArdaPipelineStateKind::WorkGraph:
				return Stage == EArdaRHIShaderStage::WorkGraph;
			default:
				return false;
			}
		}

		eastl::vector<FArdaRHIBindingLayoutRef>& GlobalLayouts(FArdaInductorPipelineConfiguration& C)
		{
			switch (C.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				return C.mCompute.mDesc.mBindingLayouts;
			case EArdaPipelineStateKind::Graphics:
				return C.mGraphics.mDesc.mBindingLayouts;
			case EArdaPipelineStateKind::Meshlet:
				return C.mMeshlet.mDesc.mBindingLayouts;
			case EArdaPipelineStateKind::RayTracing:
				return C.mRayTracing.mDesc.mGlobalBindingLayouts;
			default:
				return C.mWorkGraph.mDesc.mGlobalBindingLayouts;
			}
		}

		FArdaRHIStatus MergeLayouts(eastl::vector<FArdaRHIBindingLayoutRef>& Destination,
		    const eastl::vector<FArdaRHIBindingLayoutRef>& Source)
		{
			for (const auto& Layout : Source)
			{
				if (!Layout)
				{
					return Invalid("Pipeline global binding layouts cannot be null.");
				}
				bool Duplicate = false;
				for (const auto& Other : Destination)
				{
					if (SameLayout(Other, Layout))
					{
						Duplicate = true;
						continue;
					}
					const auto& A = Other->GetDesc();
					const auto& B = Layout->GetDesc();
					if (A.mRegisterSpace == B.mRegisterSpace &&
					    (HasAnyFlags(A.mVisibility, B.mVisibility) || Other->GetBindlessDesc() ||
					        Layout->GetBindlessDesc()))
					{
						return Invalid(
						    "Pipeline layouts sharing a register space require identical declarations or disjoint shader visibility.");
					}
				}
				if (!Duplicate)
				{
					Destination.push_back(Layout);
				}
			}
			eastl::sort(Destination.begin(),
			    Destination.end(),
			    [](const auto& A, const auto& B)
			    {
				    if (A->GetDesc().mRegisterSpace != B->GetDesc().mRegisterSpace)
				    {
					    return A->GetDesc().mRegisterSpace < B->GetDesc().mRegisterSpace;
				    }
				    FPipelineKeyWriter Left, Right;
				    Left.Layout(A);
				    Right.Layout(B);
				    return Left.mBytes < Right.mBytes;
			    });
			return {};
		}

		FArdaRHIStatus MergeLocalLayout(FArdaRHIBindingLayoutRef& Destination, const FArdaRHIBindingLayoutRef& Source)
		{
			if (Source && Destination && !SameLayout(Destination, Source))
			{
				return Invalid("One ray export or hit group has incompatible local layouts.");
			}
			if (Source)
			{
				Destination = Source;
			}
			return {};
		}

		FArdaRHIStatus AddShader(FArdaInductorPipelineConfiguration& C, const FArdaInductorPipelineContribution& S)
		{
			if (!S.mShader)
			{
				return S.mConfiguration ? FArdaRHIStatus{}
				                        : Invalid("A pipeline stage contribution requires a shader or configuration.");
			}
			const auto Stage = S.mShader->GetStage();
			if (!Matches(C.mKind, Stage) || !S.mShader->GetPersistentCacheHash())
			{
				return Invalid("A pipeline shader has an incompatible stage or lacks a deterministic content hash.");
			}
			switch (C.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				return AssignShader(C.mCompute.mDesc.mComputeShader, S.mShader, Stage);
			case EArdaPipelineStateKind::Graphics:
				switch (Stage)
				{
				case EArdaRHIShaderStage::Vertex:
					return AssignShader(C.mGraphics.mDesc.mVertexShader, S.mShader, Stage);
				case EArdaRHIShaderStage::Hull:
					return AssignShader(C.mGraphics.mDesc.mHullShader, S.mShader, Stage);
				case EArdaRHIShaderStage::Domain:
					return AssignShader(C.mGraphics.mDesc.mDomainShader, S.mShader, Stage);
				case EArdaRHIShaderStage::Geometry:
					return AssignShader(C.mGraphics.mDesc.mGeometryShader, S.mShader, Stage);
				default:
					return AssignShader(C.mGraphics.mDesc.mPixelShader, S.mShader, Stage);
				}
			case EArdaPipelineStateKind::Meshlet:
				if (Stage == EArdaRHIShaderStage::Amplification)
				{
					return AssignShader(C.mMeshlet.mDesc.mAmplificationShader, S.mShader, Stage);
				}
				if (Stage == EArdaRHIShaderStage::Mesh)
				{
					return AssignShader(C.mMeshlet.mDesc.mMeshShader, S.mShader, Stage);
				}
				return AssignShader(C.mMeshlet.mDesc.mPixelShader, S.mShader, Stage);
			case EArdaPipelineStateKind::WorkGraph:
				if (eastl::none_of(C.mWorkGraph.mDesc.mShaders.begin(),
				        C.mWorkGraph.mDesc.mShaders.end(),
				        [&](const auto& Other)
				        {
					        return SameShader(Other, S.mShader);
				        }))
				{
					C.mWorkGraph.mDesc.mShaders.push_back(S.mShader);
				}
				return {};
			case EArdaPipelineStateKind::RayTracing:
			{
				auto& D = C.mRayTracing.mDesc;
				if (Stage == EArdaRHIShaderStage::ClosestHit || Stage == EArdaRHIShaderStage::AnyHit ||
				    Stage == EArdaRHIShaderStage::Intersection)
				{
					if (S.mHitGroupName.empty())
					{
						return Invalid("Hit-stage contributions require an explicit hit-group name.");
					}
					auto Existing = eastl::find_if(D.mHitGroups.begin(),
					    D.mHitGroups.end(),
					    [&](const auto& G)
					    {
						    return G.mExportName == S.mHitGroupName;
					    });
					if (Existing == D.mHitGroups.end())
					{
						D.mHitGroups.push_back({});
						Existing = D.mHitGroups.end() - 1;
						Existing->mExportName = S.mHitGroupName;
					}
					if (auto Status = MergeLocalLayout(Existing->mLocalBindingLayout, S.mLocalBindingLayout); !Status)
					{
						return Status;
					}
					if (Stage == EArdaRHIShaderStage::ClosestHit)
					{
						return AssignShader(Existing->mClosestHitShader, S.mShader, Stage);
					}
					if (Stage == EArdaRHIShaderStage::AnyHit)
					{
						return AssignShader(Existing->mAnyHitShader, S.mShader, Stage);
					}
					Existing->mbProceduralPrimitive = true;
					return AssignShader(Existing->mIntersectionShader, S.mShader, Stage);
				}
				if (S.mExportName.empty() || !S.mHitGroupName.empty())
				{
					return Invalid("Ray-generation, miss and callable contributions require a general export name.");
				}
				auto Existing = eastl::find_if(D.mShaders.begin(),
				    D.mShaders.end(),
				    [&](const auto& Export)
				    {
					    return Export.mExportName == S.mExportName;
				    });
				if (Existing == D.mShaders.end())
				{
					D.mShaders.push_back({S.mExportName, S.mShader, S.mLocalBindingLayout});
					return {};
				}
				if (auto Status = MergeLocalLayout(Existing->mLocalBindingLayout, S.mLocalBindingLayout); !Status)
				{
					return Status;
				}
				return AssignShader(Existing->mShader, S.mShader, Stage);
			}
			default:
				return Invalid("Unknown pipeline kind.");
			}
		}

		void ClearStages(FArdaInductorPipelineConfiguration& C)
		{
			GlobalLayouts(C).clear();
			C.mCompute.mDesc.mComputeShader.Reset();
			auto& G = C.mGraphics.mDesc;
			G.mVertexShader.Reset();
			G.mHullShader.Reset();
			G.mDomainShader.Reset();
			G.mGeometryShader.Reset();
			G.mPixelShader.Reset();
			auto& M = C.mMeshlet.mDesc;
			M.mAmplificationShader.Reset();
			M.mMeshShader.Reset();
			M.mPixelShader.Reset();
			C.mRayTracing.mDesc.mShaders.clear();
			C.mRayTracing.mDesc.mHitGroups.clear();
			C.mWorkGraph.mDesc.mShaders.clear();
		}

		FArdaRHIStatus MergeConfiguration(FArdaInductorPipelineConfiguration& Out,
		    bool& bHaveSettings,
		    const FArdaInductorPipelineConfiguration& Source)
		{
			if (Out.mKind != Source.mKind)
			{
				return Invalid("A pipeline configuration disagrees with the requested pipeline kind.");
			}
			if (bHaveSettings && ConfigurationKey(Out, false).mBytes != ConfigurationKey(Source, false).mBytes)
			{
				return Invalid("Reachable pipeline stages supply incompatible fixed-function or program settings.");
			}
			if (!bHaveSettings)
			{
				Out = Source;
				ClearStages(Out);
				bHaveSettings = true;
			}
			auto Copy = Source;
			if (auto Status = MergeLayouts(GlobalLayouts(Out), GlobalLayouts(Copy)); !Status)
			{
				return Status;
			}
			auto Add = [&](const FArdaRHIShaderRef& Shader,
			               EArdaRHIShaderStage Expected,
			               const eastl::string& Export = {},
			               const eastl::string& Hit = {},
			               const FArdaRHIBindingLayoutRef& Local = {}) -> FArdaRHIStatus
			{
				if (!Shader)
				{
					return {};
				}
				if (Shader->GetStage() != Expected)
				{
					return Invalid("A pipeline configuration puts a shader in the wrong stage.");
				}
				FArdaInductorPipelineContribution C;
				C.mShader = Shader;
				C.mExportName = Export;
				C.mHitGroupName = Hit;
				C.mLocalBindingLayout = Local;
				return AddShader(Out, C);
			};
			switch (Out.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				return Add(Source.mCompute.mDesc.mComputeShader, EArdaRHIShaderStage::Compute);
			case EArdaPipelineStateKind::Graphics:
			{
				const auto& D = Source.mGraphics.mDesc;
				for (const auto& Pair :
				    {eastl::pair<FArdaRHIShaderRef, EArdaRHIShaderStage>{D.mVertexShader, EArdaRHIShaderStage::Vertex},
				        {D.mHullShader, EArdaRHIShaderStage::Hull},
				        {D.mDomainShader, EArdaRHIShaderStage::Domain},
				        {D.mGeometryShader, EArdaRHIShaderStage::Geometry},
				        {D.mPixelShader, EArdaRHIShaderStage::Pixel}})
				{
					if (auto Status = Add(Pair.first, Pair.second); !Status)
					{
						return Status;
					}
				}
				break;
			}
			case EArdaPipelineStateKind::Meshlet:
			{
				const auto& D = Source.mMeshlet.mDesc;
				for (const auto& Pair : {eastl::pair<FArdaRHIShaderRef, EArdaRHIShaderStage>{D.mAmplificationShader,
				                             EArdaRHIShaderStage::Amplification},
				         {D.mMeshShader, EArdaRHIShaderStage::Mesh},
				         {D.mPixelShader, EArdaRHIShaderStage::Pixel}})
				{
					if (auto Status = Add(Pair.first, Pair.second); !Status)
					{
						return Status;
					}
				}
				break;
			}
			case EArdaPipelineStateKind::RayTracing:
				for (const auto& S : Source.mRayTracing.mDesc.mShaders)
				{
					if (!S.mShader)
					{
						return Invalid("A ray export cannot contain a null shader.");
					}
					if (auto Status = Add(S.mShader, S.mShader->GetStage(), S.mExportName, {}, S.mLocalBindingLayout);
					    !Status)
					{
						return Status;
					}
				}
				for (const auto& G : Source.mRayTracing.mDesc.mHitGroups)
				{
					if (G.mExportName.empty() || (!G.mClosestHitShader && !G.mAnyHitShader && !G.mIntersectionShader))
					{
						return Invalid("A configured ray hit group requires a name and at least one shader.");
					}
					if (G.mbProceduralPrimitive != bool(G.mIntersectionShader))
					{
						return Invalid("Procedural hit-group settings must match the intersection shader.");
					}
					for (const auto& Pair : {eastl::pair<FArdaRHIShaderRef, EArdaRHIShaderStage>{G.mClosestHitShader,
					                             EArdaRHIShaderStage::ClosestHit},
					         {G.mAnyHitShader, EArdaRHIShaderStage::AnyHit},
					         {G.mIntersectionShader, EArdaRHIShaderStage::Intersection}})
					{
						if (auto Status = Add(Pair.first, Pair.second, {}, G.mExportName, G.mLocalBindingLayout);
						    !Status)
						{
							return Status;
						}
					}
				}
				break;
			case EArdaPipelineStateKind::WorkGraph:
				for (const auto& S : Source.mWorkGraph.mDesc.mShaders)
				{
					if (!S)
					{
						return Invalid("A work-graph configuration cannot contain a null shader.");
					}
					if (auto Status = Add(S, EArdaRHIShaderStage::WorkGraph); !Status)
					{
						return Status;
					}
				}
				break;
			default:
				return Invalid("Unknown pipeline kind.");
			}
			return {};
		}

		FArdaRHIStatus ValidatePattern(FArdaInductorPipelineConfiguration& C)
		{
			switch (C.mKind)
			{
			case EArdaPipelineStateKind::Compute:
				return Validate(C.mCompute.mDesc);
			case EArdaPipelineStateKind::Graphics:
			{
				auto D = C.mGraphics.mDesc;
				if (bool(D.mHullShader) != bool(D.mDomainShader) ||
				    (bool(D.mHullShader) != (D.mTopology == EArdaRHIPrimitiveTopology::PatchList)))
				{
					return Invalid("Tessellation requires paired hull/domain shaders and patch-list topology.");
				}
				if (!D.mSampleCount)
				{
					D.mSampleCount = 1; // Attachment-derived samples are validated at resolution.
				}
				return Validate(D);
			}
			case EArdaPipelineStateKind::Meshlet:
			{
				auto D = C.mMeshlet.mDesc;
				if (!D.mSampleCount)
				{
					D.mSampleCount = 1;
				}
				return Validate(D);
			}
			case EArdaPipelineStateKind::RayTracing:
			{
				auto& D = C.mRayTracing.mDesc;
				eastl::sort(D.mShaders.begin(),
				    D.mShaders.end(),
				    [](const auto& A, const auto& B)
				    {
					    return A.mExportName < B.mExportName;
				    });
				eastl::sort(D.mHitGroups.begin(),
				    D.mHitGroups.end(),
				    [](const auto& A, const auto& B)
				    {
					    return A.mExportName < B.mExportName;
				    });
				if (eastl::none_of(D.mShaders.begin(),
				        D.mShaders.end(),
				        [](const auto& S)
				        {
					        return S.mShader->GetStage() == EArdaRHIShaderStage::RayGeneration;
				        }))
				{
					return Invalid("A ray pipeline requires a ray-generation shader.");
				}
				for (const auto& G : D.mHitGroups)
				{
					if (eastl::any_of(D.mShaders.begin(),
					        D.mShaders.end(),
					        [&](const auto& S)
					        {
						        return S.mExportName == G.mExportName;
					        }))
					{
						return Invalid("Ray general exports and hit groups must have distinct names.");
					}
				}
				return Validate(D);
			}
			case EArdaPipelineStateKind::WorkGraph:
			{
				auto& D = C.mWorkGraph.mDesc;
				if (D.mProgramName.empty() || D.mEntryPoint.empty() || !D.mMaxInputRecords || D.mShaders.empty())
				{
					return Invalid(
					    "A work-graph pipeline requires program, entry point, shaders and non-zero record capacity.");
				}
				eastl::sort(D.mShaders.begin(),
				    D.mShaders.end(),
				    [](const auto& A, const auto& B)
				    {
					    return A->GetPersistentCacheHash() < B->GetPersistentCacheHash();
				    });
				return {};
			}
			default:
				return Invalid("Unknown pipeline kind.");
			}
		}
	}

	TArdaRHIResult<FArdaInductorPipelinePattern> InferArdaInductorPipeline(
	    const eastl::vector<FArdaInductorPipelineNode>& Nodes,
	    const FArdaInductorPipelineRequest& Request)
	{
		std::unordered_map<uint64_t, const FArdaInductorPipelineNode*> Lookup;
		for (const auto& Node : Nodes)
		{
			if (!Lookup.emplace(Node.mNodeId, &Node).second)
			{
				return {{}, Invalid("Pipeline topology has duplicate node IDs.")};
			}
		}
		std::unordered_map<uint64_t, uint8_t> States;
		eastl::vector<eastl::pair<uint64_t, bool>> Pending{{Request.mTerminalNodeId, false}};
		while (!Pending.empty())
		{
			const auto [Id, bLeave] = Pending.back();
			Pending.pop_back();
			const auto Found = Lookup.find(Id);
			if (Found == Lookup.end())
			{
				return {{}, Invalid("Pipeline topology references a missing node.")};
			}
			if (bLeave)
			{
				States[Id] = 2;
				continue;
			}
			if (States[Id] == 2)
			{
				continue;
			}
			if (States[Id] == 1)
			{
				return {{}, Invalid("Pipeline topology contains a dependency cycle.")};
			}
			States[Id] = 1;
			Pending.push_back({Id, true});
			for (auto Dependency : Found->second->mDependencies)
			{
				Pending.push_back({Dependency, false});
			}
		}
		// Validate the complete ancestry above, even across completed consumers. Stage
		// collection has narrower semantics: an upstream same-family request owns its
		// pipeline, so its shaders/settings do not become part of the next dispatch/draw.
		States.clear();
		eastl::vector<uint64_t> Candidates{Request.mTerminalNodeId};
		eastl::vector<const FArdaInductorPipelineNode*> Reachable;
		while (!Candidates.empty())
		{
			const uint64_t Id = Candidates.back();
			Candidates.pop_back();
			if (!States.emplace(Id, 1).second)
			{
				continue;
			}
			const auto* Node = Lookup.at(Id);
			if (Id != Request.mTerminalNodeId &&
			    eastl::find(Node->mPipelineBoundaries.begin(), Node->mPipelineBoundaries.end(), Request.mKind) !=
			        Node->mPipelineBoundaries.end())
			{
				continue;
			}
			Reachable.push_back(Node);
			Candidates.insert(Candidates.end(), Node->mDependencies.begin(), Node->mDependencies.end());
		}
		eastl::sort(Reachable.begin(),
		    Reachable.end(),
		    [](const auto* A, const auto* B)
		    {
			    return A->mNodeId < B->mNodeId;
		    });
		auto Eligible = [&](const FArdaInductorPipelineContribution& C)
		{
			return (C.mShader && Matches(Request.mKind, C.mShader->GetStage())) ||
			    (C.mConfiguration && C.mConfiguration->mKind == Request.mKind);
		};
		eastl::string Group = Request.mGroup;
		if (Group.empty())
		{
			for (const auto* Node : Reachable)
			{
				for (const auto& C : Node->mContributions)
				{
					if (Eligible(C) && !C.mGroup.empty())
					{
						if (!Group.empty() && Group != C.mGroup)
						{
							return {{},
							    Invalid("Multiple reachable pipeline groups require an explicit request group.")};
						}
						Group = C.mGroup;
					}
				}
			}
		}
		FArdaInductorPipelinePattern Pattern;
		Pattern.mConfiguration.mKind = Request.mKind;
		bool bHaveSettings = false;
		if (Request.mConfiguration)
		{
			if (auto Status = MergeConfiguration(Pattern.mConfiguration, bHaveSettings, *Request.mConfiguration);
			    !Status)
			{
				return {{}, Status};
			}
		}
		// Merge fixed settings before adding standalone stages so discovering a late settings
		// anchor cannot discard shaders already collected from another branch.
		for (const auto* Node : Reachable)
		{
			for (const auto& C : Node->mContributions)
			{
				if (Eligible(C) && (C.mGroup.empty() || C.mGroup == Group) && C.mConfiguration)
				{
					if (auto Status = MergeConfiguration(Pattern.mConfiguration, bHaveSettings, *C.mConfiguration);
					    !Status)
					{
						return {{}, Status};
					}
				}
			}
		}
		for (const auto* Node : Reachable)
		{
			bool bContributed = false;
			for (const auto& C : Node->mContributions)
			{
				if (!Eligible(C) || (!C.mGroup.empty() && C.mGroup != Group))
				{
					continue;
				}
				if (auto Status = MergeLayouts(GlobalLayouts(Pattern.mConfiguration), C.mBindingLayouts); !Status)
				{
					return {{}, Status};
				}
				if (auto Status = AddShader(Pattern.mConfiguration, C); !Status)
				{
					return {{}, Status};
				}
				bContributed = true;
			}
			if (bContributed)
			{
				Pattern.mContributingNodeIds.push_back(Node->mNodeId);
			}
		}
		if (auto Status = ValidatePattern(Pattern.mConfiguration); !Status)
		{
			return {{}, Status};
		}
		const auto Key = ConfigurationKey(Pattern.mConfiguration);
		if (!Key.mbValid)
		{
			return {{}, Invalid("Pipeline semantics lack a stable shader identity or use an unknown pipeline kind.")};
		}
		Pattern.mStableKey = Key.Finish();
		return {eastl::move(Pattern), {}};
	}

	TArdaRHIResult<FArdaInductorResolvedPipeline> ResolveArdaInductorPipeline(FArdaPipelineStateCache& Cache,
	    const FArdaInductorPipelinePattern& Pattern,
	    const FArdaRHIFramebufferRef& Framebuffer)
	{
		const auto Key = ConfigurationKey(Pattern.mConfiguration);
		if (!Key.mbValid || !Pattern.mStableKey || Pattern.mStableKey != Key.Finish())
		{
			return {{}, Invalid("Pipeline pattern was modified after inference or has no stable identity.")};
		}
		FArdaInductorResolvedPipeline Result;
		Result.mKind = Pattern.mConfiguration.mKind;
		Result.mStableKey = Pattern.mStableKey;
		FArdaRHIStatus Status;
		const auto& C = Pattern.mConfiguration;
		switch (Result.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			Status = Cache.GetOrCreateCompute(C.mCompute, Result.mCompute);
			break;
		case EArdaPipelineStateKind::Graphics:
			Status = Cache.GetOrCreateGraphics(C.mGraphics, Framebuffer, Result.mGraphics);
			break;
		case EArdaPipelineStateKind::Meshlet:
			Status = Cache.GetOrCreateMeshlet(C.mMeshlet, Framebuffer, Result.mMeshlet);
			break;
		case EArdaPipelineStateKind::RayTracing:
			Status = Cache.GetOrCreateRayTracing(C.mRayTracing, Result.mRayTracing);
			break;
		case EArdaPipelineStateKind::WorkGraph:
			Status = Cache.GetOrCreateWorkGraph(C.mWorkGraph, Result.mWorkGraph);
			break;
		default:
			return {{}, Invalid("Unknown pipeline kind.")};
		}
		if (!Status)
		{
			return {{}, Status};
		}
		auto Concrete = C;
		switch (Result.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			Concrete.mCompute.mDesc = Result.mCompute->GetDesc();
			break;
		case EArdaPipelineStateKind::Graphics:
			Concrete.mGraphics.mDesc = Result.mGraphics->GetDesc();
			break;
		case EArdaPipelineStateKind::Meshlet:
			Concrete.mMeshlet.mDesc = Result.mMeshlet->GetDesc();
			break;
		case EArdaPipelineStateKind::RayTracing:
			Concrete.mRayTracing.mDesc = Result.mRayTracing->GetDesc();
			break;
		case EArdaPipelineStateKind::WorkGraph:
			Concrete.mWorkGraph.mDesc = Result.mWorkGraph->GetDesc();
			break;
		}
		Result.mStableKey = ConfigurationKey(Concrete).Finish();
		return {eastl::move(Result), {}};
	}
}
