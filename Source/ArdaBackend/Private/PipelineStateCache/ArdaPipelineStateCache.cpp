#include "PipelineStateCache/ArdaPipelineStateCache.h"

#include "ArdaHash.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <type_traits>

namespace arda::backend
{
    namespace
    {
        using namespace rhi;

        FArdaRHIStatus Invalid(const char* Message)
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
        }

        class FStablePipelineHasher
        {
        public:
            template <typename T,
                std::enable_if_t<std::is_integral_v<T> &&
                    !std::is_same_v<std::remove_cv_t<T>, bool>, int> = 0>
            void Add(T Value) noexcept
            {
                AddUnsigned(static_cast<uint64_t>(Value));
            }
            void Add(bool Value) noexcept { AddUnsigned(Value ? 1u : 0u); }
            template <typename T> void AddEnum(T Value) noexcept
            {
                AddUnsigned(static_cast<uint64_t>(Value));
            }
            void AddString(const eastl::string& Value) noexcept
            {
                Add(Value.size());
                private_api::AppendFnv1a64(
                    mHash, Value.data(), Value.size());
            }
            uint64_t Finish() const noexcept
            {
                return private_api::FinishPersistentHash(mHash);
            }
        private:
            void AddUnsigned(uint64_t Value) noexcept
            {
                private_api::AppendFnv1a64LittleEndian(mHash, Value);
            }
            uint64_t mHash = private_api::ArdaFnv1a64OffsetBasis;
        };

        void HashShader(
            FStablePipelineHasher& Hash,
            const FArdaRHIShaderRef& Shader) noexcept
        {
            Hash.Add(Shader ? Shader->GetPersistentCacheHash() : 0);
        }

        void HashBindingLayout(
            FStablePipelineHasher& Hash,
            const FArdaRHIBindingLayoutRef& Layout) noexcept
        {
            if (!Layout)
            {
                Hash.Add(0);
                return;
            }
            Hash.Add(1);
            const auto& Desc = Layout->GetDesc();
            Hash.AddEnum(Desc.mVisibility);
            Hash.Add(Desc.mRegisterSpace);
            Hash.Add(Desc.mbRegisterSpaceIsDescriptorSet);
            Hash.Add(Desc.mItems.size());
            for (const auto& Item : Desc.mItems)
            {
                Hash.Add(Item.mSlot);
                Hash.Add(Item.mArraySize);
                Hash.AddEnum(Item.mType);
            }
        }

        void HashInputLayout(
            FStablePipelineHasher& Hash,
            const FArdaRHIInputLayoutRef& Layout) noexcept
        {
            if (!Layout)
            {
                Hash.Add(0);
                return;
            }
            Hash.Add(1);
            const auto& Desc = Layout->GetDesc();
            Hash.Add(Desc.mAttributes.size());
            for (const auto& Attribute : Desc.mAttributes)
            {
                Hash.AddString(Attribute.mSemanticName);
                Hash.AddEnum(Attribute.mFormat);
                Hash.Add(Attribute.mArraySize);
                Hash.Add(Attribute.mBufferIndex);
                Hash.Add(Attribute.mOffset);
                Hash.Add(Attribute.mElementStride);
                Hash.Add(Attribute.mbInstanced);
            }
        }

        void HashLayouts(
            FStablePipelineHasher& Hash,
            const eastl::vector<FArdaRHIBindingLayoutRef>& Layouts) noexcept
        {
            Hash.Add(Layouts.size());
            for (const auto& Layout : Layouts)
                HashBindingLayout(Hash, Layout);
        }

        template <typename PipelineDesc>
        void HashRasterFixedFunctionState(
            FStablePipelineHasher& Hash,
            const PipelineDesc& Desc) noexcept
        {
            Hash.Add(Desc.mBlendState.mbAlphaToCoverage);
            for (const auto& Target : Desc.mBlendState.mTargets)
            {
                Hash.Add(Target.mbEnable);
                Hash.AddEnum(Target.mSourceColor);
                Hash.AddEnum(Target.mDestinationColor);
                Hash.AddEnum(Target.mSourceAlpha);
                Hash.AddEnum(Target.mDestinationAlpha);
            }
            Hash.AddEnum(Desc.mRasterState.mFillMode);
            Hash.AddEnum(Desc.mRasterState.mCullMode);
            Hash.Add(Desc.mRasterState.mbFrontCounterClockwise);
            Hash.Add(Desc.mRasterState.mbDepthClip);
            Hash.Add(Desc.mRasterState.mbScissor);
            Hash.Add(Desc.mDepthStencilState.mbDepthTest);
            Hash.Add(Desc.mDepthStencilState.mbDepthWrite);
            Hash.AddEnum(Desc.mDepthStencilState.mDepthFunc);
            Hash.Add(Desc.mColorFormats.size());
            for (auto Format : Desc.mColorFormats)
                Hash.AddEnum(Format);
            Hash.AddEnum(Desc.mDepthFormat);
            Hash.Add(Desc.mSampleCount);
        }

        uint64_t PersistentKey(const FArdaRHIComputePipelineDesc& Desc) noexcept
        {
            FStablePipelineHasher Hash;
            Hash.Add(0x434F4D50555445ull);
            HashShader(Hash, Desc.mComputeShader);
            HashLayouts(Hash, Desc.mBindingLayouts);
            return Hash.Finish();
        }

        uint64_t PersistentKey(const FArdaRHIGraphicsPipelineDesc& Desc) noexcept
        {
            FStablePipelineHasher Hash;
            Hash.Add(0x47524150484943ull);
            Hash.AddEnum(Desc.mTopology);
            Hash.Add(Desc.mPatchControlPoints);
            HashInputLayout(Hash, Desc.mInputLayout);
            HashShader(Hash, Desc.mVertexShader);
            HashShader(Hash, Desc.mHullShader);
            HashShader(Hash, Desc.mDomainShader);
            HashShader(Hash, Desc.mGeometryShader);
            HashShader(Hash, Desc.mPixelShader);
            HashLayouts(Hash, Desc.mBindingLayouts);
            HashRasterFixedFunctionState(Hash, Desc);
            return Hash.Finish();
        }

        uint64_t PersistentKey(const FArdaRHIMeshletPipelineDesc& Desc) noexcept
        {
            FStablePipelineHasher Hash;
            Hash.Add(0x4D4553484C4554ull);
            Hash.AddEnum(Desc.mTopology);
            HashShader(Hash, Desc.mAmplificationShader);
            HashShader(Hash, Desc.mMeshShader);
            HashShader(Hash, Desc.mPixelShader);
            HashLayouts(Hash, Desc.mBindingLayouts);
            HashRasterFixedFunctionState(Hash, Desc);
            return Hash.Finish();
        }

        uint64_t PersistentKey(
            const FArdaRHIRayTracingPipelineDesc& Desc) noexcept
        {
            FStablePipelineHasher Hash;
            Hash.Add(0x52415954524143ull);
            Hash.Add(Desc.mShaders.size());
            for (const auto& Shader : Desc.mShaders)
            {
                Hash.AddString(Shader.mExportName);
                HashShader(Hash, Shader.mShader);
                HashBindingLayout(Hash, Shader.mLocalBindingLayout);
            }
            Hash.Add(Desc.mHitGroups.size());
            for (const auto& Group : Desc.mHitGroups)
            {
                Hash.AddString(Group.mExportName);
                HashShader(Hash, Group.mClosestHitShader);
                HashShader(Hash, Group.mAnyHitShader);
                HashShader(Hash, Group.mIntersectionShader);
                HashBindingLayout(Hash, Group.mLocalBindingLayout);
                Hash.Add(Group.mbProceduralPrimitive);
            }
            HashLayouts(Hash, Desc.mGlobalBindingLayouts);
            Hash.Add(Desc.mMaxPayloadSize);
            Hash.Add(Desc.mMaxAttributeSize);
            Hash.Add(Desc.mMaxRecursionDepth);
            Hash.Add(Desc.mbAllowOpacityMicromaps);
            return Hash.Finish();
        }

        uint64_t PersistentKey(
            const FArdaRHIWorkGraphPipelineDesc& Desc) noexcept
        {
            FStablePipelineHasher Hash;
            Hash.Add(0x574F524B475241ull);
            Hash.AddString(Desc.mProgramName);
            Hash.AddString(Desc.mEntryPoint);
            Hash.Add(Desc.mShaders.size());
            for (const auto& Shader : Desc.mShaders)
                HashShader(Hash, Shader);
            HashLayouts(Hash, Desc.mGlobalBindingLayouts);
            Hash.Add(Desc.mMaxInputRecords);
            return Hash.Finish();
        }

        auto CreatePipeline(
            IArdaRHIDevice& Device,
            const FArdaRHIComputePipelineDesc& Desc)
        {
            return Device.CreateComputePipeline(Desc);
        }

        auto CreatePipeline(
            IArdaRHIDevice& Device,
            const FArdaRHIGraphicsPipelineDesc& Desc)
        {
            return Device.CreateGraphicsPipeline(Desc);
        }

        auto CreatePipeline(
            IArdaRHIDevice& Device,
            const FArdaRHIMeshletPipelineDesc& Desc)
        {
            return Device.CreateMeshletPipeline(Desc);
        }

        auto CreatePipeline(
            IArdaRHIDevice& Device,
            const FArdaRHIRayTracingPipelineDesc& Desc)
        {
            return Device.CreateRayTracingPipeline(Desc);
        }

        auto CreatePipeline(
            IArdaRHIDevice& Device,
            const FArdaRHIWorkGraphPipelineDesc& Desc)
        {
            return Device.CreateWorkGraphPipeline(Desc);
        }

        template <typename PipelineDesc>
        FArdaRHIStatus CompleteFramebufferDesc(
            const PipelineDesc& InitializerDesc,
            const FArdaRHIFramebufferRef& Framebuffer,
            PipelineDesc& Out)
        {
            Out = InitializerDesc;
            if (!Framebuffer)
            {
                if (Out.mColorFormats.empty() || Out.mSampleCount == 0)
                    return Invalid("An incomplete graphics initializer requires a framebuffer.");
                return Validate(Out);
            }

            const auto& FramebufferDesc = Framebuffer->GetDesc();
            eastl::vector<EArdaRHIFormat> ColorFormats;
            ColorFormats.reserve(FramebufferDesc.mColorAttachments.size());
            uint32_t SampleCount = 0;
            auto consumeAttachment = [&SampleCount](
                const FArdaRHIFramebufferTarget& Target,
                EArdaRHIFormat& OutFormat) -> FArdaRHIStatus
            {
                if (!Target.mTexture)
                    return Invalid("Framebuffer attachments must have textures.");
                OutFormat = Target.mAttachment.mFormat != EArdaRHIFormat::Unknown
                    ? Target.mAttachment.mFormat
                    : Target.mTexture->GetDesc().mFormat;
                if (OutFormat == EArdaRHIFormat::Unknown)
                    return Invalid("Framebuffer attachment format is unknown.");
                const uint32_t AttachmentSamples = Target.mTexture->GetDesc().mSampleCount;
                if (SampleCount != 0 && SampleCount != AttachmentSamples)
                    return Invalid("Framebuffer attachments have incompatible sample counts.");
                SampleCount = AttachmentSamples;
                return {};
            };

            for (const auto& Target : FramebufferDesc.mColorAttachments)
            {
                EArdaRHIFormat Format = EArdaRHIFormat::Unknown;
                if (auto Status = consumeAttachment(Target, Format); !Status)
                    return Status;
                ColorFormats.push_back(Format);
            }

            EArdaRHIFormat DepthFormat = EArdaRHIFormat::Unknown;
            if (FramebufferDesc.mDepthAttachment.mTexture)
            {
                if (auto Status = consumeAttachment(
                        FramebufferDesc.mDepthAttachment, DepthFormat);
                    !Status)
                    return Status;
            }

            if (Out.mColorFormats.empty())
                Out.mColorFormats = ColorFormats;
            else if (Out.mColorFormats != ColorFormats)
                return Invalid("Graphics initializer color formats do not match the framebuffer.");

            if (Out.mDepthFormat == EArdaRHIFormat::Unknown)
                Out.mDepthFormat = DepthFormat;
            else if (Out.mDepthFormat != DepthFormat)
                return Invalid("Graphics initializer depth format does not match the framebuffer.");

            if (SampleCount == 0)
                return Invalid("A framebuffer must contain at least one attachment.");
            if (Out.mSampleCount == 0)
                Out.mSampleCount = SampleCount;
            else if (Out.mSampleCount != SampleCount)
                return Invalid("Graphics initializer sample count does not match the framebuffer.");

            return Validate(Out);
        }
    }

    struct FArdaPipelineStateCache::FImpl
    {
        template <typename Desc, typename Pipeline>
        struct TEntry
        {
            Desc mDesc;
            Pipeline mPipeline;
            uint64_t mLastUse = 0;
            bool mbInFlight = false;
        };

        using FComputeEntry = TEntry<rhi::FArdaRHIComputePipelineDesc,
            rhi::FArdaRHIComputePipelineRef>;
        using FGraphicsEntry = TEntry<rhi::FArdaRHIGraphicsPipelineDesc,
            rhi::FArdaRHIGraphicsPipelineRef>;
        using FMeshletEntry = TEntry<rhi::FArdaRHIMeshletPipelineDesc,
            rhi::FArdaRHIMeshletPipelineRef>;
        using FRayTracingEntry = TEntry<rhi::FArdaRHIRayTracingPipelineDesc,
            rhi::FArdaRHIRayTracingPipelineRef>;
        using FWorkGraphEntry = TEntry<rhi::FArdaRHIWorkGraphPipelineDesc,
            rhi::FArdaRHIWorkGraphPipelineRef>;

        explicit FImpl(
            rhi::FArdaRHIDeviceRef InDevice,
            FArdaPipelineStateCacheConfiguration InConfiguration)
            : mDevice(eastl::move(InDevice)), mConfiguration(InConfiguration)
        {
        }

        template <typename Entry>
        static void EvictTo(eastl::vector<Entry>& Entries, size_t Capacity)
        {
            while (Entries.size() > Capacity)
            {
                auto Victim = Entries.end();
                for (auto It = Entries.begin(); It != Entries.end(); ++It)
                {
                    if (It->mbInFlight)
                        continue;
                    if (Victim == Entries.end() || It->mLastUse < Victim->mLastUse)
                        Victim = It;
                }
                if (Victim == Entries.end())
                    break;
                Entries.erase(Victim);
            }
        }

        void AddDiagnostic(
            EArdaPipelineStateKind Kind,
            const rhi::FArdaRHIStatus& Status,
            size_t Hash,
            const eastl::string& DebugName)
        {
            if (mConfiguration.mMaxDiagnostics == 0)
                return;
            if (mDiagnostics.size() == mConfiguration.mMaxDiagnostics)
                mDiagnostics.erase(mDiagnostics.begin());
            mDiagnostics.push_back(
                { Kind, Status.mCode, Hash, DebugName, Status.mMessage });
        }

        rhi::FArdaRHIStatus CheckDevice(
            const rhi::IArdaRHIDevice* RequestingDevice,
            EArdaPipelineStateKind Kind,
            size_t Hash,
            const eastl::string& DebugName)
        {
            if (!mDevice)
            {
                auto Status = rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::InvalidState,
                    "Pipeline state cache has no device.");
                AddDiagnostic(Kind, Status, Hash, DebugName);
                return Status;
            }
            if (RequestingDevice != nullptr && RequestingDevice != mDevice.Get())
            {
                auto Status = rhi::FArdaRHIStatus::Error(
                    rhi::EArdaRHIResult::WrongDevice,
                    "Pipeline state cache was used with a different device.");
                AddDiagnostic(Kind, Status, Hash, DebugName);
                return Status;
            }
            return {};
        }

        template <typename Entry, typename Desc, typename Pipeline>
        rhi::FArdaRHIStatus GetOrCreate(
            eastl::vector<Entry>& Entries,
            const Desc& CanonicalDesc,
            const rhi::FArdaRHIStatus& PreparationStatus,
            EArdaPipelineStateKind Kind,
            size_t Capacity,
            Pipeline& OutPipeline,
            const rhi::IArdaRHIDevice* RequestingDevice)
        {
            OutPipeline.Reset();
            const size_t Hash = rhi::HashValue(CanonicalDesc);
            std::unique_lock<std::mutex> Lock(mMutex);
            if (auto Status = CheckDevice(
                    RequestingDevice, Kind, Hash,
                    CanonicalDesc.mDebugName);
                !Status)
                return Status;
            if (!PreparationStatus)
            {
                AddDiagnostic(
                    Kind, PreparationStatus, Hash,
                    CanonicalDesc.mDebugName);
                return PreparationStatus;
            }

            for (;;)
            {
                auto It = std::find_if(
                    Entries.begin(), Entries.end(),
                    [&CanonicalDesc](const Entry& Candidate)
                    {
                        return Candidate.mDesc == CanonicalDesc;
                    });
                if (It == Entries.end())
                    break;
                if (It->mbInFlight)
                {
                    ++mWaits;
                    mChanged.wait(
                        Lock,
                        [this, &Entries, &CanonicalDesc]
                        {
                            const auto Pending = std::find_if(
                                Entries.begin(), Entries.end(),
                                [&CanonicalDesc](const Entry& Candidate)
                                {
                                    return Candidate.mDesc == CanonicalDesc;
                                });
                            return Pending == Entries.end() ||
                                !Pending->mbInFlight;
                        });
                    continue;
                }
                ++mHits;
                It->mLastUse = ++mUseSerial;
                OutPipeline = It->mPipeline;
                return {};
            }

            ++mMisses;
            ++mInFlight;
            Entries.push_back(
                { CanonicalDesc, {}, ++mUseSerial, true });
            Lock.unlock();
            auto CreationDesc = CanonicalDesc;
            CreationDesc.mPersistentCacheKey = PersistentKey(CreationDesc);
            auto Created = CreatePipeline(*mDevice, CreationDesc);
            Lock.lock();
            --mInFlight;
            auto Pending = std::find_if(
                Entries.begin(), Entries.end(),
                [&CanonicalDesc](const Entry& Candidate)
                {
                    return Candidate.mbInFlight &&
                        Candidate.mDesc == CanonicalDesc;
                });
            if (!Created)
            {
                ++mCreateFailures;
                AddDiagnostic(
                    Kind, Created.mStatus, Hash,
                    CanonicalDesc.mDebugName);
                if (Pending != Entries.end())
                    Entries.erase(Pending);
                mChanged.notify_all();
                return Created.mStatus;
            }
            OutPipeline = Created.mValue;
            if (Pending != Entries.end())
            {
                Pending->mPipeline = Created.mValue;
                Pending->mbInFlight = false;
                Pending->mLastUse = ++mUseSerial;
            }
            EvictTo(Entries, Capacity);
            mChanged.notify_all();
            return {};
        }

        rhi::FArdaRHIDeviceRef mDevice;
        FArdaPipelineStateCacheConfiguration mConfiguration;
        mutable std::mutex mMutex;
        std::condition_variable mChanged;
        eastl::vector<FComputeEntry> mCompute;
        eastl::vector<FGraphicsEntry> mGraphics;
        eastl::vector<FMeshletEntry> mMeshlet;
        eastl::vector<FRayTracingEntry> mRayTracing;
        eastl::vector<FWorkGraphEntry> mWorkGraph;
        eastl::vector<FArdaPipelineStateDiagnostic> mDiagnostics;
        uint64_t mUseSerial = 0;
        uint64_t mHits = 0;
        uint64_t mMisses = 0;
        uint64_t mWaits = 0;
        uint64_t mCreateFailures = 0;
        size_t mInFlight = 0;
    };

    FArdaPipelineStateCache::FArdaPipelineStateCache(
        rhi::FArdaRHIDeviceRef Device,
        FArdaPipelineStateCacheConfiguration Configuration)
        : mImpl(std::make_unique<FImpl>(eastl::move(Device), Configuration))
    {
    }

    FArdaPipelineStateCache::~FArdaPipelineStateCache() = default;

    rhi::FArdaRHIStatus FArdaPipelineStateCache::GetOrCreateCompute(
        const FArdaComputePipelineStateInitializer& Initializer,
        rhi::FArdaRHIComputePipelineRef& OutPipeline,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        return mImpl->GetOrCreate(
            mImpl->mCompute,
            Initializer.mDesc,
            rhi::FArdaRHIStatus::Success(),
            EArdaPipelineStateKind::Compute,
            mImpl->mConfiguration.mMaxComputeEntries,
            OutPipeline,
            RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::GetOrCreateGraphics(
        const FArdaGraphicsPipelineStateInitializer& Initializer,
        const rhi::FArdaRHIFramebufferRef& Framebuffer,
        rhi::FArdaRHIGraphicsPipelineRef& OutPipeline,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        rhi::FArdaRHIGraphicsPipelineDesc Completed;
        auto CompletionStatus = CompleteFramebufferDesc(
            Initializer.mDesc, Framebuffer, Completed);
        if (!CompletionStatus)
            Completed = Initializer.mDesc;
        return mImpl->GetOrCreate(
            mImpl->mGraphics,
            Completed,
            CompletionStatus,
            EArdaPipelineStateKind::Graphics,
            mImpl->mConfiguration.mMaxGraphicsEntries,
            OutPipeline,
            RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::GetOrCreateMeshlet(
        const FArdaMeshletPipelineStateInitializer& Initializer,
        const rhi::FArdaRHIFramebufferRef& Framebuffer,
        rhi::FArdaRHIMeshletPipelineRef& OutPipeline,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        rhi::FArdaRHIMeshletPipelineDesc Completed;
        auto CompletionStatus = CompleteFramebufferDesc(
            Initializer.mDesc, Framebuffer, Completed);
        if (!CompletionStatus)
            Completed = Initializer.mDesc;
        return mImpl->GetOrCreate(
            mImpl->mMeshlet,
            Completed,
            CompletionStatus,
            EArdaPipelineStateKind::Meshlet,
            mImpl->mConfiguration.mMaxMeshletEntries,
            OutPipeline,
            RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::GetOrCreateRayTracing(
        const FArdaRayTracingPipelineStateInitializer& Initializer,
        rhi::FArdaRHIRayTracingPipelineRef& OutPipeline,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        return mImpl->GetOrCreate(
            mImpl->mRayTracing,
            Initializer.mDesc,
            rhi::FArdaRHIStatus::Success(),
            EArdaPipelineStateKind::RayTracing,
            mImpl->mConfiguration.mMaxRayTracingEntries,
            OutPipeline,
            RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::GetOrCreateWorkGraph(
        const FArdaWorkGraphPipelineStateInitializer& Initializer,
        rhi::FArdaRHIWorkGraphPipelineRef& OutPipeline,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        return mImpl->GetOrCreate(
            mImpl->mWorkGraph,
            Initializer.mDesc,
            rhi::FArdaRHIStatus::Success(),
            EArdaPipelineStateKind::WorkGraph,
            mImpl->mConfiguration.mMaxWorkGraphEntries,
            OutPipeline,
            RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::PrecacheCompute(
        const FArdaComputePipelineStateInitializer& Initializer,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        rhi::FArdaRHIComputePipelineRef Pipeline;
        return GetOrCreateCompute(Initializer, Pipeline, RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::PrecacheGraphics(
        const FArdaGraphicsPipelineStateInitializer& Initializer,
        const rhi::FArdaRHIFramebufferRef& Framebuffer,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        rhi::FArdaRHIGraphicsPipelineRef Pipeline;
        return GetOrCreateGraphics(
            Initializer, Framebuffer, Pipeline, RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::PrecacheMeshlet(
        const FArdaMeshletPipelineStateInitializer& Initializer,
        const rhi::FArdaRHIFramebufferRef& Framebuffer,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        rhi::FArdaRHIMeshletPipelineRef Pipeline;
        return GetOrCreateMeshlet(
            Initializer, Framebuffer, Pipeline, RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::PrecacheRayTracing(
        const FArdaRayTracingPipelineStateInitializer& Initializer,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        rhi::FArdaRHIRayTracingPipelineRef Pipeline;
        return GetOrCreateRayTracing(
            Initializer, Pipeline, RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::PrecacheWorkGraph(
        const FArdaWorkGraphPipelineStateInitializer& Initializer,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        rhi::FArdaRHIWorkGraphPipelineRef Pipeline;
        return GetOrCreateWorkGraph(
            Initializer, Pipeline, RequestingDevice);
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::SetComputePipelineState(
        rhi::IArdaRHICommandList& CommandList,
        const FArdaComputePipelineStateInitializer& Initializer,
        rhi::FArdaRHIComputeState State)
    {
        auto Status = GetOrCreateCompute(
            Initializer, State.mPipeline, CommandList.GetDevice());
        return Status ? CommandList.SetComputeState(State) : Status;
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::SetGraphicsPipelineState(
        rhi::IArdaRHICommandList& CommandList,
        const FArdaGraphicsPipelineStateInitializer& Initializer,
        rhi::FArdaRHIGraphicsState State)
    {
        auto Status = GetOrCreateGraphics(
            Initializer, State.mFramebuffer, State.mPipeline,
            CommandList.GetDevice());
        return Status ? CommandList.SetGraphicsState(State) : Status;
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::SetMeshletPipelineState(
        rhi::IArdaRHICommandList& CommandList,
        const FArdaMeshletPipelineStateInitializer& Initializer,
        rhi::FArdaRHIMeshletState State)
    {
        auto Status = GetOrCreateMeshlet(
            Initializer, State.mFramebuffer, State.mPipeline,
            CommandList.GetDevice());
        return Status ? CommandList.SetMeshletState(State) : Status;
    }

    void FArdaPipelineStateCache::Trim(
        size_t MaxComputeEntries,
        size_t MaxGraphicsEntries)
    {
        std::unique_lock<std::mutex> Lock(mImpl->mMutex);
        mImpl->mChanged.wait(Lock, [this] { return mImpl->mInFlight == 0; });
        FImpl::EvictTo(mImpl->mCompute, MaxComputeEntries);
        FImpl::EvictTo(mImpl->mGraphics, MaxGraphicsEntries);
    }

    void FArdaPipelineStateCache::Trim(
        size_t MaxComputeEntries,
        size_t MaxGraphicsEntries,
        size_t MaxMeshletEntries)
    {
        std::unique_lock<std::mutex> Lock(mImpl->mMutex);
        mImpl->mChanged.wait(Lock, [this] { return mImpl->mInFlight == 0; });
        FImpl::EvictTo(mImpl->mCompute, MaxComputeEntries);
        FImpl::EvictTo(mImpl->mGraphics, MaxGraphicsEntries);
        FImpl::EvictTo(mImpl->mMeshlet, MaxMeshletEntries);
    }

    void FArdaPipelineStateCache::Trim(
        size_t MaxComputeEntries,
        size_t MaxGraphicsEntries,
        size_t MaxMeshletEntries,
        size_t MaxRayTracingEntries,
        size_t MaxWorkGraphEntries)
    {
        std::unique_lock<std::mutex> Lock(mImpl->mMutex);
        mImpl->mChanged.wait(Lock, [this] { return mImpl->mInFlight == 0; });
        FImpl::EvictTo(mImpl->mCompute, MaxComputeEntries);
        FImpl::EvictTo(mImpl->mGraphics, MaxGraphicsEntries);
        FImpl::EvictTo(mImpl->mMeshlet, MaxMeshletEntries);
        FImpl::EvictTo(mImpl->mRayTracing, MaxRayTracingEntries);
        FImpl::EvictTo(mImpl->mWorkGraph, MaxWorkGraphEntries);
    }

    void FArdaPipelineStateCache::Clear()
    {
        std::unique_lock<std::mutex> Lock(mImpl->mMutex);
        mImpl->mChanged.wait(Lock, [this] { return mImpl->mInFlight == 0; });
        FImpl::EvictTo(mImpl->mCompute, 0);
        FImpl::EvictTo(mImpl->mGraphics, 0);
        FImpl::EvictTo(mImpl->mMeshlet, 0);
        FImpl::EvictTo(mImpl->mRayTracing, 0);
        FImpl::EvictTo(mImpl->mWorkGraph, 0);
    }

    FArdaPipelineStateCacheStats FArdaPipelineStateCache::GetStats() const
    {
        std::lock_guard<std::mutex> Lock(mImpl->mMutex);
        return {
            mImpl->mHits,
            mImpl->mMisses,
            mImpl->mWaits,
            mImpl->mCreateFailures,
            mImpl->mInFlight,
            mImpl->mCompute.size(),
            mImpl->mGraphics.size(),
            mImpl->mMeshlet.size(),
            mImpl->mRayTracing.size(),
            mImpl->mWorkGraph.size()
        };
    }

    eastl::vector<FArdaPipelineStateDiagnostic>
    FArdaPipelineStateCache::GetDiagnostics() const
    {
        std::lock_guard<std::mutex> Lock(mImpl->mMutex);
        return mImpl->mDiagnostics;
    }

    const rhi::IArdaRHIDevice* FArdaPipelineStateCache::GetDevice() const noexcept
    {
        return mImpl->mDevice.Get();
    }
}
