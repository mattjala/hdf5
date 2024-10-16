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

/* Run the API tests within a single thread */
static void *run_h5_API_tests_thread(void *thread_info);



const char *test_path_prefix;

size_t active_thread_ct = 0;

/* X-macro to define the following for each test:
 * - enum type
 * - name
 * - test function
 * - enabled by default
 */
#ifdef H5_API_TEST_HAVE_ASYNC
#define H5_API_TESTS                                                                                         \
    X(H5_API_TEST_NULL, "", NULL, 0)                                                                         \
    X(H5_API_TEST_FILE, "file", H5_api_file_test, 1)                                                         \
    X(H5_API_TEST_GROUP, "group", H5_api_group_test, 1)                                                      \
    X(H5_API_TEST_DATASET, "dataset", H5_api_dataset_test, 1)                                                \
    X(H5_API_TEST_DATATYPE, "datatype", H5_api_datatype_test, 1)                                             \
    X(H5_API_TEST_ATTRIBUTE, "attribute", H5_api_attribute_test, 1)                                          \
    X(H5_API_TEST_LINK, "link", H5_api_link_test, 1)                                                         \
    X(H5_API_TEST_OBJECT, "object", H5_api_object_test, 1)                                                   \
    X(H5_API_TEST_MISC, "misc", H5_api_misc_test, 1)                                                         \
    X(H5_API_TEST_ASYNC, "async", H5_api_async_test, 1)                                                      \
    X(H5_API_TEST_MAX, "", NULL, 0)
#else
#define H5_API_TESTS                                                                                         \
    X(H5_API_TEST_NULL, "", NULL, 0)                                                                         \
    X(H5_API_TEST_FILE, "file", H5_api_file_test, 1)                                                         \
    X(H5_API_TEST_GROUP, "group", H5_api_group_test, 1)                                                      \
    X(H5_API_TEST_DATASET, "dataset", H5_api_dataset_test, 1)                                                \
    X(H5_API_TEST_DATATYPE, "datatype", H5_api_datatype_test, 1)                                             \
    X(H5_API_TEST_ATTRIBUTE, "attribute", H5_api_attribute_test, 1)                                          \
    X(H5_API_TEST_LINK, "link", H5_api_link_test, 1)                                                         \
    X(H5_API_TEST_OBJECT, "object", H5_api_object_test, 1)                                                   \
    X(H5_API_TEST_MISC, "misc", H5_api_misc_test, 1)                                                         \
    X(H5_API_TEST_MAX, "", NULL, 0)
#endif

#define X(a, b, c, d) a,
enum H5_api_test_type { H5_API_TESTS };
#undef X
#define X(a, b, c, d) b,
static const char *const H5_api_test_name[] = {H5_API_TESTS};
#undef X
#define X(a, b, c, d) c,
static int (*H5_api_test_func[])(void) = {H5_API_TESTS};
#undef X
#define X(a, b, c, d) d,
static int H5_api_test_enabled[] = {H5_API_TESTS};
#undef X

#define MAX_THREAD_ID_LEN 16

static enum H5_api_test_type
H5_api_test_name_to_type(const char *test_name)
{
    enum H5_api_test_type i = 0;

    while (strcmp(H5_api_test_name[i], test_name) && i != H5_API_TEST_MAX)
        i++;

    return ((i == H5_API_TEST_MAX) ? H5_API_TEST_NULL : i);
}

/******************************************************************************/
static void
H5_api_test_run(void)
{
    enum H5_api_test_type i;

    for (i = H5_API_TEST_FILE; i < H5_API_TEST_MAX; i++)
        if (H5_api_test_enabled[i])
            (void)H5_api_test_func[i]();
}


/* Run the API tests from a single thread. 
 * Returns: Pointer to the input thread_info_t structure.
 *         A result summary of the tests run can be found in threat_info_t->result
 */
void *
run_h5_API_tests_thread(void *thread_info)
{
    unsigned       seed;
    hid_t          fapl_id                   = H5I_INVALID_HID;
    hid_t          default_con_id            = H5I_INVALID_HID;
    hid_t          registered_con_id         = H5I_INVALID_HID;
    thread_info_t *tinfo                     = NULL;
    int chars_written;

    if (!thread_info) {
        fprintf(stderr, "Thread info is NULL\n");
        tinfo->result = API_TEST_ERROR;
        goto done;
    }

    tinfo = (thread_info_t *)thread_info;

#ifdef H5_HAVE_MULTITHREAD
    if (pthread_setspecific(thread_info_key_g, (void *)tinfo) != 0) {
        fprintf(stderr, "Error setting thread-specific data\n");
        tinfo->result = API_TEST_ERROR;
        goto done;
    }

#endif

    printf("%zu: Running API tests\n", tinfo->thread_idx);

    seed = (unsigned)HDtime(NULL);
    srand(seed);

    if ((test_path_prefix = HDgetenv(HDF5_API_TEST_PATH_PREFIX)) == NULL)
        test_path_prefix = (const char *)"";

#ifdef H5_HAVE_MULTITHREAD
    if (MAX_THREAD_ID_LEN + strlen(test_path_prefix) + strlen(TEST_FILE_NAME) >= H5_API_TEST_FILENAME_MAX_LENGTH) {
        fprintf(stderr, "Test file name exceeded expected size\n");
        tinfo->result = API_TEST_ERROR;
        goto done;
    }

    if (NULL == (tinfo->H5_api_test_filename = (char *)calloc(1, H5_API_TEST_FILENAME_MAX_LENGTH))) {
        fprintf(stderr, "Unable to allocate memory for test file name\n");
        tinfo->result = API_TEST_ERROR;
        goto done;
    }

    if ((chars_written = HDsnprintf(tinfo->H5_api_test_filename, H5_API_TEST_FILENAME_MAX_LENGTH, "%zu%s%s", tinfo->thread_idx, test_path_prefix,
               TEST_FILE_NAME)) < 0) {
        fprintf(stderr, "Error while creating test file name\n");
        tinfo->result = API_TEST_ERROR;
        goto done;
    }
#else
    if ((chars_written = HDsnprintf(H5_api_test_filename, H5_API_TEST_FILENAME_MAX_LENGTH, "%zu%s%s", tinfo->thread_idx, test_path_prefix,
               TEST_FILE_NAME)) < 0) {
        fprintf(stderr, "Error while creating test file name\n");
        tinfo->result = API_TEST_ERROR;
        goto done;
    }
#endif

    if (chars_written >= H5_API_TEST_FILENAME_MAX_LENGTH) {
        fprintf(stderr, "Test file name exceeded expected size\n");
        tinfo->result = API_TEST_ERROR;
        goto done;
    }


#ifndef H5_HAVE_MULTITHREAD
    n_tests_run_g = 0;
    n_tests_passed_g = 0;
    n_tests_failed_g = 0;
    n_tests_skipped_g = 0;
#endif

    printf("Running API tests with VOL connector '%s' and info string '%s'\n\n", tinfo->vol_connector_name,
           tinfo->vol_connector_info ? tinfo->vol_connector_info : "");
    printf("Test parameters:\n");
    printf("  - Test file name: '%s'\n", H5_API_TEST_FILENAME);
    printf("  - Test seed: %u\n", seed);
    printf("  - Test path prefix: '%s'\n", test_path_prefix);
    printf("\n\n");

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
        fprintf(stderr, "Unable to create FAPL\n");
        tinfo->result = API_TEST_ERROR;
        goto done;
    }

    /*
     * If using a VOL connector other than the native
     * connector, check whether the VOL connector was
     * successfully registered before running the tests.
     * Otherwise, HDF5 will default to running the tests
     * with the native connector, which could be misleading.
     */
    if (0 != HDstrcmp(tinfo->vol_connector_name, "native")) {
        htri_t is_registered;

        if ((is_registered = H5VLis_connector_registered_by_name(tinfo->vol_connector_name)) < 0) {
            fprintf(stderr, "Unable to determine if VOL connector is registered\n");
            tinfo->result = API_TEST_ERROR;
            goto done;
        }

        if (!is_registered) {
            fprintf(stderr, "Specified VOL connector '%s' wasn't correctly registered!\n",
                    tinfo->vol_connector_name);
            tinfo->result = API_TEST_ERROR;
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
                tinfo->result = API_TEST_ERROR;
                goto done;
            }

            if ((registered_con_id = H5VLget_connector_id_by_name(tinfo->vol_connector_name)) < 0) {
                fprintf(stderr, "Couldn't retrieve ID of registered VOL connector\n");
                tinfo->result = API_TEST_ERROR;
                goto done;
            }

            if (default_con_id != registered_con_id) {
                fprintf(stderr, "VOL connector set on default FAPL didn't match specified VOL connector\n");
                tinfo->result = API_TEST_ERROR;
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
        tinfo->result = API_TEST_ERROR;
        goto done;
    }

    /*
     * Create the file that will be used for all of the tests,
     * except for those which test file creation.
     */
    if (create_test_container(H5_API_TEST_FILENAME, vol_cap_flags_g) < 0) {
        fprintf(stderr, "Unable to create testing container file '%s'\n", H5_API_TEST_FILENAME);
        tinfo->result = API_TEST_ERROR;
        goto done;
    }

    /* Run all the tests that are enabled */
    H5_api_test_run();

    printf("Cleaning up testing files\n");

    H5Fdelete(H5_API_TEST_FILENAME, fapl_id);

#ifndef H5_HAVE_MULTITHREAD
    tinfo->n_tests_run_g = n_tests_run_g;
    tinfo->n_tests_passed_g = n_tests_passed_g;
    tinfo->n_tests_failed_g = n_tests_failed_g;
    tinfo->n_tests_skipped_g = n_tests_skipped_g;
#endif

    if (tinfo->n_tests_failed_g > 0) {
        tinfo->result = API_TEST_FAIL;
    }
done:
    if (tinfo && tinfo->H5_api_test_filename)
        free(tinfo->H5_api_test_filename);

    if (default_con_id >= 0 && H5VLclose(default_con_id) < 0) {
        fprintf(stderr, "Unable to close VOL connector ID\n");
        tinfo->result = API_TEST_ERROR;
    }

    if (registered_con_id >= 0 && H5VLclose(registered_con_id) < 0) {
        fprintf(stderr, "Unable to close VOL connector ID\n");
        tinfo->result = API_TEST_ERROR;
    }

    if (fapl_id >= 0 && H5Pclose(fapl_id) < 0) {
        fprintf(stderr, "Unable to close FAPL\n");
        tinfo->result = API_TEST_ERROR;
    }

#ifdef H5_HAVE_MULTITHREAD
    pthread_exit((void *)tinfo);
#else
    return (void *)tinfo;
#endif
}

int
main(int argc, char **argv)
{
    void* retval = NULL;
    int   ret_value = EXIT_SUCCESS;
    const char *vol_connector_name = NULL;
    char *vol_connector_name_copy = NULL;
    char *vol_connector_info = NULL;
#ifdef H5_HAVE_MULTITHREAD
#define MAX_THREADS 10
    pthread_t threads[MAX_THREADS];
    size_t n_tests_run = 0;
    size_t n_tests_passed = 0;
    size_t n_tests_failed = 0;
    size_t n_tests_skipped = 0;
#else
#define MAX_THREADS 1
    active_thread_ct = 1;
    n_tests_run_g     = 0;
    n_tests_passed_g  = 0;
    n_tests_failed_g  = 0;
    n_tests_skipped_g = 0;

#endif

    thread_info_t tinfo[MAX_THREADS];

    memset(tinfo, 0, sizeof(tinfo));

    /* Simple argument checking, TODO can improve that later */
    if (argc > 1) {
        enum H5_api_test_type i = H5_api_test_name_to_type(argv[1]);
        if (i != H5_API_TEST_NULL) {
            /* Run only specific API test */
            memset(H5_api_test_enabled, 0, sizeof(H5_api_test_enabled));
            H5_api_test_enabled[i] = 1;
        }
    }

    if (NULL == (vol_connector_name = HDgetenv(HDF5_VOL_CONNECTOR))) {
        printf("No VOL connector selected; using native VOL connector\n");
        vol_connector_name = "native";
        vol_connector_info = NULL;
    }
    else {
        char *token;

        if (NULL == (vol_connector_name_copy = HDstrdup(vol_connector_name))) {
            fprintf(stderr, "Unable to copy VOL connector string\n");
            ret_value = FAIL;
            goto done;
        }

        if (NULL == (token = HDstrtok(vol_connector_name_copy, " "))) {
            fprintf(stderr, "Error while parsing VOL connector string\n");
            ret_value = FAIL;
            goto done;
        }

        tinfo->vol_connector_name = token;

        if (NULL != (token = HDstrtok(NULL, " "))) {
            vol_connector_info = token;
        }
    }

#ifdef H5_HAVE_PARALLEL
    /* If HDF5 was built with parallel enabled, go ahead and call MPI_Init before
     * running these tests. Even though these are meant to be serial tests, they will
     * likely be run using mpirun (or similar) and we cannot necessarily expect HDF5 or
     * an HDF5 VOL connector to call MPI_Init.
     */
    MPI_Init(&argc, &argv);
#endif





#ifdef H5_HAVE_MULTITHREAD

    if (pthread_key_create(&thread_info_key_g, NULL) != 0) {
        fprintf(stderr, "Error creating thread-specific data key\n");
        ret_value = FAIL;
        goto done;
    }

    for (size_t nthreads = 1; nthreads <= MAX_THREADS; nthreads++) {
        H5open();

        /* Execute API tests in each thread */
        active_thread_ct = nthreads;
        printf("== Running API tests with %zu thread(s) ==\n", nthreads);
        for (size_t thread_idx = 0; thread_idx < nthreads; thread_idx++) {

            tinfo[thread_idx].thread_idx     = thread_idx;
            tinfo[thread_idx].result = API_TEST_PASS;
            tinfo[thread_idx].vol_connector_name = vol_connector_name;
            tinfo[thread_idx].vol_connector_info = vol_connector_info;
            tinfo[thread_idx].n_tests_run_g = 0;
            tinfo[thread_idx].n_tests_passed_g = 0;
            tinfo[thread_idx].n_tests_failed_g = 0;
            tinfo[thread_idx].n_tests_skipped_g = 0;

            if (pthread_create(&threads[thread_idx], NULL, run_h5_API_tests_thread,
                            (void *)&tinfo[thread_idx]) != 0) {
                fprintf(stderr, "Error creating thread %zu\n", thread_idx);
                ret_value = FAIL;
                goto done;
            }
        }

        /* Wait for threads to finish */
        for (size_t i = 0; i < nthreads; i++) {
            if (pthread_join(threads[i], (void *)&retval) != 0) {
                fprintf(stderr, "Error joining an API test thread\n");
                ret_value = FAIL;
                goto done;
            }

            if (!retval) {
                fprintf(stderr, "No return from an API tests thread\n");
                exit(EXIT_FAILURE);
            }

            if (((thread_info_t *)(retval))->result == API_TEST_ERROR) {
                fprintf(stderr, "An internal error occurred during API tests in thread %zu\n", ((thread_info_t*)(retval))->thread_idx);
            }
                            
            if (((thread_info_t *)(retval))->result == API_TEST_FAIL) {
                fprintf(stderr, "A failure occurred during API tests in thread %zu\n", ((thread_info_t*)(retval))->thread_idx);
                ret_value = FAIL;
            }

            if (((thread_info_t *)(retval))->result == API_TEST_PASS) {
                printf("API tests in thread %zu passed\n", ((thread_info_t*)(retval))->thread_idx);
            }
        }
    
        H5close();

        /* Aggregate and display results */
        for (size_t i = 0; i < nthreads; i++) {
            n_tests_run += tinfo[i].n_tests_run_g;
            n_tests_passed += tinfo[i].n_tests_passed_g;
            n_tests_failed += tinfo[i].n_tests_failed_g;
            n_tests_skipped += tinfo[i].n_tests_skipped_g;

            printf("[T%zu] %zu/%zu (%.2f%%) API tests passed with VOL connector '%s'\n", tinfo[i].thread_idx, tinfo[i].n_tests_passed_g, tinfo[i].n_tests_run_g,
                ((double) tinfo[i].n_tests_passed_g/ (double)tinfo[i].n_tests_run_g * 100.0), tinfo[i].vol_connector_name);
            printf("[T%zu] %zu/%zu (%.2f%%) API tests did not pass with VOL connector '%s'\n", tinfo[i].thread_idx,  tinfo[i].n_tests_failed_g,
                tinfo[i].n_tests_run_g, ((double)tinfo[i].n_tests_failed_g / (double)tinfo[i].n_tests_run_g * 100.0), tinfo[i].vol_connector_name);
            printf("[T%zu] %zu/%zu (%.2f%%) API tests were skipped with VOL connector '%s'\n\n", tinfo[i].thread_idx,  tinfo[i].n_tests_skipped_g,
                tinfo[i].n_tests_run_g, ((double)tinfo[i].n_tests_skipped_g / (double)tinfo[i].n_tests_run_g * 100.0),
                tinfo[i].vol_connector_name);
        }

        printf("[All threads] %zu/%zu (%.2f%%) API tests passed\n", n_tests_passed, n_tests_run,
               ((double)n_tests_passed / (double)n_tests_run * 100.0));
        printf("[All threads] %zu/%zu (%.2f%%) API tests did not pass\n", n_tests_failed,
               n_tests_run, ((double)n_tests_failed / (double)n_tests_run * 100.0));
        printf("[All threads] %zu/%zu (%.2f%%) API tests were skipped\n",  n_tests_skipped,
               n_tests_run, ((double)n_tests_skipped / (double)n_tests_run * 100.0));

        /* Reset stats */
        n_tests_run = 0;
        n_tests_passed = 0;
        n_tests_failed = 0;
        n_tests_skipped = 0;

    }

#else
    tinfo[0].thread_idx = 0;
    tinfo[0].result = API_TEST_PASS;

    if ((retval = run_h5_API_tests_thread((void*) &tinfo[0])) == NULL) {
        fprintf(stderr, "Error running API tests\n");
        ret_value = FAIL;
        goto done;
    }

    if (n_tests_run_g > 0) {
        printf("%zu/%zu (%.2f%%) API tests passed with VOL connector '%s'\n", n_tests_passed_g, n_tests_run_g,
               ((double)n_tests_passed_g / (double)n_tests_run_g * 100.0), tinfo[0].vol_connector_name);
        printf("%zu/%zu (%.2f%%) API tests did not pass with VOL connector '%s'\n", n_tests_failed_g,
               n_tests_run_g, ((double)n_tests_failed_g / (double)n_tests_run_g * 100.0), tinfo[0].vol_connector_name);
        printf("%zu/%zu (%.2f%%) API tests were skipped with VOL connector '%s'\n", n_tests_skipped_g,
               n_tests_run_g, ((double)n_tests_skipped_g / (double)n_tests_run_g * 100.0), tinfo[0].vol_connector_name);
    } else {
        printf("No API tests were run\n");
    }

#endif



done:
    free(vol_connector_name_copy);
    H5close();

#ifdef H5_HAVE_PARALLEL
    MPI_Finalize();
#endif

    exit(ret_value);
}
