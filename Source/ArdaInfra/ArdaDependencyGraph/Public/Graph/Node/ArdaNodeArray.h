/** Typed invocation lanes. Arrays zip together and ordinary parameters broadcast. */
#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>
#include <EASTL/type_traits.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>
#include <initializer_list>

namespace arda
{
	/** An owning array of node arguments or invocation results.
	 * Passing this wrapper at any positional parameter slot invokes one logical operation per element.
	 * All array arguments must have equal lengths; scalar arguments are copied to every lane. This is
	 * zip/broadcast semantics, not a Cartesian product. Empty arrays produce no operations. Ordinary
	 * vectors and shader parameter arrays remain single parameter values unless explicitly wrapped.
	 * Invocation freezes each parameter value before returning; nested pointees retain normal shared
	 * ownership semantics. The compiler resolves dependencies/pipelines and batches the logical lanes.
	 */
	template <class Value>
	class TArdaNodeArray final
	{
	public:
		using FArdaValue = Value;
		using FArdaSize = typename eastl::vector<Value>::size_type;

		TArdaNodeArray() = default;

		TArdaNodeArray(std::initializer_list<Value> Values)
		    : mValues(Values.begin(), Values.end())
		{
		}

		explicit TArdaNodeArray(eastl::vector<Value> Values)
		    : mValues(eastl::move(Values))
		{
		}

		template <class Iterator>
		TArdaNodeArray(Iterator Begin, Iterator End)
		    : mValues(Begin, End)
		{
		}

		FArdaSize size() const noexcept
		{
			return mValues.size();
		}

		bool empty() const noexcept
		{
			return mValues.empty();
		}

		void reserve(FArdaSize Count)
		{
			mValues.reserve(Count);
		}

		void push_back(Value Item)
		{
			mValues.push_back(eastl::move(Item));
		}

		Value& operator[](FArdaSize Index)
		{
			return mValues[Index];
		}

		const Value& operator[](FArdaSize Index) const
		{
			return mValues[Index];
		}

		auto begin() noexcept
		{
			return mValues.begin();
		}

		auto end() noexcept
		{
			return mValues.end();
		}

		auto begin() const noexcept
		{
			return mValues.begin();
		}

		auto end() const noexcept
		{
			return mValues.end();
		}

		/** Transform results into another typed argument array without changing lane order. */
		template <class Transform>
		auto Map(Transform&& Function) const
		    -> TArdaNodeArray<eastl::decay_t<eastl::invoke_result_t<Transform&, const Value&>>>
		{
			TArdaNodeArray<eastl::decay_t<eastl::invoke_result_t<Transform&, const Value&>>> Result;
			Result.reserve(size());
			for (const auto& Item : mValues)
			{
				Result.push_back(eastl::invoke(Function, Item));
			}
			return Result;
		}

		/** Retrieve the same declared output slot from every operation before closing the edit. */
		template <class Item = Value>
		auto GetBuffer(const eastl::string& Slot = "Destination") const
		    -> TArdaNodeArray<decltype(eastl::declval<const Item&>().GetBuffer(Slot))>
		{
			return Map(
			    [&](const Value& Operation)
			    {
				    return Operation.GetBuffer(Slot);
			    });
		}

		template <class Item = Value>
		auto GetTexture(const eastl::string& Slot = "Destination") const
		    -> TArdaNodeArray<decltype(eastl::declval<const Item&>().GetTexture(Slot))>
		{
			return Map(
			    [&](const Value& Operation)
			    {
				    return Operation.GetTexture(Slot);
			    });
		}

		template <class Item = Value>
		auto GetAccelerationStructure(const eastl::string& Slot = "Destination") const
		    -> TArdaNodeArray<decltype(eastl::declval<const Item&>().GetAccelerationStructure(Slot))>
		{
			return Map(
			    [&](const Value& Operation)
			    {
				    return Operation.GetAccelerationStructure(Slot);
			    });
		}

	private:
		eastl::vector<Value> mValues;
	};

	template <class Value>
	struct TArdaIsNodeArray : eastl::false_type
	{
	};

	template <class Value>
	struct TArdaIsNodeArray<TArdaNodeArray<Value>> : eastl::true_type
	{
	};
	template <class... Arguments>
	inline constexpr bool ArdaHasNodeArray = (TArdaIsNodeArray<eastl::decay_t<Arguments>>::value || ...);

	namespace node_array_detail
	{
		/** Arrays borrow one lane; scalars borrow the same value for every lane. Copying into the final
		 * parameter schema preserves brace-initialization diagnostics, including narrowing checks.
		 */
		template <class Argument>
		const Argument& At(const Argument& Value, size_t)
		{
			return Value;
		}

		template <class Argument>
		const Argument& At(const TArdaNodeArray<Argument>& Values, size_t Index)
		{
			return Values[Index];
		}

		template <class... Arguments>
		bool ResolveCount(size_t& Count, const Arguments&... Values)
		{
			bool HasCount = false;
			bool Matches = true;
			const auto Check = [&](const auto& Value)
			{
				if constexpr (TArdaIsNodeArray<eastl::decay_t<decltype(Value)>>::value)
				{
					if (HasCount && Count != Value.size())
					{
						Matches = false;
					}
					Count = Value.size();
					HasCount = true;
				}
			};
			(Check(Values), ...);
			return Matches;
		}
	}
}
