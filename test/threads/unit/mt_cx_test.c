#include "H5CXprivate.h"
#include "H5Iprivate.h"
#include "H5VLint.c"

#include "../testmthdf5.h"
#include "h5test.h"

#include "mt_test_util.h"
#include "mt_passthru_wrapper_vol_connector.h"
#include "H5VLpassthru_private.h"

#define EFILE_PREFIX_1 "EFILE_PREFIX_1"
#define EFILE_PREFIX_2 "new_prefix"

#define MT_TEST_API_CTX_VOL_WRAP_CTX_FILENAME "mt_test_api_ctx_vol_wrap_ctx.h5"

#if H5_HAVE_MULTITHREAD

#include <pthread.h>

void *mt_test_api_ctx_vol_wrap_ctx_helper(void *arg);
void *mt_test_shared_plist_modify_helper(void *arg);

/* Test that the API Context's handling of the VOL wrap context is safe when executing in parallel
 */
void mt_test_api_ctx_vol_wrap_ctx(void) {
#ifndef H5_MT_TEST_VOL_DIR
    printf("Skipping test because H5_MT_TEST_VOL_DIR is not defined\n");
    return;
#else
    H5VL_wrap_ctx_t *wrap_ctx = NULL; /* Generic H5VL-controlled wrap context object */
    H5VL_pass_through_wrap_ctx_t passthru_wrap_ctx_expected = {H5VL_NATIVE, NULL}; /* MT Passthru-controlled VOL wrap context */

    /* MT Passthru -> Native VOL */
    H5VL_pass_through_info_t mt_passthru_info = {H5VL_NATIVE, NULL};
    H5VL_object_t *vol_object = NULL;

    hid_t fapl_id = H5I_INVALID_HID;
    hid_t file_id = H5I_INVALID_HID;
    hid_t mt_passthru_id = H5I_INVALID_HID;

    herr_t ret = SUCCEED;
    int res = 0;

    if (GetTestMaxNumThreads() <= 0) {
        printf("    No threadcount specified with -maxthreads; skipping test\n");
        return;
    }

    /* Allow MT Passthru VOL to be discovered by name */
    ret = H5PLprepend(H5_MT_TEST_VOL_DIR);
    CHECK(ret, FAIL, "H5PLprepend");

    /* Register connector */
    mt_passthru_id = H5VLregister_connector_by_name(MT_PASSTHRU_WRAPPER_NAME, H5P_DEFAULT);
    CHECK(mt_passthru_id, H5I_INVALID_HID, "H5VLregister_connector");

    fapl_id = H5Pcreate(H5P_FILE_ACCESS);
    CHECK(fapl_id, H5I_INVALID_HID, "H5Pcreate");

    /* Set up VOL connector stack */
    ret = H5Pset_vol(fapl_id, mt_passthru_id, &mt_passthru_info);
    CHECK(ret, FAIL, "H5Pset_vol");

    /* Get a VOL object to retrieve the VOL wrap context from */
    file_id = H5Fcreate(MT_TEST_API_CTX_VOL_WRAP_CTX_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, fapl_id);
    CHECK(file_id, H5I_INVALID_HID, "H5Fopen");

    vol_object = (H5VL_object_t*) H5I_object_verify(file_id, H5I_FILE);
    CHECK(vol_object, NULL, "H5I_object_verify");
    CHECK(vol_object->data, NULL, "H5I_object_verify");

    /* Retrieve VOL wrap context for VOL object */
    ret = H5VL__new_vol_wrapper((const H5VL_object_t *) vol_object, &wrap_ctx);
    CHECK(ret, FAIL, "H5VL__new_vol_wrapper");
    VERIFY(H5_ATOMIC_LOAD(wrap_ctx->rc), 1, "H5VLget_wrap_ctx");
    
    res = memcmp(wrap_ctx->obj_wrap_ctx, &passthru_wrap_ctx_expected, sizeof(H5VL_pass_through_wrap_ctx_t));
    VERIFY(res, 0, "H5VL__new_vol_wrapper");

    mt_test_run_helper_in_parallel(mt_test_api_ctx_vol_wrap_ctx_helper, (void *)wrap_ctx);

    /* VOL Wrap Context should still be valid */
    VERIFY(H5_ATOMIC_LOAD(wrap_ctx->rc), 1, "H5VLget_wrap_ctx");
    res = memcmp(wrap_ctx->obj_wrap_ctx, &passthru_wrap_ctx_expected, sizeof(H5VL_pass_through_wrap_ctx_t));
    VERIFY(res, 0, "H5VLget_wrap_ctx");

    ret = H5VL_dec_vol_wrapper(wrap_ctx);
    CHECK(ret, FAIL, "H5VLfree_wrap_ctx");

    ret = H5VLunregister_connector(mt_passthru_id);
    CHECK(ret, FAIL, "H5VLunregister_connector");

    ret = H5Pclose(fapl_id);
    CHECK(ret, FAIL, "H5Pclose");

    ret = H5Fclose(file_id);
    CHECK(ret, FAIL, "H5Fclose");

    return;
#endif    
}

void *mt_test_api_ctx_vol_wrap_ctx_helper(void *arg) {
    H5VL_pass_through_wrap_ctx_t wrap_ctx_expected = {H5VL_NATIVE, NULL};
    H5VL_wrap_ctx_t *wrap_ctx = (H5VL_wrap_ctx_t *)arg;
    H5CX_state_t *api_state = NULL;
    herr_t ret = SUCCEED;
    int res = 0;

    /* Create context node to store VOL Wrap Context */
    ret = H5CX_push();
    CHECK(ret, FAIL, "H5CX_push");

    ret = H5CX_set_vol_wrap_ctx(wrap_ctx);
    CHECK(ret, FAIL, "H5CX_set_vol_wrap_ctx");

    ret = H5CX_retrieve_state(&api_state);
    CHECK(ret, FAIL, "H5CX_retrieve_state");
    CHECK(api_state, NULL, "H5CX_retrieve_state");

    ret = H5CX_restore_state(api_state);
    CHECK(ret, FAIL, "H5CX_restore_state");

    ret = H5CX_free_state(api_state);
    CHECK(ret, FAIL, "H5CX_free_state");

    /* VOL Wrap context should still be valid due to reference in main thread */
    ret = H5CX_get_vol_wrap_ctx((void **)&wrap_ctx);
    CHECK(ret, FAIL, "H5CX_get_vol_wrap_ctx");
    CHECK(H5_ATOMIC_LOAD(wrap_ctx->rc), 0, "H5CX_get_vol_wrap_ctx");
    
    res = memcmp(wrap_ctx->obj_wrap_ctx, &wrap_ctx_expected, sizeof(H5VL_pass_through_wrap_ctx_t));
    VERIFY(res, 0, "H5CX_get_vol_wrap_ctx");

    /* Retrieve, restore, and free API Context state */
    ret = H5CX_pop(FALSE);
    CHECK(ret, FAIL, "H5CX_pop");

    return NULL;
}

void mt_test_api_ctx_vol_wrap_ctx_cleanup(void) {
#ifdef H5_MT_TEST_VOL_DIR
    herr_t ret = SUCCEED;

    if (GetTestMaxNumThreads() > 0) {
        ret = H5Fdelete(MT_TEST_API_CTX_VOL_WRAP_CTX_FILENAME, H5P_DEFAULT);
        CHECK(ret, FAIL, "H5Fdelete");
    }
#endif
    return;
}

/* ! This test will fail with an unsafe read until H5P is internally multi-thread safe !
 * Test that concurrent operations on the property value under the context are memory-safe.
 * This is invalid API usage, so don't expect any specific order of operations or final value.
 */
void
mt_test_shared_plist_modify(void)
{
#ifdef H5P_MT_REWORK
    hid_t  dapl_id     = H5I_INVALID_HID;
    herr_t ret         = SUCCEED;
    int    num_threads = 0;

    num_threads = GetTestMaxNumThreads();

    if (num_threads == 0) {
        printf("    No threadcount specified with -maxthreads; skipping test\n");
        return;
    }

    dapl_id = H5Pcreate(H5P_DATASET_ACCESS);
    CHECK(dapl_id, H5I_INVALID_HID, "H5Pcreate");

    /* Increase ref count of plist for each thread it will be provided to */
    for (int i = 0; i < num_threads; i++) {
        ret = H5Iinc_ref(dapl_id);
        CHECK(ret, FAIL, "H5Iinc_ref");
    }

    /* Use external file prefix in order to test against
     * a property that has dynamic memory allocation */
    ret = H5Pset_efile_prefix(dapl_id, EFILE_PREFIX_1);
    CHECK(ret, FAIL, "H5Pset_efile_prefix");

    mt_test_run_helper_in_parallel(mt_test_shared_plist_modify_helper, (void *)&dapl_id);

#else  /* H5P_MT_REWORK */
    printf("    H5P is not internally multi-thread safe. Skipping test.\n");
#endif /* H5P_MT_REWORK */
    return;
}

void *
mt_test_shared_plist_modify_helper(void *arg)
{
    hid_t       dapl_id            = H5I_INVALID_HID;
    herr_t      ret                = SUCCEED;
    const char *efile_prefix       = NULL;
    char       *saved_efile_prefix = NULL;

    assert(arg);

    dapl_id = *(hid_t *)arg;

    /* Create context node that will persist through operations */
    ret = H5VL_start_lib_state();
    CHECK(ret, FAIL, "H5VL_start_lib_state");

    /* Set the DAPL on the persistent context node */
    ret = H5CX_set_apl(&dapl_id, H5P_CLS_DACC, H5I_INVALID_HID, FALSE);
    CHECK(ret, FAIL, "H5CX_set_apl");

    ret = H5CX_get_ext_file_prefix(&efile_prefix);
    CHECK(ret, FAIL, "H5CX_get_ext_file_prefix");

    /* Save for later comparison. */
    saved_efile_prefix = strdup(efile_prefix);

    if (strcmp(efile_prefix, EFILE_PREFIX_1) != 0 && strcmp(efile_prefix, EFILE_PREFIX_2) != 0) {
        TestErrPrintf("    Unexpected value for efile prefix\n");
    }

    ret = H5Pset_efile_prefix(dapl_id, EFILE_PREFIX_2);
    CHECK(ret, FAIL, "H5Pset_efile_prefix");

    /* Retrieve value.
     * With multi-threaded H5P, this access should be thread-safe and match the previous value. */
    ret = H5CX_get_ext_file_prefix(&efile_prefix);
    CHECK(ret, FAIL, "H5CX_get_ext_file_prefix");

    VERIFY(strcmp(efile_prefix, saved_efile_prefix), 0, "H5CX_get_ext_file_prefix");

    /* Clean up */

    ret = H5VL_finish_lib_state();
    CHECK(ret, FAIL, "H5VL_finish_lib_state");

    free(saved_efile_prefix);

    return NULL;
}

#endif /* H5_HAVE_MULTITHREAD */