#include "PipelineStateCache/ArdaPipelineStateCache.h"

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
                for (char Byte : Value)
                {
                    mHash ^= static_cast<uint8_t>(Byte);
                    mHash *= 1099511628211ull;
                }
            }
            uint64_t Finish() const noexcept { return mHash == 0 ? 1 : mHash; }
        private:
            void AddUnsigned(uint64_t Value) noexcept
            {
                for (uint32_t Shift = 0; Shift < 64; Shift += 8)
                {
                    mHash ^= static_cast<uint8_t>(Value >> Shift);
                    mHash *= 1099511628211ull;
                }
            }
            uint64_t mHash = 14695981039346656037ull;
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
            HashShader(Hash, Desc.mVertexShader);
        }

        void HashLayouts(
            FStablePipelineHasher& Hash,
            const eastl::vector<FArdaRHIBindingLayoutRef>& Layouts) noexcept
        {
            Hash.Add(Layouts.size());
            for (const auto& Layout : Layouts)
                HashBindingLayout(Hash, Layout);
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
            return Hash.Finish();
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
        struct FComputeEntry
        {
            rhi::FArdaRHIComputePipelineDesc mDesc;
            rhi::FArdaRHIComputePipelineRef mPipeline;
            uint64_t mLastUse = 0;
            bool mbInFlight = false;
        };

        struct FGraphicsEntry
        {
            rhi::FArdaRHIGraphicsPipelineDesc mDesc;
            rhi::FArdaRHIGraphicsPipelineRef mPipeline;
            uint64_t mLastUse = 0;
            bool mbInFlight = false;
        };

        struct FMeshletEntry
        {
            rhi::FArdaRHIMeshletPipelineDesc mDesc;
            rhi::FArdaRHIMeshletPipelineRef mPipeline;
            uint64_t mLastUse = 0;
            bool mbInFlight = false;
        };

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

        rhi::FArdaRHIDeviceRef mDevice;
        FArdaPipelineStateCacheConfiguration mConfiguration;
        mutable std::mutex mMutex;
        std::condition_variable mChanged;
        eastl::vector<FComputeEntry> mCompute;
        eastl::vector<FGraphicsEntry> mGraphics;
        eastl::vector<FMeshletEntry> mMeshlet;
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
        OutPipeline.Reset();
        const size_t Hash = rhi::HashValue(Initializer.mDesc);
        std::unique_lock<std::mutex> Lock(mImpl->mMutex);
        if (auto Status = mImpl->CheckDevice(
                RequestingDevice, EArdaPipelineStateKind::Compute,
                Hash, Initializer.mDesc.mDebugName);
            !Status)
            return Status;

        for (;;)
        {
            auto It = std::find_if(
                mImpl->mCompute.begin(), mImpl->mCompute.end(),
                [&Initializer](const FImpl::FComputeEntry& Entry)
                {
                    return Entry.mDesc == Initializer.mDesc;
                });
            if (It == mImpl->mCompute.end())
                break;
            if (It->mbInFlight)
            {
                ++mImpl->mWaits;
                mImpl->mChanged.wait(
                    Lock,
                    [this, &Initializer]
                    {
                        const auto Pending = std::find_if(
                            mImpl->mCompute.begin(), mImpl->mCompute.end(),
                            [&Initializer](const FImpl::FComputeEntry& Entry)
                            {
                                return Entry.mDesc == Initializer.mDesc;
                            });
                        return Pending == mImpl->mCompute.end() ||
                            !Pending->mbInFlight;
                    });
                continue;
            }
            ++mImpl->mHits;
            It->mLastUse = ++mImpl->mUseSerial;
            OutPipeline = It->mPipeline;
            return {};
        }

        ++mImpl->mMisses;
        ++mImpl->mInFlight;
        mImpl->mCompute.push_back(
            { Initializer.mDesc, {}, ++mImpl->mUseSerial, true });
        Lock.unlock();
        auto CreationDesc = Initializer.mDesc;
        CreationDesc.mPersistentCacheKey = PersistentKey(CreationDesc);
        auto Created = mImpl->mDevice->CreateComputePipeline(CreationDesc);
        Lock.lock();
        --mImpl->mInFlight;
        auto Pending = std::find_if(
            mImpl->mCompute.begin(), mImpl->mCompute.end(),
            [&Initializer](const FImpl::FComputeEntry& Entry)
            {
                return Entry.mbInFlight && Entry.mDesc == Initializer.mDesc;
            });
        if (!Created)
        {
            ++mImpl->mCreateFailures;
            mImpl->AddDiagnostic(
                EArdaPipelineStateKind::Compute, Created.mStatus, Hash,
                Initializer.mDesc.mDebugName);
            if (Pending != mImpl->mCompute.end())
                mImpl->mCompute.erase(Pending);
            mImpl->mChanged.notify_all();
            return Created.mStatus;
        }
        OutPipeline = Created.mValue;
        if (Pending != mImpl->mCompute.end())
        {
            Pending->mPipeline = Created.mValue;
            Pending->mbInFlight = false;
            Pending->mLastUse = ++mImpl->mUseSerial;
        }
        FImpl::EvictTo(mImpl->mCompute, mImpl->mConfiguration.mMaxComputeEntries);
        mImpl->mChanged.notify_all();
        return {};
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::GetOrCreateGraphics(
        const FArdaGraphicsPipelineStateInitializer& Initializer,
        const rhi::FArdaRHIFramebufferRef& Framebuffer,
        rhi::FArdaRHIGraphicsPipelineRef& OutPipeline,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        OutPipeline.Reset();
        rhi::FArdaRHIGraphicsPipelineDesc Completed;
        auto CompletionStatus = CompleteFramebufferDesc(
            Initializer.mDesc, Framebuffer, Completed);
        const size_t Hash = CompletionStatus
            ? rhi::HashValue(Completed)
            : rhi::HashValue(Initializer.mDesc);
        std::unique_lock<std::mutex> Lock(mImpl->mMutex);
        if (auto Status = mImpl->CheckDevice(
                RequestingDevice, EArdaPipelineStateKind::Graphics,
                Hash, Initializer.mDesc.mDebugName);
            !Status)
            return Status;
        if (!CompletionStatus)
        {
            mImpl->AddDiagnostic(
                EArdaPipelineStateKind::Graphics, CompletionStatus, Hash,
                Initializer.mDesc.mDebugName);
            return CompletionStatus;
        }

        for (;;)
        {
            auto It = std::find_if(
                mImpl->mGraphics.begin(), mImpl->mGraphics.end(),
                [&Completed](const FImpl::FGraphicsEntry& Entry)
                {
                    return Entry.mDesc == Completed;
                });
            if (It == mImpl->mGraphics.end())
                break;
            if (It->mbInFlight)
            {
                ++mImpl->mWaits;
                mImpl->mChanged.wait(
                    Lock,
                    [this, &Completed]
                    {
                        const auto Pending = std::find_if(
                            mImpl->mGraphics.begin(), mImpl->mGraphics.end(),
                            [&Completed](const FImpl::FGraphicsEntry& Entry)
                            {
                                return Entry.mDesc == Completed;
                            });
                        return Pending == mImpl->mGraphics.end() ||
                            !Pending->mbInFlight;
                    });
                continue;
            }
            ++mImpl->mHits;
            It->mLastUse = ++mImpl->mUseSerial;
            OutPipeline = It->mPipeline;
            return {};
        }

        ++mImpl->mMisses;
        ++mImpl->mInFlight;
        mImpl->mGraphics.push_back(
            { Completed, {}, ++mImpl->mUseSerial, true });
        Lock.unlock();
        auto CreationDesc = Completed;
        CreationDesc.mPersistentCacheKey = PersistentKey(CreationDesc);
        auto Created = mImpl->mDevice->CreateGraphicsPipeline(CreationDesc);
        Lock.lock();
        --mImpl->mInFlight;
        auto Pending = std::find_if(
            mImpl->mGraphics.begin(), mImpl->mGraphics.end(),
            [&Completed](const FImpl::FGraphicsEntry& Entry)
            {
                return Entry.mbInFlight && Entry.mDesc == Completed;
            });
        if (!Created)
        {
            ++mImpl->mCreateFailures;
            mImpl->AddDiagnostic(
                EArdaPipelineStateKind::Graphics, Created.mStatus, Hash,
                Initializer.mDesc.mDebugName);
            if (Pending != mImpl->mGraphics.end())
                mImpl->mGraphics.erase(Pending);
            mImpl->mChanged.notify_all();
            return Created.mStatus;
        }
        OutPipeline = Created.mValue;
        if (Pending != mImpl->mGraphics.end())
        {
            Pending->mPipeline = Created.mValue;
            Pending->mbInFlight = false;
            Pending->mLastUse = ++mImpl->mUseSerial;
        }
        FImpl::EvictTo(mImpl->mGraphics, mImpl->mConfiguration.mMaxGraphicsEntries);
        mImpl->mChanged.notify_all();
        return {};
    }

    rhi::FArdaRHIStatus FArdaPipelineStateCache::GetOrCreateMeshlet(
        const FArdaMeshletPipelineStateInitializer& Initializer,
        const rhi::FArdaRHIFramebufferRef& Framebuffer,
        rhi::FArdaRHIMeshletPipelineRef& OutPipeline,
        const rhi::IArdaRHIDevice* RequestingDevice)
    {
        OutPipeline.Reset();
        rhi::FArdaRHIMeshletPipelineDesc Completed;
        auto CompletionStatus = CompleteFramebufferDesc(
            Initializer.mDesc, Framebuffer, Completed);
        const size_t Hash = CompletionStatus
            ? rhi::HashValue(Completed)
            : rhi::HashValue(Initializer.mDesc);
        std::unique_lock<std::mutex> Lock(mImpl->mMutex);
        if (auto Status = mImpl->CheckDevice(
                RequestingDevice, EArdaPipelineStateKind::Meshlet,
                Hash, Initializer.mDesc.mDebugName);
            !Status)
            return Status;
        if (!CompletionStatus)
        {
            mImpl->AddDiagnostic(
                EArdaPipelineStateKind::Meshlet, CompletionStatus, Hash,
                Initializer.mDesc.mDebugName);
            return CompletionStatus;
        }

        for (;;)
        {
            auto It = std::find_if(
                mImpl->mMeshlet.begin(), mImpl->mMeshlet.end(),
                [&Completed](const FImpl::FMeshletEntry& Entry)
                {
                    return Entry.mDesc == Completed;
                });
            if (It == mImpl->mMeshlet.end())
                break;
            if (It->mbInFlight)
            {
                ++mImpl->mWaits;
                mImpl->mChanged.wait(
                    Lock,
                    [this, &Completed]
                    {
                        const auto Pending = std::find_if(
                            mImpl->mMeshlet.begin(), mImpl->mMeshlet.end(),
                            [&Completed](const FImpl::FMeshletEntry& Entry)
                            {
                                return Entry.mDesc == Completed;
                            });
                        return Pending == mImpl->mMeshlet.end() ||
                            !Pending->mbInFlight;
                    });
                continue;
            }
            ++mImpl->mHits;
            It->mLastUse = ++mImpl->mUseSerial;
            OutPipeline = It->mPipeline;
            return {};
        }

        ++mImpl->mMisses;
        ++mImpl->mInFlight;
        mImpl->mMeshlet.push_back(
            { Completed, {}, ++mImpl->mUseSerial, true });
        Lock.unlock();
        auto CreationDesc = Completed;
        CreationDesc.mPersistentCacheKey = PersistentKey(CreationDesc);
        auto Created = mImpl->mDevice->CreateMeshletPipeline(CreationDesc);
        Lock.lock();
        --mImpl->mInFlight;
        auto Pending = std::find_if(
            mImpl->mMeshlet.begin(), mImpl->mMeshlet.end(),
            [&Completed](const FImpl::FMeshletEntry& Entry)
            {
                return Entry.mbInFlight && Entry.mDesc == Completed;
            });
        if (!Created)
        {
            ++mImpl->mCreateFailures;
            mImpl->AddDiagnostic(
                EArdaPipelineStateKind::Meshlet, Created.mStatus, Hash,
                Initializer.mDesc.mDebugName);
            if (Pending != mImpl->mMeshlet.end())
                mImpl->mMeshlet.erase(Pending);
            mImpl->mChanged.notify_all();
            return Created.mStatus;
        }
        OutPipeline = Created.mValue;
        if (Pending != mImpl->mMeshlet.end())
        {
            Pending->mPipeline = Created.mValue;
            Pending->mbInFlight = false;
            Pending->mLastUse = ++mImpl->mUseSerial;
        }
        FImpl::EvictTo(
            mImpl->mMeshlet, mImpl->mConfiguration.mMaxMeshletEntries);
        mImpl->mChanged.notify_all();
        return {};
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

    void FArdaPipelineStateCache::Clear()
    {
        std::unique_lock<std::mutex> Lock(mImpl->mMutex);
        mImpl->mChanged.wait(Lock, [this] { return mImpl->mInFlight == 0; });
        FImpl::EvictTo(mImpl->mCompute, 0);
        FImpl::EvictTo(mImpl->mGraphics, 0);
        FImpl::EvictTo(mImpl->mMeshlet, 0);
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
            mImpl->mMeshlet.size()
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
