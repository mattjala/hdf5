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
 * Purpose:	The public header file for the pass-through VOL connector.
 */

#ifndef H5VL_mt_test_passthru_H
#define H5VL_mt_test_passthru_H

/* Public headers needed by this file */
#include "H5VLpublic.h" /* Virtual Object Layer                 */

/* Characteristics of the multi-thread pass-through VOL connector */
#define MT_TEST_PASSTHRU_NAME "mt_test_passthru_vol_connector"
#define MT_TEST_PASSTHRU_VALUE ((H5VL_class_value_t)163)

/* Pass-through VOL connector info */
typedef struct mt_test_pass_through_info_t {
    hid_t under_vol_id;   /* VOL ID for under VOL */
    void *under_vol_info; /* VOL info for under VOL */
} mt_test_pass_through_info_t;

H5_DLL hid_t mt_test_pass_through_register(void);

#endif /* H5VL_mt_test_passthru_H */

