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
 * This header file contains information required for testing the HDF5 library.
 */

#ifndef MTSAFE_H
#define MTSAFE_H

#include "testhdf5.h"

/* Prototypes for the test routines */
typedef struct mt_test_params {
    size_t num_repetitions;
    unsigned subtest_timeout;
} mt_test_params;

#ifdef H5_HAVE_MULTITHREAD
void mt_test_registration(const void *args);
void mt_test_registration_by_name(const void *args);
void mt_test_registration_by_value(const void *args);
void mt_test_dyn_op_registration(const void *args);
void mt_test_file_open_failure_registration(const void *args);
void mt_test_vol_info(const void *args);
void mt_test_vol_property_copy(const void *args);
void mt_test_lib_state_ops(const void *args);
void mt_test_register_and_search(const void *args);

void mt_test_registration_operation(const void *args);
void mt_test_registration_operation_cleanup(void *args);

void mt_test_vol_wrap_ctx(const void *args);
void mt_test_vol_wrap_ctx_cleanup(void *args);

void mt_test_library_init(const void *args);
#endif /* H5_HAVE_MULTITHREAD */
#endif /* MTSAFE_H */