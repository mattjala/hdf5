#include "h5test.h"
#include "H5VLpassthru.h"
#include "H5VLpassthru.c"
#include "null_vol_connector.h"
#include <pthread.h>

#define MAX_THREADS        32
#define NUM_ITERS          100
#define REGISTRATION_COUNT 1
#define TEST_MSG_SIZE      256
#define MT_DUMMY_FILE_NAME "mt_dummy_file.h5"

#define SUBCLS_NAME_SIZE        100
#define NUM_VALID_SUBCLASSES    8
#define OPERATIONS_PER_SUBCLASS 5

typedef void *(*mt_vl_test_cb)(void *arg);

herr_t setup_tests(void);
herr_t cleanup_tests(void);

void *test_concurrent_registration(void *arg);
void *test_concurrent_registration_by_name(void *arg);
void *test_concurrent_registration_by_value(void *args);
void *test_concurrent_registration_operation(void *arg);
void *test_concurrent_dyn_op_registration(void *arg);

H5VL_subclass_t select_valid_vol_subclass(size_t index);

mt_vl_test_cb tests[5] = {
    test_concurrent_registration,          test_concurrent_registration_by_name,
    test_concurrent_dyn_op_registration,   test_concurrent_registration_operation,
    test_concurrent_registration_by_value,
    // test_file_open_failure_registration(); // Requires VOL to be loaded as plugin
    // test_threadsafe_vol(); // Requires MT VOL that acquires mutex
};

int
main(void)
{
    size_t    num_threads;
    pthread_t threads[MAX_THREADS];

    void *thread_return = NULL;
#ifndef H5_HAVE_MULTITHREAD
    SKIPPED();
    return 0;
#endif

    if (setup_tests() < 0) {
        printf("Failed to set up multi-thread VL tests\n");
        return -1;
    }

    for (num_threads = 1; num_threads < MAX_THREADS; num_threads++) {
        printf("Testing with %zu threads\n", num_threads);

        for (size_t test_idx = 0; test_idx < sizeof(tests) / sizeof(tests[0]); test_idx++) {
            mt_vl_test_cb test_func = tests[test_idx];

            for (size_t i = 0; i < num_threads; i++) {
                pthread_create(&threads[i], NULL, test_func, (void *)NULL);
            }

            for (size_t i = 0; i < num_threads; i++) {
                pthread_join(threads[i], &thread_return);
            }

            memset(threads, 0, sizeof(threads));
        }
    }

    if (cleanup_tests() < 0) {
        printf("Failed to clean up multi-thread VL tests\n");
        return -1;
    }

    printf("%zu/%zu tests passed (%.2f%%)\n", n_tests_passed_g, n_tests_run_g,
           (double)n_tests_passed_g / (double)n_tests_run_g * 100.0);
    printf("%zu/%zu tests failed (%.2f%%)\n", n_tests_failed_g, n_tests_run_g,
           (double)n_tests_failed_g / (double)n_tests_run_g * 100.0);
    printf("%zu/%zu tests skipped (%.2f%%)\n", n_tests_skipped_g, n_tests_run_g,
           (double)n_tests_skipped_g / (double)n_tests_run_g * 100.0);

    return 0;
}

herr_t
setup_tests(void)
{
    herr_t ret_value = SUCCEED;

    if (H5Fcreate(MT_DUMMY_FILE_NAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT) < 0) {
        printf("Failed to create dummy file\n");
        TEST_ERROR;
    }

error:
    return ret_value;
}

herr_t
cleanup_tests(void)
{
    herr_t ret_value = SUCCEED;

    if (H5Fdelete(MT_DUMMY_FILE_NAME, H5P_DEFAULT) < 0) {
        printf("Failed to remove dummy file\n");
        TEST_ERROR;
    }

error:
    return (ret_value);
}
/* Concurrently register and unregister the same VOL connector from multiple threads. */
void *
test_concurrent_registration(void H5_ATTR_UNUSED *arg)
{
    hid_t vol_ids[REGISTRATION_COUNT];

    const H5VL_class_t *vol_class = NULL;

    TESTING("concurrent VOL conn registration/unregistration");

    memset(vol_ids, 0, sizeof(vol_ids));

    vol_class = &reg_opt_vol_g;

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if ((vol_ids[i] = H5VLregister_connector(vol_class, H5P_DEFAULT)) < 0) {
            printf("Failed to register fake VOL connector\n");
            TEST_ERROR;
        }
    }

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if (H5VLunregister_connector(vol_ids[i]) < 0) {
            printf("Failed to unregister fake VOL connector\n");
            TEST_ERROR;
        }
    }

    /* Close last existing reference to VOL connector in FAPL */
    PASSED();

    return NULL;

error:
    /* Attempt to unregister each ID one time */
    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if (vol_ids[i] > 0)
            H5VLunregister_connector(vol_ids[i]);
    }

    return (void *)-1;
}

void *
test_concurrent_registration_by_name(void H5_ATTR_UNUSED *arg)
{
    hid_t vol_ids[REGISTRATION_COUNT];

    TESTING("concurrent registration by name");

    memset(vol_ids, 0, sizeof(vol_ids));

    // TODO - Replace with absolute path to prevent failure when running from different directories
    if (H5PLprepend("./test/.libs/") < 0) {
        printf("Failed to prepend path\n");
        TEST_ERROR;
    }

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if ((vol_ids[i] = H5VLregister_connector_by_name(NULL_VOL_CONNECTOR_NAME, H5P_DEFAULT)) < 0) {
            printf("Failed to register NULL VOL connector by name\n");
            TEST_ERROR;
        }
    }

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if (H5VLunregister_connector(vol_ids[i]) < 0) {
            printf("Failed to unregister VOL connector\n");
            TEST_ERROR;
        }

        vol_ids[i] = H5I_INVALID_HID;
    }

    PASSED();
    return 0;
error:
    /* Try to unregister remaining connectors */
    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if (vol_ids[i] > 0)
            H5VLunregister_connector(vol_ids[i]);
    }

    return (void *)-1;
}

void *
test_concurrent_registration_by_value(void H5_ATTR_UNUSED *arg)
{
    hid_t vol_ids[REGISTRATION_COUNT];

    TESTING("concurrent registration by value");

    memset(vol_ids, 0, sizeof(vol_ids));

    // TODO - Replace with absolute path to prevent failure when running from different directories
    if (H5PLprepend("./test/.libs/") < 0) {
        printf("Failed to prepend path\n");
        TEST_ERROR;
    }

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if ((vol_ids[i] = H5VLregister_connector_by_value(NULL_VOL_CONNECTOR_VALUE, H5P_DEFAULT)) < 0) {
            printf("Failed to register NULL VOL connector by value\n");
            TEST_ERROR;
        }
    }

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if (H5VLunregister_connector(vol_ids[i]) < 0) {
            printf("Failed to unregister NULL VOL connector\n");
            TEST_ERROR;
        }

        vol_ids[i] = H5I_INVALID_HID;
    }

    PASSED();
    return 0;
error:
    /* Try to unregister remaining connectors */
    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if (vol_ids[i] > 0)
            H5VLunregister_connector(vol_ids[i]);
    }

    return (void *)-1;
}

/* Helper to generate the appropriate VOL subclass for a given iteration */
H5VL_subclass_t
select_valid_vol_subclass(size_t index)
{
    switch (index / OPERATIONS_PER_SUBCLASS) {
        case (0):
            return H5VL_SUBCLS_ATTR;
        case (1):
            return H5VL_SUBCLS_DATASET;
        case (2):
            return H5VL_SUBCLS_DATATYPE;
        case (3):
            return H5VL_SUBCLS_FILE;
        case (4):
            return H5VL_SUBCLS_GROUP;
        case (5):
            return H5VL_SUBCLS_LINK;
        case (6):
            return H5VL_SUBCLS_OBJECT;
        case (7):
            return H5VL_SUBCLS_REQUEST;
        default:
            return H5VL_SUBCLS_NONE;
    }
}

/* Test concurrent registration and unregistration of dynamic VOL operations */
void *
test_concurrent_dyn_op_registration(void H5_ATTR_UNUSED *arg)
{
    herr_t          registration_result = FAIL;
    hid_t           vol_id              = H5I_INVALID_HID;
    H5VL_subclass_t subcls              = H5VL_SUBCLS_NONE;
    char            subcls_name[100];
    int             op_val_reg    = -1;
    int             op_val_find   = -1;
    int             chars_written = -1;

    TESTING("concurrent dynamic operation registration");

    if ((vol_id = H5VLregister_connector(&reg_opt_vol_g, H5P_DEFAULT)) < 0) {
        printf("Failed to register fake VOL connector\n");
        TEST_ERROR;
    }

    for (size_t i = 0; i < NUM_VALID_SUBCLASSES * OPERATIONS_PER_SUBCLASS; i++) {
        /* Repeat this subclass OPERATIONS_PER_SUBCLASS times */
        subcls = select_valid_vol_subclass(i);

        /* "<subclass>_<idx>"*/
        /* Operation name intentionally generated procedurally to make sure threads operate on the same ops */
        if ((chars_written = snprintf(subcls_name, SUBCLS_NAME_SIZE, "%d_%zu", subcls, i) < 0) ||
            (size_t)chars_written >= sizeof(subcls_name)) {
            printf("Failed to generate subclass name\n");
            TEST_ERROR;
        }

        /* Registration may fail due to already being registered from another thread */
        /* TODO: When merged, replace with PAUSE_ERROR */
        H5E_BEGIN_TRY
        {
            registration_result = H5VLregister_opt_operation(subcls, subcls_name, &op_val_reg);
        }
        H5E_END_TRY;

        if (registration_result == SUCCEED) {
            if (op_val_reg <= 0) {
                printf("Invalid operation value %d\n", op_val_reg);
                TEST_ERROR;
            }

            /* Find the operation - if this thread registered the operation, then no other thread should
             * unregister it before this. */
            if (H5VLfind_opt_operation(subcls, subcls_name, &op_val_find) < 0) {
                printf("Failed to find operation %s\n", subcls_name);
                TEST_ERROR;
            }

            if (op_val_find <= 0) {
                printf("Invalid operation value %d\n", op_val_find);
                TEST_ERROR;
            }

            if (op_val_find != op_val_reg) {
                printf("Retrieved optional op value does not match expected: %d != %d\n", op_val_find,
                       op_val_reg);
                TEST_ERROR;
            }

            if (H5VLunregister_opt_operation(subcls, subcls_name) < 0) {
                printf("Failed to unregister operation %s\n", subcls_name);
                TEST_ERROR;
            }
        }
    }

    if (H5VLunregister_connector(vol_id) < 0) {
        printf("Failed to unregister fake VOL connector\n");
        TEST_ERROR;
    }

    vol_id = H5I_INVALID_HID;

    PASSED();
    return NULL;

error:

    if (vol_id > 0)
        H5VLunregister_connector(vol_id);

    return (void *)-1;
}

void *
test_concurrent_registration_operation(void H5_ATTR_UNUSED *arg)
{
    hid_t                    vol_ids[REGISTRATION_COUNT];
    hid_t                    fapl_id = H5I_INVALID_HID;
    H5VL_pass_through_info_t passthru_info;

    TESTING("concurrent VOL registration and operation");

    memset(vol_ids, 0, sizeof(vol_ids));
    passthru_info.under_vol_id   = H5VL_NATIVE;
    passthru_info.under_vol_info = NULL;

    /* Don't use H5VL_PASSTHRU since that avoids double registration, which we want to test */

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if ((vol_ids[i] = H5VLregister_connector(&H5VL_pass_through_g, H5P_DEFAULT)) < 0) {
            printf("Failed to register passthrough VOL connector\n");
            TEST_ERROR;
        }
    }

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
            printf("Failed to create FAPL\n");
            TEST_ERROR;
        }

        if (H5Pset_vol(fapl_id, vol_ids[i], &passthru_info) < 0) {
            printf("Failed to set VOL connector\n");
            TEST_ERROR;
        }

        /* Simple routine that passes through VOL layer */
        if (H5Fis_accessible(MT_DUMMY_FILE_NAME, fapl_id) < 0) {
            printf("Failed to check file accessibility\n");
            TEST_ERROR;
        }

        if (H5Pclose(fapl_id) < 0) {
            printf("Failed to close FAPL\n");
            TEST_ERROR;
        }

        fapl_id = H5I_INVALID_HID;
    }

    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if (H5VLunregister_connector(vol_ids[i]) < 0) {
            printf("Failed to unregister fake VOL connector\n");
            TEST_ERROR;
        }

        vol_ids[i] = H5I_INVALID_HID;
    }

    PASSED();

error:
    if (fapl_id > 0)
        H5Pclose(fapl_id);
    for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
        if (vol_ids[i] > 0)
            H5VLunregister_connector(vol_ids[i]);
    }
    return (void *)-1;
}
