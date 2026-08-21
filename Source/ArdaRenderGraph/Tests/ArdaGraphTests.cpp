#include "ArdaRenderGraph.h"
#include "ArdaRenderGraphAllocator.h"
#include "ArdaRenderGraphRegistry.h"
#include "ArdaBackend.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <EASTL/string.h>
#include <EASTL/type_traits.h>
#include <EASTL/vector.h>

namespace
{
    using namespace arda::render_graph;
    namespace rhi = arda::rhi;

    bool ConfigureLinkedBackend(
        arda::backend::FArdaBackendConfiguration& Configuration)
    {
        const auto Modules = arda::backend::EnumerateBackendModules();
        if (Modules.empty())
            return false;
        Configuration.mBackendName = Modules.front().mName;
        Configuration.mBackend = Modules.front().mBackendType;
        return arda::backend::ConfigureBackend(Configuration);
    }

    struct FARDGTestHandleTag final
    {
    };

    using FARDGTestHandle = TARDGHandle<FARDGTestHandleTag>;

#if GTEST_HAS_DEATH_TEST
    #define EXPECT_FATAL_CHECK(Statement, MessageFragment) \
        EXPECT_DEATH( \
            { Statement; }, \
            MessageFragment)
#else
    #define EXPECT_FATAL_CHECK(Statement, MessageFragment) \
        GTEST_SKIP() << "Death tests are unavailable on this platform."
#endif

    struct FARDGRegistryRecord
    {
        FARDGRegistryRecord(FARDGTestHandle Handle, int Value)
            : mHandle(Handle)
            , mValue(Value)
        {
        }

        FARDGTestHandle mHandle;
        int mValue = 0;
    };

    struct FARDGTrackedObject
    {
        FARDGTrackedObject(int Identifier, eastl::vector<int>& DestructionOrder)
            : mIdentifier(Identifier)
            , mDestructionOrder(DestructionOrder)
        {
        }

        ~FARDGTrackedObject()
        {
            mDestructionOrder.push_back(mIdentifier);
        }

        int mIdentifier = 0;
        eastl::vector<int>& mDestructionOrder;
    };

    struct alignas(128) FARDGAlignedObject
    {
        uint64_t mValue = 0;
    };

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGInnerParameters)
        ARDG_PARAMETER(float, mScale)
        ARDG_TEXTURE(mTexture)
    ARDG_END_PARAMETER_STRUCT()

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGOuterParameters)
        ARDG_PARAMETER(uint32_t, mFrameIndex)
        ARDG_PARAMETER_STRUCT(FARDGInnerParameters, mInner)
        ARDG_PARAMETER_STRUCT_ARRAY(FARDGInnerParameters, mLayers, 2)
        ARDG_BUFFER_UAV(mOutput)
        ARDG_RENDER_TARGET_BINDING_SLOTS(mRenderTargets)
    ARDG_END_PARAMETER_STRUCT()

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGTextureAccessParameters)
        ARDG_TEXTURE_ACCESS(mInput)
        ARDG_TEXTURE_ACCESS(mOutput)
    ARDG_END_PARAMETER_STRUCT()

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGRasterParameters)
        ARDG_RENDER_TARGET_BINDING_SLOTS(mRenderTargets)
    ARDG_END_PARAMETER_STRUCT()

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGBufferAccessParameters)
        ARDG_BUFFER_ACCESS(mBuffer)
    ARDG_END_PARAMETER_STRUCT()

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGAccelStructAccessParameters)
        ARDG_ACCEL_STRUCT_ACCESS(mAccelStruct)
    ARDG_END_PARAMETER_STRUCT()

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGViewAndUniformParameters)
        ARDG_TEXTURE_SRV(mInput)
        ARDG_TEXTURE_UAV(mOutput)
        ARDG_UNIFORM_BUFFER(mUniformBuffer)
    ARDG_END_PARAMETER_STRUCT()

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGBindingSetParameters)
        ARDG_TEXTURE_UAV(mTexture)
        ARDG_BUFFER_UAV(mBuffer)
    ARDG_END_PARAMETER_STRUCT()

    ARDG_BEGIN_PARAMETER_STRUCT(FARDGRegisteredBindingParameters)
        ARDG_BUFFER_UAV(mFirst)
        ARDG_BUFFER_UAV(mSecond)
    ARDG_END_PARAMETER_STRUCT()

    ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FARDGRegisteredShaderParameters)
        ARDA_SHADER_BUFFER_UAV(
            mFirst, 2, 3, rhi::EArdaRHIShaderStage::Compute)
        ARDA_SHADER_BUFFER_UAV(
            mSecond, 5, 0, rhi::EArdaRHIShaderStage::Compute)
    ARDA_END_SHADER_PARAMETER_STRUCT()

    const arda::backend::FArdaShaderParameterMetadata*
    GetRegisteredShaderMetadata()
    {
        return &FARDGRegisteredShaderParameters::GetStaticMetadata();
    }

    struct FARDGBlackboardValue
    {
        uint32_t mFrameIndex = 0;
    };

    class FARDGTestTexture final : public rhi::IArdaRHITexture
    {
    public:
        explicit FARDGTestTexture(rhi::FArdaRHITextureDesc Desc)
            : mDesc(eastl::move(Desc))
        {
        }

        void AddRef() noexcept override
        {
            ++mRefCount;
        }

        void Release() noexcept override
        {
            const unsigned long RefCount = --mRefCount;
            if (RefCount == 0)
            {
                delete this;
            }
        }

        rhi::EArdaRHIResourceType GetResourceType() const noexcept override
        {
            return rhi::EArdaRHIResourceType::Texture;
        }

        const char* GetDebugName() const noexcept override
        {
            return mDesc.mDebugName.c_str();
        }

        const rhi::FArdaRHITextureDesc& GetDesc() const noexcept override
        {
            return mDesc;
        }

        const void* GetPhysicalIdentity() const noexcept override
        {
            return this;
        }

    private:
        unsigned long mRefCount = 0;
        rhi::FArdaRHITextureDesc mDesc;
    };

    class FARDGTestAccelStruct final : public rhi::IArdaRHIAccelStruct
    {
    public:
        explicit FARDGTestAccelStruct(rhi::FArdaRHIAccelStructDesc Desc)
            : mDesc(eastl::move(Desc)) {}
        void AddRef() noexcept override { ++mRefCount; }
        void Release() noexcept override { if (--mRefCount == 0) delete this; }
        rhi::EArdaRHIResourceType GetResourceType() const noexcept override
        {
            return rhi::EArdaRHIResourceType::AccelStruct;
        }
        const char* GetDebugName() const noexcept override
        {
            return mDesc.mDebugName.c_str();
        }
        const rhi::FArdaRHIAccelStructDesc& GetDesc() const noexcept override
        {
            return mDesc;
        }
        bool IsCompacted() const noexcept override { return false; }
        uint64_t GetDeviceAddress() const noexcept override { return 0x1000; }
        const void* GetPhysicalIdentity() const noexcept override { return this; }
    private:
        unsigned long mRefCount = 0;
        rhi::FArdaRHIAccelStructDesc mDesc;
    };

    static_assert(sizeof(FARDGPassHandle) == sizeof(uint32_t));
    static_assert(eastl::is_trivially_copyable_v<FARDGPassHandle>);
    static_assert(!eastl::is_convertible_v<FARDGPassHandle, FARDGTextureHandle>);
}

TEST(ArdaRenderGraph, ReportsModuleName)
{
    EXPECT_STREQ(arda::render_graph::GetModuleName(), "ArdaRenderGraph");
}

TEST(ArdaRenderGraph, TypedHandlesAreCompactStableAndDistinct)
{
    using namespace arda::render_graph;

    const FARDGPassHandle Invalid;
    const FARDGPassHandle First(0);
    const FARDGPassHandle Second(1);

    EXPECT_FALSE(Invalid.IsValid());
    EXPECT_EQ(Invalid.GetIndex(), FARDGPassHandle::InvalidIndex);
    EXPECT_TRUE(First.IsValid());
    EXPECT_EQ(First.GetIndex(), 0u);
    EXPECT_NE(First, Second);
    EXPECT_LT(First, Second);

    TARDGHandleHasher<FARDGPassHandleTag> Hasher;
    EXPECT_EQ(Hasher(First), eastl::hash<uint32_t>{}(0u));
}

TEST(ArdaRenderGraph, AccelStructHandleResourceAndTransitionPath)
{
    using namespace arda::render_graph;

    rhi::FArdaRHIAccelStructDesc Desc;
    Desc.mDebugName = "SceneTLAS";
    Desc.mbTopLevel = true;
    Desc.mTopLevelMaxInstances = 4;
    rhi::FArdaRHIAccelStructRef Physical(new FARDGTestAccelStruct(Desc));

    FARDGBuilder Builder;
    FARDGAccelStructRef Logical = Builder.RegisterExternalAccelStruct(
        Physical, rhi::EArdaRHIResourceState::AccelStructRead);
    ASSERT_NE(Logical, nullptr);
    EXPECT_EQ(Builder.TryGetAccelStruct(Logical->GetHandle()), Logical);
    EXPECT_EQ(Logical->GetAccelStruct(), Physical);

    FARDGAccelStructAccessParameters Parameters;
    Parameters.mAccelStruct.mAccelStruct = Logical;
    Parameters.mAccelStruct.mState =
        rhi::EArdaRHIResourceState::AccelStructWrite;
    const FARDGPassHandle Build = Builder.AddPass(
        "BuildTLAS", &Parameters, EARDGPassFlags::Compute, [] {});
    Logical->SetFinalState(rhi::EArdaRHIResourceState::AccelStructRead);

    const FARDGCompileResult& Result = Builder.Compile();
    ASSERT_FALSE(Result.mExecutionOrder.empty());
    const auto& BuildTransitions =
        Builder.TryGetPass(Build)->GetState().mAccelStructTransitions;
    ASSERT_EQ(BuildTransitions.size(), 1u);
    EXPECT_EQ(
        BuildTransitions[0].mStateBefore,
        rhi::EArdaRHIResourceState::AccelStructRead);
    EXPECT_EQ(
        BuildTransitions[0].mStateAfter,
        rhi::EArdaRHIResourceState::AccelStructWrite);
    const auto& FinalTransitions =
        Builder.TryGetPass(Result.mEpilogue)->GetState().mAccelStructTransitions;
    ASSERT_EQ(FinalTransitions.size(), 1u);
    EXPECT_EQ(
        FinalTransitions[0].mStateAfter,
        rhi::EArdaRHIResourceState::AccelStructRead);
}

TEST(ArdaRenderGraph, ArenaHonorsAlignmentAndDestroysInReverseOrder)
{
    using namespace arda::render_graph;

    eastl::vector<int> DestructionOrder;
    FARDGArena Arena(64);
    FARDGTrackedObject* First = Arena.Allocate<FARDGTrackedObject>(1, DestructionOrder);
    FARDGAlignedObject* Aligned = Arena.Allocate<FARDGAlignedObject>();
    FARDGTrackedObject* Second = Arena.Allocate<FARDGTrackedObject>(2, DestructionOrder);

    ASSERT_NE(First, nullptr);
    ASSERT_NE(Second, nullptr);
    ASSERT_NE(Aligned, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(Aligned) % alignof(FARDGAlignedObject), 0u);
    EXPECT_EQ(Arena.GetObjectCount(), 3u);
    EXPECT_GE(Arena.GetBlockCount(), 2u);

    Arena.Reset();

    EXPECT_EQ(DestructionOrder, (eastl::vector<int>{2, 1}));
    EXPECT_EQ(Arena.GetObjectCount(), 0u);
    EXPECT_EQ(Arena.GetBlockCount(), 0u);
}

TEST(ArdaRenderGraph, RegistryAssignsDenseHandlesAndRejectsInvalidLookup)
{
    using namespace arda::render_graph;

    FARDGArena Arena;
    TARDGHandleRegistry<FARDGRegistryRecord, FARDGTestHandle> Registry(Arena);

    const FARDGTestHandle First = Registry.Emplace(17);
    const FARDGTestHandle Second = Registry.Emplace(29);

    EXPECT_EQ(First.GetIndex(), 0u);
    EXPECT_EQ(Second.GetIndex(), 1u);
    EXPECT_EQ(Registry.GetCount(), 2u);
    EXPECT_EQ(Registry.Get(First).mHandle, First);
    EXPECT_EQ(Registry.Get(Second).mValue, 29);
    EXPECT_EQ(Registry.TryGet(FARDGTestHandle()), nullptr);
    EXPECT_EQ(Registry.TryGet(FARDGTestHandle(99)), nullptr);
    EXPECT_FATAL_CHECK(
        (void)Registry.Get(FARDGTestHandle(99)),
        "Invalid render-graph registry handle");
}

TEST(ArdaRenderGraph, LogicalResourcesRetainArdaDescriptorsAndViewRanges)
{
    using namespace arda::render_graph;

    rhi::FArdaRHITextureDesc TextureDesc;
    TextureDesc.mDebugName = "SceneColor";
    TextureDesc.mWidth = 1920;
    TextureDesc.mHeight = 1080;
    TextureDesc.mMipLevels = 5;
    TextureDesc.mInitialState = rhi::EArdaRHIResourceState::RenderTarget;

    FARDGTexture Texture(
        FARDGTextureHandle(3),
        TextureDesc,
        EARDGResourceFlags::External | EARDGResourceFlags::Extracted);

    EXPECT_STREQ(Texture.GetName().c_str(), "SceneColor");
    EXPECT_EQ(Texture.GetDesc().mWidth, 1920u);
    EXPECT_EQ(Texture.GetDesc().mMipLevels, 5u);
    EXPECT_EQ(Texture.GetTexture().Get(), nullptr);
    EXPECT_TRUE(Texture.IsExternal());
    EXPECT_TRUE(Texture.IsExtracted());
    EXPECT_EQ(Texture.GetInitialState(), rhi::EArdaRHIResourceState::RenderTarget);

    FARDGTextureViewDesc ViewDesc;
    ViewDesc.mTexture = Texture.GetHandle();
    ViewDesc.mSubresources = {2, 1, 0, 1};
    ViewDesc.mFormat = rhi::EArdaRHIFormat::RGBA16Float;
    FARDGTextureSRV View(FARDGViewHandle(4), "SceneColorMip2", ViewDesc);

    EXPECT_EQ(View.GetDesc().mTexture, Texture.GetHandle());
    EXPECT_EQ(View.GetDesc().mSubresources.mBaseMipLevel, 2u);
    EXPECT_EQ(View.GetDesc().mFormat, rhi::EArdaRHIFormat::RGBA16Float);

    rhi::FArdaRHIBufferDesc BufferDesc;
    BufferDesc.mDebugName = "LightList";
    BufferDesc.mByteSize = 4096;
    BufferDesc.mStructureStride = 16;
    BufferDesc.mInitialState = rhi::EArdaRHIResourceState::UnorderedAccess;
    FARDGBuffer Buffer(FARDGBufferHandle(7), BufferDesc);
    EXPECT_EQ(Buffer.GetDesc().mByteSize, 4096u);
    EXPECT_EQ(Buffer.GetBuffer().Get(), nullptr);

    FARDGBufferViewDesc BufferViewDesc;
    BufferViewDesc.mBuffer = Buffer.GetHandle();
    BufferViewDesc.mRange = {256, 512};
    FARDGBufferUAV BufferView(FARDGViewHandle(8), "LightListRange", BufferViewDesc);
    EXPECT_EQ(BufferView.GetDesc().mBuffer, Buffer.GetHandle());
    EXPECT_EQ(BufferView.GetDesc().mRange.mByteOffset, 256u);
    EXPECT_EQ(BufferView.GetDesc().mRange.mByteSize, 512u);

    FARDGOuterParameters Parameters;
    rhi::FArdaRHIBufferDesc UniformDesc;
    UniformDesc.mDebugName = "PassConstants";
    UniformDesc.mByteSize = sizeof(Parameters);
    UniformDesc.mUsage = rhi::EArdaRHIBufferUsage::Constant;
    FARDGUniformBuffer UniformBuffer(
        FARDGUniformBufferHandle(2),
        "PassConstants",
        UniformDesc,
        &FARDGOuterParameters::GetStaticMetadata(),
        &Parameters);
    EXPECT_EQ(UniformBuffer.GetMetadata(), &FARDGOuterParameters::GetStaticMetadata());
    EXPECT_EQ(UniformBuffer.GetContents(), &Parameters);
    EXPECT_EQ(UniformBuffer.GetBuffer().Get(), nullptr);
}

TEST(ArdaRenderGraph, PassBaseStateTracksPipelineDependenciesAndResourceStates)
{
    using namespace arda::render_graph;

    FARDGPass Pass(
        FARDGPassHandle(2),
        "Async Lighting",
        EARDGPassFlags::Compute | EARDGPassFlags::AsyncCompute);

    Pass.AddProducer(FARDGPassHandle(0));
    Pass.AddProducer(FARDGPassHandle(0));
    Pass.AddProducer(FARDGPassHandle());
    Pass.AddTextureState(
        {FARDGTextureHandle(1), {}, rhi::EArdaRHIResourceState::ShaderResource});
    Pass.AddBufferState(
        {FARDGBufferHandle(2), {}, rhi::EArdaRHIResourceState::UnorderedAccess});

    EXPECT_EQ(Pass.GetState().mPipeline, EARDGPipeline::AsyncCompute);
    ASSERT_EQ(Pass.GetState().mProducers.size(), 1u);
    EXPECT_EQ(Pass.GetState().mProducers.front(), FARDGPassHandle(0));
    ASSERT_EQ(Pass.GetState().mTextureStates.size(), 1u);
    EXPECT_EQ(
        Pass.GetState().mTextureStates.front().mState,
        rhi::EArdaRHIResourceState::ShaderResource);
    ASSERT_EQ(Pass.GetState().mBufferStates.size(), 1u);
    EXPECT_EQ(
        Pass.GetState().mBufferStates.front().mState,
        rhi::EArdaRHIResourceState::UnorderedAccess);
}

TEST(ArdaRenderGraph, ParameterMetadataPreservesMemberOrderTypesAndDefaults)
{
    using namespace arda::render_graph;

    const FARDGParameterMetadata& Metadata = FARDGOuterParameters::GetStaticMetadata();
    ASSERT_EQ(Metadata.GetMembers().size(), 5u);
    EXPECT_STREQ(Metadata.GetName(), "FARDGOuterParameters");
    EXPECT_EQ(Metadata.GetSize(), sizeof(FARDGOuterParameters));
    EXPECT_STREQ(Metadata.GetMembers()[0].mName, "mFrameIndex");
    EXPECT_STREQ(Metadata.GetMembers()[1].mName, "mInner");
    EXPECT_STREQ(Metadata.GetMembers()[2].mName, "mLayers");
    EXPECT_EQ(Metadata.GetMembers()[1].mType, EARDGParameterType::NestedStruct);
    EXPECT_EQ(Metadata.GetMembers()[2].mElementCount, 2u);
    EXPECT_EQ(
        Metadata.GetMembers()[3].mDefaultState,
        rhi::EArdaRHIResourceState::UnorderedAccess);
    EXPECT_EQ(
        Metadata.GetMembers()[4].mType,
        EARDGParameterType::RenderTargetBindingSlots);
    EXPECT_EQ(Metadata.FindMember("mMissing"), nullptr);
    EXPECT_EQ(Metadata.FindMember("mOutput"), &Metadata.GetMembers()[3]);
}

TEST(ArdaRenderGraph, ParameterEnumerationRecursesThroughNestedStructsAndArrays)
{
    using namespace arda::render_graph;

    FARDGOuterParameters Parameters;
    Parameters.mFrameIndex = 42;
    Parameters.mInner.mScale = 1.5f;
    Parameters.mLayers[0].mScale = 2.0f;
    Parameters.mLayers[1].mScale = 3.0f;

    eastl::vector<eastl::string> Paths;
    eastl::vector<float> Scales;
    FARDGOuterParameters::GetStaticMetadata().Enumerate(
        &Parameters,
        [&Paths, &Scales](const FARDGParameter& Parameter)
        {
            Paths.push_back(Parameter.mPath);
            if (Parameter.mPath.find("mScale") != eastl::string::npos)
            {
                Scales.push_back(Parameter.GetValue<float>());
            }
        });

    EXPECT_EQ(
        Paths,
        (eastl::vector<eastl::string>{
            "mFrameIndex",
            "mInner.mScale",
            "mInner.mTexture",
            "mLayers[0].mScale",
            "mLayers[0].mTexture",
            "mLayers[1].mScale",
            "mLayers[1].mTexture",
            "mOutput",
            "mRenderTargets"}));
    EXPECT_EQ(Scales, (eastl::vector<float>{1.5f, 2.0f, 3.0f}));

    size_t NestedContainerCount = 0;
    FARDGOuterParameters::GetStaticMetadata().Enumerate(
        &Parameters,
        [&NestedContainerCount](const FARDGParameter& Parameter)
        {
            if (Parameter.mMember->mType == EARDGParameterType::NestedStruct)
            {
                ++NestedContainerCount;
            }
        },
        true);
    EXPECT_EQ(NestedContainerCount, 3u);
}

TEST(ArdaRenderGraph, BuilderAllocatesParametersAndStoresTypedBlackboardValues)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    FARDGInnerParameters* Parameters =
        Builder.AllocateParameters<FARDGInnerParameters>();
    Parameters->mScale = 2.5f;

    EXPECT_NE(Parameters, nullptr);
    EXPECT_FLOAT_EQ(Parameters->mScale, 2.5f);
    EXPECT_FALSE(Builder.GetBlackboard().Contains<FARDGBlackboardValue>());

    FARDGBlackboardValue& Value =
        Builder.GetBlackboard().Emplace<FARDGBlackboardValue>();
    Value.mFrameIndex = 37;
    EXPECT_TRUE(Builder.GetBlackboard().Contains<FARDGBlackboardValue>());
    EXPECT_EQ(
        Builder.GetBlackboard().Get<FARDGBlackboardValue>().mFrameIndex,
        37u);
    EXPECT_FATAL_CHECK(
        (void)Builder.GetBlackboard().Get<eastl::string>(),
        "The requested render-graph blackboard value is absent");

    FARDGInnerParameters StackParameters;
    StackParameters.mScale = 4.0f;
    const FARDGPassHandle Pass = Builder.AddPass(
        "FrozenParameters",
        &StackParameters,
        EARDGPassFlags::None,
        [] {});
    StackParameters.mScale = 9.0f;
    const auto* FrozenParameters = static_cast<const FARDGInnerParameters*>(
        Builder.TryGetPass(Pass)->GetParameters());
    ASSERT_NE(FrozenParameters, nullptr);
    EXPECT_FLOAT_EQ(FrozenParameters->mScale, 4.0f);
}

TEST(ArdaRenderGraph, BuilderCreatesLogicalResourcesViewsAndExtractionDeclarations)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    rhi::FArdaRHITextureDesc TextureDesc;
    TextureDesc.mDebugName = "History";
    TextureDesc.mWidth = 64;
    TextureDesc.mHeight = 64;
    TextureDesc.mUsage |= rhi::EArdaRHITextureUsage::UnorderedAccess;
    FARDGTextureRef Texture = Builder.CreateTexture(TextureDesc);

    FARDGTextureViewDesc ViewDesc;
    ViewDesc.mTexture = Texture->GetHandle();
    ViewDesc.mSubresources = {1, 1, 0, 1};
    FARDGTextureUAVRef View =
        Builder.CreateTextureUAV("HistoryMip1", ViewDesc);

    rhi::FArdaRHIBufferDesc BufferDesc;
    BufferDesc.mDebugName = "Readback";
    BufferDesc.mByteSize = 1024;
    BufferDesc.mUsage = rhi::EArdaRHIBufferUsage::UnorderedAccess;
    FARDGBufferRef Buffer = Builder.CreateBuffer(BufferDesc);
    FARDGBufferViewDesc BufferViewDesc;
    BufferViewDesc.mBuffer = Buffer->GetHandle();
    FARDGBufferUAVRef BufferView =
        Builder.CreateBufferUAV("ReadbackUAV", BufferViewDesc);

    rhi::FArdaRHITextureRef ExtractedTexture;
    rhi::FArdaRHIBufferRef ExtractedBuffer;
    Builder.QueueTextureExtraction(
        Texture,
        ExtractedTexture,
        rhi::EArdaRHIResourceState::ShaderResource);
    Builder.QueueBufferExtraction(
        Buffer,
        ExtractedBuffer,
        rhi::EArdaRHIResourceState::CopySource);

    EXPECT_EQ(View->GetDesc().mTexture, Texture->GetHandle());
    EXPECT_EQ(BufferView->GetDesc().mBuffer, Buffer->GetHandle());
    EXPECT_TRUE(Texture->IsExtracted());
    EXPECT_TRUE(Buffer->IsExtracted());
    EXPECT_EQ(Builder.GetTextureExtractions().size(), 1u);
    EXPECT_EQ(Builder.GetBufferExtractions().size(), 1u);
    EXPECT_EQ(
        Texture->GetFinalState(),
        rhi::EArdaRHIResourceState::ShaderResource);
}

TEST(ArdaRenderGraph, BuilderDeduplicatesExternalImportsAndRootsExternalWrites)
{
    using namespace arda::render_graph;

    rhi::FArdaRHITextureDesc Desc;
    Desc.mDebugName = "SwapChain";
    Desc.mWidth = 128;
    Desc.mHeight = 72;
    Desc.mUsage |= rhi::EArdaRHITextureUsage::RenderTarget;
    rhi::FArdaRHITextureRef PhysicalTexture(new FARDGTestTexture(Desc));

    FARDGBuilder Builder;
    FARDGTextureRef First = Builder.RegisterExternalTexture(
        PhysicalTexture,
        rhi::EArdaRHIResourceState::Present);
    FARDGTextureRef Second = Builder.RegisterExternalTexture(
        PhysicalTexture,
        rhi::EArdaRHIResourceState::Present);
    EXPECT_EQ(First, Second);
    EXPECT_TRUE(First->IsExternal());
    EXPECT_EQ(First->GetTexture(), PhysicalTexture);
    EXPECT_EQ(First->GetInitialState(), rhi::EArdaRHIResourceState::Present);

    FARDGTextureAccessParameters ReadParameters;
    ReadParameters.mInput.mTexture = First;
    ReadParameters.mInput.mState = rhi::EArdaRHIResourceState::ShaderResource;
    const FARDGPassHandle DeadRead = Builder.AddPass(
        "DeadExternalRead",
        &ReadParameters,
        EARDGPassFlags::Compute,
        [] {});

    FARDGTextureAccessParameters Parameters;
    Parameters.mOutput.mTexture = First;
    Parameters.mOutput.mState = rhi::EArdaRHIResourceState::RenderTarget;
    const FARDGPassHandle Write = Builder.AddPass(
        "ExternalWrite",
        &Parameters,
        EARDGPassFlags::Raster,
        [] {});
    (void)Builder.Compile();

    EXPECT_TRUE(Builder.TryGetPass(DeadRead)->GetState().mbCulled);
    EXPECT_FALSE(Builder.TryGetPass(Write)->GetState().mbCulled);
    EXPECT_EQ(
        Builder.TryGetPass(Write)->GetState().mProducers,
        (eastl::vector<FARDGPassHandle>{Builder.GetProloguePass()}));
    EXPECT_EQ(
        Builder.TryGetPass(Write)->GetState().mSynchronizationProducers,
        (eastl::vector<FARDGPassHandle>{DeadRead}));
}

TEST(ArdaRenderGraph, CompilerTracksProducersCullsDeadPassesAndPreservesSentinels)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    rhi::FArdaRHITextureDesc Desc;
    Desc.mWidth = 16;
    Desc.mHeight = 16;
    Desc.mUsage |= rhi::EArdaRHITextureUsage::UnorderedAccess;
    Desc.mDebugName = "Intermediate";
    FARDGTextureRef Intermediate = Builder.CreateTexture(Desc);
    Desc.mDebugName = "Output";
    FARDGTextureRef Output = Builder.CreateTexture(Desc);
    Desc.mDebugName = "Dead";
    FARDGTextureRef Dead = Builder.CreateTexture(Desc);

    FARDGTextureAccessParameters ProduceParameters;
    ProduceParameters.mOutput = {
        Intermediate,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {}};
    const FARDGPassHandle Produce = Builder.AddPass(
        "Produce",
        &ProduceParameters,
        EARDGPassFlags::Compute,
        [] {});

    FARDGTextureAccessParameters ConsumeParameters;
    ConsumeParameters.mInput = {
        Intermediate,
        rhi::EArdaRHIResourceState::ShaderResource,
        {}};
    ConsumeParameters.mOutput = {
        Output,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {}};
    const FARDGPassHandle Consume = Builder.AddPass(
        "Consume",
        &ConsumeParameters,
        EARDGPassFlags::Compute,
        [] {});

    FARDGTextureAccessParameters DeadParameters;
    DeadParameters.mOutput = {
        Dead,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {}};
    const FARDGPassHandle DeadPass = Builder.AddPass(
        "Dead",
        &DeadParameters,
        EARDGPassFlags::Compute,
        [] {});

    rhi::FArdaRHITextureRef Extracted;
    Builder.QueueTextureExtraction(
        Output,
        Extracted,
        rhi::EArdaRHIResourceState::ShaderResource);
    const FARDGCompileResult& Result = Builder.Compile();

    EXPECT_EQ(Builder.GetProloguePass().GetIndex(), 0u);
    EXPECT_EQ(Result.mPrologue, Builder.GetProloguePass());
    EXPECT_EQ(Result.mEpilogue, Builder.GetEpiloguePass());
    EXPECT_TRUE(Builder.TryGetPass(Result.mPrologue)->GetState().mbSentinel);
    EXPECT_TRUE(Builder.TryGetPass(Result.mEpilogue)->GetState().mbSentinel);
    EXPECT_FALSE(Builder.TryGetPass(Produce)->GetState().mbCulled);
    EXPECT_FALSE(Builder.TryGetPass(Consume)->GetState().mbCulled);
    EXPECT_TRUE(Builder.TryGetPass(DeadPass)->GetState().mbCulled);
    EXPECT_EQ(
        Builder.TryGetPass(Consume)->GetState().mProducers,
        (eastl::vector<FARDGPassHandle>{Produce}));
    EXPECT_EQ(Intermediate->GetFirstUse(), Produce);
    EXPECT_EQ(Intermediate->GetLastUse(), Consume);
    EXPECT_EQ(Output->GetLastUse(), Result.mEpilogue);
    EXPECT_FALSE(Dead->GetFirstUse().IsValid());
}

TEST(ArdaRenderGraph, SetupTraversesViewsAndNestedUniformBufferMetadata)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    rhi::FArdaRHITextureDesc Desc;
    Desc.mDebugName = "Source";
    Desc.mUsage |= rhi::EArdaRHITextureUsage::UnorderedAccess;
    FARDGTextureRef Source = Builder.CreateTexture(Desc);
    Desc.mDebugName = "Destination";
    FARDGTextureRef Destination = Builder.CreateTexture(Desc);

    FARDGTextureAccessParameters ProduceParameters;
    ProduceParameters.mOutput.mTexture = Source;
    ProduceParameters.mOutput.mState = rhi::EArdaRHIResourceState::UnorderedAccess;
    const FARDGPassHandle Produce = Builder.AddPass(
        "ProduceSource",
        &ProduceParameters,
        EARDGPassFlags::Compute,
        [] {});

    FARDGTextureViewDesc SourceViewDesc;
    SourceViewDesc.mTexture = Source->GetHandle();
    FARDGTextureSRVRef SourceView =
        Builder.CreateTextureSRV("SourceSRV", SourceViewDesc);
    FARDGTextureViewDesc DestinationViewDesc;
    DestinationViewDesc.mTexture = Destination->GetHandle();
    FARDGTextureUAVRef DestinationView =
        Builder.CreateTextureUAV("DestinationUAV", DestinationViewDesc);

    FARDGInnerParameters UniformParameters;
    UniformParameters.mTexture = Source;
    FARDGUniformBufferRef UniformBuffer = Builder.CreateUniformBuffer(
        "NestedUniform",
        &UniformParameters);

    FARDGViewAndUniformParameters Parameters;
    Parameters.mInput = SourceView;
    Parameters.mOutput = DestinationView;
    Parameters.mUniformBuffer = UniformBuffer;
    const FARDGPassHandle Consume = Builder.AddPass(
        "ConsumeViews",
        &Parameters,
        EARDGPassFlags::Compute,
        [] {});

    rhi::FArdaRHITextureRef Extracted;
    Builder.QueueTextureExtraction(
        Destination,
        Extracted,
        rhi::EArdaRHIResourceState::ShaderResource);
    (void)Builder.Compile();

    const FARDGPassState& State = Builder.TryGetPass(Consume)->GetState();
    EXPECT_EQ(State.mTextureStates.size(), 3u);
    EXPECT_EQ(State.mProducers, (eastl::vector<FARDGPassHandle>{Produce}));
    EXPECT_FALSE(Builder.TryGetPass(Produce)->GetState().mbCulled);
    EXPECT_FALSE(Builder.TryGetPass(Consume)->GetState().mbCulled);
}

TEST(ArdaRenderGraph, CompilerUsesManualDependenciesAsCullingEdges)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    const FARDGPassHandle Setup = Builder.AddPass(
        "Setup",
        EARDGPassFlags::None,
        [] {});
    const FARDGPassHandle Root = Builder.AddPass(
        "Root",
        EARDGPassFlags::NeverCull,
        [] {});
    Builder.AddDependency(Setup, Root);

    (void)Builder.Compile();

    EXPECT_FALSE(Builder.TryGetPass(Setup)->GetState().mbCulled);
    EXPECT_FALSE(Builder.TryGetPass(Root)->GetState().mbCulled);
    EXPECT_FATAL_CHECK(
        Builder.AddDependency(Root, Setup),
        "Cannot add a dependency outside graph building");
}

TEST(ArdaRenderGraph, CompilerAssignsQueueFallbackAndAsyncForkJoinMetadata)
{
    using namespace arda::render_graph;

    FARDGRenderGraphContext Context;
    Context.mQueueCapabilities.mbCompute = true;
    Context.mQueueCapabilities.mbCopy = true;
    FARDGBuilder Builder(Context);

    rhi::FArdaRHITextureDesc Desc;
    Desc.mDebugName = "AsyncInput";
    Desc.mUsage |= rhi::EArdaRHITextureUsage::UnorderedAccess;
    FARDGTextureRef Input = Builder.CreateTexture(Desc);
    Desc.mDebugName = "AsyncOutput";
    FARDGTextureRef Output = Builder.CreateTexture(Desc);

    FARDGTextureAccessParameters ProduceParameters;
    ProduceParameters.mOutput.mTexture = Input;
    ProduceParameters.mOutput.mState = rhi::EArdaRHIResourceState::UnorderedAccess;
    const FARDGPassHandle GraphicsProducer = Builder.AddPass(
        "GraphicsProducer",
        &ProduceParameters,
        EARDGPassFlags::Compute,
        [] {});

    FARDGTextureAccessParameters AsyncParameters;
    AsyncParameters.mInput.mTexture = Input;
    AsyncParameters.mInput.mState = rhi::EArdaRHIResourceState::ShaderResource;
    AsyncParameters.mOutput.mTexture = Output;
    AsyncParameters.mOutput.mState = rhi::EArdaRHIResourceState::UnorderedAccess;
    const FARDGPassHandle AsyncPass = Builder.AddPass(
        "Async",
        &AsyncParameters,
        EARDGPassFlags::Compute | EARDGPassFlags::AsyncCompute,
        [] {});

    FARDGTextureAccessParameters ConsumeParameters;
    ConsumeParameters.mInput.mTexture = Output;
    ConsumeParameters.mInput.mState = rhi::EArdaRHIResourceState::ShaderResource;
    const FARDGPassHandle GraphicsConsumer = Builder.AddPass(
        "GraphicsConsumer",
        &ConsumeParameters,
        EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
        [] {});
    FARDGTextureAccessParameters PixelOnlyParameters;
    PixelOnlyParameters.mInput.mTexture = Input;
    PixelOnlyParameters.mInput.mState =
        rhi::EArdaRHIResourceState::PixelShaderResource;
    const FARDGPassHandle PixelOnlyPass = Builder.AddPass(
        "PixelOnlyFallback",
        &PixelOnlyParameters,
        EARDGPassFlags::Compute |
            EARDGPassFlags::AsyncCompute |
            EARDGPassFlags::NeverCull,
        [] {});
    const FARDGPassHandle CopyPass = Builder.AddPass(
        "Copy",
        EARDGPassFlags::Copy | EARDGPassFlags::NeverCull,
        [] {});

    (void)Builder.Compile();

    const FARDGPassState& AsyncState =
        Builder.TryGetPass(AsyncPass)->GetState();
    EXPECT_EQ(AsyncState.mPipeline, EARDGPipeline::AsyncCompute);
    EXPECT_EQ(AsyncState.mAsyncFork, GraphicsProducer);
    EXPECT_EQ(AsyncState.mAsyncJoin, GraphicsConsumer);
    bool bFoundNormalizedShaderResource = false;
    for (const FARDGTextureTransition& Transition :
         AsyncState.mTextureTransitions)
    {
        if (Transition.mTexture == Input->GetHandle() &&
            Transition.mStateAfter ==
                rhi::EArdaRHIResourceState::NonPixelShaderResource)
        {
            bFoundNormalizedShaderResource = true;
            break;
        }
    }
    EXPECT_TRUE(bFoundNormalizedShaderResource);
    EXPECT_EQ(
        Builder.TryGetPass(PixelOnlyPass)->GetState().mPipeline,
        EARDGPipeline::Graphics);
    EXPECT_EQ(
        Builder.TryGetPass(CopyPass)->GetState().mPipeline,
        EARDGPipeline::Copy);

    FARDGBuilder FallbackBuilder;
    const FARDGPassHandle FallbackAsync = FallbackBuilder.AddPass(
        "FallbackAsync",
        EARDGPassFlags::Compute |
            EARDGPassFlags::AsyncCompute |
            EARDGPassFlags::NeverCull,
        [] {});
    const FARDGPassHandle FallbackCopy = FallbackBuilder.AddPass(
        "FallbackCopy",
        EARDGPassFlags::Copy | EARDGPassFlags::NeverCull,
        [] {});
    (void)FallbackBuilder.Compile();
    EXPECT_EQ(
        FallbackBuilder.TryGetPass(FallbackAsync)->GetState().mPipeline,
        EARDGPipeline::Graphics);
    EXPECT_EQ(
        FallbackBuilder.TryGetPass(FallbackCopy)->GetState().mPipeline,
        EARDGPipeline::Graphics);
}

TEST(ArdaRenderGraph, CompilerGroupsCompatibleConsecutiveRasterPasses)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    rhi::FArdaRHITextureDesc Desc;
    Desc.mDebugName = "ColorA";
    Desc.mUsage |= rhi::EArdaRHITextureUsage::RenderTarget;
    FARDGTextureRef ColorA = Builder.CreateTexture(Desc);
    Desc.mDebugName = "ColorB";
    FARDGTextureRef ColorB = Builder.CreateTexture(Desc);

    FARDGRasterParameters FirstParameters;
    FirstParameters.mRenderTargets.mColor[0].mTexture = ColorA;
    const FARDGPassHandle First = Builder.AddPass(
        "RasterA0",
        &FirstParameters,
        EARDGPassFlags::Raster | EARDGPassFlags::NeverCull,
        [] {});

    FARDGRasterParameters SecondParameters;
    SecondParameters.mRenderTargets.mColor[0].mTexture = ColorA;
    const FARDGPassHandle Second = Builder.AddPass(
        "RasterA1",
        &SecondParameters,
        EARDGPassFlags::Raster | EARDGPassFlags::NeverCull,
        [] {});

    FARDGRasterParameters ThirdParameters;
    ThirdParameters.mRenderTargets.mColor[0].mTexture = ColorB;
    const FARDGPassHandle Third = Builder.AddPass(
        "RasterB",
        &ThirdParameters,
        EARDGPassFlags::Raster | EARDGPassFlags::NeverCull,
        [] {});

    const FARDGCompileResult& Result = Builder.Compile();

    EXPECT_EQ(Builder.TryGetPass(First)->GetState().mRasterGroup, 0u);
    EXPECT_EQ(Builder.TryGetPass(Second)->GetState().mRasterGroup, 0u);
    EXPECT_EQ(Builder.TryGetPass(Third)->GetState().mRasterGroup, 1u);
    EXPECT_EQ(Result.mRasterGroupCount, 2u);
}

TEST(ArdaRenderGraph, CompilerRejectsReadBeforeProduce)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    rhi::FArdaRHITextureDesc Desc;
    Desc.mDebugName = "Uninitialized";
    FARDGTextureRef Texture = Builder.CreateTexture(Desc);

    FARDGTextureAccessParameters Parameters;
    Parameters.mInput.mTexture = Texture;
    Parameters.mInput.mState = rhi::EArdaRHIResourceState::ShaderResource;
    (void)Builder.AddPass(
        "InvalidRead",
        &Parameters,
        EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
        [] {});

    EXPECT_FATAL_CHECK((void)Builder.Compile(), "reads a texture subresource before it is produced");
}

TEST(ArdaRenderGraph, BuilderRejectsMutationsOutsideBuildingLifecycle)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    rhi::FArdaRHIBufferDesc Desc;
    Desc.mDebugName = "Lifecycle";
    Desc.mByteSize = 64;
    FARDGBufferRef Buffer = Builder.CreateBuffer(Desc);
    (void)Builder.AddPass(
        "Root",
        EARDGPassFlags::NeverCull,
        [] {});
    (void)Builder.Compile();

    EXPECT_FATAL_CHECK((void)Builder.CreateBuffer(Desc), "Cannot create a buffer outside graph building");
    EXPECT_FATAL_CHECK(
        (void)Builder.AddPass(
            "LatePass",
            EARDGPassFlags::NeverCull,
            [] {}),
        "Cannot add a pass outside graph building");
    EXPECT_FATAL_CHECK((void)Builder.GetBlackboard(), "Cannot mutate the render-graph blackboard after building");
    rhi::FArdaRHIBufferRef Output;
    EXPECT_FATAL_CHECK(
        Builder.QueueBufferExtraction(
            Buffer,
            Output,
            rhi::EArdaRHIResourceState::CopySource),
        "Invalid logical buffer extraction");
}

TEST(ArdaRenderGraph, BuilderRejectsIllegalFlagsOwnershipAndDuplicateExtraction)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    EXPECT_FATAL_CHECK(
        (void)Builder.AddPass(
            "IllegalFlags",
            EARDGPassFlags::Raster | EARDGPassFlags::AsyncCompute,
            [] {}),
        "Incompatible render-graph pass flags");

    rhi::FArdaRHIBufferDesc Desc;
    Desc.mDebugName = "Ownership";
    Desc.mByteSize = 64;
    Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
    EXPECT_FATAL_CHECK(
        (void)Builder.CreateBuffer(Desc, EARDGResourceFlags::External),
        "Invalid logical buffer declaration");

    FARDGBufferRef Buffer = Builder.CreateBuffer(Desc);
    FARDGBufferAccessParameters Parameters;
    Parameters.mBuffer = {
        Buffer,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {}};
    (void)Builder.AddPass(
        "Produce",
        &Parameters,
        EARDGPassFlags::Compute,
        [] {});
    rhi::FArdaRHIBufferRef Output;
    Builder.QueueBufferExtraction(
        Buffer,
        Output,
        rhi::EArdaRHIResourceState::CopySource);
    EXPECT_FATAL_CHECK(
        Builder.QueueBufferExtraction(
            Buffer,
            Output,
            rhi::EArdaRHIResourceState::CopySource),
        "A logical buffer extraction cannot be queued twice");
}

TEST(ArdaRenderGraph, CompilerRejectsIllegalQueueStatesAndSubresourceReads)
{
    using namespace arda::render_graph;

    {
        FARDGBuilder Builder;
        rhi::FArdaRHIBufferDesc Desc;
        Desc.mDebugName = "IllegalCopyState";
        Desc.mByteSize = 64;
        Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
        FARDGBufferRef Buffer = Builder.CreateBuffer(Desc);
        FARDGBufferAccessParameters Parameters;
        Parameters.mBuffer = {
            Buffer,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "InvalidCopy",
            &Parameters,
            EARDGPassFlags::Copy | EARDGPassFlags::NeverCull,
            [] {});
        EXPECT_FATAL_CHECK((void)Builder.Compile(), "declares a state unsupported by a copy queue");
    }

    {
        FARDGBuilder Builder;
        rhi::FArdaRHITextureDesc Desc;
        Desc.mDebugName = "PartialProduction";
        Desc.mMipLevels = 2;
        Desc.mUsage |= rhi::EArdaRHITextureUsage::UnorderedAccess;
        FARDGTextureRef Texture = Builder.CreateTexture(Desc);

        FARDGTextureAccessParameters Produce;
        Produce.mOutput = {
            Texture,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {0, 1, 0, 1}};
        (void)Builder.AddPass(
            "ProduceMip0",
            &Produce,
            EARDGPassFlags::Compute,
            [] {});

        FARDGTextureAccessParameters Read;
        Read.mInput = {
            Texture,
            rhi::EArdaRHIResourceState::ShaderResource,
            {1, 1, 0, 1}};
        (void)Builder.AddPass(
            "ReadMip1",
            &Read,
            EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
            [] {});
        EXPECT_FATAL_CHECK((void)Builder.Compile(), "reads a texture subresource before it is produced");
    }
}

TEST(ArdaRenderGraph, DebugModesExposeConservativeBarriersAndExtendedLifetimes)
{
    using namespace arda::render_graph;

    FARDGRenderGraphContext Context;
    Context.mQueueCapabilities.mbCompute = true;
    Context.mDebugOptions.mbImmediateMode = true;
    Context.mDebugOptions.mbConservativeBarriers = true;
    Context.mDebugOptions.mbExtendResourceLifetimes = true;
    FARDGBuilder Builder(Context);

    rhi::FArdaRHIBufferDesc Desc;
    Desc.mDebugName = "DebugBuffer";
    Desc.mByteSize = 64;
    Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
    FARDGBufferRef Buffer = Builder.CreateBuffer(Desc);
    FARDGBufferAccessParameters FirstParameters;
    FirstParameters.mBuffer = {
        Buffer,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {}};
    const FARDGPassHandle First = Builder.AddPass(
        "FirstWrite",
        &FirstParameters,
        EARDGPassFlags::Compute,
        [] {});
    FARDGBufferAccessParameters SecondParameters = FirstParameters;
    SecondParameters.mBuffer.mState = rhi::EArdaRHIResourceState::ShaderResource;
    const FARDGPassHandle Second = Builder.AddPass(
        "FirstRead",
        &SecondParameters,
        EARDGPassFlags::Compute | EARDGPassFlags::AsyncCompute,
        [] {});
    const FARDGPassHandle Third = Builder.AddPass(
        "SecondRead",
        &SecondParameters,
        EARDGPassFlags::Compute,
        [] {});

    const FARDGCompileResult& Result = Builder.Compile();
    EXPECT_FALSE(Builder.TryGetPass(First)->GetState().mbCulled);
    EXPECT_FALSE(Builder.TryGetPass(Second)->GetState().mbCulled);
    EXPECT_FALSE(Builder.TryGetPass(Third)->GetState().mbCulled);
    EXPECT_EQ(
        Builder.TryGetPass(Second)->GetState().mPipeline,
        EARDGPipeline::Graphics);
    ASSERT_EQ(
        Builder.TryGetPass(Third)->GetState().mBufferTransitions.size(),
        1u);
    EXPECT_TRUE(
        Builder.TryGetPass(Third)
            ->GetState()
            .mBufferTransitions[0]
            .mbForceBarrier);
    ASSERT_EQ(Result.mResourceLifetimes.size(), 1u);
    EXPECT_EQ(Result.mResourceLifetimes[0].mFirstUse, 0u);
    EXPECT_EQ(
        Result.mResourceLifetimes[0].mLastUse,
        Result.mExecutionOrder.size() - 1u);

    const eastl::string Dump = Builder.DumpGraph();
    EXPECT_NE(Dump.find("ExecutionOrder ["), eastl::string::npos);
    EXPECT_NE(Dump.find("conservativeBarriers=1"), eastl::string::npos);
    EXPECT_NE(Dump.find("forced=1"), eastl::string::npos);
    EXPECT_NE(Dump.find("UAV->PixelSRV|NonPixelSRV"), eastl::string::npos);
}

TEST(ArdaRenderGraph, GraphDumpIsDeterministicAndContainsCompilerProducts)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    const FARDGPassHandle Pass = Builder.AddPass(
        "StablePass",
        EARDGPassFlags::NeverCull,
        [] {});
    (void)Builder.Compile();

    const eastl::string First = Builder.DumpGraph();
    const eastl::string Second = Builder.DumpGraph();
    EXPECT_TRUE(First == Second);
    EXPECT_NE(First.find("ArdaRenderGraph"), eastl::string::npos);
    EXPECT_NE(First.find("\"StablePass\""), eastl::string::npos);
    char PassText[32];
    std::snprintf(PassText, sizeof(PassText), " P%u", Pass.GetIndex());
    EXPECT_NE(
        First.find(PassText),
        eastl::string::npos);
    EXPECT_NE(First.find("GraphEpilogue"), eastl::string::npos);
}

TEST(ArdaRenderGraph, TransientHeapAllocatorReusesOnlyExpiredIntervals)
{
    using namespace arda::render_graph;

    const eastl::vector<FARDGTransientAllocationRequest> Requests{
        {0, 1, 3, 256, 64},
        {1, 2, 4, 128, 64},
        {2, 5, 6, 192, 64}};

    const FARDGTransientHeapLayout Aliased =
        FARDGTransientHeapAllocator::Allocate(Requests, true);
    ASSERT_EQ(Aliased.mAllocations.size(), 3u);
    EXPECT_EQ(Aliased.mAllocations[0].mOffset, 0u);
    EXPECT_EQ(Aliased.mAllocations[1].mOffset, 256u);
    EXPECT_EQ(Aliased.mAllocations[2].mOffset, 0u);
    EXPECT_TRUE(Aliased.mAllocations[2].mbReusedMemory);
    EXPECT_TRUE(Aliased.mbContainsAliases);
    EXPECT_EQ(Aliased.mCapacity, 384u);

    const FARDGTransientHeapLayout Packed =
        FARDGTransientHeapAllocator::Allocate(Requests, false);
    EXPECT_FALSE(Packed.mbContainsAliases);
    EXPECT_EQ(Packed.mAllocations[2].mOffset, 384u);
    EXPECT_EQ(Packed.mCapacity, 576u);
}

TEST(ArdaRenderGraph, CompilerLowersTextureSubresourcesUavAndFinalTransitions)
{
    using namespace arda::render_graph;

    rhi::FArdaRHITextureDesc Desc;
    Desc.mDebugName = "ExternalMips";
    Desc.mWidth = 32;
    Desc.mHeight = 32;
    Desc.mMipLevels = 2;
    Desc.mUsage |= rhi::EArdaRHITextureUsage::UnorderedAccess;
    rhi::FArdaRHITextureRef PhysicalTexture(new FARDGTestTexture(Desc));

    FARDGBuilder Builder;
    FARDGTextureRef Texture = Builder.RegisterExternalTexture(
        PhysicalTexture,
        rhi::EArdaRHIResourceState::Present);

    FARDGTextureAccessParameters FirstParameters;
    FirstParameters.mOutput = {
        Texture,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {0, 1, 0, 1}};
    const FARDGPassHandle First = Builder.AddPass(
        "FirstUAV",
        &FirstParameters,
        EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
        [] {});

    FARDGTextureAccessParameters SecondParameters;
    SecondParameters.mOutput = {
        Texture,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {0, 1, 0, 1}};
    const FARDGPassHandle Second = Builder.AddPass(
        "SecondUAV",
        &SecondParameters,
        EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
        [] {});

    const FARDGCompileResult& Result = Builder.Compile();
    const auto& FirstTransitions =
        Builder.TryGetPass(First)->GetState().mTextureTransitions;
    const auto& SecondTransitions =
        Builder.TryGetPass(Second)->GetState().mTextureTransitions;
    const auto& FinalTransitions =
        Builder.TryGetPass(Result.mEpilogue)->GetState().mTextureTransitions;

    ASSERT_EQ(FirstTransitions.size(), 1u);
    EXPECT_EQ(
        FirstTransitions[0].mStateBefore,
        rhi::EArdaRHIResourceState::Present);
    EXPECT_EQ(
        FirstTransitions[0].mStateAfter,
        rhi::EArdaRHIResourceState::UnorderedAccess);
    ASSERT_EQ(SecondTransitions.size(), 1u);
    EXPECT_TRUE(SecondTransitions[0].mbUAVBarrier);
    ASSERT_EQ(FinalTransitions.size(), 1u);
    EXPECT_EQ(
        FinalTransitions[0].mStateAfter,
        rhi::EArdaRHIResourceState::Present);
    EXPECT_EQ(FinalTransitions[0].mSubresources.mBaseMipLevel, 0u);
}

TEST(ArdaRenderGraph, CompilerUsesWholeBufferStatesAndExecutionOrderLifetimes)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    rhi::FArdaRHIBufferDesc Desc;
    Desc.mDebugName = "Intervals";
    Desc.mByteSize = 1024;
    Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
    FARDGBufferRef Buffer = Builder.CreateBuffer(Desc);

    FARDGBufferAccessParameters FirstParameters;
    FirstParameters.mBuffer = {
        Buffer,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {0, 256}};
    const FARDGPassHandle First = Builder.AddPass(
        "WriteRange0",
        &FirstParameters,
        EARDGPassFlags::Compute,
        [] {});

    FARDGBufferAccessParameters SecondParameters;
    SecondParameters.mBuffer = {
        Buffer,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {512, 256}};
    const FARDGPassHandle Second = Builder.AddPass(
        "WriteRange1",
        &SecondParameters,
        EARDGPassFlags::Compute,
        [] {});

    rhi::FArdaRHIBufferRef Extracted;
    Builder.QueueBufferExtraction(
        Buffer,
        Extracted,
        rhi::EArdaRHIResourceState::CopySource);
    const FARDGCompileResult& Result = Builder.Compile();

    ASSERT_EQ(
        Builder.TryGetPass(First)->GetState().mBufferTransitions.size(),
        1u);
    ASSERT_EQ(
        Builder.TryGetPass(Second)->GetState().mBufferTransitions.size(),
        1u);
    EXPECT_TRUE(
        Builder.TryGetPass(Second)
            ->GetState()
            .mBufferTransitions[0]
            .mbUAVBarrier);
    ASSERT_EQ(Result.mResourceLifetimes.size(), 1u);
    EXPECT_EQ(Result.mResourceLifetimes[0].mType, EARDGResourceType::Buffer);
    EXPECT_EQ(Result.mResourceLifetimes[0].mFirstUse, 1u);
    EXPECT_EQ(
        Result.mResourceLifetimes[0].mLastUse,
        Result.mExecutionOrder.size() - 1u);
    EXPECT_FALSE(Result.mResourceLifetimes[0].mbTransient);
}

TEST(ArdaRenderGraph, CompilerLowersCrossQueueDependencies)
{
    using namespace arda::render_graph;

    FARDGRenderGraphContext Context;
    Context.mQueueCapabilities.mbCompute = true;
    FARDGBuilder Builder(Context);

    rhi::FArdaRHIBufferDesc Desc;
    Desc.mDebugName = "QueueBuffer";
    Desc.mByteSize = 256;
    Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
    FARDGBufferRef Buffer = Builder.CreateBuffer(Desc);

    FARDGBufferAccessParameters ProduceParameters;
    ProduceParameters.mBuffer = {
        Buffer,
        rhi::EArdaRHIResourceState::UnorderedAccess,
        {}};
    const FARDGPassHandle Produce = Builder.AddPass(
        "GraphicsProduce",
        &ProduceParameters,
        EARDGPassFlags::Compute,
        [] {});

    FARDGBufferAccessParameters ConsumeParameters;
    ConsumeParameters.mBuffer = {
        Buffer,
        rhi::EArdaRHIResourceState::ShaderResource,
        {}};
    const FARDGPassHandle Consume = Builder.AddPass(
        "AsyncConsume",
        &ConsumeParameters,
        EARDGPassFlags::Compute |
            EARDGPassFlags::AsyncCompute |
            EARDGPassFlags::NeverCull,
        [] {});

    const FARDGCompileResult& Result = Builder.Compile();
    const auto Iterator = eastl::find_if(
        Result.mQueueDependencies.begin(),
        Result.mQueueDependencies.end(),
        [Produce, Consume](const FARDGQueueDependency& Dependency)
        {
            return Dependency.mProducer == Produce &&
                Dependency.mConsumer == Consume;
        });
    ASSERT_NE(Iterator, Result.mQueueDependencies.end());
    EXPECT_EQ(Iterator->mProducerPipeline, EARDGPipeline::Graphics);
    EXPECT_EQ(Iterator->mConsumerPipeline, EARDGPipeline::AsyncCompute);
}

TEST(ArdaRenderGraph, DispatchPassApiRegistersComputeWork)
{
    using namespace arda::render_graph;

    FARDGBuilder Builder;
    FARDGInnerParameters Parameters;
    const FARDGPassHandle Dispatch = Builder.AddDispatchPass(
        "DirectDispatch",
        &Parameters,
        FARDGDispatchArguments{2, 3, 4},
        [] {},
        EARDGPassFlags::NeverCull);
    (void)Builder.Compile();

    const FARDGPass* Pass = Builder.TryGetPass(Dispatch);
    ASSERT_NE(Pass, nullptr);
    EXPECT_TRUE(HasAllFlags(Pass->GetFlags(), EARDGPassFlags::Compute));
    EXPECT_FALSE(Pass->GetState().mbCulled);
}

TEST(ArdaRenderGraph, PassContextCreatesBindingsFromParameterDescriptors)
{
    using namespace arda::backend;
    using namespace arda::render_graph;

    FArdaBackendConfiguration Configuration;
    Configuration.mbEnableValidation = true;
    if (!ConfigureLinkedBackend(Configuration) || !InitializeBackend())
    {
        GTEST_SKIP() << GetBackendError().c_str();
    }

    {
        const FArdaDeviceContext& DeviceContext = GetDeviceContext();
        FARDGRenderGraphContext GraphContext;
        GraphContext.mDevice = DeviceContext.mDevice;
        GraphContext.mQueueCapabilities.mbGraphics = true;
        GraphContext.mQueueCapabilities.mbCompute =
            DeviceContext.mQueueCapabilities.mbCompute;
        GraphContext.mQueueCapabilities.mbCopy =
            DeviceContext.mQueueCapabilities.mbCopy;
        FARDGBuilder Builder(GraphContext);

        rhi::FArdaRHITextureDesc TextureDesc;
        TextureDesc.mDebugName = "BindingSetTexture";
        TextureDesc.mWidth = 4;
        TextureDesc.mHeight = 4;
        TextureDesc.mFormat = rhi::EArdaRHIFormat::R32UInt;
        TextureDesc.mUsage |= rhi::EArdaRHITextureUsage::UnorderedAccess;
        FARDGTextureRef Texture = Builder.CreateTexture(TextureDesc);
        FARDGTextureViewDesc TextureViewDesc;
        TextureViewDesc.mTexture = Texture->GetHandle();
        TextureViewDesc.mSubresources = {0, 1, 0, 1};
        FARDGTextureUAVRef TextureView =
            Builder.CreateTextureUAV("BindingSetTextureUAV", TextureViewDesc);

        rhi::FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mDebugName = "BindingSetBuffer";
        BufferDesc.mByteSize = 64;
        BufferDesc.mStructureStride = sizeof(uint32_t);
        BufferDesc.mUsage =
            rhi::EArdaRHIBufferUsage::Structured |
            rhi::EArdaRHIBufferUsage::UnorderedAccess;
        FARDGBufferRef Buffer = Builder.CreateBuffer(BufferDesc);
        FARDGBufferViewDesc BufferViewDesc;
        BufferViewDesc.mBuffer = Buffer->GetHandle();
        FARDGBufferUAVRef BufferView =
            Builder.CreateBufferUAV("BindingSetBufferUAV", BufferViewDesc);

        rhi::FArdaRHIBindingLayoutDesc LayoutDesc;
        LayoutDesc.mVisibility = rhi::EArdaRHIShaderStage::Compute;
        LayoutDesc.mItems.push_back(
            {0, 1, rhi::EArdaRHIBindingType::TextureUAV});
        LayoutDesc.mItems.push_back(
            {1, 1, rhi::EArdaRHIBindingType::StructuredBufferUAV});
        auto LayoutResult = DeviceContext.mDevice->CreateBindingLayout(LayoutDesc);
        ASSERT_TRUE(LayoutResult);
        rhi::FArdaRHIBindingLayoutRef Layout = eastl::move(LayoutResult.mValue);

        FARDGBindingSetParameters Parameters;
        Parameters.mTexture = TextureView;
        Parameters.mBuffer = BufferView;
        rhi::FArdaRHIBindingSetRef GeneratedBindings;
        (void)Builder.AddPass(
            "CreateParameterBindings",
            &Parameters,
            EARDGPassFlags::Compute |
                EARDGPassFlags::NeverCull |
                EARDGPassFlags::NeverParallel,
            [&GeneratedBindings, Layout](
                FARDGPassExecutionContext& Context)
            {
                GeneratedBindings = Context.CreateBindingSet(Layout.Get());
            });

        FARDGExecuteOptions Options;
        Options.mbParallelRecording = false;
        (void)Builder.Execute(Options);

        ASSERT_TRUE(GeneratedBindings);
        const rhi::FArdaRHIBindingSetDesc& GeneratedDesc =
            GeneratedBindings->GetDesc();
        ASSERT_EQ(GeneratedDesc.mItems.size(), 2u);
        EXPECT_EQ(
            GeneratedDesc.mItems[0].mType,
            rhi::EArdaRHIBindingType::TextureUAV);
        EXPECT_EQ(GeneratedDesc.mItems[0].mSlot, 0u);
        EXPECT_EQ(
            GeneratedDesc.mItems[1].mType,
            rhi::EArdaRHIBindingType::StructuredBufferUAV);
        EXPECT_EQ(GeneratedDesc.mItems[1].mSlot, 1u);
        EXPECT_TRUE(DeviceContext.mDevice->WaitForIdle());
    }
    ShutdownBackend();
}

TEST(ArdaRenderGraph, RegisteredShaderBridgeUsesExplicitSlotsAndAllLayouts)
{
    using namespace arda::backend;
    using namespace arda::render_graph;

    FArdaBackendConfiguration Configuration;
    Configuration.mbEnableValidation = false;
    Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
    if (!ConfigureLinkedBackend(Configuration) || !InitializeBackend())
        GTEST_SKIP() << GetBackendError().c_str();

    {
        const std::string ShaderSource =
            (std::filesystem::path(ARDA_BACKEND_TEST_SHADER_SOURCE_DIR) /
             "ArdaShaderStructTest.hlsl").string();
        FArdaShaderTypeRegistration Registration(
            "ARDGRegisteredShader",
            ShaderSource.c_str(),
            "ArdaShaderStructTest",
            "ShaderStructTestCS",
            rhi::EArdaRHIShaderStage::Compute,
            &GetRegisteredShaderMetadata);
        const FArdaShaderCompilerConfiguration PreviousCompiler =
            GetShaderCompilerConfiguration();
        FArdaShaderCompilerConfiguration RuntimeCompiler = PreviousCompiler;
        RuntimeCompiler.mbCompileMissingArtifacts = true;
        RuntimeCompiler.mbCompileOutdatedArtifacts = true;
        ConfigureShaderCompiler(RuntimeCompiler);
        const FArdaShaderCompileResult CompileResult =
            EnsureRegisteredShaderArtifact(
                Registration.GetType(),
                GetDeviceContext().mBackendName.c_str(),
                0,
                std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR));
        ConfigureShaderCompiler(PreviousCompiler);
        ASSERT_TRUE(CompileResult)
            << (CompileResult.mDiagnostics.empty()
                ? "Ardashir shader compilation failed."
                : CompileResult.mDiagnostics.front().mMessage.c_str());
        FArdaGlobalShaderMap ShaderMap;
        ASSERT_TRUE(ShaderMap.Initialize(
            GetDeviceContext(),
            std::filesystem::path(ARDA_BACKEND_TEST_SHADER_DIR)));
        const FArdaGlobalShaderInstance* Shader =
            ShaderMap.Find(Registration.GetType());
        ASSERT_NE(Shader, nullptr);
        ASSERT_EQ(Shader->GetBindingLayouts().size(), 2u);

        FARDGRenderGraphContext GraphContext;
        GraphContext.mDevice = GetDeviceContext().mDevice;
        GraphContext.mQueueCapabilities.mbGraphics = true;
        GraphContext.mQueueCapabilities.mbCompute =
            GetDeviceContext().mQueueCapabilities.mbCompute;
        GraphContext.mQueueCapabilities.mbCopy =
            GetDeviceContext().mQueueCapabilities.mbCopy;
        FARDGBuilder Builder(GraphContext);

        rhi::FArdaRHIBufferDesc BufferDesc;
        BufferDesc.mDebugName = "RegisteredFirstBuffer";
        BufferDesc.mByteSize = 64;
        BufferDesc.mStructureStride = sizeof(uint32_t);
        BufferDesc.mUsage =
            rhi::EArdaRHIBufferUsage::Structured |
            rhi::EArdaRHIBufferUsage::UnorderedAccess;
        FARDGBufferRef FirstBuffer = Builder.CreateBuffer(BufferDesc);
        BufferDesc.mDebugName = "RegisteredSecondBuffer";
        FARDGBufferRef SecondBuffer = Builder.CreateBuffer(BufferDesc);
        FARDGBufferViewDesc FirstViewDesc;
        FirstViewDesc.mBuffer = FirstBuffer->GetHandle();
        FARDGBufferViewDesc SecondViewDesc;
        SecondViewDesc.mBuffer = SecondBuffer->GetHandle();
        FARDGBufferUAVRef FirstView =
            Builder.CreateBufferUAV("RegisteredFirst", FirstViewDesc);
        FARDGBufferUAVRef SecondView =
            Builder.CreateBufferUAV("RegisteredSecond", SecondViewDesc);

        FARDGRegisteredBindingParameters Parameters;
        Parameters.mFirst = FirstView;
        Parameters.mSecond = SecondView;
        eastl::vector<rhi::FArdaRHIBindingSetRef> BindingSets;
        (void)Builder.AddPass(
            "RegisteredShaderBindings",
            &Parameters,
            EARDGPassFlags::Compute |
                EARDGPassFlags::NeverCull |
                EARDGPassFlags::NeverParallel,
            [&BindingSets, Shader](FARDGPassExecutionContext& Context)
            {
                BindingSets = Context.CreateBindingSets(*Shader);
            });
        FARDGExecuteOptions Options;
        Options.mbParallelRecording = false;
        (void)Builder.Execute(Options);

        ASSERT_EQ(BindingSets.size(), 2u);
        bool bFoundSpaceThreeSlotTwo = false;
        bool bFoundSpaceZeroSlotFive = false;
        for (const rhi::FArdaRHIBindingSetRef& BindingSet : BindingSets)
        {
            ASSERT_TRUE(BindingSet);
            const rhi::FArdaRHIBindingSetDesc& Desc =
                BindingSet->GetDesc();
            ASSERT_EQ(Desc.mItems.size(), 1u);
            const uint32_t Space =
                Desc.mLayout->GetDesc().mRegisterSpace;
            if (Space == 3 && Desc.mItems[0].mSlot == 2)
                bFoundSpaceThreeSlotTwo = true;
            if (Space == 0 && Desc.mItems[0].mSlot == 5)
                bFoundSpaceZeroSlotFive = true;
        }
        EXPECT_TRUE(bFoundSpaceThreeSlotTwo);
        EXPECT_TRUE(bFoundSpaceZeroSlotFive);
        EXPECT_TRUE(GetDeviceContext().mDevice->WaitForIdle());
    }
    ShutdownBackend();
}

TEST(ArdaRenderGraph, FrameTemporaryResourcesAndPassBindingsAreReleased)
{
    using namespace arda::backend;
    using namespace arda::render_graph;

    size_t TestedBackends = 0;
    for (const FArdaBackendModuleDescriptor& Module : EnumerateBackendModules())
    {
        if (Module.mBackendType != EArdaBackendType::D3D12 &&
            Module.mBackendType != EArdaBackendType::Vulkan)
            continue;
        ShutdownBackend();
        FArdaBackendConfiguration Configuration;
        Configuration.mBackendName = Module.mName;
        Configuration.mBackend = Module.mBackendType;
        Configuration.mbEnableValidation = false;
        Configuration.mShaderCompilationMode = EArdaShaderCompilationMode::LoadOnly;
        ASSERT_TRUE(ConfigureBackend(Configuration));
        ASSERT_TRUE(InitializeBackend())
            << Module.mName.c_str() << ": " << GetBackendError().c_str();
        ++TestedBackends;

        rhi::FArdaRHIDeviceRef Device = GetDevice();
        ASSERT_TRUE(Device);
        Device->TrimDescriptorCaches();
        const rhi::FArdaRHIResourceLifetimeStats Baseline =
            Device->GetResourceLifetimeStats();

        {
            FARDGRenderGraphContext Context;
            Context.mDevice = Device;
            Context.mQueueCapabilities.mbGraphics =
                GetDeviceContext().mQueueCapabilities.mbGraphics;
            Context.mQueueCapabilities.mbCompute =
                GetDeviceContext().mQueueCapabilities.mbCompute;
            Context.mQueueCapabilities.mbCopy =
                GetDeviceContext().mQueueCapabilities.mbCopy;
            FARDGBuilder Builder(Context);

            rhi::FArdaRHITextureDesc TextureDesc;
            TextureDesc.mDebugName = "FrameTemporaryTexture";
            TextureDesc.mWidth = 8;
            TextureDesc.mHeight = 8;
            TextureDesc.mFormat = rhi::EArdaRHIFormat::RGBA8UNorm;
            TextureDesc.mUsage = rhi::EArdaRHITextureUsage::ShaderResource |
                rhi::EArdaRHITextureUsage::UnorderedAccess;
            FARDGTextureRef Texture = Builder.CreateTexture(TextureDesc);
            FARDGTextureViewDesc TextureViewDesc;
            TextureViewDesc.mTexture = Texture->GetHandle();
            FARDGTextureSRVRef TextureSrv =
                Builder.CreateTextureSRV("FrameTemporaryTextureSRV", TextureViewDesc);
            FARDGTextureUAVRef TextureUav =
                Builder.CreateTextureUAV("FrameTemporaryTextureUAV", TextureViewDesc);

            rhi::FArdaRHIBufferDesc BufferDesc;
            BufferDesc.mDebugName = "FrameTemporaryBuffer";
            BufferDesc.mByteSize = 256;
            BufferDesc.mStructureStride = sizeof(uint32_t);
            BufferDesc.mUsage = rhi::EArdaRHIBufferUsage::Structured |
                rhi::EArdaRHIBufferUsage::ShaderResource |
                rhi::EArdaRHIBufferUsage::UnorderedAccess;
            FARDGBufferRef Buffer = Builder.CreateBuffer(BufferDesc);
            FARDGBufferViewDesc BufferViewDesc;
            BufferViewDesc.mBuffer = Buffer->GetHandle();
            FARDGBufferSRVRef BufferSrv =
                Builder.CreateBufferSRV("FrameTemporaryBufferSRV", BufferViewDesc);
            FARDGBufferUAVRef BufferUav =
                Builder.CreateBufferUAV("FrameTemporaryBufferUAV", BufferViewDesc);

            FARDGInnerParameters UniformContents;
            UniformContents.mScale = 1.0f;
            UniformContents.mTexture = Texture;
            FARDGUniformBufferRef Uniform = Builder.CreateUniformBuffer(
                "FrameTemporaryUniform", &UniformContents);
            ASSERT_NE(Uniform, nullptr);
            ASSERT_NE(TextureSrv, nullptr);
            ASSERT_NE(BufferSrv, nullptr);

            rhi::FArdaRHIBindingLayoutDesc LayoutDesc;
            LayoutDesc.mVisibility = rhi::EArdaRHIShaderStage::Compute;
            LayoutDesc.mItems.push_back(
                { 0, 1, rhi::EArdaRHIBindingType::TextureUAV });
            LayoutDesc.mItems.push_back(
                { 1, 1, rhi::EArdaRHIBindingType::StructuredBufferUAV });
            auto Layout = Device->CreateBindingLayout(LayoutDesc);
            ASSERT_TRUE(Layout);

            FARDGBindingSetParameters Parameters;
            Parameters.mTexture = TextureUav;
            Parameters.mBuffer = BufferUav;
            (void)Builder.AddPass(
                "FrameTemporaryBindings",
                &Parameters,
                EARDGPassFlags::Compute |
                    EARDGPassFlags::NeverCull |
                    EARDGPassFlags::NeverParallel,
                [Layout = Layout.mValue](FARDGPassExecutionContext& PassContext)
                {
                    rhi::FArdaRHIBindingSetRef BindingSet =
                        PassContext.CreateBindingSet(Layout.Get());
                    ASSERT_TRUE(BindingSet);
                });
            FARDGExecuteOptions Options;
            Options.mbParallelRecording = false;
            const FARDGExecutionResult& Result = Builder.Execute(Options);
            EXPECT_GT(Result.mSubmittedCommandListCount, 0u);
        }

        ASSERT_TRUE(Device->WaitForIdle());
        Device->RunGarbageCollection();
        Device->TrimDescriptorCaches();
        const rhi::FArdaRHIResourceLifetimeStats After =
            Device->GetResourceLifetimeStats();
        for (size_t Index = 0;
             Index < static_cast<size_t>(rhi::EArdaRHIResourceType::Count);
             ++Index)
        {
            EXPECT_EQ(After.mLiveResources[Index], Baseline.mLiveResources[Index])
                << Module.mName.c_str() << " retained frame resource type " << Index;
        }
        EXPECT_EQ(After.mResourceDescriptors, Baseline.mResourceDescriptors);
        EXPECT_EQ(After.mSamplerDescriptors, Baseline.mSamplerDescriptors);
        EXPECT_EQ(After.mDescriptorSets, Baseline.mDescriptorSets);
        EXPECT_EQ(After.mPendingSubmissions, Baseline.mPendingSubmissions);
        Device = nullptr;
        ShutdownBackend();
    }
    EXPECT_GT(TestedBackends, 0u);
    ShutdownBackend();
}

TEST(ArdaRenderGraph, ExecutesAndExtractsOnAvailableBackend)
{
    using namespace arda::backend;
    using namespace arda::render_graph;

    FArdaBackendConfiguration Configuration;
    Configuration.mbEnableValidation = true;
    if (!ConfigureLinkedBackend(Configuration) || !InitializeBackend())
    {
        GTEST_SKIP() << GetBackendError().c_str();
    }

    {
        const FArdaDeviceContext& DeviceContext = GetDeviceContext();
        FARDGRenderGraphContext GraphContext;
        GraphContext.mDevice = DeviceContext.mDevice;
        GraphContext.mQueueCapabilities.mbGraphics =
            DeviceContext.mQueueCapabilities.mbGraphics;
        GraphContext.mQueueCapabilities.mbCompute =
            DeviceContext.mQueueCapabilities.mbCompute;
        GraphContext.mQueueCapabilities.mbCopy =
            DeviceContext.mQueueCapabilities.mbCopy;
        FARDGBuilder Builder(GraphContext);

        rhi::FArdaRHIBufferDesc Desc;
        Desc.mDebugName = "RuntimeBuffer";
        Desc.mByteSize = 256;
        Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
        FARDGBufferRef Buffer = Builder.CreateBuffer(Desc);
        FARDGBufferAccessParameters Parameters;
        Parameters.mBuffer = {
            Buffer,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "ClearBuffer",
            &Parameters,
            EARDGPassFlags::Compute,
            [](FARDGPassExecutionContext& Context,
               const FARDGBufferAccessParameters& Frozen)
            {
                Context.mCommandList.ClearBufferUInt(
                    *Context.GetBuffer(Frozen.mBuffer.mBuffer),
                    0x12345678u);
            });

        Desc.mDebugName = "TransientRuntimeBuffer";
        FARDGBufferRef TransientBuffer = Builder.CreateBuffer(Desc);
        FARDGBufferAccessParameters TransientParameters;
        TransientParameters.mBuffer = {
            TransientBuffer,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "ClearTransientBuffer",
            &TransientParameters,
            EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
            [](FARDGPassExecutionContext& Context,
               const FARDGBufferAccessParameters& Frozen)
            {
                Context.mCommandList.ClearBufferUInt(
                    *Context.GetBuffer(Frozen.mBuffer.mBuffer),
                    0u);
            });

        Desc.mDebugName = "ReusedTransientRuntimeBuffer";
        FARDGBufferRef ReusedTransientBuffer = Builder.CreateBuffer(Desc);
        FARDGBufferAccessParameters ReusedParameters;
        ReusedParameters.mBuffer = {
            ReusedTransientBuffer,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "ClearReusedTransientBuffer",
            &ReusedParameters,
            EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
            [](FARDGPassExecutionContext& Context,
               const FARDGBufferAccessParameters& Frozen)
            {
                Context.mCommandList.ClearBufferUInt(
                    *Context.GetBuffer(Frozen.mBuffer.mBuffer),
                    1u);
            });

        rhi::FArdaRHIBufferRef Extracted;
        Builder.QueueBufferExtraction(
            Buffer,
            Extracted,
            rhi::EArdaRHIResourceState::CopySource);
        FARDGExecuteOptions Options;
        Options.mbParallelRecording = false;
        const FARDGExecutionResult& Result = Builder.Execute(Options);

        EXPECT_TRUE(Extracted);
        EXPECT_GE(Result.mSubmittedCommandListCount, 2u);
        EXPECT_TRUE(Result.mbUsedTransientFallback);
        EXPECT_EQ(Result.mBufferPoolReuseCount, 1u);
        EXPECT_NE(Builder.GetLastExecutionResult(), nullptr);
        EXPECT_TRUE(DeviceContext.mDevice->WaitForIdle());
    }
    ShutdownBackend();
}

TEST(ArdaRenderGraph, ImmediateModeExecutesSeriallyWithFirstWriteClobbering)
{
    using namespace arda::backend;
    using namespace arda::render_graph;

    FArdaBackendConfiguration Configuration;
    Configuration.mbEnableValidation = true;
    if (!ConfigureLinkedBackend(Configuration) || !InitializeBackend())
    {
        GTEST_SKIP() << GetBackendError().c_str();
    }

    {
        const FArdaDeviceContext& DeviceContext = GetDeviceContext();
        FARDGRenderGraphContext GraphContext;
        GraphContext.mDevice = DeviceContext.mDevice;
        GraphContext.mQueueCapabilities.mbGraphics = true;
        GraphContext.mQueueCapabilities.mbCompute =
            DeviceContext.mQueueCapabilities.mbCompute;
        GraphContext.mQueueCapabilities.mbCopy =
            DeviceContext.mQueueCapabilities.mbCopy;
        GraphContext.mDebugOptions.mbImmediateMode = true;
        GraphContext.mDebugOptions.mbClobberFirstWrites = true;
        FARDGBuilder Builder(GraphContext);

        rhi::FArdaRHIBufferDesc Desc;
        Desc.mDebugName = "ImmediateBuffer";
        Desc.mByteSize = 64;
        Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
        FARDGBufferRef Buffer = Builder.CreateBuffer(Desc);
        FARDGBufferAccessParameters Parameters;
        Parameters.mBuffer = {
            Buffer,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "ImmediateWrite",
            &Parameters,
            EARDGPassFlags::Compute,
            [](FARDGPassExecutionContext& Context,
               const FARDGBufferAccessParameters& Frozen)
            {
                Context.mCommandList.ClearBufferUInt(
                    *Context.GetBuffer(Frozen.mBuffer.mBuffer),
                    17u);
            });

        rhi::FArdaRHIBufferRef Extracted;
        Builder.QueueBufferExtraction(
            Buffer,
            Extracted,
            rhi::EArdaRHIResourceState::CopySource);
        const FARDGExecutionResult& Result = Builder.Execute();

        EXPECT_TRUE(Extracted);
        EXPECT_TRUE(Result.mbUsedImmediateMode);
        EXPECT_FALSE(Result.mbUsedParallelRecording);
        EXPECT_EQ(Result.mClobberedResourceCount, 1u);
        EXPECT_EQ(Result.mBufferPoolReuseCount, 0u);
        EXPECT_TRUE(DeviceContext.mDevice->WaitForIdle());
    }
    ShutdownBackend();
}

TEST(ArdaRenderGraph, PassContextRejectsUndeclaredPhysicalAccess)
{
    using namespace arda::backend;
    using namespace arda::render_graph;

    FArdaBackendConfiguration Configuration;
    Configuration.mbEnableValidation = true;
    if (!ConfigureLinkedBackend(Configuration) || !InitializeBackend())
    {
        GTEST_SKIP() << GetBackendError().c_str();
    }

    {
        const FArdaDeviceContext& DeviceContext = GetDeviceContext();
        FARDGRenderGraphContext GraphContext;
        GraphContext.mDevice = DeviceContext.mDevice;
        GraphContext.mQueueCapabilities.mbGraphics = true;
        GraphContext.mQueueCapabilities.mbCompute =
            DeviceContext.mQueueCapabilities.mbCompute;
        GraphContext.mQueueCapabilities.mbCopy =
            DeviceContext.mQueueCapabilities.mbCopy;
        GraphContext.mDebugOptions.mbImmediateMode = true;
        FARDGBuilder Builder(GraphContext);

        rhi::FArdaRHIBufferDesc Desc;
        Desc.mDebugName = "DeclaredBuffer";
        Desc.mByteSize = 64;
        Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
        FARDGBufferRef Declared = Builder.CreateBuffer(Desc);
        Desc.mDebugName = "UndeclaredBuffer";
        FARDGBufferRef Undeclared = Builder.CreateBuffer(Desc);

        FARDGBufferAccessParameters UndeclaredParameters;
        UndeclaredParameters.mBuffer = {
            Undeclared,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "MaterializeUndeclared",
            &UndeclaredParameters,
            EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
            [] {});

        FARDGBufferAccessParameters DeclaredParameters;
        DeclaredParameters.mBuffer = {
            Declared,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "AttemptUndeclaredAccess",
            &DeclaredParameters,
            EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
            [Undeclared](
                FARDGPassExecutionContext& Context,
                const FARDGBufferAccessParameters&)
            {
                (void)Context.GetBuffer(Undeclared);
            });

        EXPECT_FATAL_CHECK(
            (void)Builder.Execute(),
            "A pass requested a buffer absent from its parameter declarations");
        EXPECT_EQ(Builder.GetLastExecutionResult(), nullptr);
        EXPECT_TRUE(DeviceContext.mDevice->WaitForIdle());
    }
    ShutdownBackend();
}

TEST(ArdaRenderGraph, RecordsIndependentPassesAndSubmitsCrossQueueWaits)
{
    using namespace arda::backend;
    using namespace arda::render_graph;

    FArdaBackendConfiguration Configuration;
    Configuration.mbEnableValidation = true;
    if (!ConfigureLinkedBackend(Configuration) || !InitializeBackend())
    {
        GTEST_SKIP() << GetBackendError().c_str();
    }

    {
        const FArdaDeviceContext& DeviceContext = GetDeviceContext();
        if (!DeviceContext.mQueueCapabilities.mbCompute)
        {
            ShutdownBackend();
            GTEST_SKIP() << "A distinct compute queue is unavailable.";
        }

        FARDGRenderGraphContext GraphContext;
        GraphContext.mDevice = DeviceContext.mDevice;
        GraphContext.mQueueCapabilities.mbGraphics = true;
        GraphContext.mQueueCapabilities.mbCompute = true;
        GraphContext.mQueueCapabilities.mbCopy =
            DeviceContext.mQueueCapabilities.mbCopy;
        FARDGBuilder Builder(GraphContext);

        rhi::FArdaRHIBufferDesc Desc;
        Desc.mDebugName = "CrossQueueBuffer";
        Desc.mByteSize = 256;
        Desc.mUsage |= rhi::EArdaRHIBufferUsage::UnorderedAccess;
        FARDGBufferRef CrossQueueBuffer = Builder.CreateBuffer(Desc);
        FARDGBufferAccessParameters ProduceParameters;
        ProduceParameters.mBuffer = {
            CrossQueueBuffer,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "GraphicsProduce",
            &ProduceParameters,
            EARDGPassFlags::Compute,
            [](FARDGPassExecutionContext& Context,
               const FARDGBufferAccessParameters& Frozen)
            {
                Context.mCommandList.ClearBufferUInt(
                    *Context.GetBuffer(Frozen.mBuffer.mBuffer),
                    7u);
            });

        FARDGBufferAccessParameters ConsumeParameters;
        ConsumeParameters.mBuffer = {
            CrossQueueBuffer,
            rhi::EArdaRHIResourceState::ShaderResource,
            {}};
        (void)Builder.AddPass(
            "AsyncConsume",
            &ConsumeParameters,
            EARDGPassFlags::Compute |
                EARDGPassFlags::AsyncCompute |
                EARDGPassFlags::NeverCull,
            [] {});

        Desc.mDebugName = "IndependentBuffer";
        FARDGBufferRef IndependentBuffer = Builder.CreateBuffer(Desc);
        FARDGBufferAccessParameters IndependentParameters;
        IndependentParameters.mBuffer = {
            IndependentBuffer,
            rhi::EArdaRHIResourceState::UnorderedAccess,
            {}};
        (void)Builder.AddPass(
            "Independent",
            &IndependentParameters,
            EARDGPassFlags::Compute | EARDGPassFlags::NeverCull,
            [](FARDGPassExecutionContext& Context,
               const FARDGBufferAccessParameters& Frozen)
            {
                Context.mCommandList.ClearBufferUInt(
                    *Context.GetBuffer(Frozen.mBuffer.mBuffer),
                    11u);
            });

        rhi::FArdaRHIBufferRef Extracted;
        Builder.QueueBufferExtraction(
            CrossQueueBuffer,
            Extracted,
            rhi::EArdaRHIResourceState::CopySource);
        const FARDGExecutionResult& Result = Builder.Execute();

        EXPECT_TRUE(Extracted);
        EXPECT_TRUE(Result.mbUsedParallelRecording);
        EXPECT_GE(Result.mQueueWaitCount, 2u);
        EXPECT_NE(Result.mLastSubmittedInstances[0], 0u);
        EXPECT_NE(Result.mLastSubmittedInstances[1], 0u);
        EXPECT_TRUE(DeviceContext.mDevice->WaitForIdle());
    }
    ShutdownBackend();
}
