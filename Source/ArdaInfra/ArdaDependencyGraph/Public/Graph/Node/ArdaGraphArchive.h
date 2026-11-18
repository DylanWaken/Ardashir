/** Portable field codecs used by graph serialization; never persist object memory or native pointers. */
#pragma once

#include "Graph/Resources/ArdaGraphValue.h"
#include "Graph/Resources/ArdaGraphicsState.h"
#include "Graph/Resources/ArdaGraphExternal.h"
#include "RHI/Scheduling/ArdaRHIResourceCopies.h"
#include <EASTL/array.h>
#include <EASTL/functional.h>
#include <EASTL/type_traits.h>
#include <cstring>
#include <limits>

namespace arda
{
	/** Specialize for application value types; encode semantic fields individually with Write/Read. */
	template <class T, class = void>
	struct TArdaGraphCodec
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = false;
	};

	/** Bounded little-endian archive with callbacks that translate live handles to file-local indices. */
	class FArdaGraphArchiveWriter final
	{
	public:
		/** Maximum archive bytes, also enforced when reading untrusted input. */
		static constexpr size_t mMaximumBytes = 512 * 1024 * 1024;
		/** Translate a live resource to its file-local index, or UINT32_MAX for an empty value. */
		eastl::function<uint32_t(FArdaDependencyResourceHandle)> mResourceIndex;
		/** Translate a live node to its file-local index, or UINT32_MAX for an empty value. */
		eastl::function<uint32_t(FArdaGraphNodeHandle)> mNodeIndex;
		/** Resolve explicitly named contextual references without persisting addresses. */
		eastl::function<TArdaRHIResult<eastl::string>(const void*, const void*)> mExternalReference;
		eastl::function<FArdaRHIStatus(const eastl::string&, const void*)> mExternalSlot;
		/** Retained slot names let unbound parameters be re-encoded after topology edits. */
		const eastl::vector<eastl::string>* mExternalNames = nullptr;
		size_t mNextExternalName = 0;

		/** Encode a portable value, retaining the first failure. */
		template <class T>
		void Write(const T& Value)
		{
			if (!mStatus)
			{
				return;
			}
			if constexpr (TArdaGraphCodec<T>::mSupported)
			{
				TArdaGraphCodec<T>::Write(*this, Value);
			}
			else
			{
				Fail("A node parameter has no portable graph codec.", EArdaRHIResult::Unsupported);
			}
		}

		/** Encode several semantic fields in declaration order. */
		template <class... T>
		void Fields(const T&... Values)
		{
			(Write(Values), ...);
		}

		/** Append an unsigned integer in little-endian byte order. */
		void WriteInteger(uint64_t Value, size_t Bytes);
		/** Append a bounded string or byte sequence. */
		void WriteBytes(const char* Data, size_t Count);
		/** Retain a diagnostic; a failed writer produces no usable document. */
		void Fail(const char* Message, EArdaRHIResult Code = EArdaRHIResult::InvalidArgument);
		/** Inspect the first error. */
		const FArdaRHIStatus& GetStatus() const noexcept;
		/** Read encoded bytes, including embedded zero bytes. */
		const eastl::string& GetData() const noexcept;

	private:
		eastl::string mData;
		FArdaRHIStatus mStatus;
	};

	/** Validating archive reader; handles resolve only to declared resources and preceding nodes. */
	class FArdaGraphArchiveReader final
	{
	public:
		/** Borrow a complete archive or a single parameter payload for this reader's lifetime. */
		explicit FArdaGraphArchiveReader(const eastl::string& Data);
		/** Resource identities allocated for this document. */
		const eastl::vector<FArdaDependencyResourceHandle>* mResources = nullptr;
		/** Declared resource kinds used to reject typed-handle mismatches. */
		const eastl::vector<EArdaGraphResourceKind>* mResourceKinds = nullptr;
		/** Node identities already reconstructed for this document. */
		const eastl::vector<FArdaGraphNodeHandle>* mNodes = nullptr;
		/** Optional decoded parameter references used to validate archived output declarations. */
		eastl::vector<FArdaDependencyResourceHandle>* mResourceReferences = nullptr;
		eastl::function<TArdaRHIResult<eastl::shared_ptr<const void>>(const eastl::string&, const void*)>
		    mExternalReference;
		bool mbHasUnboundExternalReferences = false;
		eastl::vector<eastl::string>* mExternalNames = nullptr;

		/** Decode a portable field, preserving the first error. */
		template <class T>
		void Read(T& Value)
		{
			if (!mStatus)
			{
				return;
			}
			if constexpr (TArdaGraphCodec<T>::mSupported)
			{
				TArdaGraphCodec<T>::Read(*this, Value);
			}
			else
			{
				Fail("A node parameter has no portable graph codec.", EArdaRHIResult::Unsupported);
			}
		}

		/** Decode several fields in declaration order. */
		template <class... T>
		void Fields(T&... Values)
		{
			(Read(Values), ...);
		}

		/** Decode an unsigned little-endian integer after checking the remaining bytes. */
		uint64_t ReadInteger(size_t Bytes);
		/** Decode a bounded string without reading past the document. */
		void ReadBytes(eastl::string& Value, size_t Count);
		/** Decode a bounded collection count; invalid counts retain a failure and return zero. */
		size_t ReadCount();
		/** Decode and validate a document-local resource identity. */
		FArdaDependencyResourceHandle ReadResource();
		/** Decode and validate a document-local node identity. */
		FArdaGraphNodeHandle ReadNode();

		/** Decode a typed resource, checking its declared kind before constructing the facade. */
		template <EArdaGraphResourceKind Kind>
		TArdaGraphValue<Kind> ReadTypedResource()
		{
			const auto Handle = ReadResource();
			if (Handle &&
			    (!mResourceKinds || Handle.mIndex >= mResourceKinds->size() ||
			        (*mResourceKinds)[Handle.mIndex] != Kind))
			{
				Fail("Archive resource kind does not match the node parameter.");
				return {};
			}
			return TArdaGraphValue<Kind>(Handle);
		}

		/** Decode a typed stage after validating the stored output kind. */
		template <EArdaGraphStageValueKind Kind>
		TArdaGraphStageValue<Kind> ReadTypedStage()
		{
			const auto Node = ReadNode();
			const auto StoredKind = static_cast<EArdaGraphStageValueKind>(ReadInteger(1));
			if (StoredKind != Kind && (Node || StoredKind != EArdaGraphStageValueKind::None))
			{
				Fail("Archive stage kind does not match the node parameter.");
				return {};
			}
			return Node ? TArdaGraphStageValue<Kind>(Node) : TArdaGraphStageValue<Kind>{};
		}

		/** Retain the first malformed-input or unsupported-schema diagnostic. */
		void Fail(const char* Message, EArdaRHIResult Code = EArdaRHIResult::InvalidArgument);
		/** Inspect the first error. */
		const FArdaRHIStatus& GetStatus() const noexcept;
		/** Require successful decoding of the whole document, rejecting trailing bytes. */
		FArdaRHIStatus Finish() const;

	private:
		const eastl::string& mData;
		size_t mPosition = 0;
		FArdaRHIStatus mStatus;
	};

	template <class T>
	struct TArdaGraphCodec<T, eastl::enable_if_t<eastl::is_integral_v<T> || eastl::is_enum_v<T>>>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = sizeof(T) <= 8;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, T Value)
		{
			Archive.WriteInteger(static_cast<uint64_t>(Value), sizeof(T));
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, T& Value)
		{
			const auto Raw = Archive.ReadInteger(sizeof(T));
			if constexpr (eastl::is_same_v<T, bool>)
			{
				if (Raw > 1)
				{
					Archive.Fail("Archive contains an invalid boolean.");
				}
			}
			Value = static_cast<T>(Raw);
		}
	};

	template <class T>
	struct TArdaGraphCodec<T, eastl::enable_if_t<eastl::is_floating_point_v<T>>>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = (sizeof(T) == 4 || sizeof(T) == 8) && std::numeric_limits<T>::is_iec559;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, T Value)
		{
			using FArdaBits = eastl::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
			FArdaBits Bits = 0;
			std::memcpy(&Bits, &Value, sizeof(T));
			Archive.Write(Bits);
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, T& Value)
		{
			using FArdaBits = eastl::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
			FArdaBits Bits = 0;
			Archive.Read(Bits);
			std::memcpy(&Value, &Bits, sizeof(T));
		}
	};

	template <>
	struct TArdaGraphCodec<eastl::string>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const eastl::string& Value)
		{
			Archive.WriteInteger(Value.size(), 8);
			Archive.WriteBytes(Value.data(), Value.size());
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, eastl::string& Value)
		{
			const auto Count = Archive.ReadInteger(8);
			if (Count > FArdaGraphArchiveWriter::mMaximumBytes)
			{
				Archive.Fail("Archive string exceeds the size limit.");
				return;
			}
			Archive.ReadBytes(Value, static_cast<size_t>(Count));
		}
	};

	template <class T, size_t Count>
	struct TArdaGraphCodec<eastl::array<T, Count>>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = TArdaGraphCodec<T>::mSupported;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const eastl::array<T, Count>& Values)
		{
			for (const auto& Value : Values)
			{
				Archive.Write(Value);
			}
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, eastl::array<T, Count>& Values)
		{
			for (auto& Value : Values)
			{
				Archive.Read(Value);
			}
		}
	};

	template <class T, class Allocator>
	struct TArdaGraphCodec<eastl::vector<T, Allocator>>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = TArdaGraphCodec<T>::mSupported;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const eastl::vector<T, Allocator>& Values)
		{
			if constexpr (!eastl::is_same_v<T, uint8_t>)
			{
				if (Values.size() > 1024 * 1024)
				{
					Archive.Fail("Graph archive collection exceeds the element limit.");
					return;
				}
			}
			Archive.WriteInteger(Values.size(), 8);
			if constexpr (eastl::is_same_v<T, uint8_t>)
			{
				Archive.WriteBytes(reinterpret_cast<const char*>(Values.data()), Values.size());
			}
			else
			{
				for (const auto& Value : Values)
				{
					Archive.Write(Value);
				}
			}
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, eastl::vector<T, Allocator>& Values)
		{
			if constexpr (eastl::is_same_v<T, uint8_t>)
			{
				eastl::string Bytes;
				Archive.Read(Bytes);
				if (Archive.GetStatus())
				{
					const auto* Begin = reinterpret_cast<const uint8_t*>(Bytes.data());
					Values.assign(Begin, Begin + Bytes.size());
				}
				return;
			}
			const auto Count = Archive.ReadCount();
			Values.clear();
			for (size_t Index = 0; Index < Count && Archive.GetStatus(); ++Index)
			{
				T Value{};
				Archive.Read(Value);
				if (Archive.GetStatus())
				{
					Values.push_back(eastl::move(Value));
				}
			}
		}
	};

	template <>
	struct TArdaGraphCodec<FArdaDependencyResourceHandle>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, FArdaDependencyResourceHandle Value)
		{
			if (Value && !Archive.mResourceIndex)
			{
				Archive.Fail("Resource serialization requires a graph context.");
				return;
			}
			Archive.WriteInteger(Value ? Archive.mResourceIndex(Value) : UINT32_MAX, 4);
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaDependencyResourceHandle& Value)
		{
			Value = Archive.ReadResource();
		}
	};

	template <EArdaGraphResourceKind Kind>
	struct TArdaGraphCodec<TArdaGraphValue<Kind>>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, TArdaGraphValue<Kind> Value)
		{
			Archive.Write(Value.GetHandle());
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, TArdaGraphValue<Kind>& Value)
		{
			Value = Archive.ReadTypedResource<Kind>();
		}
	};

	template <>
	struct TArdaGraphCodec<FArdaGraphStageValue>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, FArdaGraphStageValue Value)
		{
			if (Value && !Archive.mNodeIndex)
			{
				Archive.Fail("Stage serialization requires a graph context.");
				return;
			}
			Archive.WriteInteger(Value ? Archive.mNodeIndex(Value.GetProducer()) : UINT32_MAX, 4);
			Archive.Write(Value.GetKind());
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaGraphStageValue& Value)
		{
			const auto Node = Archive.ReadNode();
			EArdaGraphStageValueKind Kind{};
			Archive.Read(Kind);
			if (Kind > EArdaGraphStageValueKind::RayShader)
			{
				Archive.Fail("Archive contains an invalid stage kind.");
			}
			Value = {Node, Kind};
		}
	};

	template <EArdaGraphStageValueKind Kind>
	struct TArdaGraphCodec<TArdaGraphStageValue<Kind>> : TArdaGraphCodec<FArdaGraphStageValue>
	{
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, TArdaGraphStageValue<Kind>& Value)
		{
			Value = Archive.ReadTypedStage<Kind>();
		}
	};

	/** Nested metadata-authored aggregates retain the same portable member schema. */
	template <class T>
	struct TArdaGraphCodec<T, eastl::void_t<decltype(T::GetNodeParameterMetadata())>>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;

		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const T& Value)
		{
			(void)T::GetNodeParameterMetadata().Serialize(Value, Archive);
		}

		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, T& Value)
		{
			(void)T::GetNodeParameterMetadata().Deserialize(Value, Archive);
		}
	};

	/** Portable buffer descriptor, excluding any allocated storage. */
	template <>
	struct TArdaGraphCodec<FArdaRHIBufferDesc>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIBufferDesc& Value);
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHIBufferDesc& Value);
	};

	/** Portable texture descriptor, excluding any allocated storage. */
	template <>
	struct TArdaGraphCodec<FArdaRHITextureDesc>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHITextureDesc& Value);
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHITextureDesc& Value);
	};

	/** Portable byte range. */
	template <>
	struct TArdaGraphCodec<FArdaRHIBufferRange>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIBufferRange& Value);
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHIBufferRange& Value);
	};

	/** Portable mip, array and plane range. */
	template <>
	struct TArdaGraphCodec<FArdaRHITextureSubresourceRange>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHITextureSubresourceRange& Value);
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHITextureSubresourceRange& Value);
	};

	/** Portable resource view declaration. */
	template <>
	struct TArdaGraphCodec<FArdaRHIViewDesc>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIViewDesc& Value);
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHIViewDesc& Value);
	};

	/** Portable clear color value. */
	template <>
	struct TArdaGraphCodec<FArdaRHIColor>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIColor& Value);
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHIColor& Value);
	};

	/** Portable texture copy region. */
	template <>
	struct TArdaGraphCodec<FArdaRHITextureSlice>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHITextureSlice& Value);
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHITextureSlice& Value);
	};

	/** Portable texture row layout in buffer storage. */
	template <>
	struct TArdaGraphCodec<FArdaRHITextureBufferLayout>
	{
		/** Whether this field type has an explicit portable representation. */
		static constexpr bool mSupported = true;
		/** Encode semantic fields in their declared order. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHITextureBufferLayout& Value);
		/** Restore semantic fields while retaining malformed-input diagnostics. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHITextureBufferLayout& Value);
	};

	/** Portable acceleration-structure shape. Geometry native resource references must be empty. */
	template <>
	struct TArdaGraphCodec<FArdaRHIAccelStructDesc>
	{
		/** Whether a pointer-free shape can be archived. */
		static constexpr bool mSupported = true;
		/** Encode geometry capacities, formats and build flags without native addresses. */
		static void Write(FArdaGraphArchiveWriter& Archive, const FArdaRHIAccelStructDesc& Value);
		/** Restore the expected shape with no native geometry bindings. */
		static void Read(FArdaGraphArchiveReader& Archive, FArdaRHIAccelStructDesc& Value);
	};

	/** Context-owned references use the graph's explicit named binding table. */
	template <class T>
	struct TArdaGraphExternalReferenceCodec
	{
		static constexpr bool mSupported = true;

		static void Write(FArdaGraphArchiveWriter& Archive, const T& Value)
		{
			if (Archive.mExternalNames)
			{
				if (Archive.mNextExternalName >= Archive.mExternalNames->size())
				{
					Archive.Fail("External parameter schema no longer matches its retained slot names.");
					return;
				}
				const auto& Name = (*Archive.mExternalNames)[Archive.mNextExternalName++];
				if (!Name.empty())
				{
					if (!Archive.mExternalSlot)
					{
						Archive.Fail("External parameter requires a named slot table.");
						return;
					}
					if (auto Status = Archive.mExternalSlot(Name, &ArdaGraphExternalType<T>); !Status)
					{
						Archive.Fail(Status.mMessage.c_str(), Status.mCode);
						return;
					}
				}
				Archive.Write(Name);
				return;
			}
			const auto Identity = TArdaGraphExternalReference<T>::Identity(Value);
			if (!Identity)
			{
				Archive.Write(eastl::string{});
				return;
			}
			if (!Archive.mExternalReference)
			{
				Archive.Fail("External parameter references require a named graph binding.",
				    EArdaRHIResult::Unsupported);
				return;
			}
			auto Name = Archive.mExternalReference(Identity, &ArdaGraphExternalType<T>);
			if (!Name)
			{
				Archive.Fail(Name.mStatus.mMessage.c_str(), Name.mStatus.mCode);
				return;
			}
			Archive.Write(Name.mValue);
		}

		static void Read(FArdaGraphArchiveReader& Archive, T& Value)
		{
			eastl::string Name;
			Archive.Read(Name);
			if (Archive.mExternalNames)
			{
				Archive.mExternalNames->push_back(Name);
			}
			Value = {};
			if (Name.empty() || !Archive.GetStatus())
			{
				return;
			}
			if (!Archive.mExternalReference)
			{
				Archive.Fail("Archive external parameter requires a slot table.");
				return;
			}
			auto Binding = Archive.mExternalReference(Name, &ArdaGraphExternalType<T>);
			if (!Binding)
			{
				Archive.Fail(Binding.mStatus.mMessage.c_str(), Binding.mStatus.mCode);
				return;
			}
			if (Binding.mValue)
			{
				Value = *eastl::static_pointer_cast<const T>(Binding.mValue);
			}
			else
			{
				Archive.mbHasUnboundExternalReferences = true;
			}
		}
	};

	template <class T>
	struct TArdaGraphCodec<T*> : TArdaGraphExternalReferenceCodec<T*>
	{
	};

	template <class T>
	struct TArdaGraphCodec<eastl::shared_ptr<T>> : TArdaGraphExternalReferenceCodec<eastl::shared_ptr<T>>
	{
	};

	template <class T>
	struct TArdaGraphCodec<std::shared_ptr<T>> : TArdaGraphExternalReferenceCodec<std::shared_ptr<T>>
	{
	};

	template <class T>
	struct TArdaGraphCodec<TArdaRHIRef<T>> : TArdaGraphExternalReferenceCodec<TArdaRHIRef<T>>
	{
	};

	template <class T>
	struct TArdaGraphCodec<TArdaGraphExternal<T>>
	{
		static constexpr bool mSupported = true;

		static void Write(FArdaGraphArchiveWriter& Archive, const TArdaGraphExternal<T>& Value)
		{
			if (!Archive.mExternalSlot || Value.mName.empty())
			{
				Archive.Fail("A named external value requires a declared slot.");
				return;
			}
			if (auto Status = Archive.mExternalSlot(Value.mName, &ArdaGraphExternalType<T>); !Status)
			{
				Archive.Fail(Status.mMessage.c_str(), Status.mCode);
				return;
			}
			Archive.Write(Value.mName);
		}

		static void Read(FArdaGraphArchiveReader& Archive, TArdaGraphExternal<T>& Value)
		{
			Archive.Read(Value.mName);
			if (!Archive.mExternalReference || Value.mName.empty())
			{
				Archive.Fail("A named external value requires a declared slot.");
				return;
			}
			auto Binding = Archive.mExternalReference(Value.mName, &ArdaGraphExternalType<T>);
			if (!Binding)
			{
				Archive.Fail(Binding.mStatus.mMessage.c_str(), Binding.mStatus.mCode);
			}
			else if (!Binding.mValue)
			{
				Archive.mbHasUnboundExternalReferences = true;
			}
		}
	};

}
