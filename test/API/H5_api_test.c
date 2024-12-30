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
#endif

#define TEST_FILE_NAME "H5_api_test.h5"

/* Array of filenames used by the test. Will contain
 * only a single entry if running tests serially. If
 * running tests in a multi-threaded manner, will
 * contain a thread-local filename for each thread,
 * up to GetTestMaxNumThreads().
 */
char **H5_api_test_filenames_g     = NULL;
size_t H5_api_test_num_filenames_g = 0;

/* Base filename used by the test, which could include
 * a prefix specified by the HDF5_API_TEST_PATH_PREFIX
 * environment variable. If thread-local filenames need
 * to be created, an additional prefix will be added
 * between the main prefix (if any) and the basename()
 * of this filename.
 */
static char *H5_api_test_base_filename_g = NULL;

const char *test_path_prefix;

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
usage(FILE *stream)
{
    fprintf(stream, "file        run only the file interface tests\n");
    fprintf(stream, "group       run only the group interface tests\n");
    fprintf(stream, "dataset     run only the dataset interface tests\n");
    fprintf(stream, "attribute   run only the attribute interface tests\n");
    fprintf(stream, "datatype    run only the datatype interface tests\n");
    fprintf(stream, "link        run only the link interface tests\n");
    fprintf(stream, "object      run only the object interface tests\n");
    fprintf(stream, "misc        run only the miscellaneous tests\n");
    fprintf(stream, "async       run only the async interface tests\n");
}

static herr_t
H5_api_test_setup_container_names(const char *prefix, const char *filename)
{
    herr_t ret_value = SUCCEED;

    /* Populate base test filename */
    if (NULL == (H5_api_test_base_filename_g = malloc(H5_API_TEST_FILENAME_MAX_LENGTH))) {
        TestErrPrintf("Couldn't allocate space for base testing file name\n");
        goto done;
    }

    if (HDsnprintf(H5_api_test_base_filename_g, H5_API_TEST_FILENAME_MAX_LENGTH,
                   "%s%s", prefix, filename) < 0) {
        TestErrPrintf("Error while creating test file name\n");
        goto done;
    }

    if (H5_API_TEST_EXECUTION_THREADED) {
        int max_num_threads = GetTestMaxNumThreads();

        if (NULL == (H5_api_test_filenames_g = calloc((size_t)max_num_threads, sizeof(char *)))) {
            TestErrPrintf("Couldn't allocate space for file names\n");
            goto done;
        }

        /* Create a unique filename for up to GetTestMaxNumThreads()
         * threads. Note that this assumes the testing framework will
         * assign thread IDs serially.
         */
        for (int i = 0; i < max_num_threads; i++) {
            if (NULL == (H5_api_test_filenames_g[i] = malloc(H5_API_TEST_FILENAME_MAX_LENGTH))) {
                TestErrPrintf("Couldn't allocate space for thread-local file name %d\n", i);
                goto done;
            }

            if (HDsnprintf(H5_api_test_filenames_g[i], H5_API_TEST_FILENAME_MAX_LENGTH,
                           "%sThread%d%s", prefix, i, filename) < 0) {
                TestErrPrintf("Error while creating thread-local test file name %d\n", i);
                goto done;
            }
        }

        H5_api_test_num_filenames_g = (size_t)max_num_threads;
    }
    else {
        H5_api_test_filenames_g     = &H5_api_test_base_filename_g;
        H5_api_test_num_filenames_g = 1;
    }

done:
    if (ret_value < 0) {
        if (H5_api_test_filenames_g) {
            for (int i = 0; i < GetTestMaxNumThreads(); i++)
                free(H5_api_test_filenames_g[i]);

            free(H5_api_test_filenames_g);
            H5_api_test_filenames_g = NULL;
        }

        free(H5_api_test_base_filename_g);
        H5_api_test_base_filename_g = NULL;
    }

    return ret_value;
}

int
main(int argc, char **argv)
{
    const char *vol_connector_string;
    const char *vol_connector_name;
    unsigned    seed;
    hid_t       fapl_id                   = H5I_INVALID_HID;
    hid_t       default_con_id            = H5I_INVALID_HID;
    hid_t       registered_con_id         = H5I_INVALID_HID;
    char       *vol_connector_string_copy = NULL;
    char       *vol_connector_info        = NULL;

    H5open();

    /* Initialize testing framework */
    if (TestInit(argv[0], usage, NULL, NULL, NULL, H5_MULTITHREAD_TEST, 0) < 0) {
        TestErrPrintf("Unable to initialize testing framework\n");
        goto done;
    }

    /* Parse command line separately from the test framework since
     * tests need to be added before TestParseCmdLine in order for
     * the -help option to show them, but we need to know ahead of
     * time which tests to add if only a specific interface's tests
     * are going to be run.
     */
    parse_command_line(argc, argv);

    /* Add tests */
    H5_api_test_add();

    /* Display testing information */
    TestInfo(stdout);

    /* Parse command line arguments */
    if (TestParseCmdLine(argc, argv) < 0) {
        TestErrPrintf("Unable to parse command-line arguments\n");
        goto done;
    }

    seed = (unsigned)HDtime(NULL);
    srand(seed);

    if (NULL == (test_path_prefix = getenv(HDF5_API_TEST_PATH_PREFIX)))
        test_path_prefix = "";

    if (H5_api_test_setup_container_names(test_path_prefix, TEST_FILE_NAME) < 0) {
        TestErrPrintf("Unable to setup testing container file names\n");
        goto done;
    }

#ifndef H5_HAVE_MULTITHREAD
    if (H5_API_TEST_EXECUTION_THREADED) {
        TestErrPrintf("HDF5 must be built with multi-thread support to run threaded API tests\n");
        goto done;
    }
#endif

    if (NULL == (vol_connector_string = getenv(HDF5_VOL_CONNECTOR))) {
        printf("No VOL connector selected; using native VOL connector\n");
        vol_connector_name = "native";
        vol_connector_info = NULL;
    }
    else {
        char *token;

        if (NULL == (vol_connector_string_copy = HDstrdup(vol_connector_string))) {
            TestErrPrintf("Unable to copy VOL connector string\n");
            goto done;
        }

        if (NULL == (token = strtok(vol_connector_string_copy, " "))) {
            TestErrPrintf("Error while parsing VOL connector string\n");
            goto done;
        }

        vol_connector_name = token;

        if (NULL != (token = strtok(NULL, " "))) {
            vol_connector_info = token;
        }
    }

    printf("Running API tests with VOL connector '%s' and info string '%s'\n\n", vol_connector_name,
           vol_connector_info ? vol_connector_info : "");
    printf("Test parameters:\n");
    printf("  - Base test file name: '%s'\n", H5_api_test_base_filename_g);
    printf("  - Test seed: %u\n", seed);
    printf("\n");

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
        TestErrPrintf("Unable to create FAPL\n");
        goto done;
    }

    /*
     * If using a VOL connector other than the native
     * connector, check whether the VOL connector was
     * successfully registered before running the tests.
     * Otherwise, HDF5 will default to running the tests
     * with the native connector, which could be misleading.
     */
    if (0 != strcmp(vol_connector_name, "native")) {
        htri_t is_registered;

        if ((is_registered = H5VLis_connector_registered_by_name(vol_connector_name)) < 0) {
            TestErrPrintf("Unable to determine if VOL connector is registered\n");
            goto done;
        }

        if (!is_registered) {
            TestErrPrintf("Specified VOL connector '%s' wasn't correctly registered!\n",
                          vol_connector_name);
            goto done;
        }
        else {
            /*
             * If the connector was successfully registered, check that
             * the connector ID set on the default FAPL matches the ID
             * for the registered connector before running the tests.
             */
            if (H5Pget_vol_id(fapl_id, &default_con_id) < 0) {
                TestErrPrintf("Couldn't retrieve ID of VOL connector set on default FAPL\n");
                goto done;
            }

            if ((registered_con_id = H5VLget_connector_id_by_name(vol_connector_name)) < 0) {
                TestErrPrintf("Couldn't retrieve ID of registered VOL connector\n");
                goto done;
            }

            if (default_con_id != registered_con_id) {
                TestErrPrintf("VOL connector set on default FAPL didn't match specified VOL connector\n");
                goto done;
            }
        }
    }

    /* Retrieve the VOL cap flags - work around an HDF5
     * library issue by creating a FAPL
     */
    vol_cap_flags_g = H5VL_CAP_FLAG_NONE;
    if (H5Pget_vol_cap_flags(fapl_id, &vol_cap_flags_g) < 0) {
        TestErrPrintf("Unable to retrieve VOL connector capability flags\n");
        goto done;
    }

    /* Create the file(s) that will be used for all of the tests,
     * except for those which test file creation.
     */
    if (H5_api_test_create_containers(H5_api_test_filenames_g, H5_api_test_num_filenames_g,
                                      vol_cap_flags_g) < 0) {
        TestErrPrintf("Unable to create testing container files\n");
        goto done;
    }

    /* Perform tests */
    PerformTests();

    printf("\n");

    /* Display test summary, if requested */
    if (GetTestSummary())
        TestSummary(stdout);

    if (GetTestCleanup()) {
        printf("Deleting container file(s) for tests\n\n");

        if (H5_api_test_destroy_container_files(H5_api_test_filenames_g, H5_api_test_num_filenames_g, H5P_DEFAULT) < 0) {
            TestErrPrintf("Error cleaning up testing container files\n");
            goto done;
        }
    }

done:
    free(vol_connector_string_copy);

    if (H5_API_TEST_EXECUTION_THREADED && H5_api_test_filenames_g) {
        for (int i = 0; i < GetTestMaxNumThreads(); i++)
            free(H5_api_test_filenames_g[i]);

        free(H5_api_test_filenames_g);
    }

    free(H5_api_test_base_filename_g);

    if (default_con_id >= 0 && H5VLclose(default_con_id) < 0)
        TestErrPrintf("Unable to close VOL connector ID\n");

    if (registered_con_id >= 0 && H5VLclose(registered_con_id) < 0)
        TestErrPrintf("Unable to close VOL connector ID\n");

    if (fapl_id >= 0 && H5Pclose(fapl_id) < 0)
        TestErrPrintf("Unable to close FAPL\n");

    /* Release test infrastructure */
    if (TestShutdown() < 0)
        TestErrPrintf("Unable to shut down testing framework\n");

    H5close();

    /* Exit failure if errors encountered; else exit success. */
    if (GetTestNumErrs() > 0 || GetTestsFailedCount() > 0)
        exit(EXIT_FAILURE);
    else
        exit(EXIT_SUCCESS);
}
