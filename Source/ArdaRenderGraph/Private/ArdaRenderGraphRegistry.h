#pragma once

#include "ArdaRenderGraphAllocator.h"
#include "ArdaRenderGraphDefinitions.h"
#include "ArdaRenderGraphLog.h"

#include <cstddef>
#include <cstdint>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace arda::render_graph
{
    namespace detail
    {
        /** Identifies the strongly typed handle template accepted by registries. */
        template <typename Type>
        struct TIsARDGHandle : eastl::false_type
        {
        };

        /** Matches any TARDGHandle specialization while preserving its tag type. */
        template <typename TagType>
        struct TIsARDGHandle<TARDGHandle<TagType>> : eastl::true_type
        {
        };
    }

    /**
     * Maps small, strongly typed handles to one category of graph records.
     *
     * A builder owns a separate registry for passes, textures, buffers, views,
     * and uniform buffers. "Registering" means constructing one of those
     * logical records in the graph's arena and appending its pointer to
     * mEntries. The new array-slot number is wrapped in HandleType and returned;
     * the handle stores this array position, not the object's memory address.
     *
     * For example, if mEntries already contains PassA and PassB, its size is 2.
     * Registering PassC appends its pointer at mEntries[2] and returns
     * FARDGPassHandle(2):
     *
     * @code
     * before: mEntries = [PassA*, PassB*]
     * after:  mEntries = [PassA*, PassB*, PassC*]
     *                                      ^
     *                                      index 2 -> FARDGPassHandle(2)
     * @endcode
     *
     * The pass registry is declared and used as follows:
     *
     * @code
     * TARDGHandleRegistry<FARDGPass, FARDGPassHandle> mPasses;
     *
     * FARDGPassHandle H = mPasses.Emplace<FARDGLambdaPass>(...);
     *
     * H(index = 2)
     *      |
     *      v
     * mPasses.mEntries[2] -----> FARDGLambdaPass in FARDGArena
     *                              (viewed through FARDGPass*)
     * @endcode
     *
     * Texture registration works the same way, but uses FARDGTexture and
     * FARDGTextureHandle. Because the handle types differ, a texture handle
     * cannot accidentally index the pass registry at compile time.
     *
     * Entries are append-only and arena-owned. Nothing is individually erased
     * or moved, so both the numeric handle and the pointed-to object address
     * remain stable until the graph and its arena are destroyed.
     *
     * @tparam ObjectType The common type stored in mEntries, such as FARDGPass
     * or FARDGTexture. It may be a base class when Emplace constructs a derived
     * record such as FARDGLambdaPass.
     * @tparam HandleType The matching strongly typed array index, such as
     * FARDGPassHandle. InvalidIndex is reserved for "no object" and therefore
     * also bounds the registry's capacity.
     */
    template <typename ObjectType, typename HandleType>
    class TARDGHandleRegistry final
    {
        static_assert(
            detail::TIsARDGHandle<HandleType>::value,
            "TARDGHandleRegistry HandleType must specialize TARDGHandle<TagType>.");

    public:
        /** Binds an initially empty registry to its graph-owned arena. */
        explicit TARDGHandleRegistry(FARDGArena& Arena) noexcept
            : mArena(Arena)
        {
        }

        /** Copying is disabled because entries are owned by the bound arena. */
        TARDGHandleRegistry(const TARDGHandleRegistry&) = delete;
        /** Copying is disabled because entries are owned by the bound arena. */
        TARDGHandleRegistry& operator=(const TARDGHandleRegistry&) = delete;

        /**
         * Constructs and registers the next logical graph record.
         *
         * The current entry count becomes the object's typed handle, which is
         * passed as the first constructor argument. The arena owns the object;
         * this registry stores only its stable pointer. Exhausting the handle
         * type's index space is rejected before construction.
         *
         * The unnamed enable_if template parameter is a compile-time gate:
         * Emplace exists only when ConcreteType is ObjectType itself or derives
         * from ObjectType. This guarantees that the constructed ConcreteType*
         * can be stored safely as an ObjectType*; attempting to insert an
         * unrelated record type fails during compilation with no runtime cost.
         *
         * @tparam ConcreteType The concrete record to construct; defaults to
         * the registry's ObjectType.
         * @tparam ArgumentTypes Constructor argument types inferred from
         * Arguments.
         */
        template <
            typename ConcreteType = ObjectType,
            typename... ArgumentTypes,
            typename = eastl::enable_if_t<eastl::is_base_of_v<ObjectType, ConcreteType>>>
        [[nodiscard]] HandleType Emplace(ArgumentTypes&&... Arguments)
        {
            // HandleType reserves uint32_t::max() as InvalidIndex. Valid entries
            // therefore use indices [0, InvalidIndex), so the registry can hold
            // at most InvalidIndex entries before the next index would be invalid.
            if (mEntries.size() >= HandleType::InvalidIndex)
            {
                ARDA_CHECK_MSG("A render-graph registry exhausted its handle index space.");
            }

            const HandleType Handle(static_cast<uint32_t>(mEntries.size()));
            ConcreteType* Object = mArena.Allocate<ConcreteType>(
                Handle,
                eastl::forward<ArgumentTypes>(Arguments)...);

            mEntries.push_back(Object);

            return Handle;
        }

        /**
         * Resolves a mutable record during graph build/compile, or returns null
         * when the typed handle is invalid or outside this registry.
         */
        [[nodiscard]] ObjectType* TryGet(HandleType Handle) noexcept
        {
            if (!Handle.IsValid() || Handle.GetIndex() >= mEntries.size())
            {
                return nullptr;
            }
            return mEntries[Handle.GetIndex()];
        }

        /** Const overload of TryGet with the same non-failing validation. */
        [[nodiscard]] const ObjectType* TryGet(HandleType Handle) const noexcept
        {
            if (!Handle.IsValid() || Handle.GetIndex() >= mEntries.size())
            {
                return nullptr;
            }
            return mEntries[Handle.GetIndex()];
        }

        /**
         * Resolves a mutable record and treats an invalid handle as a graph error.
         *
         * The returned reference remains stable until the owning arena resets.
         */
        [[nodiscard]] ObjectType& Get(HandleType Handle)
        {
            ObjectType* Object = TryGet(Handle);
            if (!Object)
            {
                ARDA_CHECK_MSG("Invalid render-graph registry handle.");
            }
            return *Object;
        }

        /** Const, checked counterpart to the mutable Get overload. */
        [[nodiscard]] const ObjectType& Get(HandleType Handle) const
        {
            const ObjectType* Object = TryGet(Handle);
            if (!Object)
            {
                ARDA_CHECK_MSG("Invalid render-graph registry handle.");
            }
            return *Object;
        }

        /** Returns the number of records registered in this append-only category. */
        [[nodiscard]] size_t GetCount() const noexcept
        {
            return mEntries.size();
        }

        /** Returns whether no records have yet been registered. */
        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return mEntries.empty();
        }

        /**
         * Exposes entries in handle-index/registration order for graph stages
         * that need deterministic whole-registry traversal.
         */
        [[nodiscard]] const eastl::vector<ObjectType*>& GetEntries() const noexcept
        {
            return mEntries;
        }

    private:
        /** Non-owning arena reference that remains valid for the registry's full lifetime. */
        FARDGArena& mArena;
        /** Stable non-owning object pointers in typed-handle index/registration order. */
        eastl::vector<ObjectType*> mEntries;
    };
}
