/** @file ArdaTestBackend.h
 * GoogleTest initialization policy shared by backend and RDG GPU fixtures.
 * Skip only native validation unavailability; preserve all other assertion failures.
 */
#pragma once
#include "ArdaBackend.h"
#include <gtest/gtest.h>

// Like ASSERT_TRUE, this returns from the surrounding test/fixture/helper. Only
// missing validation is a skip; shader, device and other initialization bugs fail.
#define ARDA_REQUIRE_BACKEND() \
    GTEST_AMBIGUOUS_ELSE_BLOCKER_ \
    if (const bool ArdaInitialized = ::arda::backend::InitializeBackend(); \
        !ArdaInitialized && ::arda::backend::GetBackendInitializeResult() == \
            ::arda::backend::EArdaInitializeResult::ValidationUnavailable) \
        GTEST_SKIP() << ::arda::backend::GetBackendError().c_str(); \
    else \
        ASSERT_TRUE(ArdaInitialized) << ::arda::backend::GetBackendError().c_str()
