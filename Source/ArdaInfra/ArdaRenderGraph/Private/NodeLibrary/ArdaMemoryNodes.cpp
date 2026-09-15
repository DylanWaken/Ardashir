#include "NodeLibrary/ArdaMemoryNodes.h"

#include "ArdaDependencyKey.h"

namespace arda
{
	namespace
	{
		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		const FArdaRHIBufferDesc* FindBuffer(FArdaDependencyResourceContext& Context,
		    FArdaDependencyResourceHandle Resource)
		{
			const auto* Description = Context.Find(Resource);
			return Description && !Description->mbTexture && !Description->mExternalAccelerationStructure
			    ? &Description->mBuffer
			    : nullptr;
		}

		FArdaRHIStatus ResolveRange(const FArdaRHIBufferDesc& Buffer, uint64_t Offset, uint64_t& ByteSize)
		{
			if (Offset >= Buffer.mByteSize)
			{
				return Invalid("Memory operation offset must lie inside the buffer.");
			}
			const uint64_t Remaining = Buffer.mByteSize - Offset;
			if (ByteSize == ArdaRHIWholeBuffer)
			{
				ByteSize = Remaining;
			}
			if (!ByteSize || ByteSize > Remaining)
			{
				return Invalid("Memory operation requires a nonempty range contained in the buffer.");
			}
			return {};
		}

		FArdaDependencyAccess BufferAccess(FArdaDependencyResourceHandle Resource,
		    EArdaDependencyAccess Access,
		    EArdaRHIResourceState State,
		    uint64_t Offset = 0,
		    uint64_t ByteSize = ArdaRHIWholeBuffer)
		{
			FArdaDependencyAccess Result;
			Result.mResource = Resource;
			Result.mAccess = Access;
			Result.mState = State;
			Result.mBufferRange = {Offset, ByteSize};
			return Result;
		}
	}

	FArdaDependencyNodeMetadata FArdaMemoryUploadBufferNode::GetMetadata()
	{
		return {"arda.memory.upload-buffer", 1};
	}

	FArdaRHIStatus FArdaMemoryUploadBufferNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		const uint64_t ByteSize = Parameters.mBytes.size();
		if (!ByteSize || Parameters.mDestinationOffset > UINT64_MAX - ByteSize)
		{
			return Invalid("Buffer upload requires nonempty bytes and a nonoverflowing destination range.");
		}

		FArdaRHIBufferDesc Description;
		if (Parameters.mDestination)
		{
			const auto* Destination = FindBuffer(Context, Parameters.mDestination);
			if (!Destination || Destination->mCpuAccess != EArdaRHICpuAccess::None)
			{
				return Invalid("Buffer upload requires a valid GPU destination with no CPU access.");
			}
			Description = *Destination;
		}
		Description.mByteSize = Parameters.mDestinationOffset + ByteSize;
		return Context.Buffer(Parameters.mDestination, "Destination", Description);
	}

	eastl::string FArdaMemoryUploadBufferNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(Parameters.mDestination)
		    .Value(Parameters.mDestinationOffset)
		    .Bytes(Parameters.mBytes.data(), Parameters.mBytes.size())
		    .Build();
	}

	FArdaDependencyNodeDesc FArdaMemoryUploadBufferNode::Describe(const FArdaParameters& Parameters, const FArdaState&)
	{
		FArdaDependencyNodeDesc Description;
		Description.mAccesses = {BufferAccess(Parameters.mDestination,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::CopyDest,
		    Parameters.mDestinationOffset,
		    Parameters.mBytes.size())};
		return Description;
	}

	FArdaRHIStatus FArdaMemoryUploadBufferNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.GetCommands().WriteBuffer(*Context.GetBuffer(Parameters.mDestination),
		    Parameters.mBytes.data(),
		    Parameters.mBytes.size(),
		    Parameters.mDestinationOffset);
	}

	FArdaDependencyNodeMetadata FArdaMemoryCopyBufferNode::GetMetadata()
	{
		return {"arda.memory.copy-buffer", 1};
	}

	FArdaRHIStatus FArdaMemoryCopyBufferNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		const auto* Source = FindBuffer(Context, Parameters.mSource);
		if (!Source || Source->mCpuAccess == EArdaRHICpuAccess::Read)
		{
			return Invalid("Buffer copy requires a valid source outside a CPU readback heap.");
		}
		if (auto Status = ResolveRange(*Source, Parameters.mSourceOffset, Parameters.mByteSize); !Status)
		{
			return Status;
		}
		if (Parameters.mSource == Parameters.mDestination ||
		    Parameters.mDestinationOffset > UINT64_MAX - Parameters.mByteSize)
		{
			return Invalid("Buffer copy requires distinct resources and a nonoverflowing destination range.");
		}

		// Infer resource shape without inheriting a source's CPU heap or backing-allocation policy.
		FArdaRHIBufferDesc Description;
		Description.mStructureStride = Source->mStructureStride;
		Description.mFormat = Source->mFormat;
		Description.mUsage = static_cast<EArdaRHIBufferUsage>(
		    static_cast<uint16_t>(Source->mUsage) & ~static_cast<uint16_t>(EArdaRHIBufferUsage::Volatile));
		Description.mbCudaInterop = Source->mbCudaInterop;
		if (Parameters.mDestination)
		{
			const auto* Destination = FindBuffer(Context, Parameters.mDestination);
			if (!Destination || Destination->mCpuAccess == EArdaRHICpuAccess::Write)
			{
				return Invalid("Buffer copy requires a valid destination outside a CPU upload heap.");
			}
			Description = *Destination;
		}
		Description.mByteSize = Parameters.mDestinationOffset + Parameters.mByteSize;
		return Context.Buffer(Parameters.mDestination, "Destination", Description);
	}

	eastl::string FArdaMemoryCopyBufferNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(Parameters.mSource)
		    .Resource(Parameters.mDestination)
		    .Value(Parameters.mByteSize)
		    .Value(Parameters.mSourceOffset)
		    .Value(Parameters.mDestinationOffset)
		    .Build();
	}

	FArdaDependencyNodeDesc FArdaMemoryCopyBufferNode::Describe(const FArdaParameters& Parameters, const FArdaState&)
	{
		FArdaDependencyNodeDesc Description;
		Description.mAccesses = {BufferAccess(Parameters.mSource,
		                             EArdaDependencyAccess::Read,
		                             EArdaRHIResourceState::CopySource,
		                             Parameters.mSourceOffset,
		                             Parameters.mByteSize),
		    BufferAccess(Parameters.mDestination,
		        EArdaDependencyAccess::Write,
		        EArdaRHIResourceState::CopyDest,
		        Parameters.mDestinationOffset,
		        Parameters.mByteSize)};
		return Description;
	}

	FArdaRHIStatus FArdaMemoryCopyBufferNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.GetCommands().CopyBuffer(*Context.GetBuffer(Parameters.mDestination),
		    Parameters.mDestinationOffset,
		    *Context.GetBuffer(Parameters.mSource),
		    Parameters.mSourceOffset,
		    Parameters.mByteSize);
	}

	FArdaDependencyNodeMetadata FArdaMemoryReadbackBufferNode::GetMetadata()
	{
		return {"arda.memory.readback-buffer", 1};
	}

	FArdaRHIStatus FArdaMemoryReadbackBufferNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		const auto* Source = FindBuffer(Context, Parameters.mSource);
		if (!Parameters.mDestination || !Source || Source->mCpuAccess == EArdaRHICpuAccess::Read)
		{
			return Invalid("Buffer readback requires a retained destination and a source outside a CPU readback heap.");
		}
		return ResolveRange(*Source, Parameters.mSourceOffset, Parameters.mByteSize);
	}

	eastl::string FArdaMemoryReadbackBufferNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(Parameters.mSource)
		    .Value(reinterpret_cast<uintptr_t>(Parameters.mDestination.get()))
		    .Value(Parameters.mSourceOffset)
		    .Value(Parameters.mByteSize)
		    .Build();
	}

	FArdaDependencyNodeDesc FArdaMemoryReadbackBufferNode::Describe(const FArdaParameters& Parameters,
	    const FArdaState&)
	{
		FArdaDependencyNodeDesc Description;
		Description.mbSideEffect = true;
		Description.mAccesses = {BufferAccess(Parameters.mSource,
		    EArdaDependencyAccess::Read,
		    EArdaRHIResourceState::CopySource,
		    Parameters.mSourceOffset,
		    Parameters.mByteSize)};
		return Description;
	}

	FArdaRHIStatus FArdaMemoryReadbackBufferNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.ReadbackBuffer(Parameters.mSource,
		    Parameters.mDestination,
		    Parameters.mSourceOffset,
		    Parameters.mByteSize);
	}

	FArdaDependencyNodeMetadata FArdaMemoryClearBufferNode::GetMetadata()
	{
		return {"arda.memory.clear-buffer", 1};
	}

	FArdaRHIStatus FArdaMemoryClearBufferNode::DeclareResources(FArdaDependencyResourceContext& Context,
	    FArdaParameters& Parameters)
	{
		const auto* Destination = FindBuffer(Context, Parameters.mDestination);
		if (!Destination || Destination->mCpuAccess != EArdaRHICpuAccess::None || !Destination->mByteSize ||
		    Destination->mByteSize % sizeof(uint32_t) != 0 ||
		    !HasAnyFlags(Destination->mUsage, EArdaRHIBufferUsage::UnorderedAccess))
		{
			return Invalid("Buffer clear requires a GPU unordered-access buffer with a nonzero multiple-of-four size.");
		}
		return {};
	}

	eastl::string FArdaMemoryClearBufferNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder().Resource(Parameters.mDestination).Value(Parameters.mValue).Build();
	}

	FArdaDependencyNodeDesc FArdaMemoryClearBufferNode::Describe(const FArdaParameters& Parameters, const FArdaState&)
	{
		FArdaDependencyNodeDesc Description;
		Description.mAccesses = {BufferAccess(Parameters.mDestination,
		    EArdaDependencyAccess::Write,
		    EArdaRHIResourceState::UnorderedAccess)};
		return Description;
	}

	FArdaRHIStatus FArdaMemoryClearBufferNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		return Context.GetCommands().ClearBufferUInt(*Context.GetBuffer(Parameters.mDestination), Parameters.mValue);
	}

	FArdaRHIStatus RegisterArdaMemoryNodes()
	{
		if (auto Status = FArdaMemoryUploadBufferNode::Register(); !Status)
		{
			return Status;
		}
		if (auto Status = FArdaMemoryCopyBufferNode::Register(); !Status)
		{
			return Status;
		}
		if (auto Status = FArdaMemoryReadbackBufferNode::Register(); !Status)
		{
			return Status;
		}
		if (auto Status = FArdaMemoryClearBufferNode::Register(); !Status)
		{
			return Status;
		}
		if (auto Status = RegisterArdaMemoryTextureNodes(); !Status)
		{
			return Status;
		}
		return RegisterArdaMemoryAccelerationStructureNodes();
	}
}
