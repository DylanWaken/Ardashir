/** @file ArdaRHIStatus.h
 * Declares Status definitions for the RHI config module.
 */

#pragma once


#include <EASTL/string.h>
#include <cstdint>

namespace arda
{
	/** Enumerates result values. */
	enum class EArdaRHIResult : uint8_t
	{
		Success,
		InvalidArgument,
		Unsupported,
		BackendFailure,
		InvalidState,
		WrongDevice
	};

	/** Describes status. */
	struct FArdaRHIStatus
	{
		/** Stores the code. */
		EArdaRHIResult mCode = EArdaRHIResult::Success;
		/** Stores the message. */
		eastl::string mMessage;

		/**
         * Tests whether the status represents success.
         * @return True when the condition is satisfied; otherwise false.
         */
		[[nodiscard]] bool IsSuccess() const noexcept
		{
			return mCode == EArdaRHIResult::Success;
		}

		/**
         * Converts the status to a success flag.
         * @return True when the reference or result is valid; otherwise false.
         */
		[[nodiscard]] explicit operator bool() const noexcept
		{
			return IsSuccess();
		}

		/**
         * Performs the success operation.
         * @return A status describing whether the operation succeeded.
         */
		static FArdaRHIStatus Success()
		{
			return {};
		}

		/**
         * Performs the error operation.
         * @param Code The code.
         * @param Message The message.
         * @return A status describing whether the operation succeeded.
         */
		static FArdaRHIStatus Error(EArdaRHIResult Code, const char* Message)
		{
			return {Code, Message ? Message : ""};
		}
	};

	/** Describes result. */
	template <typename T>
	struct TArdaRHIResult
	{
		/** Stores the value. */
		T mValue{};
		/** Stores the status. */
		FArdaRHIStatus mStatus;

		/**
         * Converts the result to a success flag.
         * @return True when the reference or result is valid; otherwise false.
         */
		[[nodiscard]] explicit operator bool() const noexcept
		{
			return mStatus.IsSuccess();
		}
	};
}
