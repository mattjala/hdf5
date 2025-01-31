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

#ifndef H5_API_TEST_UTIL_H_
#define H5_API_TEST_UTIL_H_

#include "hdf5.h"

hid_t  generate_random_datatype(H5T_class_t parent_class, bool is_compact);
hid_t  generate_random_dataspace(int rank, const hsize_t *max_dims, hsize_t *dims_out, bool is_compact);

void H5_api_test_setup_container(void H5_ATTR_UNUSED *params);
#endif /* H5_API_TEST_UTIL_H_ */
