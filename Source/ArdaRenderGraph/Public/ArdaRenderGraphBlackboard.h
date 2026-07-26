#pragma once

#include "ArdaRenderGraphLog.h"

#include <any>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace arda::render_graph
{
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
                std::is_copy_constructible_v<ValueType>,
                "Render-graph blackboard values must be copy constructible.");
            const std::type_index Key(typeid(ValueType));
            mValues.insert_or_assign(Key, std::move(Value));
            return std::any_cast<ValueType&>(mValues.at(Key));
        }

        /** Constructs the value associated with ValueType and returns it. */
        template <typename ValueType, typename... ArgumentTypes>
        ValueType& Emplace(ArgumentTypes&&... Arguments)
        {
            return Set(ValueType(std::forward<ArgumentTypes>(Arguments)...));
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
                : std::any_cast<ValueType>(&Iterator->second);
        }

        /** Returns the immutable value associated with ValueType, or null when absent. */
        template <typename ValueType>
        [[nodiscard]] const ValueType* TryGet() const noexcept
        {
            const auto Iterator = mValues.find(std::type_index(typeid(ValueType)));
            return Iterator == mValues.end()
                ? nullptr
                : std::any_cast<ValueType>(&Iterator->second);
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
        std::unordered_map<std::type_index, std::any> mValues;
    };
}
