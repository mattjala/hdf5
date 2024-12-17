#include "testhdf5.h"
#include "H5CXprivate.h"
#include "../testmthdf5.h"
#include "mt_test_util.h"

#define EFILE_PREFIX_1 "EFILE_PREFIX_1"
#define EFILE_PREFIX_2 "new_prefix"

#if H5_HAVE_MULTITHREAD

#include <pthread.h>

void *mt_test_shared_plist_modify_helper(void *arg);

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