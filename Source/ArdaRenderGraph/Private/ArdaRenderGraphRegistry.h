#pragma once

#include "ArdaRenderGraphAllocator.h"
#include "ArdaRenderGraphLog.h"

#include <cstddef>
#include <cstdint>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace arda::render_graph
{
    template <typename ObjectType, typename HandleType>
    class TARDGHandleRegistry final
    {
    public:
        explicit TARDGHandleRegistry(FARDGArena& Arena) noexcept
            : mArena(Arena)
        {
        }

        TARDGHandleRegistry(const TARDGHandleRegistry&) = delete;
        TARDGHandleRegistry& operator=(const TARDGHandleRegistry&) = delete;

        template <
            typename ConcreteType = ObjectType,
            typename... ArgumentTypes,
            typename = eastl::enable_if_t<eastl::is_base_of_v<ObjectType, ConcreteType>>>
        [[nodiscard]] HandleType Emplace(ArgumentTypes&&... Arguments)
        {
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

        [[nodiscard]] ObjectType* TryGet(HandleType Handle) noexcept
        {
            if (!Handle.IsValid() || Handle.GetIndex() >= mEntries.size())
            {
                return nullptr;
            }
            return mEntries[Handle.GetIndex()];
        }

        [[nodiscard]] const ObjectType* TryGet(HandleType Handle) const noexcept
        {
            if (!Handle.IsValid() || Handle.GetIndex() >= mEntries.size())
            {
                return nullptr;
            }
            return mEntries[Handle.GetIndex()];
        }

        [[nodiscard]] ObjectType& Get(HandleType Handle)
        {
            ObjectType* Object = TryGet(Handle);
            if (Object == nullptr)
            {
                ARDA_CHECK_MSG("Invalid render-graph registry handle.");
            }
            return *Object;
        }

        [[nodiscard]] const ObjectType& Get(HandleType Handle) const
        {
            const ObjectType* Object = TryGet(Handle);
            if (Object == nullptr)
            {
                ARDA_CHECK_MSG("Invalid render-graph registry handle.");
            }
            return *Object;
        }

        [[nodiscard]] size_t GetCount() const noexcept
        {
            return mEntries.size();
        }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return mEntries.empty();
        }

        [[nodiscard]] const eastl::vector<ObjectType*>& GetEntries() const noexcept
        {
            return mEntries;
        }

    private:
        FARDGArena& mArena;
        eastl::vector<ObjectType*> mEntries;
    };
}
