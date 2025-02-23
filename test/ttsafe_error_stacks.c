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
#include "ttsafe.h"

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)

#define ERR_CLS_NAME        "Custom error class"
#define ERR_CLS_LIB_NAME    "example_lib"
#define ERR_CLS_LIB_VERSION "0.1"

#define ERR_MAJOR_MSG "Okay, Houston, we've had a problem here"
#define ERR_MINOR_MSG "Oops!"

void* generate_hdf5_error(void *arg);
void* generate_user_error(void *arg);

/* Helper routine to generate an HDF5 library error */
void*
generate_hdf5_error(void H5_ATTR_UNUSED *arg)
{
    void* ret_value = NULL;
    ssize_t           nobjs     = 0;

    H5E_BEGIN_TRY
    {
        nobjs = H5Fget_obj_count(H5I_INVALID_HID, H5F_OBJ_ALL);
    }
    H5E_END_TRY

    /* Expect call to fail */
    VERIFY(nobjs, FAIL, "H5Fget_obj_count");

    return ret_value;
}

/* Helper routine to generate a user-defined error */
void*
generate_user_error(void H5_ATTR_UNUSED *arg)
{
    void* ret_value = NULL;
    hid_t             cls       = H5I_INVALID_HID;
    hid_t             major     = H5I_INVALID_HID;
    hid_t             minor     = H5I_INVALID_HID;
    herr_t            status    = FAIL;

    cls = H5Eregister_class(ERR_CLS_NAME, ERR_CLS_LIB_NAME, ERR_CLS_LIB_VERSION);
    CHECK(cls, H5I_INVALID_HID, "H5Eregister_class");

    major = H5Ecreate_msg(cls, H5E_MAJOR, ERR_MAJOR_MSG);
    CHECK(major, H5I_INVALID_HID, "H5Ecreate_msg");

    minor = H5Ecreate_msg(cls, H5E_MINOR, ERR_MINOR_MSG);
    CHECK(minor, H5I_INVALID_HID, "H5Ecreate_msg");

    status = H5Epush2(H5E_DEFAULT, __FILE__, __func__, __LINE__, cls, major, minor, "Hello, error\n");
    CHECK(status, FAIL, "H5Epush2");

    ret_value = (void*)cls;

    return ret_value;
}

/*
**********************************************************************
* tts_errstk
*
* Test that error stacks with user-defined error classes and messages
* in secondary threads are properly cleaned up at library shutdown time.
**********************************************************************
*/
herr_t
tts_errstk(TestParams_t H5_ATTR_UNUSED *params)
{
    H5TS_thread_t threads[2];
    herr_t        status     = FAIL;
    hid_t         err_cls_id = H5I_INVALID_HID;

    /* Open library */
    H5open();

    threads[0] = H5TS_create_thread(generate_hdf5_error, NULL, NULL);

    status = H5TS_wait_for_thread(threads[0]);
    CHECK(status, FAIL, "H5TS_wait_for_thread");

    threads[1] = H5TS_create_thread(generate_user_error, NULL, NULL);

    status = H5TS_wait_for_thread_ret(threads[1], (void *)&err_cls_id);
    CHECK(status, FAIL, "H5TS_wait_for_thread");

    if (err_cls_id <= 0) {
        TestErrPrintf("Failed to set up user error\n");
        return FAIL;
    }

    status = H5Eunregister_class(err_cls_id);
    CHECK(status, FAIL, "H5Eunregister_class");

    /* Close library */
    H5close();

    return SUCCEED;
}

#endif
