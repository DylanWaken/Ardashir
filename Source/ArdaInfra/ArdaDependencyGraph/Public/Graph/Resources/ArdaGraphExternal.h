/** Named context-owned values. Archives contain names and types, never addresses. */
#pragma once

#include "RHI/Resources/ArdaRHIFwd.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/type_traits.h>
#include <memory>

namespace arda
{
	template <class T>
	inline const uint8_t ArdaGraphExternalType = 0;

	/** Named slot for any application-owned object, callback or reference type. */
	template <class T>
	struct TArdaGraphExternal
	{
		eastl::string mName;
	};

	/** Portable slot description; the type name is supplied by the application. */
	struct FArdaGraphExternalValueSlot
	{
		eastl::string mName;
		eastl::string mTypeName;
		bool mbBound = false;
	};

	/** In-memory binding retained separately from its portable slot declaration. */
	struct FArdaGraphExternalValueBinding
	{
		eastl::string mName;
		eastl::string mTypeName;
		const void* mType = nullptr;
		const void* mIdentity = nullptr;
		eastl::shared_ptr<const void> mValue;
	};

	template <class T>
	struct TArdaGraphExternalReference
	{
		static constexpr bool mReference = false;

		static const void* Identity(const T&)
		{
			return nullptr;
		}
	};

	template <class T>
	struct TArdaGraphExternalReference<T*>
	{
		static constexpr bool mReference = true;

		static const void* Identity(T* Value)
		{
			return reinterpret_cast<const void*>(Value);
		}
	};

	template <class T>
	struct TArdaGraphExternalReference<eastl::shared_ptr<T>>
	{
		static constexpr bool mReference = true;

		static const void* Identity(const eastl::shared_ptr<T>& Value)
		{
			return Value.get();
		}
	};

	template <class T>
	struct TArdaGraphExternalReference<std::shared_ptr<T>>
	{
		static constexpr bool mReference = true;

		static const void* Identity(const std::shared_ptr<T>& Value)
		{
			return Value.get();
		}
	};

	template <class T>
	struct TArdaGraphExternalReference<TArdaRHIRef<T>>
	{
		static constexpr bool mReference = true;

		static const void* Identity(const TArdaRHIRef<T>& Value)
		{
			return Value.Get();
		}
	};
}
