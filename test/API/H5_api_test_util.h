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

#include "testframe.h"

hid_t  generate_random_datatype(H5T_class_t parent_class, bool is_compact);
hid_t  generate_random_dataspace(int rank, const hsize_t *max_dims, hsize_t *dims_out, bool is_compact);

herr_t prefix_test_filename(TestParams_t *test_params, const char *prefix, const char *filename,
                            char **filename_out);
herr_t remove_test_file(const char *filename, hid_t fapl_id);

int H5_api_test_create_containers(char **filenames, size_t num_filenames, uint64_t vol_cap_flags);
int H5_api_test_destroy_container_files(char **filenames, size_t num_filenames, hid_t fapl_id);

#endif /* H5_API_TEST_UTIL_H_ */
