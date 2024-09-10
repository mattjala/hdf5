#include "h5test.h"
#include "H5VLpassthru.h"
#include "H5VLpassthru.c"
#include <pthread.h>
#define MAX_THREADS   32
#define NUM_ITERS 100
#define TEST_MSG_SIZE 256

typedef void *(*mt_vl_test_cb)(void *arg);

void *test_concurrent_registration(void *arg);
void *test_concurrent_dyn_op_registration(void *arg);

H5VL_subclass_t select_valid_vol_subclass(size_t index);

mt_vl_test_cb tests[2] = {
    test_concurrent_registration, test_concurrent_dyn_op_registration,
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

    printf("%zu/%zu tests passed (%.2f%%)\n", n_tests_passed_g, n_tests_run_g,
           (double)n_tests_passed_g / (double)n_tests_run_g * 100.0);
    printf("%zu/%zu tests failed (%.2f%%)\n", n_tests_failed_g, n_tests_run_g,
           (double)n_tests_failed_g / (double)n_tests_run_g * 100.0);
    printf("%zu/%zu tests skipped (%.2f%%)\n", n_tests_skipped_g, n_tests_run_g,
           (double)n_tests_skipped_g / (double)n_tests_run_g * 100.0);

    return 0;
}

/* Concurrently register and unregister the same VOL connector from multiple threads. */
void *
test_concurrent_registration(void H5_ATTR_UNUSED *arg)
{
    hid_t vol_id  = H5I_INVALID_HID;
    hid_t file_id = H5I_INVALID_HID;
    hid_t fapl_id = H5I_INVALID_HID;

    const H5VL_class_t *vol_class     = NULL;

    TESTING("concurrent VOL conn registration/unregistration");

    vol_class = &reg_opt_vol_g;

    if ((vol_id = H5VLregister_connector(vol_class, H5P_DEFAULT)) < 0) {
        printf("Failed to register native VOL connector\n");
        TEST_ERROR;
    }

    /* Set VOL on FAPL */
    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
        printf("Failed to create FAPL\n");
        TEST_ERROR;
    }

    if (H5Pset_vol(fapl_id, vol_id, NULL) < 0) {
        printf("Failed to set VOL connector on FAPL\n");
        TEST_ERROR;
    }

    /* Perform an operation via the FAPL */
    if ((file_id = H5Fcreate("test_concurrent_registration.h5", H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id)) < 0) {
        printf("Failed to create file with FAPL\n");
        TEST_ERROR;
    }

    if (H5VLunregister_connector(vol_id) < 0) {
        printf("Failed to unregister native VOL connector\n");
        TEST_ERROR;
    }

    vol_id = H5I_INVALID_HID;

    /* Close last existing reference to VOL connector in FAPL */
    if (H5Pclose(fapl_id) < 0)
        TEST_ERROR;
    if (H5Fclose(file_id) < 0)
        TEST_ERROR;
    PASSED();

    return NULL;

error:
    H5Pclose(fapl_id);
    H5Fclose(file_id);
    H5VLunregister_connector(vol_id);
    return (void *)-1;
}

#define SUBCLS_NAME_SIZE        100
#define NUM_VALID_SUBCLASSES    8
#define OPERATIONS_PER_SUBCLASS 5

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
    hid_t           vol_id = H5I_INVALID_HID;
    H5VL_subclass_t subcls = H5VL_SUBCLS_NONE;
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

        if (H5VLregister_opt_operation(subcls, subcls_name, &op_val_reg) < 0) {
            printf("Failed to register operation %s\n", subcls_name);
            TEST_ERROR;
        }

        if (op_val_reg <= 0) {
            printf("Invalid operation value %d\n", op_val_reg);
            TEST_ERROR;
        }

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
    }

    for (size_t i = 0; i < NUM_VALID_SUBCLASSES * OPERATIONS_PER_SUBCLASS; i++) {
        /* Re-generate subclass name */
        subcls = select_valid_vol_subclass(i);

        if ((chars_written = snprintf(subcls_name, SUBCLS_NAME_SIZE, "%d_%zu", subcls, i) < 0) ||
            (size_t)chars_written >= sizeof(subcls_name)) {
            printf("Failed to generate subclass name\n");
            TEST_ERROR;
        }

        if (H5VLunregister_opt_operation(subcls, subcls_name) < 0) {
            printf("Failed to unregister operation %s\n", subcls_name);
            TEST_ERROR;
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