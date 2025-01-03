/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://www.hdfgroup.org/licenses.               *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * FILE
 * testm5hdf5.c - HDF5 multi-threaded testing framework main file
 * 
 * REMARKS
 * General test wrapper for HDF5 library multi-threaded safety test programs
 *
 * DESIGN
 * Each test function should be implemented as function having no
 * parameters and returning void (i.e. no return value).  They should be put
 * into the list of AddTest() calls in main() below. Functions which depend
 * on other functionality should be placed below the AddTest() call for the
 * base functionality testing.
 * Each test module should include testm5hdf5.h and define a unique set of
 * names for the test files they create.
 * 
 * BUGS/LIMITATIONS
 */

#include "testmthdf5.h"

/* Margin of runtime for each subtest allocated to cleanup */
#define MT_VL_TEST_MARGIN 1

/* Parameter to determine extent of stress testing */
#define NUM_ITERS 100

int main(int argc, char *argv[]) 
{
    unsigned runtime;           /* Maximum run-time for test (in seconds) */
    unsigned num_subtests = 11;
    int testExpress;
    int num_errs_occurred = 0;
    mt_test_params params;
    int64_t threaded_test_flag = ALLOW_MULTITHREAD;
    int64_t no_threaded_test_flag = 0;

    /* Silence compiler warnings */
    (void) params;

    /* Initialize testing framework */
    TestInit(argv[0], NULL, NULL, NULL, NULL, 0);

    testExpress = GetTestExpress();

    if (testExpress == 0) {
        runtime = 0; /* Run with no timeout  */
    } else if (testExpress == 1) {
        runtime = 1800; /* 30 minute timeout */
    } else if (testExpress == 2) {
        runtime = 600; /* 10 minute timeout */
    } else {
        runtime = 60; /* 1 minute timeout */
    }

    params.num_repetitions = NUM_ITERS;

    if (testExpress > 0) {
        params.subtest_timeout = (runtime - MT_VL_TEST_MARGIN) / num_subtests;
    } else {
        params.subtest_timeout = 0;
    }

#ifdef H5_HAVE_MULTITHREAD
    /* H5VL Tests */
    AddTest("mt_reg_unreg", mt_test_registration,
        NULL, NULL, &params, sizeof(mt_test_params), threaded_test_flag, "MT reg/unreg of a single connector");

    AddTest("mt_reg_by_name", mt_test_registration_by_name,
        NULL, NULL, &params, sizeof(mt_test_params), threaded_test_flag, "MT reg/unreg of a single connector by name");

    AddTest("mt_reg_by_val", mt_test_registration_by_value,
        NULL, NULL, &params, sizeof(mt_test_params), threaded_test_flag, "MT reg/unreg of a single connector by value");

    AddTest("mt_dyn_op_reg", mt_test_dyn_op_registration,
        NULL, NULL, &params, sizeof(mt_test_params), threaded_test_flag, "MT reg/unreg of dynamic optional VOL operations");

    AddTest("mt_fopen_fail", mt_test_file_open_failure_registration,
        NULL, NULL, &params, sizeof(mt_test_params), threaded_test_flag, "MT dynamic VOL loading on file open failure");

    AddTest("mt_lib_state_ops", mt_test_lib_state_ops,
        NULL, NULL, &params, sizeof(mt_test_params), threaded_test_flag, "MT usage of library state routines");

    AddTest("mt_vol_info", mt_test_vol_info,
        NULL, NULL, &params, sizeof(mt_test_params), threaded_test_flag, "MT usage of VOL info routines");

    /* These H5VL tests do their own threading internally - do not provide threading flag */
    AddTest("mt_reg_op", mt_test_registration_operation, NULL, mt_test_registration_operation_cleanup,
        &params, sizeof(mt_test_params), no_threaded_test_flag,  "MT reg/unreg of a connector and usage of its routines");

    AddTest("mt_prop_copy", mt_test_vol_property_copy, NULL, NULL,
        &params, sizeof(mt_test_params), no_threaded_test_flag, "MT VOL property copying");

    AddTest("mp_vol_wrp_ctx", mt_test_vol_wrap_ctx, NULL, mt_test_vol_wrap_ctx_cleanup,
        &params, sizeof(mt_test_params), no_threaded_test_flag, "MT usage of VOL wrap context routines");

    AddTest("mt_reg_search", mt_test_register_and_search, NULL, NULL,
        &params, sizeof(mt_test_params), no_threaded_test_flag, "MT reg/unreg of connectors while searching for connector");

    /* Misc MT tests */
    AddTest("mt_library_init", mt_test_library_init, NULL, NULL,
        &params, sizeof(mt_test_params), threaded_test_flag, "MT usage of H5open/H5close");

#else
    printf("Multi-threading is disabled.  Skipping multi-threaded tests.\n");
#endif /* H5_HAVE_MULTITHREAD */
    /* Display testing information */
    TestInfo(stdout);

    /* TODO: Refactor TestAlarmOn to accept specific timeout */
    TestAlarmOn();

    /* Parse command line arguments */
    TestParseCmdLine(argc, argv);

    /* Perform requested testing */
    PerformTests();

    /* Display test summary, if requested */
    if (GetTestSummary())
        TestSummary(stdout);

    /* TODO: Refactor TestAlarmOff to accept specific timeout */
    TestAlarmOff();

    num_errs_occurred = GetTestNumErrs();

    /* Release test infrastructure */
    TestShutdown();

    if (num_errs_occurred > 0) {
        exit(EXIT_FAILURE);
    } else {
        exit(EXIT_SUCCESS);
    }
}