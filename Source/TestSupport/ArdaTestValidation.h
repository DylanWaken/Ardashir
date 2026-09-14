/** @file ArdaTestValidation.h
 * Build policy for GPU API validation in examples and tests. Result checks and
 * facade validation remain enabled independently of the native debug layers.
 */
#pragma once
#include "ArdaBackend.h"

#ifndef ARDA_TEST_ENABLE_VALIDATION
#error "Configure this target with ardashir_test_validation."
#endif

namespace arda
{
	inline constexpr bool ArdaTestValidationEnabled = ARDA_TEST_ENABLE_VALIDATION != 0;

	[[nodiscard]] inline FArdaBackendConfiguration MakeArdaTestBackendConfiguration()
	{
		FArdaBackendConfiguration Configuration;
		Configuration.mbEnableValidation = ArdaTestValidationEnabled;
		return Configuration;
	}
}
