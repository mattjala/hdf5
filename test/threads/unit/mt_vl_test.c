#include "H5VLpassthru.c"
#include "H5VLpassthru.h"
#include "thread_test_vol_connector.c"
#include "thread_test_vol_connector.h"
#include "h5test.h"
#include "null_vol_connector.h"
#include <pthread.h>

#include "H5VLprivate.h"
#include "H5CXprivate.h"
#include "H5Iprivate.h"
#define H5F_FRIEND
#include "H5Fpkg.h"

#define MAX_THREADS 32
#define NUM_ITERS 100
#define REGISTRATION_COUNT 50

#define MT_DUMMY_FILE_NAME "mt_dummy_file.h5"
#define MT_DUMMY_GROUP_NAME "mt_dummy_group"
#define NONEXISTENT_FILENAME "nonexistent.h5"

#define SUBCLS_NAME_SIZE 100
#define NUM_VALID_SUBCLASSES 8
#define OPERATIONS_PER_SUBCLASS 5

#define TEMP_RELATIVE_VOL_PATH "../../.libs/"
#define H5F_ACS_VOL_CONN_NAME                                                  \
  "vol_connector_info" /* VOL connector ID & info. Duplicate definition from   \
                          H5Fprivate.h */

/* Shared VOL Connector Property for testing */
H5VL_connector_prop_t conn_prop_g;

typedef void *(*mt_vl_test_cb)(void *arg);

herr_t setup_test_files(void);
herr_t cleanup_test_files(void);

/* Test cases that are directly run in parallel */
void *test_concurrent_registration(void *arg);
void *test_concurrent_registration_by_name(void *arg);
void *test_concurrent_registration_by_value(void *args);
void *test_concurrent_registration_operation(void *arg);
void *test_concurrent_dyn_op_registration(void *arg);
void *test_file_open_failure_registration(void *arg);
void *test_vol_property_copy(void *arg);
void *test_lib_state_vol_conn_prop(void *arg);
void *test_vol_wrap_ctx(void *arg);
void *test_vol_info(void *arg);

/* Test cases that do their own threading */
void *test_concurrent_register_and_search(void *arg);

/* Helper routines used by the tests */
H5VL_subclass_t select_valid_vol_subclass(size_t index);
void *register_helper(void *arg);
void *search_by_name_helper(void *arg);
void *search_by_value_helper(void *arg);

mt_vl_test_cb tests[] = {test_concurrent_registration,
                         test_concurrent_registration_by_name,
                         test_concurrent_dyn_op_registration,
                         test_concurrent_registration_operation,
                         test_concurrent_registration_by_value,
                         test_file_open_failure_registration,
                         test_vol_property_copy,
                         test_lib_state_vol_conn_prop,
                         test_vol_wrap_ctx,
                         test_vol_info};

int main(void) {
  size_t num_threads;
  pthread_t threads[MAX_THREADS];

  hid_t vol_id = H5I_INVALID_HID;
  hid_t fapl_id = H5I_INVALID_HID;

  void *test_args = NULL;
  void *thread_return = NULL;

  H5VL_pass_through_info_t passthru_info = {H5VL_NATIVE, NULL};
#ifndef H5_HAVE_MULTITHREAD
  SKIPPED();
  return 0;
#endif

  /* Test setup */
  if (setup_test_files() < 0) {
    printf("Failed to set up multi-thread VL tests\n");
    TEST_ERROR;
  }

  if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
    printf("Failed to create FAPL\n");
    TEST_ERROR;
  }

  /* Set up PASSTHRU VOL on plist */
  if ((vol_id = H5VLregister_connector(&H5VL_pass_through_g, H5P_DEFAULT)) <
      0) {
    printf("Failed to register passthrough VOL connector\n");
    TEST_ERROR;
  }

  if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
    printf("Failed to create FAPL\n");
    TEST_ERROR;
  }

  if (H5Pset_vol(fapl_id, vol_id, &passthru_info) < 0) {
    printf("Failed to set VOL connector\n");
    TEST_ERROR;
  }

  /* Populate shared VOL Connector property */
  conn_prop_g.connector_id = vol_id;
  conn_prop_g.connector_info = (void*) &passthru_info;

  /* Run tests that run directly in parallel with themselves */
  for (num_threads = 1; num_threads <= MAX_THREADS; num_threads++) {
    printf("Testing with %zu threads\n", num_threads);

    for (size_t test_idx = 0; test_idx < sizeof(tests) / sizeof(tests[0]);
         test_idx++) {
      mt_vl_test_cb test_func = tests[test_idx];

      if (test_func == test_vol_property_copy) {
        test_args = (void *)fapl_id;
      } else {
        test_args = NULL;
      }

      for (size_t i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, test_func, test_args);
      }

      for (size_t i = 0; i < num_threads; i++) {
        pthread_join(threads[i], &thread_return);
      }

      memset(threads, 0, sizeof(threads));
    }
  }

  /* Run tests that do their own thread handling */
  test_concurrent_register_and_search(NULL);

  if (cleanup_test_files() < 0) {
    printf("Failed to clean up multi-thread VL tests\n");
    TEST_ERROR;
  }

  if (H5VLunregister_connector(vol_id) < 0) {
    printf("Failed to unregister passthrough VOL connector\n");
    TEST_ERROR;
  }

  vol_id = H5I_INVALID_HID;

  if (H5Pclose(fapl_id) < 0) {
    printf("Failed to close FAPL\n");
    TEST_ERROR;
  }

  fapl_id = H5I_INVALID_HID;

  printf("%zu/%zu tests passed (%.2f%%)\n", n_tests_passed_g, n_tests_run_g,
         (double)n_tests_passed_g / (double)n_tests_run_g * 100.0);
  printf("%zu/%zu tests failed (%.2f%%)\n", n_tests_failed_g, n_tests_run_g,
         (double)n_tests_failed_g / (double)n_tests_run_g * 100.0);
  printf("%zu/%zu tests skipped (%.2f%%)\n", n_tests_skipped_g, n_tests_run_g,
         (double)n_tests_skipped_g / (double)n_tests_run_g * 100.0);

  return 0;

error:
  if (vol_id > 0)
    H5VLunregister_connector(vol_id);
  if (fapl_id > 0)
    H5Pclose(fapl_id);

  return -1;
}

herr_t setup_test_files(void) {
  herr_t ret_value = SUCCEED;

  if (H5Fcreate(MT_DUMMY_FILE_NAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT) <
      0) {
    printf("Failed to create dummy file\n");
    TEST_ERROR;
  }

error:
  return ret_value;
}

herr_t cleanup_test_files(void) {
  herr_t ret_value = SUCCEED;

  if (H5Fdelete(MT_DUMMY_FILE_NAME, H5P_DEFAULT) < 0) {
    printf("Failed to remove dummy file\n");
    TEST_ERROR;
  }

error:
  return (ret_value);
}
/* Concurrently register and unregister the same VOL connector from multiple
 * threads. */
void *test_concurrent_registration(void H5_ATTR_UNUSED *arg) {
  hid_t vol_ids[REGISTRATION_COUNT];

  const H5VL_class_t *vol_class = NULL;

  TESTING("concurrent VOL connector registration/unregistration");

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

void *test_concurrent_registration_by_name(void H5_ATTR_UNUSED *arg) {
  hid_t vol_ids[REGISTRATION_COUNT];

  TESTING("concurrent registration by name");

  memset(vol_ids, 0, sizeof(vol_ids));

  // TODO - Replace with absolute path to prevent failure when running from
  // different directories
  if (H5PLprepend(TEMP_RELATIVE_VOL_PATH) < 0) {
    printf("Failed to prepend path\n");
    TEST_ERROR;
  }

  for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
    if ((vol_ids[i] = H5VLregister_connector_by_name(NULL_VOL_CONNECTOR_NAME,
                                                     H5P_DEFAULT)) < 0) {
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

void *test_concurrent_registration_by_value(void H5_ATTR_UNUSED *arg) {
  hid_t vol_ids[REGISTRATION_COUNT];

  TESTING("concurrent registration by value");

  memset(vol_ids, 0, sizeof(vol_ids));

  // TODO - Replace with absolute path to prevent failure when running from
  // different directories
  if (H5PLprepend(TEMP_RELATIVE_VOL_PATH) < 0) {
    printf("Failed to prepend path\n");
    TEST_ERROR;
  }

  for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
    if ((vol_ids[i] = H5VLregister_connector_by_value(NULL_VOL_CONNECTOR_VALUE,
                                                      H5P_DEFAULT)) < 0) {
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
H5VL_subclass_t select_valid_vol_subclass(size_t index) {
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
void *test_concurrent_dyn_op_registration(void H5_ATTR_UNUSED *arg) {
  herr_t registration_result = FAIL;
  hid_t vol_id = H5I_INVALID_HID;
  H5VL_subclass_t subcls = H5VL_SUBCLS_NONE;
  char subcls_name[100];
  int op_val_reg = -1;
  int op_val_find = -1;
  int chars_written = -1;

  TESTING("concurrent dynamic operation registration");

  if ((vol_id = H5VLregister_connector(&reg_opt_vol_g, H5P_DEFAULT)) < 0) {
    printf("Failed to register fake VOL connector\n");
    TEST_ERROR;
  }

  for (size_t i = 0; i < NUM_VALID_SUBCLASSES * OPERATIONS_PER_SUBCLASS; i++) {
    /* Repeat this subclass OPERATIONS_PER_SUBCLASS times */
    subcls = select_valid_vol_subclass(i);

    /* "<subclass>_<idx>"*/
    /* Operation name intentionally generated procedurally to make sure threads
     * operate on the same ops */
    if ((chars_written = snprintf(subcls_name, SUBCLS_NAME_SIZE, "%d_%zu",
                                  subcls, i) < 0) ||
        (size_t)chars_written >= sizeof(subcls_name)) {
      printf("Failed to generate subclass name\n");
      TEST_ERROR;
    }

    /* Registration may fail due to already being registered from another thread
     */
    /* TODO: When merged, replace with PAUSE_ERROR */
    H5E_BEGIN_TRY {
      registration_result =
          H5VLregister_opt_operation(subcls, subcls_name, &op_val_reg);
    }
    H5E_END_TRY;

    if (registration_result == SUCCEED) {
      if (op_val_reg <= 0) {
        printf("Invalid operation value %d\n", op_val_reg);
        TEST_ERROR;
      }

      /* Find the operation - if this thread registered the operation, then no
       * other thread should unregister it before this. */
      if (H5VLfind_opt_operation(subcls, subcls_name, &op_val_find) < 0) {
        printf("Failed to find operation %s\n", subcls_name);
        TEST_ERROR;
      }

      if (op_val_find <= 0) {
        printf("Invalid operation value %d\n", op_val_find);
        TEST_ERROR;
      }

      if (op_val_find != op_val_reg) {
        printf(
            "Retrieved optional op value does not match expected: %d != %d\n",
            op_val_find, op_val_reg);
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

void *test_concurrent_registration_operation(void H5_ATTR_UNUSED *arg) {
  hid_t vol_ids[REGISTRATION_COUNT];
  hid_t fapl_id = H5I_INVALID_HID;
  H5VL_pass_through_info_t passthru_info;

  TESTING("concurrent VOL registration and operation");

  memset(vol_ids, 0, sizeof(vol_ids));
  passthru_info.under_vol_id = H5VL_NATIVE;
  passthru_info.under_vol_info = NULL;

  /* Don't use H5VL_PASSTHRU since that avoids double registration, which we
   * want to test */
  for (size_t i = 0; i < REGISTRATION_COUNT; i++) {
    if ((vol_ids[i] =
             H5VLregister_connector(&H5VL_pass_through_g, H5P_DEFAULT)) < 0) {
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

/* Test that upon file open failure, loading an available VOL connector from
 * H5PL works in a multi-threaded environment */
void *test_file_open_failure_registration(void H5_ATTR_UNUSED *arg) {
  hid_t file_id = H5I_INVALID_HID;

  TESTING("VOL registration on file open failure");
  /* Make the NULL VOL connector available via H5PL */
  // TODO - Replace with absolute path to prevent failures when running tests
  // from an unexpected directory
  if (H5PLprepend(TEMP_RELATIVE_VOL_PATH) < 0) {
    printf("Failed to prepend path for fake open VOL connector\n");
    TEST_ERROR;
  }

  /* Attempt to open an unopenable file with Native VOL, triggering use of the
   * fake open VOL, which "succeeds" */
  H5E_BEGIN_TRY { /* Don't display error from the Native failed open */
    if ((file_id = H5Fopen(NONEXISTENT_FILENAME, H5F_ACC_RDWR, H5P_DEFAULT)) <
        0) {
      printf("Failed to fake open nonexistent file\n");
      TEST_ERROR;
    }
  }
  H5E_END_TRY;

  /* Clean up library-internal state for fake file */
  if (H5Fclose(file_id) < 0) {
    printf("Failed to close fake file\n");
    TEST_ERROR;
  }

  PASSED();
  return NULL;
error:

  H5Fclose(file_id);
  return (void *)-1;
}

/* Test that implicit copying of a VOL connector property on a FAPL is handled
 * correctly */
void *test_vol_property_copy(void *arg) {
  hid_t fapl_id = H5I_INVALID_HID;
  hid_t fapl_id2 = H5I_INVALID_HID;
  hid_t fapl_id3 = H5I_INVALID_HID;

  TESTING("VOL connector property copying");

  if (!arg) {
    printf("No FAPL provided to test!\n");
    TEST_ERROR;
  }

  fapl_id = (hid_t)arg;

  /* Copy entire property list */
  if ((fapl_id2 = H5Pcopy(fapl_id)) < 0) {
    printf("Failed to copy FAPL\n");
    TEST_ERROR;
  }

  /* Copy single property between lists */

  if ((fapl_id3 = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
    printf("Failed to create FAPL\n");
    TEST_ERROR;
  }

  if (H5Pcopy_prop(fapl_id3, fapl_id, H5F_ACS_VOL_CONN_NAME) < 0) {
    printf("Failed to copy VOL connector property\n");
    TEST_ERROR;
  }

  if (H5Pclose(fapl_id2) < 0) {
    printf("Failed to close copied FAPL\n");
    TEST_ERROR;
  }

  if (H5Pclose(fapl_id3) < 0) {
    printf("Failed to close copied FAPL\n");
    TEST_ERROR;
  }
  PASSED();
  return NULL;

error:
  /* First FAPL came from main, do not free that one */
  if (fapl_id2 > 0)
    H5Pclose(fapl_id2);

  if (fapl_id3 > 0)
    H5Pclose(fapl_id3);

  return (void *)-1;
}

void *register_helper(void *arg) {
  hid_t vol_id = H5I_INVALID_HID;
  const H5VL_class_t *vol_class = (const H5VL_class_t*) arg;
  size_t i;

  for (i = 0; i < NUM_ITERS; i++) {
    if ((vol_id = H5VLregister_connector(vol_class, H5P_DEFAULT)) < 0)
      return (void *)-1;

    if (H5VLunregister_connector(vol_id) < 0)
      return (void *)-1;

    vol_id = H5I_INVALID_HID;
  }

  return NULL;
}

void *search_by_name_helper(void *arg) {
  char *vol_name = (char *)arg;
  hid_t vol_id = H5I_INVALID_HID;
  size_t i;

  for (i = 0; i < NUM_ITERS; i++) {
    /* Either failure or success is acceptable as long as no consistency/memory errors occur */
    H5E_BEGIN_TRY {
      vol_id = H5VLget_connector_id_by_name(vol_name);
    } H5E_END_TRY;

    /* If request succeeded, close the handle we opened */
    if (vol_id != H5I_INVALID_HID) {
      if (H5VLclose(vol_id) < 0)
        return (void *)-1;

      vol_id = H5I_INVALID_HID;
    }
  }

  return NULL;
}

void *search_by_value_helper(void *arg) {
  H5VL_class_value_t vol_value = *((H5VL_class_value_t *)arg);
  hid_t vol_id = H5I_INVALID_HID;
  size_t i;

  for (i = 0; i < NUM_ITERS; i++) {
    /* Either failure or success is acceptable as long as no consistency/memory errors occur */
    H5E_BEGIN_TRY {
      vol_id = H5VLget_connector_id_by_value(vol_value);
    } H5E_END_TRY;

    /* If request succeeded, close the handle we opened */
    if (vol_id != H5I_INVALID_HID) {
      if (H5VLclose(vol_id) < 0)
        return (void *)-1;

      vol_id = H5I_INVALID_HID;
    }
  }

  return NULL;
}

/* Spawn and run 3 groups of threads:
 * - Threads registering and unregistering a connector
 * - Threads searching for that connector by name
 * - Threads searching for that connector by value 
 */
void *test_concurrent_register_and_search(void H5_ATTR_UNUSED *arg) {
  size_t threads_per_group = MAX_THREADS / 3;
  size_t i;

  pthread_t threads_register[threads_per_group];
  pthread_t threads_search_name[threads_per_group];
  pthread_t threads_search_value[threads_per_group];

  void *thread_return = NULL;

  char *vol_name = NULL;
  H5VL_class_value_t vol_value = -1;
  H5VL_class_t vol_class;

  TESTING("Concurrent registration/unregistration and search");

  /* Set up arguments to avoid warnings about cast from const */
  if ((vol_name = (char *)malloc(strlen(H5VL_PASSTHRU_NAME) + 1)) == NULL) {
    printf("Failed to allocate memory for VOL name\n");
    TEST_ERROR;
  }

  memcpy(&vol_class, (const void *) &H5VL_pass_through_g, sizeof(H5VL_class_t));

  vol_value = H5VL_PASSTHRU_VALUE;

  /* Spawn threads */
  for (i = 0; i < threads_per_group; i++) {
    pthread_create(&threads_register[i], NULL, register_helper, (void*) &vol_class);
    pthread_create(&threads_search_name[i], NULL, search_by_name_helper, (void*)vol_name);
    pthread_create(&threads_search_value[i], NULL, search_by_value_helper, (void*) &vol_value);
  }

  for (i = 0; i < threads_per_group; i++) {
    pthread_join(threads_register[i], &thread_return);

    if (thread_return != NULL) {
      printf("Failed to register/unregister VOL connector\n");
      TEST_ERROR;
    }

    pthread_join(threads_search_name[i], &thread_return);

    if (thread_return != NULL) {
      printf("Failed to search for VOL connector by name\n");
      TEST_ERROR;
    }

    pthread_join(threads_search_value[i], &thread_return);

    if (thread_return != NULL) {
      printf("Failed to search for VOL connector by value\n");
      TEST_ERROR;
    }
  
  }

  PASSED();

  free(vol_name);
  return NULL;

error:
  free(vol_name);
  return (void *)-1;
}


void *test_lib_state_vol_conn_prop(void H5_ATTR_UNUSED *arg) {
  void *lib_state = NULL;
  bool lib_state_started = false;

  TESTING("Library state consistency");

  if (H5VLstart_lib_state() < 0) {
    printf("Failed to start library state\n");
    TEST_ERROR;
  }

  lib_state_started = true;

  /* Set the VOL Connector property on the API Context for this thread */
  if (H5CX_set_vol_connector_prop(&conn_prop_g) < 0) {
    printf("Failed to set VOL connector property\n");
    TEST_ERROR;
  }

  /* This routine must copy the vol conn property */
  if (H5VLretrieve_lib_state(&lib_state) < 0) {
    printf("Failed to retrieve library state\n");
    TEST_ERROR;
  }

  if (lib_state == NULL) {
    printf("Library state is NULL\n");
    TEST_ERROR;
  }

  if (H5VLrestore_lib_state(lib_state) < 0) {
    printf("Failed to restore library state\n");
    TEST_ERROR;
  }

  if (H5VLfree_lib_state(lib_state) < 0) {
    printf("Failed to free library state\n");
    TEST_ERROR;
  }

  if (H5VLfinish_lib_state() < 0) {
    printf("Failed to finish library state\n");
    TEST_ERROR;
  }

  PASSED();

  return NULL;

error:
  if (lib_state_started)
    H5VLfinish_lib_state();

  if (lib_state)
    H5VLfree_lib_state(lib_state);

  return (void *)-1;
}

/* Retrieve and free the VOL wrap context in multiple threads executing in parallel.
 *
 * TBD: This largely depends on the get_wrap_ctx()/free_wrap_ctx() callbacks of the active connector(s), and
 * so should probably have a counterpart placed in the API tests for use with various VOL connectors. */
void *test_vol_wrap_ctx(void H5_ATTR_UNUSED *arg) {
  void *wrap_ctx = NULL;
  H5VL_object_t *vol_object = NULL;

  H5VL_pass_through_info_t passthru_info = {H5VL_NATIVE, NULL};
  hid_t passthru_id = H5I_INVALID_HID;
  hid_t fapl_id = H5I_INVALID_HID;
  hid_t file_id = H5I_INVALID_HID;

  TESTING("VOL wrap context");

  /* Register the passthrough connector */
  if ((passthru_id = H5VLregister_connector(&H5VL_pass_through_g, H5P_DEFAULT)) < 0) {
    printf("Failed to register passthrough VOL connector\n");
    TEST_ERROR;
  }

  if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
    printf("Failed to create FAPL\n");
    TEST_ERROR;
  }

  if (H5Pset_vol(fapl_id, passthru_id, (const void*) &passthru_info) < 0) {
    printf("Failed to set VOL connector\n");
    TEST_ERROR;
  }

  /* Open a VOL object to retrieve the context from */
  if ((file_id = H5Fopen(MT_DUMMY_FILE_NAME, H5F_ACC_RDONLY, fapl_id)) < 0) {
    printf("Failed to open file\n");
    TEST_ERROR;
  }

  if ((vol_object = (H5VL_object_t*) H5I_object_verify(file_id, H5I_FILE)) == NULL) {
    printf("Failed to verify file ID\n");
    TEST_ERROR;
  }

  if (!vol_object->data) {
    printf("Passthrough connector object is NULL\n");
    TEST_ERROR;
  }

  /* Retrieve & subsequently free VOL wrap context */
  if (H5VLget_wrap_ctx((void*) (vol_object->data), passthru_id, &wrap_ctx) < 0) {
    printf("Failed to retrieve VOL wrap context\n");
    TEST_ERROR;
  }

  if (!wrap_ctx) {
    printf("Retrieved wrap context is NULL\n");
    TEST_ERROR;
  }

  if (H5VLfree_wrap_ctx(wrap_ctx, passthru_id) < 0) {
    printf("Failed to free VOL wrap context\n");
    TEST_ERROR;
  }

  /* Clean up */
  if (H5Fclose(file_id) < 0) {
    printf("Failed to close file\n");
    TEST_ERROR;
  }

  if (H5Pclose(fapl_id) < 0) {
    printf("Failed to close FAPL\n");
    TEST_ERROR;
  }

  if (H5VLunregister_connector(passthru_id) < 0) {
    printf("Failed to unregister passthrough VOL connector\n");
    TEST_ERROR;
  }

  PASSED();

  return NULL;
error:

  if (wrap_ctx && (passthru_id > 0))
    H5VLfree_wrap_ctx(wrap_ctx, passthru_id);

  if (passthru_id > 0)
    H5VLunregister_connector(passthru_id);

  H5Fclose(file_id);
  H5Pclose(fapl_id);

  return (void *)-1;
}

/* Retrieve and free the VOL information in multiple threads executing in parallel.
 *
 * TBD: This largely depends on the connector callbacks of the active connector(s), and
 * so should probably have a counterpart placed in the API tests for use with various VOL connectors. */
void *test_vol_info(void H5_ATTR_UNUSED *arg) {
  H5VL_pass_through_info_t vol_info = {H5VL_NATIVE, NULL};
  void *vol_info2 = NULL;
  hid_t vol_id = H5I_INVALID_HID;
  hid_t fapl_id = H5I_INVALID_HID;
  hid_t fapl_id2 = H5I_INVALID_HID;
  hid_t fapl_id3 = H5I_INVALID_HID;

  TESTING("VOL connector information");

  /* Use Passthrough connector, since it has a non-NULL information field */
  if ((vol_id = H5VLregister_connector(&H5VL_pass_through_g, H5P_DEFAULT)) < 0) {
    printf("Failed to register passthrough VOL connector\n");
    TEST_ERROR;
  }

  if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
    printf("Failed to create FAPL\n");
    TEST_ERROR;
  }

  /* Directly copy information */
  if (H5VLcopy_connector_info(vol_id, &vol_info2, &vol_info) < 0) {
    printf("Failed to copy VOL connector info\n");
    TEST_ERROR;
  }

  /* Copy information into property list */
  if (H5Pset_vol(fapl_id, vol_id, &vol_info) < 0) {
    printf("Failed to set VOL connector\n");
    TEST_ERROR;
  }

  /* Copy info via copying entire property list */
  if ((fapl_id2 = H5Pcopy(fapl_id)) < 0) {
    printf("Failed to copy FAPL\n");
    TEST_ERROR;
  }

  /* Copy info via copying single property between lists */
  if ((fapl_id3 = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
    printf("Failed to create FAPL\n");
    TEST_ERROR;
  }

  if (H5Pcopy_prop(fapl_id3, fapl_id, H5F_ACS_VOL_CONN_NAME) < 0) {
    printf("Failed to copy VOL connector property\n");
    TEST_ERROR;
  }

  /* Free information on each property list */
  if (H5Pclose(fapl_id) < 0) {
    printf("Failed to close FAPL\n");
    TEST_ERROR;
  }

  if (H5Pclose(fapl_id2) < 0) {
    printf("Failed to close copied FAPL\n");
    TEST_ERROR;
  }

  if (H5Pclose(fapl_id3) < 0) {
    printf("Failed to close copied FAPL\n");
    TEST_ERROR;
  }

  /* Free directly copied information */
  if (H5VLfree_connector_info(vol_id, vol_info2) < 0) {
    printf("Failed to free VOL connector info\n");
    TEST_ERROR;
  }

  if (H5VLunregister_connector(vol_id) < 0) {
    printf("Failed to unregister passthrough VOL connector\n");
    TEST_ERROR;
  }

  PASSED();

  return NULL;
error:

  H5Pclose(fapl_id);
  H5Pclose(fapl_id2);
  H5Pclose(fapl_id3);

  if (vol_id > 0)
    H5VLunregister_connector(vol_id);

  if (vol_info2)
    H5VLfree_connector_info(vol_id, vol_info2);
  
  return (void*)-1;
}