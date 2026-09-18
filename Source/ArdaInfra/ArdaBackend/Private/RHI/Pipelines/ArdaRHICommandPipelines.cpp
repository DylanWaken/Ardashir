#include "RHI/Device/ArdaRHIDeviceImpl.h"

namespace arda::detail
{
	FArdaRHIStatus FArdaCommandList::ResolveBindings(const eastl::vector<FArdaRHIBindingSetRef>& Bindings,
	    eastl::vector<FArdaProviderObjectRef>& OutBindings,
	    const eastl::vector<FArdaRHIBindingLayoutRef>& Layouts) const
	{
		eastl::vector<FArdaRHIBindingLayoutRef> SuppliedLayouts;
		eastl::vector<FArdaProviderObjectRef> SuppliedBindings;
		for (const auto& Binding : Bindings)
		{
			auto* Resource = Cast<FArdaResource>(Binding.Get());
			if (!RetainOwned(Resource))
			{
				return WrongDevice();
			}
			FArdaProviderObjectRef Native;
			FArdaRHIBindingLayoutRef Layout;
			if (const auto* Table = Cast<FArdaDescriptorTable>(Resource))
			{
				std::lock_guard<std::mutex> Lock(Table->mMutex);
				Layout = Table->mDesc.mLayout;
				Native = Table->mNative;
			}
			else if (const auto* Set = Cast<FArdaBindingSet>(Resource))
			{
				Layout = Set->mDesc.mLayout;
				Native = Set->mNative;
			}
			if (!Native)
			{
				return WrongDevice();
			}
			if (eastl::find(Layouts.begin(), Layouts.end(), Layout) == Layouts.end() ||
			    eastl::find(SuppliedLayouts.begin(), SuppliedLayouts.end(), Layout) != SuppliedLayouts.end())
			{
				return Invalid("Binding sets require distinct layouts declared by the active pipeline.");
			}
			SuppliedLayouts.push_back(Layout);
			SuppliedBindings.push_back(eastl::move(Native));
		}
		OutBindings.reserve(Layouts.size());
		for (const auto& Layout : Layouts)
		{
			const auto Found = eastl::find(SuppliedLayouts.begin(), SuppliedLayouts.end(), Layout);
			if (Found != SuppliedLayouts.end())
			{
				OutBindings.push_back(SuppliedBindings[Found - SuppliedLayouts.begin()]);
				continue;
			}
			if (eastl::any_of(Layout->GetDesc().mItems.begin(),
			        Layout->GetDesc().mItems.end(),
			        [](const FArdaRHIBindingLayoutItem& Item)
			        {
				        return Item.mType != EArdaRHIBindingType::PushConstants;
			        }))
			{
				return Invalid("Every descriptor-bearing pipeline layout requires a matching binding set.");
			}
			// Push-only layouts need no caller set; preserve native positional layout order on both providers.
			auto Empty = mEmptyBindingSets.find(Layout.Get());
			if (Empty == mEmptyBindingSets.end())
			{
				FArdaRHIBindingSetDesc EmptyDesc;
				EmptyDesc.mLayout = Layout;
				auto Created = mDevice->CreateBindingSet(EmptyDesc);
				if (!Created)
				{
					return Created.mStatus;
				}
				Empty = mEmptyBindingSets.emplace(Layout.Get(), eastl::move(Created.mValue)).first;
			}
			auto* Resource = Cast<FArdaResource>(Empty->second.Get());
			RetainOwned(Resource);
			OutBindings.push_back(CaptureNativeBindings(Resource));
		}
		return {};
	}

	FArdaRHIStatus FArdaCommandList::ValidateRecording(bool bGraphics) const
	{
		if (!mRecordingStatus)
		{
			return mRecordingStatus;
		}
		if (!mbRecordingOpen || mQueue == EArdaRHIQueueType::Copy ||
		    (bGraphics && mQueue != EArdaRHIQueueType::Graphics))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Shader commands require an open command list on a compatible queue.");
		}
		return {};
	}

	FArdaRHIStatus FArdaCommandList::ValidateWork(EArdaPipelineKind Kind) const
	{
		if (auto Status = ValidateRecording(Kind == EArdaPipelineKind::Graphics || Kind == EArdaPipelineKind::Mesh);
		    !Status)
		{
			return Status;
		}
		if (mPipelineKind != Kind)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "The command requires its pipeline state to be bound in this recording generation.");
		}
		return {};
	}

	FArdaRHIStatus FArdaCommandList::ValidateDraw(const FArdaRHIDrawArguments& Arguments, bool bIndexed) const
	{
		if (auto Status = ValidateWork(EArdaPipelineKind::Graphics); !Status)
		{
			return Status;
		}
		if (bIndexed && !mGraphicsState.mIndexBuffer)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Indexed draws require an index buffer.");
		}
		// Zero-work draws still require valid state, but never fetch vertex or index data.
		if (!Arguments.mVertexCount || !Arguments.mInstanceCount)
		{
			return {};
		}
		if (bIndexed)
		{
			const uint64_t IndexSize = mGraphicsState.mIndexFormat == EArdaRHIFormat::R16UInt ? 2u : 4u;
			const uint64_t Available =
			    mGraphicsState.mIndexBuffer->GetDesc().mByteSize - mGraphicsState.mIndexOffset;
			if ((uint64_t(Arguments.mStartIndex) + Arguments.mVertexCount) > Available / IndexSize)
			{
				return Invalid("Indexed draw exceeds the bound index-buffer range.");
			}
		}
		const auto& Layout = mGraphicsState.mPipeline->GetDesc().mInputLayout;
		if (Layout)
		{
			for (const auto& Attribute : Layout->GetDesc().mAttributes)
			{
				// Index values reside on the GPU; their vertex-buffer bounds remain the caller's responsibility.
				if (bIndexed && !Attribute.mbInstanced)
				{
					continue;
				}
				const auto Binding = eastl::find_if(mGraphicsState.mVertexBuffers.begin(),
				    mGraphicsState.mVertexBuffers.end(),
				    [&](const FArdaRHIVertexBufferBinding& Value)
				    {
					    return Value.mSlot == Attribute.mBufferIndex;
				    });
				const uint64_t Last = Attribute.mbInstanced
				    ? uint64_t(Arguments.mStartInstance) + Arguments.mInstanceCount - 1
				    : uint64_t(Arguments.mStartVertex) + Arguments.mVertexCount - 1;
				const uint64_t ElementEnd = uint64_t(Attribute.mOffset) +
				    uint64_t(GetArdaRHIFormatElementSize(Attribute.mFormat)) * Attribute.mArraySize;
				const uint64_t Available = Binding->mBuffer->GetDesc().mByteSize - Binding->mOffset;
				if (ElementEnd > Available ||
				    (Attribute.mElementStride && Last > (Available - ElementEnd) / Attribute.mElementStride))
				{
					return Invalid("Draw exceeds a bound vertex-buffer range.");
				}
			}
		}
		return {};
	}

	void FArdaCommandList::Draw(const FArdaRHIDrawArguments& Arguments)
	{
		const auto Status = ValidateDraw(Arguments, false);
		LatchError(Status);
		if (Status && Arguments.mVertexCount && Arguments.mInstanceCount)
		{
			mNative->Draw(Arguments);
		}
	}

	void FArdaCommandList::DrawIndexed(const FArdaRHIDrawArguments& Arguments)
	{
		const auto Status = ValidateDraw(Arguments, true);
		LatchError(Status);
		if (Status && Arguments.mVertexCount && Arguments.mInstanceCount)
		{
			mNative->DrawIndexed(Arguments);
		}
	}

	void FArdaCommandList::Dispatch(uint32_t X, uint32_t Y, uint32_t Z)
	{
		if (auto Status = ValidateWork(EArdaPipelineKind::Compute); !Status)
		{
			LatchError(Status);
			return;
		}
		const uint32_t Groups[] = {X, Y, Z};
		const auto& Limits = mDevice->GetCapabilities().mLimits;
		for (uint32_t Axis = 0; Axis < 3; ++Axis)
		{
			if (Limits.mMaxComputeWorkGroupCount[Axis] && Groups[Axis] > Limits.mMaxComputeWorkGroupCount[Axis])
			{
				LatchError(Invalid("Dispatch exceeds the device work-group count limit."));
				return;
			}
		}
		if (X && Y && Z)
		{
			mNative->Dispatch(X, Y, Z);
		}
	}

	template <typename TPipeline, typename TState>
	FArdaRHIStatus ValidateRasterState(const TPipeline& Pipeline,
	    const TState& State,
	    const FArdaRHIDeviceLimits& Limits)
	{
		const auto& Framebuffer = State.mFramebuffer->GetDesc();
		const auto Format = [](const FArdaRHIFramebufferTarget& Target)
		{
			return !Target.mTexture                                     ? EArdaRHIFormat::Unknown
			    : Target.mAttachment.mFormat == EArdaRHIFormat::Unknown ? Target.mTexture->GetDesc().mFormat
			                                                            : Target.mAttachment.mFormat;
		};
		if (Pipeline.mColorFormats.size() != Framebuffer.mColorAttachments.size() ||
		    Pipeline.mDepthFormat != Format(Framebuffer.mDepthAttachment))
		{
			return Invalid("Pipeline attachment formats must match the framebuffer.");
		}
		for (size_t Index = 0; Index < Framebuffer.mColorAttachments.size(); ++Index)
		{
			const auto& Target = Framebuffer.mColorAttachments[Index];
			if (Pipeline.mColorFormats[Index] != Format(Target) ||
			    Pipeline.mSampleCount != Target.mTexture->GetDesc().mSampleCount)
			{
				return Invalid("Pipeline attachment formats and sample counts must match the framebuffer.");
			}
		}
		if (Framebuffer.mDepthAttachment.mTexture &&
		    Pipeline.mSampleCount != Framebuffer.mDepthAttachment.mTexture->GetDesc().mSampleCount)
		{
			return Invalid("Pipeline depth sample count must match the framebuffer.");
		}
		if (Framebuffer.mDepthAttachment.mAttachment.mbReadOnly && Pipeline.mDepthStencilState.mbDepthTest &&
		    Pipeline.mDepthStencilState.mbDepthWrite)
		{
			return Invalid("A depth-writing pipeline cannot bind a read-only depth attachment.");
		}
		const size_t ViewportCount = eastl::max<size_t>(1, State.mViewports.size());
		const size_t ScissorCount = eastl::max<size_t>(1, State.mScissors.size());
		if (ViewportCount != ScissorCount || (Limits.mMaxViewports && ViewportCount > Limits.mMaxViewports))
		{
			return Invalid("Viewport and scissor counts must match and fit the device limit.");
		}
		for (const auto& Viewport : State.mViewports)
		{
			if (!std::isfinite(Viewport.mMinX) || !std::isfinite(Viewport.mMaxX) ||
			    !std::isfinite(Viewport.mMinY) || !std::isfinite(Viewport.mMaxY) ||
			    !std::isfinite(Viewport.mMinZ) || !std::isfinite(Viewport.mMaxZ) ||
			    Viewport.mMinX >= Viewport.mMaxX || Viewport.mMinY >= Viewport.mMaxY || Viewport.mMinZ < 0.f ||
			    Viewport.mMinZ > 1.f || Viewport.mMaxZ < 0.f || Viewport.mMaxZ > 1.f)
			{
				return Invalid("Viewports require finite positive extents and depth endpoints in [0, 1].");
			}
			const float Minimum[] = {Viewport.mMinX, Viewport.mMinY};
			const float Maximum[] = {Viewport.mMaxX, Viewport.mMaxY};
			for (uint32_t Axis = 0; Axis < 2; ++Axis)
			{
				if ((Limits.mMaxViewportDimensions[Axis] &&
				        double(Maximum[Axis]) - Minimum[Axis] > Limits.mMaxViewportDimensions[Axis]) ||
				    (Limits.mViewportBounds[0] < Limits.mViewportBounds[1] &&
				        (Minimum[Axis] < Limits.mViewportBounds[0] || Maximum[Axis] > Limits.mViewportBounds[1])))
				{
					return Invalid("Viewport exceeds native dimensions or coordinate bounds.");
				}
			}
		}
		for (const auto& Scissor : State.mScissors)
		{
			if (Scissor.mMinX < 0 || Scissor.mMinY < 0 || Scissor.mMaxX < Scissor.mMinX ||
			    Scissor.mMaxY < Scissor.mMinY)
			{
				return Invalid("Scissors require nonnegative ordered coordinates.");
			}
		}
		return {};
	}

	FArdaRHIStatus FArdaCommandList::SetGraphicsState(const FArdaRHIGraphicsState& State)
	{
		if (auto Status = ValidateRecording(true); !Status)
		{
			return Status;
		}
		auto* Pipeline = Cast<FArdaGraphicsPipeline>(State.mPipeline.Get());
		auto* Framebuffer = Cast<FArdaFramebuffer>(State.mFramebuffer.Get());
		if (!Pipeline || !Framebuffer || !RetainOwned(Pipeline) || !RetainOwned(Framebuffer))
		{
			return WrongDevice();
		}
		FArdaProviderGraphicsState Native;
		const auto& Limits = mDevice->GetCapabilities().mLimits;
		if (auto Status = ValidateRasterState(Pipeline->mDesc, State, Limits); !Status)
		{
			return Status;
		}
		for (size_t Index = 0; Index < State.mVertexBuffers.size(); ++Index)
		{
			const auto& Binding = State.mVertexBuffers[Index];
			if ((Limits.mMaxVertexBindings && Binding.mSlot >= Limits.mMaxVertexBindings) ||
			    eastl::find_if(State.mVertexBuffers.begin(),
			        State.mVertexBuffers.begin() + Index,
			        [&](const FArdaRHIVertexBufferBinding& Other)
			        {
				        return Other.mSlot == Binding.mSlot;
			        }) != State.mVertexBuffers.begin() + Index)
			{
				return Invalid("Vertex-buffer slots must be unique and fit the device limit.");
			}
		}
		if (Pipeline->mDesc.mInputLayout)
		{
			for (const auto& Attribute : Pipeline->mDesc.mInputLayout->GetDesc().mAttributes)
			{
				if (eastl::find_if(State.mVertexBuffers.begin(),
				        State.mVertexBuffers.end(),
				        [&](const FArdaRHIVertexBufferBinding& Binding)
				        {
					        return Binding.mSlot == Attribute.mBufferIndex;
				        }) == State.mVertexBuffers.end())
				{
					return Invalid("Every input-layout slot requires a vertex-buffer binding.");
				}
			}
		}
		Native.mPipeline = Pipeline->mNative;
		Native.mFramebuffer = Framebuffer->mNative;
		Native.mIndexFormat = State.mIndexFormat;
		Native.mIndexOffset = State.mIndexOffset;
		Native.mViewports = State.mViewports;
		Native.mScissors = State.mScissors;
		if (auto Status = ResolveBindings(State.mBindings, Native.mBindings, Pipeline->mDesc.mBindingLayouts);
		    !Status)
		{
			return Status;
		}
		for (const auto& Binding : State.mVertexBuffers)
		{
			auto* Buffer = Cast<FArdaBuffer>(Binding.mBuffer.Get());
			if (!Buffer || !RetainOwned(Buffer))
			{
				return WrongDevice();
			}
			uint32_t Stride = 0;
			if (!HasAnyFlags(Buffer->mDesc.mUsage, EArdaRHIBufferUsage::Vertex) ||
			    Binding.mOffset >= Buffer->mDesc.mByteSize)
			{
				return Invalid("Vertex bindings require Vertex usage and an offset within the buffer.");
			}
			if (Pipeline->mDesc.mInputLayout)
			{
				const auto& Attributes = Pipeline->mDesc.mInputLayout->GetDesc().mAttributes;
				for (const auto& Attribute : Attributes)
				{
					if (Attribute.mBufferIndex == Binding.mSlot)
					{
						const uint32_t Alignment = GetArdaRHIVertexFormatAlignment(Attribute.mFormat);
						if (!Alignment || Binding.mOffset % Alignment)
						{
							return Invalid("Vertex-buffer offset is not aligned for its attribute format.");
						}
						const uint64_t ElementEnd = uint64_t(Attribute.mOffset) +
						    uint64_t(GetArdaRHIFormatElementSize(Attribute.mFormat)) * Attribute.mArraySize;
						if (ElementEnd > Buffer->mDesc.mByteSize - Binding.mOffset)
						{
							return Invalid("Vertex attribute exceeds the bound buffer range.");
						}
						Stride = eastl::max(Stride, Attribute.mElementStride);
					}
				}
			}
			Native.mVertexBuffers.push_back(
			    {Buffer->mNative, Binding.mSlot, Binding.mOffset, Stride, Buffer->mDesc.mByteSize});
		}
		if (State.mIndexBuffer)
		{
			auto* Buffer = Cast<FArdaBuffer>(State.mIndexBuffer.Get());
			if (!Buffer || !RetainOwned(Buffer))
			{
				return WrongDevice();
			}
			Native.mIndexBuffer = Buffer->mNative;
			const uint32_t IndexSize = State.mIndexFormat == EArdaRHIFormat::R16UInt ? 2u
			    : State.mIndexFormat == EArdaRHIFormat::R32UInt                      ? 4u
			                                                                         : 0u;
			if (!IndexSize || State.mIndexOffset % IndexSize || State.mIndexOffset >= Buffer->mDesc.mByteSize ||
			    IndexSize > Buffer->mDesc.mByteSize - State.mIndexOffset ||
			    !HasAnyFlags(Buffer->mDesc.mUsage, EArdaRHIBufferUsage::Index))
			{
				return Invalid("Index bindings require Index usage, R16_UINT or R32_UINT, and an aligned range.");
			}
		}
		const auto Status = mNative->SetGraphicsState(Native);
		LatchError(Status);
		if (Status)
		{
			mGraphicsState = State;
			mPipelineKind = EArdaPipelineKind::Graphics;
			mPushConstantCapacity = PushConstantCapacity(Pipeline->mDesc.mBindingLayouts);
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::SetComputeState(const FArdaRHIComputeState& State)
	{
		if (auto Status = ValidateRecording(false); !Status)
		{
			return Status;
		}
		auto* Pipeline = Cast<FArdaComputePipeline>(State.mPipeline.Get());
		if (!Pipeline || !RetainOwned(Pipeline))
		{
			return WrongDevice();
		}
		FArdaProviderComputeState Native;
		Native.mPipeline = Pipeline->mNative;
		if (auto Status = ResolveBindings(State.mBindings, Native.mBindings, Pipeline->mDesc.mBindingLayouts);
		    !Status)
		{
			return Status;
		}
		const auto Status = mNative->SetComputeState(Native);
		LatchError(Status);
		if (Status)
		{
			mPipelineKind = EArdaPipelineKind::Compute;
			mPushConstantCapacity = PushConstantCapacity(Pipeline->mDesc.mBindingLayouts);
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::DrawIndirect(IArdaRHIBuffer& Arguments,
	    uint64_t Offset,
	    uint32_t DrawCount,
	    uint32_t Stride)
	{
		auto* Buffer = Cast<FArdaBuffer>(&Arguments);
		constexpr uint32_t ArgumentSize = 4u * sizeof(uint32_t);
		const uint32_t ResolvedStride = Stride ? Stride : ArgumentSize;
		if (!Buffer || !RetainOwned(Buffer))
		{
			return WrongDevice();
		}
		if (!HasAnyFlags(Buffer->mDesc.mUsage, EArdaRHIBufferUsage::Indirect) || DrawCount == 0 ||
		    Offset % sizeof(uint32_t) || ResolvedStride % sizeof(uint32_t) || ResolvedStride < ArgumentSize ||
		    Offset > Buffer->mDesc.mByteSize ||
		    static_cast<uint64_t>(DrawCount - 1) * ResolvedStride + ArgumentSize > Buffer->mDesc.mByteSize - Offset)
		{
			return Invalid("Indirect draw arguments require DWORD alignment, valid ranges, and Indirect usage.");
		}
		const auto State = QueryBufferState(*Buffer);
		if (!State)
		{
			return State.mStatus;
		}
		if (!State.mValue.IsConsistent() ||
		    !HasAnyFlags(State.mValue.mFacadeState, EArdaRHIResourceState::IndirectArgument))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Indirect draw arguments must be in IndirectArgument state across facade, backend, and native tracking.");
		}
		if (auto Status = ValidateWork(EArdaPipelineKind::Graphics); !Status)
		{
			return Status;
		}
		return mNative->DrawIndirect(Buffer->mNative, Offset, DrawCount, ResolvedStride);
	}

	FArdaRHIStatus FArdaCommandList::DrawIndexedIndirect(IArdaRHIBuffer& Arguments,
	    uint64_t Offset,
	    uint32_t DrawCount,
	    uint32_t Stride)
	{
		auto* Buffer = Cast<FArdaBuffer>(&Arguments);
		constexpr uint32_t ArgumentSize = 5u * sizeof(uint32_t);
		const uint32_t ResolvedStride = Stride ? Stride : ArgumentSize;
		if (!Buffer || !RetainOwned(Buffer))
		{
			return WrongDevice();
		}
		if (!HasAnyFlags(Buffer->mDesc.mUsage, EArdaRHIBufferUsage::Indirect) || DrawCount == 0 ||
		    Offset % sizeof(uint32_t) || ResolvedStride % sizeof(uint32_t) || ResolvedStride < ArgumentSize ||
		    Offset > Buffer->mDesc.mByteSize ||
		    static_cast<uint64_t>(DrawCount - 1) * ResolvedStride + ArgumentSize > Buffer->mDesc.mByteSize - Offset)
		{
			return Invalid(
			    "Indexed indirect draw arguments require DWORD alignment, valid ranges, and Indirect usage.");
		}
		const auto State = QueryBufferState(*Buffer);
		if (!State)
		{
			return State.mStatus;
		}
		if (!State.mValue.IsConsistent() ||
		    !HasAnyFlags(State.mValue.mFacadeState, EArdaRHIResourceState::IndirectArgument))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Indexed indirect draw arguments must be in IndirectArgument state across facade, backend, and native tracking.");
		}
		if (auto Status = ValidateWork(EArdaPipelineKind::Graphics); !Status)
		{
			return Status;
		}
		if (!mGraphicsState.mIndexBuffer)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Indexed draws require an index buffer.");
		}
		return mNative->DrawIndexedIndirect(Buffer->mNative, Offset, DrawCount, ResolvedStride);
	}

	FArdaRHIStatus FArdaCommandList::DispatchIndirect(IArdaRHIBuffer& Arguments, uint64_t Offset)
	{
		auto* Buffer = Cast<FArdaBuffer>(&Arguments);
		constexpr uint64_t ArgumentSize = 3u * sizeof(uint32_t);
		if (!Buffer || !RetainOwned(Buffer))
		{
			return WrongDevice();
		}
		if (!HasAnyFlags(Buffer->mDesc.mUsage, EArdaRHIBufferUsage::Indirect) || Offset % sizeof(uint32_t) ||
		    Offset > Buffer->mDesc.mByteSize || ArgumentSize > Buffer->mDesc.mByteSize - Offset)
		{
			return Invalid(
			    "Indirect dispatch arguments require DWORD alignment, a valid range, and Indirect usage.");
		}
		const auto State = QueryBufferState(*Buffer);
		if (!State)
		{
			return State.mStatus;
		}
		if (!State.mValue.IsConsistent() ||
		    !HasAnyFlags(State.mValue.mFacadeState, EArdaRHIResourceState::IndirectArgument))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Indirect dispatch arguments must be in IndirectArgument state across facade, backend, and native tracking.");
		}
		if (auto Status = ValidateWork(EArdaPipelineKind::Compute); !Status)
		{
			return Status;
		}
		return mNative->DispatchIndirect(Buffer->mNative, Offset);
	}

	FArdaRHIStatus FArdaCommandList::SetMeshletState(const FArdaRHIMeshletState& State)
	{
		if (auto Status = ValidateRecording(true); !Status)
		{
			return Status;
		}
		if (mDevice->GetCapabilities().mMeshShaderTier == EArdaRHIMeshShaderTier::None)
		{
			return Unsupported("Mesh shaders are unsupported by this device.");
		}
		auto* Pipeline = Cast<FArdaMeshletPipeline>(State.mPipeline.Get());
		auto* Framebuffer = Cast<FArdaFramebuffer>(State.mFramebuffer.Get());
		if (!Pipeline || !Framebuffer || !RetainOwned(Pipeline) || !RetainOwned(Framebuffer))
		{
			return WrongDevice();
		}
		FArdaProviderMeshletState Native;
		if (auto Status = ValidateRasterState(Pipeline->mDesc, State, mDevice->GetCapabilities().mLimits); !Status)
		{
			return Status;
		}
		Native.mPipeline = Pipeline->mNative;
		Native.mFramebuffer = Framebuffer->mNative;
		Native.mViewports = State.mViewports;
		Native.mScissors = State.mScissors;
		if (auto Status = ResolveBindings(State.mBindings, Native.mBindings, Pipeline->mDesc.mBindingLayouts);
		    !Status)
		{
			return Status;
		}
		const auto Status = mNative->SetMeshletState(Native);
		LatchError(Status);
		if (Status)
		{
			mMeshletState = State;
			mPipelineKind = EArdaPipelineKind::Mesh;
			mPushConstantCapacity = PushConstantCapacity(Pipeline->mDesc.mBindingLayouts);
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::DispatchMesh(uint32_t GroupsX, uint32_t GroupsY, uint32_t GroupsZ)
	{
		if (mDevice->GetCapabilities().mMeshShaderTier == EArdaRHIMeshShaderTier::None)
		{
			return Unsupported("Mesh shaders are unsupported by this device.");
		}
		if (GroupsX == 0 || GroupsY == 0 || GroupsZ == 0)
		{
			return Invalid("Mesh dispatch group counts must be non-zero.");
		}
		if (auto Status = ValidateWork(EArdaPipelineKind::Mesh); !Status)
		{
			return Status;
		}
		return mNative->DispatchMesh(GroupsX, GroupsY, GroupsZ);
	}

	FArdaRHIStatus FArdaCommandList::SetRayTracingState(const FArdaRHIRayTracingState& State)
	{
		if (auto Status = ValidateRecording(false); !Status)
		{
			return Status;
		}
		if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
		{
			return Unsupported("Ray tracing is unsupported by this device.");
		}
		auto* Table = Cast<FArdaShaderTable>(State.mShaderTable.Get());
		if (!Table || !RetainOwned(Table))
		{
			return WrongDevice();
		}
		FArdaProviderRayTracingState Native;
		{
			std::lock_guard<std::mutex> Lock(Table->mMutex);
			if (!Table->HasRayGeneration())
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				    "The shader table has no ray-generation record.");
			}
			Native.mShaderTable = Table->mNative;
		}
		if (auto Status = ResolveBindings(State.mBindings,
		        Native.mBindings,
		        Table->mPipeline->GetDesc().mGlobalBindingLayouts);
		    !Status)
		{
			return Status;
		}
		const auto Status = mNative->SetRayTracingState(Native);
		LatchError(Status);
		if (Status)
		{
			mPipelineKind = EArdaPipelineKind::RayTracing;
			mPushConstantCapacity = PushConstantCapacity(Table->mPipeline->GetDesc().mGlobalBindingLayouts);
		}
		return Status;
	}

	FArdaRHIStatus FArdaCommandList::DispatchRays(uint32_t Width, uint32_t Height, uint32_t Depth)
	{
		if (!mDevice->GetCapabilities().mRayTracing.mbPipelineShaders)
		{
			return Unsupported("Ray tracing is unsupported by this device.");
		}
		if (!Width || !Height || !Depth)
		{
			return Invalid("Ray dispatch dimensions must be non-zero.");
		}
		const uint64_t PlaneInvocations = uint64_t(Width) * uint64_t(Height);
		if (PlaneInvocations > UINT64_MAX / Depth)
		{
			return Invalid("Ray dispatch invocation count overflows the supported range.");
		}
		const uint64_t InvocationCount = PlaneInvocations * Depth;
		const uint32_t MaxInvocations = mDevice->GetCapabilities().mRayTracing.mMaxRayDispatchInvocations;
		if (MaxInvocations != 0 && InvocationCount > MaxInvocations)
		{
			return Invalid("Ray dispatch exceeds the device invocation-count limit.");
		}
		if (auto Status = ValidateWork(EArdaPipelineKind::RayTracing); !Status)
		{
			return Status;
		}
		return mNative->DispatchRays(Width, Height, Depth);
	}

	FArdaRHIStatus FArdaCommandList::DispatchRaysIndirect(IArdaRHIBuffer& Arguments, uint64_t Offset)
	{
		if (!mDevice->GetCapabilities().mRayTracing.mbIndirectDispatch)
		{
			return Unsupported("Indirect ray dispatch is unsupported by this device.");
		}
		auto* Buffer = Cast<FArdaBuffer>(&Arguments);
		if (!Buffer || !RetainOwned(Buffer))
		{
			return WrongDevice();
		}
		if (!HasAnyFlags(Buffer->mDesc.mUsage, EArdaRHIBufferUsage::Indirect) || Offset % sizeof(uint32_t) ||
		    Offset > Buffer->mDesc.mByteSize || sizeof(uint32_t) * 3 > Buffer->mDesc.mByteSize - Offset)
		{
			return Invalid("Indirect ray-dispatch arguments are invalid.");
		}
		const auto State = QueryBufferState(*Buffer);
		if (!State)
		{
			return State.mStatus;
		}
		if (!State.mValue.IsConsistent() ||
		    !HasAnyFlags(State.mValue.mFacadeState, EArdaRHIResourceState::IndirectArgument))
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Indirect ray-dispatch arguments must be in IndirectArgument state across facade, backend, and native tracking.");
		}
		if (auto Status = ValidateWork(EArdaPipelineKind::RayTracing); !Status)
		{
			return Status;
		}
		return mNative->DispatchRaysIndirect(Buffer->mNative, Offset);
	}

	FArdaRHIStatus FArdaCommandList::DispatchShaderBundle(IArdaRHIShaderBundle& Resource)
	{
		auto* Bundle = Cast<FArdaShaderBundle>(&Resource);
		if (!Bundle || !RetainOwned(Bundle))
		{
			return WrongDevice();
		}
		if (!mDevice->GetCapabilities().mbShaderBundleDispatch)
		{
			return Unsupported("Shader bundles are unsupported by this device.");
		}
		std::lock_guard<std::mutex> Lock(Bundle->mMutex);
		for (const auto& Record : Bundle->mRecords)
		{
			FArdaRHIStatus Status;
			if (Record.mComputePipeline)
			{
				FArdaRHIComputeState State;
				State.mPipeline = Record.mComputePipeline;
				State.mBindings = Record.mBindings;
				Status = SetComputeState(State);
			}
			else
			{
				if (!mMeshletState.mFramebuffer)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
					    "Mesh bundles require an active mesh framebuffer and raster state.");
				}
				FArdaRHIMeshletState State = mMeshletState;
				State.mPipeline = Record.mMeshPipeline;
				State.mBindings = Record.mBindings;
				Status = SetMeshletState(State);
			}
			if (!Status)
			{
				return Status;
			}
			if (!Record.mLocalArguments.empty())
			{
				SetPushConstants(Record.mLocalArguments.data(), Record.mLocalArguments.size());
			}
			if (Record.mComputePipeline)
			{
				Dispatch(Record.mGroupsX, Record.mGroupsY, Record.mGroupsZ);
			}
			else
			{
				Status = DispatchMesh(Record.mGroupsX, Record.mGroupsY, Record.mGroupsZ);
				if (!Status)
				{
					return Status;
				}
			}
		}
		return {};
	}

	FArdaRHIStatus FArdaCommandList::DispatchWorkGraph(IArdaRHIWorkGraphPipeline& Resource,
	    const void* Records,
	    uint32_t RecordCount,
	    uint32_t RecordStride,
	    const eastl::vector<FArdaRHIBindingSetRef>& BindingRefs)
	{
		if (auto Status = ValidateRecording(false); !Status)
		{
			return Status;
		}
		auto* Pipeline = Cast<FArdaWorkGraphPipeline>(&Resource);
		if (!Pipeline || !RetainOwned(Pipeline))
		{
			return WrongDevice();
		}
		if (mDevice->GetCapabilities().mWorkGraphTier == EArdaRHIWorkGraphTier::None)
		{
			return Unsupported("Work graphs are unsupported by this device.");
		}
		if (!Records || !RecordCount || !RecordStride || RecordCount > Pipeline->mDesc.mMaxInputRecords)
		{
			return Invalid("Work-graph CPU input records are invalid or exceed capacity.");
		}
		eastl::vector<FArdaProviderObjectRef> Bindings;
		if (auto Status = ResolveBindings(BindingRefs, Bindings, Pipeline->mDesc.mGlobalBindingLayouts); !Status)
		{
			return Status;
		}
		const auto Status =
		    mNative->DispatchWorkGraph(Pipeline->mNative, Records, RecordCount, RecordStride, Bindings);
		if (Status)
		{
			mPipelineKind = EArdaPipelineKind::None;
			mPushConstantCapacity = 0;
		}
		return Status;
	}
}
