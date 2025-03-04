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

/* Purpose: Tests for API Context behavior in multi-threaded environments.
 *          While API Context functionality is implicitly validated through
 *          other multi-threaded tests, this file contains
 *          unit tests targeting for specific uncommon
 *          usage patterns.
 */


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

void *mt_test_api_ctx_vol_conn_prop_helper(void *args);
void *mt_test_api_ctx_vol_wrap_ctx_helper(void *arg);

/* Test that the API Context's handling of the VOL Connector property is safe when executing in parallel
 *
 * Specifically, verify that the API Context State routines properly deep
 * copy the VOL Connector property and leave the original value unmodified.
*/
herr_t mt_test_api_ctx_vol_conn_prop(TestParams_t H5_ATTR_UNUSED *args) {
#ifndef H5_MT_TEST_VOL_DIR
    printf("Skipping test because H5_MT_TEST_VOL_DIR is not defined\n");
    return SUCCEED;
#else
    H5VL_pass_through_info_t mt_passthru_info_expected = {H5VL_NATIVE, NULL};
    H5VL_connector_prop_t vol_connector_prop = {0, &mt_passthru_info_expected};
    hid_t mt_passthru_id = H5I_INVALID_HID;
    herr_t ret = SUCCEED;
    int res = 0;

    if (GetTestMaxNumThreads() <= 0) {
        printf("    No threadcount specified with -maxthreads; skipping test\n");
        return SUCCEED;
    }

    /* Use the MT Native VOL Wrapper in order to 
     * test the case where multi-threading proceeds into a connector
     */

    /* Allow MT Passthru VOL to be discovered by name */
    ret = H5PLprepend(H5_MT_TEST_VOL_DIR);
    CHECK(ret, FAIL, "H5PLprepend");

    /* Register connector */
    mt_passthru_id = H5VLregister_connector_by_name(MT_PASSTHRU_WRAPPER_NAME, H5P_DEFAULT);
    CHECK(mt_passthru_id, H5I_INVALID_HID, "H5VLregister_connector");

    vol_connector_prop.connector_id = mt_passthru_id;

    mt_test_run_helper_in_parallel(mt_test_api_ctx_vol_conn_prop_helper, (void *)&vol_connector_prop);

    /* Verify that the original VOL connector property buffer still exists with correct values */
    VERIFY(vol_connector_prop.connector_id, mt_passthru_id, "H5CX");
    res = memcmp(vol_connector_prop.connector_info, &mt_passthru_info_expected, sizeof(H5VL_pass_through_info_t));
    VERIFY(res, 0, "H5CX");

    /* Unregister connector */
    ret = H5VLunregister_connector(mt_passthru_id);
    CHECK(ret, FAIL, "H5VLunregister_connector");
#endif

    return SUCCEED;
}

void *mt_test_api_ctx_vol_conn_prop_helper(void *args) {
    H5VL_connector_prop_t *vol_connector_prop_in = (H5VL_connector_prop_t *)args;
    H5VL_connector_prop_t vol_connector_prop_out = {0, NULL};
    H5VL_pass_through_info_t mt_passthru_info_expected = {H5VL_NATIVE, NULL};
    H5VL_connector_prop_t vol_connector_prop_expected = {vol_connector_prop_in->connector_id,
                                                         (void*) &mt_passthru_info_expected};
    H5CX_state_t *api_state = NULL;
    herr_t ret = SUCCEED;

    /* Create context node to store VOL Connector Property */
    ret = H5CX_push();
    CHECK(ret, FAIL, "H5CX_push");

    ret = H5CX_set_vol_connector_prop(vol_connector_prop_in);
    CHECK(ret, FAIL, "H5CX_set_vol_connector_prop");

    ret = H5CX_retrieve_state(&api_state);
    CHECK(ret, FAIL, "H5CX_retrieve_state");
    CHECK(api_state, NULL, "H5CX_retrieve_state");

    ret = H5CX_free_state(api_state);
    CHECK(ret, FAIL, "H5CX_free_state");

    /* Check that the VOL Connector Property is still valid */
    ret = H5CX_get_vol_connector_prop(&vol_connector_prop_out);
    CHECK(ret, FAIL, "H5CX_get_vol_connector_prop");
    VERIFY(vol_connector_prop_out.connector_id, vol_connector_prop_expected.connector_id, "H5CX_get_vol_connector_prop");

    ret = memcmp(vol_connector_prop_out.connector_info, vol_connector_prop_expected.connector_info, sizeof(H5VL_pass_through_info_t));
    VERIFY(ret, 0, "memcmp");

    ret = H5CX_pop(false);
    CHECK(ret, FAIL, "H5CX_pop");

    return NULL;
}

/* Test that the API Context's handling of the VOL wrap context is safe when executing in parallel
 *
 * Specifically, verify that the API Context State routines properly deep
 * copy the VOL wrap context and leave the original value unmodified.
 */
herr_t mt_test_api_ctx_vol_wrap_ctx(TestParams_t H5_ATTR_UNUSED *args) {
#ifndef H5_MT_TEST_VOL_DIR
    printf("Skipping test because H5_MT_TEST_VOL_DIR is not defined\n");
    return SUCCEED;
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
        return SUCCEED;
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

    return SUCCEED;
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

herr_t mt_test_api_ctx_vol_wrap_ctx_cleanup(TestParams_t  H5_ATTR_UNUSED *args) {
#ifdef H5_MT_TEST_VOL_DIR
    herr_t ret = SUCCEED;

    if (GetTestMaxNumThreads() > 0) {
        ret = H5Fdelete(MT_TEST_API_CTX_VOL_WRAP_CTX_FILENAME, H5P_DEFAULT);
        CHECK(ret, FAIL, "H5Fdelete");
    }
#endif
    return SUCCEED;
}

#endif /* H5_HAVE_MULTITHREAD */