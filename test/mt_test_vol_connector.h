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
 * Purpose: The header file for the "multi-thread test" VOL connector,
 *           which duplicates HDF5 operations across multiple threads
 *           to test the multi-thread safety of the H5VL module.
 */

#ifndef H5VL_mt_test_H
#define H5VL_mt_test_H

#define MT_TEST_VOL_CONNECTOR_VALUE ((H5VL_class_value_t)162)
#define MT_TEST_VOL_CONNECTOR_NAME "mt_test_vol_connector"

#endif /* H5VL_mt_test_H */