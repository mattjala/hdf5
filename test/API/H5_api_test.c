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
 * A test suite which only makes public HDF5 API calls and which is meant
 * to test the native VOL connector or a specified HDF5 VOL connector (or
 * set of connectors stacked with each other). This test suite must assume
 * that a VOL connector could only implement the File interface. Therefore,
 * the suite should check that a particular piece of functionality is supported
 * by the VOL connector before actually testing it. If the functionality is
 * not supported, the test should simply be skipped, perhaps with a note as
 * to why the test was skipped, if possible.
 *
 * If the VOL connector being used supports the creation of groups, this
 * test suite will attempt to organize the output of these various tests
 * into groups based on their respective HDF5 interface.
 */

#include "H5_api_test.h"

#include "H5_api_attribute_test.h"
#include "H5_api_dataset_test.h"
#include "H5_api_datatype_test.h"
#include "H5_api_file_test.h"
#include "H5_api_group_test.h"
#include "H5_api_link_test.h"
#include "H5_api_misc_test.h"
#include "H5_api_object_test.h"
#include "H5_api_test_util.h"
#ifdef H5_API_TEST_HAVE_ASYNC
#include "H5_api_async_test.h"
#endif

#ifdef H5_HAVE_MULTITHREAD
#include <pthread.h>
#else
char H5_api_test_filename[H5_API_TEST_FILENAME_MAX_LENGTH];
#endif

const char *test_path_prefix = NULL;

size_t active_thread_ct = 0;

/* Margin of runtime for each subtest allocated to cleanup */
#define API_TEST_MARGIN 1

/* X-macro to define the following for each test:
 * - enum type
 * - name
 * - test function
 * - enabled by default
 */
#ifdef H5_API_TEST_HAVE_ASYNC
#define H5_API_TESTS                                                                                         \
    X(H5_API_TEST_NULL, "", NULL, 0)                                                                         \
    X(H5_API_TEST_FILE, "file", H5_api_file_test_add, 1)                                                     \
    X(H5_API_TEST_GROUP, "group", H5_api_group_test_add, 1)                                                  \
    X(H5_API_TEST_DATASET, "dataset", H5_api_dataset_test_add, 1)                                            \
    X(H5_API_TEST_DATATYPE, "datatype", H5_api_datatype_test_add, 1)                                         \
    X(H5_API_TEST_ATTRIBUTE, "attribute", H5_api_attribute_test_add, 1)                                      \
    X(H5_API_TEST_LINK, "link", H5_api_link_test_add, 1)                                                     \
    X(H5_API_TEST_OBJECT, "object", H5_api_object_test_add, 1)                                               \
    X(H5_API_TEST_MISC, "misc", H5_api_misc_test_add, 1)                                                     \
    X(H5_API_TEST_ASYNC, "async", H5_api_async_test_add, 1)                                                  \
    X(H5_API_TEST_MAX, "", NULL, 0)
#else
#define H5_API_TESTS                                                                                         \
    X(H5_API_TEST_NULL, "", NULL, 0)                                                                         \
    X(H5_API_TEST_FILE, "file", H5_api_file_test_add, 1)                                                     \
    X(H5_API_TEST_GROUP, "group", H5_api_group_test_add, 1)                                                  \
    X(H5_API_TEST_DATASET, "dataset", H5_api_dataset_test_add, 1)                                            \
    X(H5_API_TEST_DATATYPE, "datatype", H5_api_datatype_test_add, 1)                                         \
    X(H5_API_TEST_ATTRIBUTE, "attribute", H5_api_attribute_test_add, 1)                                      \
    X(H5_API_TEST_LINK, "link", H5_api_link_test_add, 1)                                                     \
    X(H5_API_TEST_OBJECT, "object", H5_api_object_test_add, 1)                                               \
    X(H5_API_TEST_MISC, "misc", H5_api_misc_test_add, 1)                                                     \
    X(H5_API_TEST_MAX, "", NULL, 0)
#endif

#define X(a, b, c, d) a,
enum H5_api_test_type { H5_API_TESTS };
#undef X
#define X(a, b, c, d) b,
static const char *const H5_api_test_name[] = {H5_API_TESTS};
#undef X
#define X(a, b, c, d) c,
static void (*H5_api_test_add_func[])(void) = {H5_API_TESTS};
#undef X
#define X(a, b, c, d) d,
static int H5_api_test_enabled[] = {H5_API_TESTS};
#undef X

static enum H5_api_test_type
H5_api_test_name_to_type(const char *test_name)
{
    enum H5_api_test_type i = 0;

    while (strcmp(H5_api_test_name[i], test_name) && i != H5_API_TEST_MAX)
        i++;

    return ((i == H5_API_TEST_MAX) ? H5_API_TEST_NULL : i);
}

static void
H5_api_test_add(void)
{
    enum H5_api_test_type i;

    for (i = H5_API_TEST_FILE; i < H5_API_TEST_MAX; i++)
        if (H5_api_test_enabled[i])
            H5_api_test_add_func[i]();
}

static int
parse_command_line(int argc, char **argv)
{
    /* Simple argument checking, TODO can improve that later */
    if (argc > 1) {
        enum H5_api_test_type i = H5_api_test_name_to_type(argv[argc - 1]);
        if (i != H5_API_TEST_NULL) {
            /* Run only specific API test */
            memset(H5_api_test_enabled, 0, sizeof(H5_api_test_enabled));
            H5_api_test_enabled[i] = 1;
        }
    }

    return 0;
}

static void
usage(void)
{
    print_func("file        run only the file interface tests\n");
    print_func("group       run only the group interface tests\n");
    print_func("dataset     run only the dataset interface tests\n");
    print_func("attribute   run only the attribute interface tests\n");
    print_func("datatype    run only the datatype interface tests\n");
    print_func("link        run only the link interface tests\n");
    print_func("object      run only the object interface tests\n");
    print_func("misc        run only the miscellaneous tests\n");
    print_func("async       run only the async interface tests\n");
}


int
main(int argc, char **argv)
{
    H5E_auto2_t default_err_func;
    void       *default_err_data          = NULL;
    bool        err_occurred              = false;

    int testExpress = 0;

    H5open();

    /* Store current error stack printing function since TestInit unsets it */
    H5Eget_auto2(H5E_DEFAULT, &default_err_func, &default_err_data);
    /* Initialize testing framework */
    TestInit(argv[0], usage, NULL);
    /* Reset error stack printing function */
    H5Eset_auto2(H5E_DEFAULT, default_err_func, default_err_data);

    /* Hide all output from testing framework and replace with our own */
    SetTestVerbosity(VERBO_NONE);

    /* Parse command line separately from the test framework since
     * tests need to be added before TestParseCmdLine in order for
     * the -help option to show them, but we need to know ahead of
     * time which tests to add if only a specific interface's tests
     * are going to be run.
     */
    parse_command_line(argc, argv);
    
    testExpress = GetTestExpress();

    // TODO
    UNUSED(testExpress);

    /* Parse command line arguments */
    TestParseCmdLine(argc, argv);

#ifndef H5_HAVE_MULTITHREAD
    if (GetTestMaxNumThreads() > 1) {
        fprintf(stderr, "HDF5 must be built with multi-thread support to run multi-threaded API tests\n");
        exit(EXIT_FAILURE);
    }
#endif

    if (GetTestMaxNumThreads() <= 0)
        SetTestMaxNumThreads(API_TESTS_DEFAULT_NUM_THREADS);

    /* API Test Specific Setup */
    if (H5_api_test_global_setup() < 0) {
        fprintf(stderr, "Error setting up global API test info\n");
        return EXIT_FAILURE;
    }

    /* Display VOL information */
    if (H5_api_test_display_information() < 0) {
        fprintf(stderr, "Error displaying VOL information\n");
        return EXIT_FAILURE;
    }

    /* Display generic testing information */
    TestInfo(argv[0]);

    /* TODO: Refactor TestAlarmOn to accept specific timeout */
    TestAlarmOn();

    /*
     * If using a VOL connector other than the native
     * connector, check whether the VOL connector was
     * successfully registered before running the tests.
     * Otherwise, HDF5 will default to running the tests
     * with the native connector, which could be misleading.
     */
    if (H5_api_check_vol_registration() < 0) {
        fprintf(stderr, "Active VOL connector not properly registered\n");
        return EXIT_FAILURE;
    }

    H5_api_test_add();

    /* Perform requested testing */
    PerformTests();

    /* Display test summary, if requested */
    if (GetTestSummary())
        TestSummary();
    
    /* Clean up test files, if allowed */
    if (GetTestCleanup() && !HDgetenv(HDF5_NOCLEANUP))
        TestCleanup();

    printf("Deleting container file for tests\n\n");

    if (H5_api_test_destroy_container_files() < 0) {
        fprintf(stderr, "Error cleaning up global API test info\n");
        err_occurred = true;
        goto done;
    }

    if (n_tests_run_g > 0)
        H5_api_test_display_results();

done:
    TestAlarmOff();

    if (GetTestNumErrs() > 0)
        n_tests_failed_g += GetTestNumErrs();

    /* Release test infrastructure */
    TestShutdown();

    H5close();

    if (err_occurred || n_tests_failed_g > 0) {
        exit(EXIT_FAILURE);
    } else {
        exit(EXIT_SUCCESS);
    }
}