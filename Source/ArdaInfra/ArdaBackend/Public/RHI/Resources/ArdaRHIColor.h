/** @file ArdaRHIColor.h
 * Declares Color definitions for the RHI resources module.
 */

#pragma once



namespace arda
{
	/** Describes color. */
	struct FArdaRHIColor
	{
		/** Red component. */
		float mR = 0.f;
		/** Green component. */
		float mG = 0.f;
		/** Blue component. */
		float mB = 0.f;
		/** Alpha component. */
		float mA = 0.f;

		/**
         * Compares two values for equality.
         * @param O The o.
         * @return True when the condition is satisfied; otherwise false.
         */
		bool operator==(const FArdaRHIColor& O) const noexcept
		{
			return mR == O.mR && mG == O.mG && mB == O.mB && mA == O.mA;
		}
	};
}
