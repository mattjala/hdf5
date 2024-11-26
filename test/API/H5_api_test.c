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

char H5_api_test_filename_g[H5_TEST_FILENAME_MAX_LENGTH];

static int H5_api_test_create_containers(const char *filename, uint64_t vol_cap_flags);
static int H5_api_test_create_single_container(const char *filename, uint64_t vol_cap_flags);
static int H5_api_test_destroy_container_files(void);

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

int
main(int argc, char **argv)
{
    H5E_auto2_t default_err_func;
    const char *vol_connector_string;
    const char *vol_connector_name;
    unsigned    seed;
    hid_t       fapl_id                   = H5I_INVALID_HID;
    hid_t       default_con_id            = H5I_INVALID_HID;
    hid_t       registered_con_id         = H5I_INVALID_HID;
    char       *vol_connector_string_copy = NULL;
    char       *vol_connector_info        = NULL;
    void       *default_err_data          = NULL;
    bool        err_occurred              = false;
    int         chars_written             = 0;

    H5open();

    /* Store current error stack printing function since TestInit unsets it */
    H5Eget_auto2(H5E_DEFAULT, &default_err_func, &default_err_data);

    /* Initialize testing framework */
    if (TestInit(argv[0], usage, NULL, NULL, NULL, 0) < 0) {
        fprintf(stderr, "Unable to initialize testing framework\n");
        err_occurred = true;
        goto done;
    }

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

    /* Add tests */
    H5_api_test_add();

    /* Display testing information */
    TestInfo(stdout);

    /* Parse command line arguments */
    if (TestParseCmdLine(argc, argv) < 0) {
        fprintf(stderr, "Unable to parse command-line arguments\n");
        err_occurred = true;
        goto done;
    }

    n_tests_run_g     = 0;
    n_tests_passed_g  = 0;
    n_tests_failed_g  = 0;
    n_tests_skipped_g = 0;

    seed = (unsigned)HDtime(NULL);
    srand(seed);

    if (NULL == (test_path_prefix = getenv(HDF5_API_TEST_PATH_PREFIX)))
        test_path_prefix = "";

#ifndef H5_HAVE_MULTITHREAD
    if (TEST_EXECUTION_THREADED) {
        fprintf(stderr, "HDF5 must be built with multi-thread support to run threaded API tests\n");
        err_occurred = TRUE;
        goto done;
    }
#endif

    if (!TEST_EXECUTION_THREADED) {
        /* Populate global test filename */
        if ((chars_written = HDsnprintf(H5_api_test_filename_g, H5_TEST_FILENAME_MAX_LENGTH, "%s%s",test_path_prefix,
                TEST_FILE_NAME)) < 0) {
            fprintf(stderr, "Error while creating test file name\n");
            err_occurred = TRUE;
            goto done;
        }

        if ((size_t)chars_written >= H5_TEST_FILENAME_MAX_LENGTH) {
            fprintf(stderr, "Test file name exceeded expected size\n");
            err_occurred = TRUE;
            goto done;
        }
    }

    if (NULL == (vol_connector_string = getenv(HDF5_VOL_CONNECTOR))) {
        printf("No VOL connector selected; using native VOL connector\n");
        vol_connector_name = "native";
        vol_connector_info = NULL;
    }
    else {
        char *token;

        if (NULL == (vol_connector_string_copy = HDstrdup(vol_connector_string))) {
            fprintf(stderr, "Unable to copy VOL connector string\n");
            err_occurred = true;
            goto done;
        }

        if (NULL == (token = strtok(vol_connector_string_copy, " "))) {
            fprintf(stderr, "Error while parsing VOL connector string\n");
            err_occurred = true;
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
    printf("  - Test file name: '%s'\n", TEST_FILE_NAME);
    printf("  - Test seed: %u\n", seed);
    printf("\n");

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
        fprintf(stderr, "Unable to create FAPL\n");
        err_occurred = true;
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
            fprintf(stderr, "Unable to determine if VOL connector is registered\n");
            err_occurred = true;
            goto done;
        }

        if (!is_registered) {
            fprintf(stderr, "Specified VOL connector '%s' wasn't correctly registered!\n",
                    vol_connector_name);
            err_occurred = true;
            goto done;
        }
        else {
            /*
             * If the connector was successfully registered, check that
             * the connector ID set on the default FAPL matches the ID
             * for the registered connector before running the tests.
             */
            if (H5Pget_vol_id(fapl_id, &default_con_id) < 0) {
                fprintf(stderr, "Couldn't retrieve ID of VOL connector set on default FAPL\n");
                err_occurred = true;
                goto done;
            }

            if ((registered_con_id = H5VLget_connector_id_by_name(vol_connector_name)) < 0) {
                fprintf(stderr, "Couldn't retrieve ID of registered VOL connector\n");
                err_occurred = true;
                goto done;
            }

            if (default_con_id != registered_con_id) {
                fprintf(stderr, "VOL connector set on default FAPL didn't match specified VOL connector\n");
                err_occurred = true;
                goto done;
            }
        }
    }

    /* Retrieve the VOL cap flags - work around an HDF5
     * library issue by creating a FAPL
     */
    vol_cap_flags_g = H5VL_CAP_FLAG_NONE;
    if (H5Pget_vol_cap_flags(fapl_id, &vol_cap_flags_g) < 0) {
        fprintf(stderr, "Unable to retrieve VOL connector capability flags\n");
        err_occurred = true;
        goto done;
    }

    /* Create the file(s) that will be used for all of the tests,
     * except for those which test file creation.*/
    if (H5_api_test_create_containers(TEST_FILE_NAME, vol_cap_flags_g) < 0) {
        fprintf(stderr, "Unable to create testing container file with basename '%s'\n", TEST_FILE_NAME);
        err_occurred = true;
        goto done;
    }

    /* Perform tests */
    PerformTests();

    printf("\n");

    /* Display test summary, if requested */
    if (GetTestSummary())
        TestSummary(stdout);

    printf("Deleting container file(s) for tests\n\n");

    if (GetTestCleanup()) {
        if (H5_api_test_destroy_container_files() < 0) {
            fprintf(stderr, "Error cleaning up global API test info\n");
            err_occurred = true;
            goto done;
        }
    }

    if (n_tests_run_g > 0) {
        printf("%zu/%zu (%.2f%%) API tests passed with VOL connector '%s'\n", n_tests_passed_g, n_tests_run_g,
               ((double)n_tests_passed_g / (double)n_tests_run_g * 100.0), vol_connector_name);
        printf("%zu/%zu (%.2f%%) API tests did not pass with VOL connector '%s'\n", n_tests_failed_g,
               n_tests_run_g, ((double)n_tests_failed_g / (double)n_tests_run_g * 100.0), vol_connector_name);
        printf("%zu/%zu (%.2f%%) API tests were skipped with VOL connector '%s'\n", n_tests_skipped_g,
               n_tests_run_g, ((double)n_tests_skipped_g / (double)n_tests_run_g * 100.0),
               vol_connector_name);
    }

done:
    free(vol_connector_string_copy);

    if (default_con_id >= 0 && H5VLclose(default_con_id) < 0) {
        fprintf(stderr, "Unable to close VOL connector ID\n");
        err_occurred = true;
    }

    if (registered_con_id >= 0 && H5VLclose(registered_con_id) < 0) {
        fprintf(stderr, "Unable to close VOL connector ID\n");
        err_occurred = true;
    }

    if (fapl_id >= 0 && H5Pclose(fapl_id) < 0) {
        fprintf(stderr, "Unable to close FAPL\n");
        err_occurred = true;
    }

    if (GetTestNumErrs() > 0)
        n_tests_failed_g += (size_t)GetTestNumErrs();

    /* Release test infrastructure */
    TestShutdown();

    H5close();

    /* Exit failure if errors encountered; else exit success. */
    if (err_occurred || n_tests_failed_g > 0)
        exit(EXIT_FAILURE);
    else
        exit(EXIT_SUCCESS);
}

/* Create the API container test file(s), one per thread.
 * Returns negative on failure, 0 on success */
static int
H5_api_test_create_containers(const char *filename, uint64_t vol_cap_flags)
{
    char *tl_filename = NULL;

    if (!(vol_cap_flags & H5VL_CAP_FLAG_FILE_BASIC)) {
        printf("   VOL connector doesn't support file creation\n");
        goto error;
    }

    if (TEST_EXECUTION_THREADED) {
#ifdef H5_HAVE_MULTITHREAD
        for (int i = 0; i < GetTestMaxNumThreads(); i++) {
            if ((tl_filename = generate_threadlocal_filename(test_path_prefix, i, filename)) == NULL) {
                printf("    failed to generate thread-local API test filename\n");
                goto error;
            }

            if (H5_api_test_create_single_container((const char *)tl_filename, vol_cap_flags) < 0) {
                printf("    failed to create thread-local API test container");
                goto error;
            }

            free(tl_filename);
            tl_filename = NULL;
        }
#else
        printf("    thread-specific filename requested, but multithread support not enabled\n");
        goto error;
#endif

    } else {
        if (H5_api_test_create_single_container((const char *)filename, vol_cap_flags) < 0) {
            printf("    failed to create test container\n");
            goto error;
        }
    }

    return 0;

error:
    free(tl_filename);
    return -1;
}

/* Helper for H5_api_test_create_containers().
 * Returns negative on failure, 0 on success */
static int
H5_api_test_create_single_container(const char *filename, uint64_t vol_cap_flags) {
    hid_t file_id  = H5I_INVALID_HID;
    hid_t group_id = H5I_INVALID_HID;

    if ((file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        printf("    couldn't create testing container file '%s'\n", filename);
        goto error;
    }

    printf("    created container file\n");

    if (vol_cap_flags & H5VL_CAP_FLAG_GROUP_BASIC) {
        /* Create container groups for each of the test interfaces
         * (group, attribute, dataset, etc.).
         */
        if ((group_id = H5Gcreate2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >=
            0) {
            H5Gclose(group_id);
        }

        if ((group_id = H5Gcreate2(file_id, ATTRIBUTE_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                                   H5P_DEFAULT)) >= 0) {
            H5Gclose(group_id);
        }

        if ((group_id =
                 H5Gcreate2(file_id, DATASET_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >= 0) {
            H5Gclose(group_id);
        }

        if ((group_id =
                 H5Gcreate2(file_id, DATATYPE_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >= 0) {
            H5Gclose(group_id);
        }

        if ((group_id = H5Gcreate2(file_id, LINK_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >=
            0) {
            H5Gclose(group_id);
        }

        if ((group_id = H5Gcreate2(file_id, OBJECT_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) >=
            0) {
            H5Gclose(group_id);
        }

        if ((group_id = H5Gcreate2(file_id, MISCELLANEOUS_TEST_GROUP_NAME, H5P_DEFAULT, H5P_DEFAULT,
                                   H5P_DEFAULT)) >= 0) {
            H5Gclose(group_id);
        }
    }

    if (H5Fclose(file_id) < 0) {
        printf("    failed to close testing container %s\n", filename);
        goto error;
    }

    return 0;
error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return -1;

}

/* Delete the API test container file(s).
 * Returns negative on failure, 0 on success */
static int
H5_api_test_destroy_container_files(void) {

    char *filename = NULL;

    if (!(vol_cap_flags_g & H5VL_CAP_FLAG_FILE_BASIC)) {
        printf("   container should not have been created\n");
        goto error;
    }

    if (TEST_EXECUTION_THREADED) {
#ifndef H5_HAVE_MULTITHREAD
        printf("    thread-specific cleanup requested, but multithread support not enabled\n");
        goto error;
#endif
        
        for (int i = 0; i < GetTestMaxNumThreads(); i++) {
            if ((filename = generate_threadlocal_filename(test_path_prefix, i, TEST_FILE_NAME)) == NULL) {
                printf("    failed to generate thread-local API test filename\n");
                goto error;
            }

            H5E_BEGIN_TRY {
                if (H5Fis_accessible(filename, H5P_DEFAULT) > 0) {
                    if (H5Fdelete(filename, H5P_DEFAULT) < 0) {
                        printf("    failed to destroy thread-local API test container");
                        goto error;
                    }
                }
            }
            H5E_END_TRY

            free(filename);
            filename = NULL;
        }
    } else {
        H5E_BEGIN_TRY {
            
            if (prefix_filename(test_path_prefix, TEST_FILE_NAME, &filename) < 0) {
                printf("    failed to prefix filename\n");
                goto error;
            }

            if (H5Fis_accessible(filename, H5P_DEFAULT) > 0) {
                if (H5Fdelete(filename, H5P_DEFAULT) < 0) {
                    printf("    failed to destroy thread-local API test container");
                    goto error;
                }
            }
        }
        H5E_END_TRY
    }

    free(filename);
    filename = NULL;
    return 0;

error:
    free(filename);
    filename = NULL;
    return -1;
}
