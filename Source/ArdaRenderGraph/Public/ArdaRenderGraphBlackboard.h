#pragma once

#include "ArdaRenderGraphLog.h"

#include <EASTL/any.h>
#include <typeindex>
#include <EASTL/type_traits.h>
#include <EASTL/unordered_map.h>
#include <EASTL/utility.h>

namespace arda::render_graph
{
    struct FARDGTypeIndexHash
    {
        [[nodiscard]] size_t operator()(const std::type_index& Type) const noexcept
        {
            return Type.hash_code();
        }
    };

    /** Stores one graph-scoped value per C++ type. */
    class FARDGBlackboard final
    {
    public:
        /** Returns whether a value of ValueType is present. */
        template <typename ValueType>
        [[nodiscard]] bool Contains() const noexcept
        {
            return mValues.find(std::type_index(typeid(ValueType))) != mValues.end();
        }

        /** Replaces the value associated with ValueType and returns it. */
        template <typename ValueType>
        ValueType& Set(ValueType Value)
        {
            static_assert(
                eastl::is_copy_constructible_v<ValueType>,
                "Render-graph blackboard values must be copy constructible.");
            const std::type_index Key(typeid(ValueType));
            mValues.insert_or_assign(Key, eastl::move(Value));
            return eastl::any_cast<ValueType&>(mValues.at(Key));
        }

        /** Constructs the value associated with ValueType and returns it. */
        template <typename ValueType, typename... ArgumentTypes>
        ValueType& Emplace(ArgumentTypes&&... Arguments)
        {
            return Set(ValueType(eastl::forward<ArgumentTypes>(Arguments)...));
        }

        /** Returns the value associated with ValueType. */
        template <typename ValueType>
        [[nodiscard]] ValueType& Get()
        {
            ValueType* Value = TryGet<ValueType>();
            if (Value == nullptr)
            {
                ARDA_CHECK_MSG("The requested render-graph blackboard value is absent.");
            }
            return *Value;
        }

        /** Returns the immutable value associated with ValueType. */
        template <typename ValueType>
        [[nodiscard]] const ValueType& Get() const
        {
            const ValueType* Value = TryGet<ValueType>();
            if (Value == nullptr)
            {
                ARDA_CHECK_MSG("The requested render-graph blackboard value is absent.");
            }
            return *Value;
        }

        /** Returns the value associated with ValueType, or null when absent. */
        template <typename ValueType>
        [[nodiscard]] ValueType* TryGet() noexcept
        {
            const auto Iterator = mValues.find(std::type_index(typeid(ValueType)));
            return Iterator == mValues.end()
                ? nullptr
                : eastl::any_cast<ValueType>(&Iterator->second);
        }

        /** Returns the immutable value associated with ValueType, or null when absent. */
        template <typename ValueType>
        [[nodiscard]] const ValueType* TryGet() const noexcept
        {
            const auto Iterator = mValues.find(std::type_index(typeid(ValueType)));
            return Iterator == mValues.end()
                ? nullptr
                : eastl::any_cast<ValueType>(&Iterator->second);
        }

        /** Returns an existing value or default-constructs it when absent. */
        template <typename ValueType>
        ValueType& GetOrCreate()
        {
            if (ValueType* Existing = TryGet<ValueType>())
            {
                return *Existing;
            }
            return Emplace<ValueType>();
        }

    private:
        eastl::unordered_map<std::type_index, eastl::any, FARDGTypeIndexHash>
            mValues;
    };
}
