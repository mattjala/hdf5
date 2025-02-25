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

#include "H5_api_link_test_parallel.h"

#ifdef NOT_YET
static herr_t print_link_test_header(TestParams_t *params);

static herr_t
print_link_test_header(TestParams_t H5_ATTR_UNUSED *params)
{
    if (MAINPROCESS) {
        printf("\n");
        printf("**********************************************\n");
        printf("*                                            *\n");
        printf("*          API Parallel Link Tests           *\n");
        printf("*                                            *\n");
        printf("**********************************************\n\n");
    }

    return SUCCEED;
}
#endif

void
H5_api_link_test_parallel_add(void)
{
    /* No tests yet */
    return;
}
