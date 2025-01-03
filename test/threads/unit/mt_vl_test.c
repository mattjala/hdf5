/* VOL connectors used for testing */
#include "H5VLpassthru.c"
#include "H5VLpassthru.h"
#include "mt_vl_test_vol_connector.c"
#include "mt_vl_test_vol_connector.h"
#include "null_vol_connector.h"
#include <pthread.h>

#include "../testmthdf5.h"
#include "h5test.h"
#include "mt_test_util.h"

#include "H5VLprivate.h"
#include "H5CXprivate.h"
#include "H5Iprivate.h"
#define H5F_FRIEND
#include "H5Fpkg.h"

#define MT_TEST_VOL_REGISTRATION_FILENAME "mt_test_vol_registration.h5"
#define MT_TEST_VOL_WRAP_CTX_FILE_NAME "mt_test_vol_wrap_ctx_file.h5"
#define MT_DUMMY_GROUP_NAME "mt_dummy_group"
#define NONEXISTENT_FILENAME "nonexistent.h5"
#define SUBCLS_NAME_SIZE 100
#define H5F_ACS_VOL_CONN_NAME "vol_connector_info" /* Name of the VOL connector info property */

/* Parameters describing dynamic VOL operations - these cannot be changed */
#define NUM_VALID_SUBCLASSES 8
#define OPERATIONS_PER_SUBCLASS 5

#ifdef H5_HAVE_MULTITHREAD

/* Shared VOL Connector Property for testing */
H5VL_connector_prop_t conn_prop_g;

void *mt_test_registration_operation_helper(void *args);
void *mt_test_vol_property_copy_helper(void *args);
void *mt_test_vol_wrap_ctx_helper(void *args);


/* Helper routines used by the tests */
H5VL_subclass_t mt_test_dyn_op_get_vol_subclass(size_t index);
void *mt_test_search_register_helper(void *args);
void *mt_test_search_search_by_name_helper(void *args);
void *mt_test_search_search_by_value_helper(void *args);


/* Concurrently register and unregister the same VOL connector from multiple
 * threads. */
void mt_test_registration(const void *args) {
  hid_t *vol_ids;
  herr_t ret = SUCCEED;
  const mt_test_params *params = (const mt_test_params *) args;
  const H5VL_class_t *vol_class = NULL;

  assert(params != NULL);

  vol_ids = (hid_t *)calloc(params->num_repetitions, sizeof(hid_t));
  assert(vol_ids != NULL);

  vol_class = &mt_vl_test_vol_g;

  for (size_t i = 0; i < params->num_repetitions; i++) {
    vol_ids[i] = H5VLregister_connector(vol_class, H5P_DEFAULT);
    CHECK(vol_ids[i], H5I_INVALID_HID, "H5VLregister_connector");
  }

  for (size_t i = 0; i < params->num_repetitions; i++) {
    ret = H5VLunregister_connector(vol_ids[i]);
    VERIFY(ret, SUCCEED, "H5VLunregister_connector");
  }

  free(vol_ids);
  return;
}

/* Concurrently register and unregister the same VOL connector by name from multiple
 * threads. */
void mt_test_registration_by_name(const void *args) {
#ifndef H5_MT_TEST_VOL_DIR
  printf("Skipping test because H5_MT_TEST_VOL_DIR is not defined\n");
  return;
#else
  hid_t *vol_ids;
  herr_t ret = SUCCEED;
  const mt_test_params *params = (const mt_test_params *) args;

  assert(params != NULL);

  vol_ids = (hid_t *)calloc(params->num_repetitions, sizeof(hid_t));
  assert(vol_ids != NULL);

  ret = H5PLprepend(H5_MT_TEST_VOL_DIR);
  CHECK(ret, FAIL, "H5PLprepend");

  for (size_t i = 0; i < params->num_repetitions; i++) {
    vol_ids[i] = H5VLregister_connector_by_name(NULL_VOL_CONNECTOR_NAME, H5P_DEFAULT);

    if (vol_ids[i] == H5I_INVALID_HID)
      TestErrPrintf("Failed to register VOL connector by name (Make sure test is run from 'test' directory)\n");
  }

  for (size_t i = 0; i < params->num_repetitions; i++) {
    ret = H5VLunregister_connector(vol_ids[i]);
    CHECK(ret, FAIL, "H5VLunregister_connector");

    vol_ids[i] = H5I_INVALID_HID;
  }

  free(vol_ids);
  return;
#endif
}

/* Concurrently register and unregister the same VOL connector by value from multiple
 * threads. */
void mt_test_registration_by_value(const void *args) {
#ifndef H5_MT_TEST_VOL_DIR
  printf("Skipping test because H5_MT_TEST_VOL_DIR is not defined\n");
  return;
#else
  hid_t *vol_ids;
  herr_t ret = SUCCEED;
  const mt_test_params *params = (const mt_test_params *) args;

  assert(params != NULL);

  vol_ids = (hid_t *)calloc(params->num_repetitions, sizeof(hid_t));
  assert(vol_ids != NULL);

  ret = H5PLprepend(H5_MT_TEST_VOL_DIR);
  CHECK(ret, FAIL, "H5PLprepend");

  for (size_t i = 0; i < params->num_repetitions; i++) {
    vol_ids[i] = H5VLregister_connector_by_value(NULL_VOL_CONNECTOR_VALUE,
                                                      H5P_DEFAULT);
    
    if (vol_ids[i] == H5I_INVALID_HID)
      TestErrPrintf("Failed to register VOL connector by value (Make sure test is run from 'test' directory)\n");
  }

  for (size_t i = 0; i < params->num_repetitions; i++) {
    ret = H5VLunregister_connector(vol_ids[i]);
    CHECK(ret, FAIL, "H5VLunregister_connector");
    vol_ids[i] = H5I_INVALID_HID;
  }

  free(vol_ids);
  return;
#endif
}

/* Test concurrent registration and unregistration of dynamic VOL operations */
void mt_test_dyn_op_registration(const void H5_ATTR_UNUSED *args) {
  herr_t registration_result = FAIL;
  hid_t vol_id = H5I_INVALID_HID;
  H5VL_subclass_t subcls = H5VL_SUBCLS_NONE;
  char subcls_name[100];
  int op_val_reg = -1;
  int op_val_find = -1;
  int chars_written = -1;
  int ret = 0;

  vol_id = H5VLregister_connector(&reg_opt_vol_g, H5P_DEFAULT);
  CHECK(vol_id, H5I_INVALID_HID, "H5VLregister_connector");

  for (size_t i = 0; i < NUM_VALID_SUBCLASSES * OPERATIONS_PER_SUBCLASS; i++) {
    /* Repeat this subclass OPERATIONS_PER_SUBCLASS times */
    subcls = mt_test_dyn_op_get_vol_subclass(i);

    /* Generate operation name "<subclass>_<idx>"*/
    /* Operation name intentionally generated procedurally to make sure threads
     * operate on the same ops */
    chars_written = snprintf(subcls_name, SUBCLS_NAME_SIZE, "%d_%zu", subcls, i);
    CHECK(chars_written, -1, "snprintf");
    CHECK(chars_written, sizeof(subcls_name), "snprintf");

    /* Registration may fail due to already being registered from another thread
     */
    H5E_BEGIN_TRY {
      registration_result =
          H5VLregister_opt_operation(subcls, subcls_name, &op_val_reg);
    }
    H5E_END_TRY;

    if (registration_result == SUCCEED) {
      /* Should be a positive nonzero value */
      CHECK(op_val_reg, 0, "H5VLregister_opt_operation");
      CHECK(op_val_reg, -1, "H5VLregister_opt_operation");

      /* Find the operation - if this thread registered the operation, then no
       * other thread should unregister it before this. */
      ret = H5VLfind_opt_operation(subcls, subcls_name, &op_val_find);
      CHECK(ret, FAIL, "H5VLfind_opt_operation");

      /* Should be a positive nonzero value */
      CHECK(op_val_find, 0, "H5VLfind_opt_operation");
      CHECK(op_val_find, -1, "H5VLfind_opt_operation");

      VERIFY(op_val_find, op_val_reg, "H5VLfind_opt_operation");

      ret = H5VLunregister_opt_operation(subcls, subcls_name);
      VERIFY(ret, SUCCEED, "H5VLunregister_opt_operation");
    }
  }

  ret = H5VLunregister_connector(vol_id);
  VERIFY(ret, SUCCEED, "H5VLunregister_connector");

  return;
}

/* Helper to generate the appropriate VOL subclass for a given iteration */
H5VL_subclass_t mt_test_dyn_op_get_vol_subclass(size_t index) {
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

/* Test concurrent registration of a VOL connector with usage of one of its callbacks */
void mt_test_registration_operation(const void *args) {
  hid_t file_id = H5I_INVALID_HID;
  herr_t ret = SUCCEED;
  
  assert(args != NULL);

  mt_test_params params = *(const mt_test_params*)args;
  alarm(params.subtest_timeout);

  /* Create test file  */
  file_id = H5Fcreate(MT_TEST_VOL_REGISTRATION_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  CHECK(file_id, H5I_INVALID_HID, "H5Fcreate");

  mt_test_run_helper_in_parallel(mt_test_registration_operation_helper, (void*) &params);
  
  ret = H5Fclose(file_id);
  CHECK(ret, FAIL, "H5Fclose");

  return;
}

void *mt_test_registration_operation_helper(void *args) {
  hid_t *vol_ids;
  hid_t fapl_id = H5I_INVALID_HID;
  H5VL_pass_through_info_t passthru_info;
  herr_t ret = 0;
  const mt_test_params *params = (const mt_test_params *) args;

  assert(params != NULL);

  vol_ids = (hid_t *)calloc(params->num_repetitions, sizeof(hid_t));
  assert(vol_ids != NULL);

  passthru_info.under_vol_id = H5VL_NATIVE;
  passthru_info.under_vol_info = NULL;

  /* Don't use H5VL_PASSTHRU since that avoids double registration, which we
   * want to test */
  for (size_t i = 0; i < params->num_repetitions; i++) {
    vol_ids[i] = H5VLregister_connector(&H5VL_pass_through_g, H5P_DEFAULT);
    CHECK(vol_ids[i], H5I_INVALID_HID, "H5VLregister_connector");
  }

  for (size_t i = 0; i < params->num_repetitions; i++) {
    fapl_id = H5Pcreate(H5P_FILE_ACCESS);
    CHECK(fapl_id, H5I_INVALID_HID, "H5Pcreate");

    ret = H5Pset_vol(fapl_id, vol_ids[i], &passthru_info);
    CHECK(ret, FAIL, "H5Pset_vol");

    /* Simple routine that passes through VOL layer */
    ret = H5Fis_accessible(MT_TEST_VOL_REGISTRATION_FILENAME, fapl_id);
    CHECK(ret, FAIL, "H5Fis_accessible");

    ret = H5Pclose(fapl_id);
    CHECK(ret, FAIL, "H5Pclose");

    fapl_id = H5I_INVALID_HID;
  }

  for (size_t i = 0; i < params->num_repetitions; i++) {
    ret = H5VLunregister_connector(vol_ids[i]);
    VERIFY(ret, SUCCEED, "H5VLunregister_connector");
  }

  free(vol_ids);
  return NULL;
}

void mt_test_registration_operation_cleanup(void H5_ATTR_UNUSED *args) {
  herr_t ret = SUCCEED;

  ret = H5Fdelete(MT_TEST_VOL_REGISTRATION_FILENAME, H5P_DEFAULT);
  CHECK(ret, FAIL, "H5Fdelete");

  return;
}

/* Test that upon file open failure, loading an available VOL connector from
 * H5PL works in a multi-threaded environment */
void mt_test_file_open_failure_registration(const void H5_ATTR_UNUSED *args) {
#ifndef H5_MT_TEST_VOL_DIR
  printf("Skipping test because H5_MT_TEST_VOL_DIR is not defined\n");
  return;
#else
  hid_t file_id = H5I_INVALID_HID;
  hid_t fapl_id = H5I_INVALID_HID;
  hid_t curr_vol_id = H5I_INVALID_HID;
  herr_t ret = 0;

  fapl_id = H5Pcreate(H5P_FILE_ACCESS);
  CHECK(fapl_id, H5I_INVALID_HID, "H5Pcreate");

  /* Dynamic VOL loading on file open failure only occurs when using the Native VOL, 
   * so skip this test otherwise */
  ret = H5Pget_vol_id(fapl_id, &curr_vol_id);
  CHECK(ret, FAIL, "H5Pget_vol_id");

  if (curr_vol_id != H5VL_NATIVE) {
    goto done;
  }

  /* Make the NULL VOL connector available via H5PL */
  ret = H5PLprepend(H5_MT_TEST_VOL_DIR);
  CHECK(ret, FAIL, "H5PLprepend");

  /* Attempt to open an unopenable file with Native VOL, triggering dynamic load and usage of the
   * mt vl test VOL, which "succeeds" */
  H5E_BEGIN_TRY { /* Don't display error from the Native VOL's failure to open */
    file_id = H5Fopen(NONEXISTENT_FILENAME, H5F_ACC_RDWR, H5P_DEFAULT);
  }
  H5E_END_TRY;

  if (file_id < 0) {
    TestErrPrintf("Failed to load and use dynamic VOL connector (Make sure test is run from 'test' directory)\n");
  }

  /* Clean up library-internal state for fake file */
  ret = H5Fclose(file_id);
  CHECK(ret, FAIL, "H5Fclose");
  file_id = H5I_INVALID_HID;

  ret = H5Pclose(fapl_id);
  CHECK(ret, FAIL, "H5Pclose");
  fapl_id = H5I_INVALID_HID;

  ret = H5VLclose(curr_vol_id);
  CHECK(ret, FAIL, "H5VLclose");
  curr_vol_id = H5I_INVALID_HID;

done:
  if (file_id != H5I_INVALID_HID)
    H5Fclose(file_id);
  if (fapl_id != H5I_INVALID_HID)
    H5Pclose(fapl_id);
  if (curr_vol_id != H5I_INVALID_HID)
    H5VLclose(curr_vol_id);
  return;
#endif
}

/* Test that implicit copying of a VOL connector property on a FAPL is handled
 * correctly */
void mt_test_vol_property_copy(const void *args) {
  hid_t fapl_id = H5I_INVALID_HID;
  herr_t ret = SUCCEED;

  const mt_test_params *params = (const mt_test_params *) args;
  assert(params != NULL);
  alarm(params->subtest_timeout);

  fapl_id = H5Pcreate(H5P_FILE_ACCESS);
  CHECK(fapl_id, H5I_INVALID_HID, "H5Pcreate");
  
  ret = H5Pset_vol(fapl_id, H5VL_NATIVE, NULL);
  CHECK(ret, FAIL, "H5Pset_vol");

  mt_test_run_helper_in_parallel(mt_test_vol_property_copy_helper, (void *)fapl_id);

  ret = H5Pclose(fapl_id);
  CHECK(ret, FAIL, "H5Pclose");

  return;
}

void *mt_test_vol_property_copy_helper(void *args) {
  hid_t fapl_id = H5I_INVALID_HID;
  hid_t fapl_id2 = H5I_INVALID_HID;
  hid_t fapl_id3 = H5I_INVALID_HID;
  herr_t ret = SUCCEED;

  assert(args != NULL);

  fapl_id = (hid_t)args;

  /* Copy entire property list, duplicating VOL property */

  fapl_id2 = H5Pcopy(fapl_id);
  CHECK(fapl_id2, H5I_INVALID_HID, "H5Pcopy");

  /* Copy specific VOL property between lists */
  fapl_id3 = H5Pcreate(H5P_FILE_ACCESS);
  CHECK(fapl_id3, H5I_INVALID_HID, "H5Pcreate");

  ret = H5Pcopy_prop(fapl_id3, fapl_id, H5F_ACS_VOL_CONN_NAME);
  CHECK(ret, FAIL, "H5Pcopy_prop");

  /* Clean up */
  ret = H5Pclose(fapl_id2);
  CHECK(ret, FAIL, "H5Pclose");

  ret = H5Pclose(fapl_id3);
  CHECK(ret, FAIL, "H5Pclose");

  return NULL;
}

typedef struct mt_test_reg_helper_args {
  const mt_test_params *params;
  H5VL_class_t vol_class;
  char *vol_name;
  H5VL_class_value_t vol_value;
} mt_test_reg_helper_args;

/* Spawn and run 3 groups of threads:
 * - Threads registering and unregistering a connector
 * - Threads searching for that connector by name
 * - Threads searching for that connector by value 
 */
void mt_test_register_and_search(const void *args) {
  int threads_per_group;
  int i;
  mt_test_reg_helper_args helper_args;

  pthread_t *threads_register;
  pthread_t *threads_search_by_name;
  pthread_t *threads_search_by_value;

  void *thread_return = NULL;

  int ret = 0;

  int num_threads = GetTestMaxNumThreads();

  const mt_test_params *params = (const mt_test_params *) args;
  assert(params != NULL);
  alarm(params->subtest_timeout);

  if (num_threads <= 0) {
    printf("No threadcount specified with -maxthreads; skipping test\n");
    return;
  }

  threads_per_group = num_threads / 3;

  /* Set up thread information */
  threads_register = (pthread_t *)malloc((size_t) threads_per_group * sizeof(pthread_t));
  assert(threads_register != NULL);

  threads_search_by_name = (pthread_t *)malloc((size_t) threads_per_group * sizeof(pthread_t));
  assert(threads_search_by_name != NULL);

  threads_search_by_value = (pthread_t *)malloc((size_t) threads_per_group * sizeof(pthread_t));
  assert(threads_search_by_value != NULL);

  /* Set up argsuments to avoid warnings about cast from const */
  helper_args.vol_name = (char *)malloc(strlen(H5VL_PASSTHRU_NAME) + 1);
  assert(helper_args.vol_name != NULL);
  strcpy(helper_args.vol_name, H5VL_PASSTHRU_NAME);

  memcpy(&helper_args.vol_class, (const void *) &H5VL_pass_through_g, sizeof(H5VL_class_t));

  helper_args.vol_value = H5VL_PASSTHRU_VALUE;

  helper_args.params = params;

  /* Spawn threads */
  for (i = 0; i < threads_per_group; i++) {
    ret = pthread_create(&threads_register[i], NULL, mt_test_search_register_helper, (void*) &helper_args);
    VERIFY(ret, 0, "pthread_create");

    ret = pthread_create(&threads_search_by_name[i], NULL, mt_test_search_search_by_name_helper, (void*) &helper_args);
    VERIFY(ret, 0, "pthread_create");

    ret = pthread_create(&threads_search_by_value[i], NULL, mt_test_search_search_by_value_helper, (void*) &helper_args);
    VERIFY(ret, 0, "pthread_create");
  }

  for (i = 0; i < threads_per_group; i++) {
    ret = pthread_join(threads_register[i], &thread_return);
    VERIFY(ret, 0, "pthread_join");
    VERIFY(thread_return, NULL, "mt_test_search_register_helper");

    ret = pthread_join(threads_search_by_name[i], &thread_return);
    VERIFY(ret, 0, "pthread_join");
    VERIFY(thread_return, NULL, "mt_test_search_search_by_name_helper");

    ret = pthread_join(threads_search_by_value[i], &thread_return);
    VERIFY(ret, 0, "pthread_join");
    VERIFY(thread_return, NULL, "mt_test_search_search_by_value_helper");
  }

  free(helper_args.vol_name);
  free(threads_register);
  free(threads_search_by_name);
  free(threads_search_by_value);
  return;
}

void *mt_test_search_register_helper(void *args) {
  hid_t vol_id = H5I_INVALID_HID;
  mt_test_reg_helper_args *helper_args = (mt_test_reg_helper_args *) args;
  size_t i;

  assert(helper_args != NULL);

  for (i = 0; i < helper_args->params->num_repetitions; i++) {
    if ((vol_id = H5VLregister_connector(&helper_args->vol_class, H5P_DEFAULT)) < 0)
      return (void *)-1;

    if (H5VLunregister_connector(vol_id) < 0)
      return (void *)-1;

    vol_id = H5I_INVALID_HID;
  }

  return NULL;
}

void *mt_test_search_search_by_name_helper(void *args) {
  hid_t vol_id = H5I_INVALID_HID;
  size_t i;
  mt_test_reg_helper_args *helper_args = (mt_test_reg_helper_args *) args;
  assert(helper_args != NULL);

  for (i = 0; i < helper_args->params->num_repetitions; i++) {
    /* Either failure or success is acceptable as long as no consistency/memory errors occur */
    H5E_BEGIN_TRY {
      vol_id = H5VLget_connector_id_by_name(helper_args->vol_name);
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

void *mt_test_search_search_by_value_helper(void *args) {
  hid_t vol_id = H5I_INVALID_HID;
  size_t i;
  mt_test_reg_helper_args *helper_args = (mt_test_reg_helper_args *) args;
  assert(helper_args != NULL);

  for (i = 0; i < helper_args->params->num_repetitions; i++) {
    /* Either failure or success is acceptable as long as no consistency/memory errors occur */
    H5E_BEGIN_TRY {
      vol_id = H5VLget_connector_id_by_value(helper_args->vol_value);
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

/* Test concurrent usage of library state routines */
void mt_test_lib_state_ops(const void H5_ATTR_UNUSED *args) {
  void *lib_state = NULL;
  herr_t ret = SUCCEED;

  ret = H5VLstart_lib_state();
  VERIFY(ret, SUCCEED, "H5VLstart_lib_state");

  /* Set the VOL Connector property on the API Context for this thread */
  ret = H5CX_set_vol_connector_prop(&conn_prop_g);
  VERIFY(ret, SUCCEED, "H5CX_set_vol_connector_prop");

  /* Copy the VOL property into new state object */
  ret = H5VLretrieve_lib_state(&lib_state);
  VERIFY(ret, SUCCEED, "H5VLretrieve_lib_state");

  CHECK(lib_state, NULL, "H5VLretrieve_lib_state");

  ret = H5VLrestore_lib_state(lib_state);
  VERIFY(ret, SUCCEED, "H5VLrestore_lib_state");

  ret = H5VLfree_lib_state(lib_state);
  VERIFY(ret, SUCCEED, "H5VLfree_lib_state");

  ret = H5VLfinish_lib_state();
  VERIFY(ret, SUCCEED, "H5VLfinish_lib_state");

  return;
}

/* Retrieve and free the VOL wrap context in multiple threads executing in parallel.
 *
 * TBD: This largsely depends on the get_wrap_ctx()/free_wrap_ctx() callbacks of the active connector(s), and
 * so should probably have a counterpart placed in the API tests for use with various VOL connectors. */
void mt_test_vol_wrap_ctx(const void *args) {
  hid_t file_id = H5I_INVALID_HID;
  herr_t ret = SUCCEED;
  hid_t fapl_id = H5I_INVALID_HID;
  int max_num_threads = GetTestMaxNumThreads();

  H5VL_pass_through_info_t passthru_info = {H5VL_NATIVE, NULL};
  hid_t passthru_id = H5I_INVALID_HID;

  const mt_test_params *params = (const mt_test_params *) args;
  assert(params != NULL);
  alarm(params->subtest_timeout);

  if (max_num_threads <= 0) {
    printf("No threadcount specified with -maxthreads; skipping test\n");
    return;
  }
  /* Register the passthrough connector */
  passthru_id = H5VLregister_connector(&H5VL_pass_through_g, H5P_DEFAULT);
  CHECK(passthru_id, H5I_INVALID_HID, "H5VLregister_connector");

  fapl_id = H5Pcreate(H5P_FILE_ACCESS);
  CHECK(fapl_id, H5I_INVALID_HID, "H5Pcreate");

  /* To avoid dealing with concurrent registration in this test, we register the VOL
   * a single time and pass a shared FAPL to the helper threads. 
   * To comply with the API, the ref count of the FAPL must be incremented
   * for each new thread it will be passed to. */
  for (int i = 0; i < max_num_threads; i++) {
    ret = H5Iinc_ref(fapl_id);
    VERIFY(ret, i + 2, "H5Iinc_ref");
  }

  ret = H5Pset_vol(fapl_id, passthru_id, (const void*) &passthru_info);
  CHECK(ret, FAIL, "H5Pset_vol");

  /* File will be used by each helper thread */
  file_id = H5Fcreate(MT_TEST_VOL_WRAP_CTX_FILE_NAME, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id);
  CHECK(file_id, H5I_INVALID_HID, "H5Fcreate");

  mt_test_run_helper_in_parallel(mt_test_vol_wrap_ctx_helper, (void*) fapl_id);

  /* Clean up */
  for (int i = 0; i < max_num_threads; i++) {
    ret = H5Idec_ref(fapl_id);
    VERIFY(ret, max_num_threads - i, "H5Idec_ref");
  }

  ret = H5Fclose(file_id);
  CHECK(ret, FAIL, "H5Fclose");

  ret = H5Pclose(fapl_id);
  CHECK(ret, FAIL, "H5Pclose");

  ret = H5VLunregister_connector(passthru_id);
  CHECK(ret, FAIL, "H5VLunregister_connector");

  return;
}

void *mt_test_vol_wrap_ctx_helper(void H5_ATTR_UNUSED *args) {
  void *wrap_ctx = NULL;
  H5VL_object_t *vol_object = NULL;

  hid_t fapl_id = H5I_INVALID_HID;
  hid_t file_id = H5I_INVALID_HID;
  hid_t vol_id = H5I_INVALID_HID;

  herr_t ret = SUCCEED;

  assert(args != NULL);
  fapl_id = (hid_t) args;

  /* Open a VOL object to retrieve the context from */
  file_id = H5Fopen(MT_TEST_VOL_WRAP_CTX_FILE_NAME, H5F_ACC_RDONLY, fapl_id);
  CHECK(file_id, H5I_INVALID_HID, "H5Fopen");
  
  vol_object = (H5VL_object_t*) H5I_object_verify(file_id, H5I_FILE);
  CHECK(vol_object, NULL, "H5I_object_verify");
  CHECK(vol_object->data, NULL, "H5I_object_verify");

  /* Retrieve ID of VOL connector */
  ret = H5Pget_vol_id(fapl_id, &vol_id);
  CHECK(ret, FAIL, "H5Pget_vol_id");

  /* Retrieve & subsequently free VOL wrap context */
  ret = H5VLget_wrap_ctx((void*) (vol_object->data), vol_id, &wrap_ctx);
  CHECK(ret, FAIL, "H5VLget_wrap_ctx");

  CHECK(wrap_ctx, NULL, "H5VLget_wrap_ctx");

  ret = H5VLfree_wrap_ctx(wrap_ctx, vol_id);
  CHECK(ret, FAIL, "H5VLfree_wrap_ctx");


  return NULL;
}

void mt_test_vol_wrap_ctx_cleanup(void H5_ATTR_UNUSED *args) {
#ifdef H5_MT_TEST_VOL_DIR
  herr_t ret = SUCCEED;
  if (GetTestMaxNumThreads() > 0) {
    ret = H5Fdelete(MT_TEST_VOL_WRAP_CTX_FILE_NAME, H5P_DEFAULT);
    CHECK(ret, FAIL, "H5Fdelete");
  }
#endif
  return;
}

/* Retrieve and free the VOL information in multiple threads executing in parallel.
 *
 * TBD: This largsely depends on the connector callbacks of the active connector(s), and
 * so should probably have a counterpart placed in the API tests for use with various VOL connectors. */
void mt_test_vol_info(const void H5_ATTR_UNUSED *args) {
  H5VL_pass_through_info_t vol_info = {H5VL_NATIVE, NULL};
  void *vol_info2 = NULL;
  hid_t vol_id = H5I_INVALID_HID;
  hid_t fapl_id = H5I_INVALID_HID;
  hid_t fapl_id2 = H5I_INVALID_HID;
  hid_t fapl_id3 = H5I_INVALID_HID;
  herr_t ret = SUCCEED;

  /* Use Passthrough connector, since it has a non-NULL information field */
  vol_id = H5VLregister_connector(&H5VL_pass_through_g, H5P_DEFAULT);
  CHECK(vol_id, H5I_INVALID_HID, "H5VLregister_connector");
  
  fapl_id = H5Pcreate(H5P_FILE_ACCESS);
  CHECK(fapl_id, H5I_INVALID_HID, "H5Pcreate");
  
  /* Directly copy information */
  ret = H5VLcopy_connector_info(vol_id, &vol_info2, &vol_info);
  CHECK(ret, FAIL, "H5VLcopy_connector_info");
  
  /* Copy information into property list */
  ret = H5Pset_vol(fapl_id, vol_id, &vol_info);
  CHECK(ret, FAIL, "H5Pset_vol");
  
  /* Copy info via copying entire property list */
  fapl_id2 = H5Pcopy(fapl_id);
  CHECK(fapl_id2, H5I_INVALID_HID, "H5Pcopy");

  /* Copy info via copying single property between lists */
  fapl_id3 = H5Pcreate(H5P_FILE_ACCESS);
  CHECK(fapl_id3, H5I_INVALID_HID, "H5Pcreate");

  ret = H5Pcopy_prop(fapl_id3, fapl_id, H5F_ACS_VOL_CONN_NAME);
  CHECK(ret, FAIL, "H5Pcopy_prop");

  /* Free information on each property list via close */
  ret = H5Pclose(fapl_id);
  CHECK(ret, FAIL, "H5Pclose");

  ret = H5Pclose(fapl_id2);
  CHECK(ret, FAIL, "H5Pclose");

  ret = H5Pclose(fapl_id3);
  CHECK(ret, FAIL, "H5Pclose");

  /* Free directly copied information */
  ret = H5VLfree_connector_info(vol_id, vol_info2);
  CHECK(ret, FAIL, "H5VLfree_connector_info");

  ret = H5VLunregister_connector(vol_id);
  CHECK(ret, FAIL, "H5VLunregister_connector");

  return;
}

#endif /* H5_HAVE_MULTITHREAD */