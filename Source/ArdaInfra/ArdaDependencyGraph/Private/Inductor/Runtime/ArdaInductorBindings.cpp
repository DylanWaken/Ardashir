#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Rules/ArdaInductorBindingArguments.h"
#include "Inductor/Runtime/ArdaInductorBindings.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		const eastl::vector<FArdaRHIBindingLayoutRef>& PipelineLayouts(const FArdaInductorResolvedPipeline& P)
		{
			if (P.mCompute)
			{
				return P.mCompute->GetDesc().mBindingLayouts;
			}
			if (P.mGraphics)
			{
				return P.mGraphics->GetDesc().mBindingLayouts;
			}
			if (P.mMeshlet)
			{
				return P.mMeshlet->GetDesc().mBindingLayouts;
			}
			if (P.mRayTracing)
			{
				return P.mRayTracing->GetDesc().mGlobalBindingLayouts;
			}
			if (P.mWorkGraph)
			{
				return P.mWorkGraph->GetDesc().mGlobalBindingLayouts;
			}
			static const eastl::vector<FArdaRHIBindingLayoutRef> Empty;
			return Empty;
		}

		bool SameItems(const FArdaRHIBindingSetDesc& A, const FArdaRHIBindingSetDesc& B)
		{
			if (A.mItems.size() != B.mItems.size())
			{
				return false;
			}
			for (const auto& I : A.mItems)
			{
				if (!eastl::any_of(B.mItems.begin(),
				        B.mItems.end(),
				        [&](const auto& J)
				        {
					        return I.mSlot == J.mSlot && I.mArrayElement == J.mArrayElement && I.mType == J.mType &&
					            I.mResource == J.mResource && I.mView == J.mView;
				        }))
				{
					return false;
				}
			}
			return true;
		}

	}

	FArdaRHIStatus PrepareArdaShaderBindings(const FArdaDependencyGraph::FArdaImpl& G, FArdaInductorFrame& Frame)
	{
		for (auto Handle : G.mCompile.mExecutionOrder)
		{
			const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			const auto Slots = Frame.mNodeBindings.find(Handle.GetIndex());
			if (Slots == Frame.mNodeBindings.end() || Slots->second.mPipelines.empty())
			{
				if (!Node.mDesc.mShaderBindings.empty() || !Node.mDesc.mBindlessTables.empty() ||
				    !Node.mDesc.mShaderTables.empty())
				{
					return BindingError("No compiled pipeline for declared shader arguments.");
				}
				continue;
			}

			// Prepare each resolved pipeline slot independently for this retained frame allocation.
			for (const auto& Slot : Slots->second.mPipelines)
			{
				const auto& Layouts = PipelineLayouts(Slot.second);
				const bool Automatic = eastl::any_of(Node.mDesc.mShaderBindings.begin(),
				                           Node.mDesc.mShaderBindings.end(),
				                           [&](const auto& B)
				                           {
					                           return B.mPipelineSlot == Slot.first;
				                           }) ||
				    eastl::any_of(Node.mDesc.mBindlessTables.begin(),
				        Node.mDesc.mBindlessTables.end(),
				        [&](const auto& B)
				        {
					        return B.mPipelineSlot == Slot.first;
				        });
				if (Automatic)
				{
					for (const auto& B : Node.mDesc.mShaderBindings)
					{
						if (B.mPipelineSlot != Slot.first)
						{
							continue;
						}
						eastl::vector<FArdaRHIBindingLayoutDesc> Generated;
						if (auto S = B.mMetadata->BuildBindingLayoutDescs(Generated); !S)
						{
							return BindingError(S.mMessage);
						}
						for (const auto& D : Generated)
						{
							if (!eastl::any_of(Layouts.begin(),
							        Layouts.end(),
							        [&](const auto& L)
							        {
								        return L && !L->GetBindlessDesc() && L->GetDesc() == D;
							        }))
							{
								return BindingError("Shader schema has a layout absent from the compiled pipeline.");
							}
						}
					}

					// Materialize fixed sets and bindless table entries from this frame's physical resources.
					auto& Sets = Slots->second.mBindings[Slot.first];
					for (const auto& Layout : Layouts)
					{
						if (!Layout)
						{
							return BindingError("Null compiled pipeline layout.");
						}
						if (Layout->GetBindlessDesc())
						{
							const auto B = eastl::find_if(Node.mDesc.mBindlessTables.begin(),
							    Node.mDesc.mBindlessTables.end(),
							    [&](const auto& T)
							    {
								    return T.mPipelineSlot == Slot.first &&
								        T.mLayout.mRegisterSpace == Layout->GetDesc().mRegisterSpace;
							    });
							if (B == Node.mDesc.mBindlessTables.end())
							{
								return BindingError("Pipeline bindless layout has no declared table.");
							}
							auto Table = G.mDevice->CreateDescriptorTable(Layout);
							if (!Table)
							{
								return Table.mStatus;
							}
							if (auto S = G.mDevice->ResizeDescriptorTable(Table.mValue, B->mCapacity); !S)
							{
								return S;
							}
							for (const auto& R : B->mEntries)
							{
								FArdaRHIBindingItem Item;
								Item.mSlot = R.mSlot;
								Item.mArrayElement = R.mArrayElement;
								Item.mType = R.mType;
								Item.mView = R.mView;
								Item.mResource = Physical(G, Frame, R.mResource, R.mSampler);
								if (auto S = G.mDevice->WriteDescriptorTable(Table.mValue, Item); !S)
								{
									return S;
								}
							}
							Sets.push_back(FArdaRHIBindingSetRef(Table.mValue.Get()));
							Slots->second.mDescriptorTables[B->mName] = eastl::move(Table.mValue);
							continue;
						}
						FArdaRHIBindingSetDesc Set;
						bool Matched = false;
						for (const auto& B : Node.mDesc.mShaderBindings)
						{
							if (B.mPipelineSlot != Slot.first || !MatchesSchema(B, Layout))
							{
								continue;
							}
							auto Candidate = MakeSet(G, Frame, B, Layout);
							if (Matched && !SameItems(Set, Candidate))
							{
								return BindingError("Different shader arguments target the same pipeline layout.");
							}
							Set = eastl::move(Candidate);
							Matched = true;
						}
						if (!Matched)
						{
							return BindingError("Compiled pipeline layout has no matching shader parameter schema.");
						}
						Set.mDebugName = Node.mName;
						auto Created = G.mDevice->CreateBindingSet(Set);
						if (!Created)
						{
							return Created.mStatus;
						}
						Sets.push_back(eastl::move(Created.mValue));
					}
				}

				// Relocate logical descriptor references only after table storage and heap indices are known.
				bool HavePush = false;
				auto& Push = Slots->second.mShaderParameters[Slot.first];
				for (const auto& B : Node.mDesc.mShaderBindings)
				{
					if (B.mPipelineSlot != Slot.first)
					{
						continue;
					}
					for (const auto& V : B.mValues)
					{
						auto Bytes = V.mBytes;
						for (size_t I = 0; I < V.mDescriptorIndices.size(); ++I)
						{
							const auto& Index = V.mDescriptorIndices[I];
							if (Index.mByteOffset % 4 || Index.mByteOffset > Bytes.size() ||
							    Bytes.size() - Index.mByteOffset < 4)
							{
								return BindingError(
								    "Descriptor index parameter exceeds or misaligns its uint32 field.");
							}
							for (size_t J = 0; J < I; ++J)
							{
								if (V.mDescriptorIndices[J].mByteOffset == Index.mByteOffset)
								{
									return BindingError("Duplicate descriptor-index parameter field.");
								}
							}
							const auto Table = eastl::find_if(Node.mDesc.mBindlessTables.begin(),
							    Node.mDesc.mBindlessTables.end(),
							    [&](const auto& T)
							    {
								    return T.mName == Index.mTable && T.mPipelineSlot == Slot.first;
							    });
							if (Table == Node.mDesc.mBindlessTables.end() ||
							    !eastl::any_of(Table->mEntries.begin(),
							        Table->mEntries.end(),
							        [&](const auto& E)
							        {
								        return E.mSlot == Index.mSlot && E.mType == Index.mType &&
								            E.mArrayElement == Index.mArrayElement;
							        }))
							{
								return BindingError("Descriptor index parameter names an undeclared table entry.");
							}
							uint64_t Value = Index.mArrayElement;
							if (Index.mbAbsoluteHeapIndex)
							{
								if (!Table->mLayout.mbDirectHeapIndexing || Table->mLayout.mRegisterSpaces.size() != 1)
								{
									return BindingError(
									    "Absolute indices require a direct-heap table with exactly one descriptor bank.");
								}
								Value +=
								    Slots->second.mDescriptorTables.at(Table->mName)->GetFirstDescriptorIndexInHeap();
							}
							if (Value > UINT32_MAX)
							{
								return BindingError("Descriptor heap index overflows uint32.");
							}
							const uint32_t NativeIndex = static_cast<uint32_t>(Value);
							std::memcpy(Bytes.data() + Index.mByteOffset, &NativeIndex, sizeof(NativeIndex));
						}
						if (HavePush && Push != Bytes)
						{
							return BindingError(
							    "A pipeline slot must supply identical bytes for its shared push-constant block.");
						}
						Push = eastl::move(Bytes);
						HavePush = true;
					}
				}

				// Build shader records after their pipeline identifiers and local arguments are available.
				if (Slot.second.mRayTracing)
				{
					if (auto S =
					        PrepareRayTable(G, Frame, Handle.GetIndex(), Node, Slot.first, Slot.second.mRayTracing);
					    !S)
					{
						return S;
					}
				}
			}
		}
		return {};
	}
}
