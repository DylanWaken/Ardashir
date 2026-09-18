/** Opaque native handles shared by device and resource interop. */
#pragma once

#include <cstddef>
#include <cstdint>

namespace arda
{
	/** Stores a platform-native handle without exposing its native type. */
	struct FArdaNativeObject
	{
		/** Integer representation of the native handle. */
		uintptr_t mValue = 0;

		/** Creates an empty native object. */
		constexpr FArdaNativeObject() noexcept = default;

		/** Creates an empty native object from null. */
		constexpr FArdaNativeObject(std::nullptr_t) noexcept
		{
		}

		/**
         * Creates a native object from an integer handle.
         * @param Value Integer representation of the native handle.
         */
		explicit constexpr FArdaNativeObject(uintptr_t Value) noexcept
		    : mValue(Value)
		{
		}

		/**
         * Creates a native object from a pointer.
         * @tparam T Pointed-to native type.
         * @param Value Native pointer to encode.
         */
		template <typename T>
		explicit FArdaNativeObject(T* Value) noexcept
		    : mValue(reinterpret_cast<uintptr_t>(Value))
		{
		}

		/** @return True when the native handle is nonzero. */
		[[nodiscard]] explicit constexpr operator bool() const noexcept
		{
			return mValue != 0;
		}

		/**
         * Decodes the native handle as the requested type.
         * @tparam T Pointer or integer type to return.
         * @return The native handle converted to T.
         */
		template <typename T>
		[[nodiscard]] T As() const noexcept
		{
			return reinterpret_cast<T>(mValue);
		}
	};
}
