// App::run()'s exit-code derivation.
//
// Before this, App.cpp always returned EXIT_SUCCESS regardless of how the
// frame loop ended, so a device-lost or fatal-submit run was reported as a
// clean quit - Invoke-SyncValidation.ps1 and the Invoke-ClangCl*.ps1 helpers had
// no way to tell a broken run from a normal window close.

#include <gtest/gtest.h>

#include "app/AppExitCode.hpp"

using Kataglyphis::appExitCode;

TEST(AppExitCodeUnit, CleanRunSucceeds)
{
    EXPECT_EQ(appExitCode(false, false), EXIT_SUCCESS);
}

TEST(AppExitCodeUnit, DeviceLossFails)
{
    EXPECT_EQ(appExitCode(true, false), EXIT_FAILURE);
}

TEST(AppExitCodeUnit, FatalFrameErrorWithoutDeviceLossFails)
{
    // The case hasDeviceLost() alone would miss: a failed wait/submit or an
    // invalid sync handle that never set device_lost_detected.
    EXPECT_EQ(appExitCode(false, true), EXIT_FAILURE);
}

TEST(AppExitCodeUnit, BothFail)
{
    EXPECT_EQ(appExitCode(true, true), EXIT_FAILURE);
}
