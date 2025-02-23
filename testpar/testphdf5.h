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

/* common definitions used by all parallel hdf5 test programs. */

#ifndef PHDF5TEST_H
#define PHDF5TEST_H

/* Include testing framework functionality */
#include "testframe.h"

#include "testpar.h"

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/* Constants definitions */
#define DIM0         600  /* Default dataset sizes. */
#define DIM1         1200 /* Values are from a monitor pixel sizes */
#define ROW_FACTOR   8    /* Nominal row factor for dataset size */
#define COL_FACTOR   16   /* Nominal column factor for dataset size */
#define RANK         2
#define DATASETNAME1 "Data1"
#define DATASETNAME2 "Data2"
#define DATASETNAME3 "Data3"
#define DATASETNAME4 "Data4"
#define DATASETNAME5 "Data5"
#define DATASETNAME6 "Data6"
#define DATASETNAME7 "Data7"
#define DATASETNAME8 "Data8"
#define DATASETNAME9 "Data9"

/*Constants for collective chunk definitions */
#define SPACE_DIM1            24
#define SPACE_DIM2            4
#define BYROW_CONT            1
#define BYROW_DISCONT         2
#define BYROW_SELECTNONE      3
#define BYROW_SELECTUNBALANCE 4
#define BYROW_SELECTINCHUNK   5

#define DIMO_NUM_CHUNK        4
#define DIM1_NUM_CHUNK        2
#define LINK_TRUE_NUM_CHUNK   2
#define LINK_FALSE_NUM_CHUNK  6
#define MULTI_TRUE_PERCENT    50
#define LINK_TRUE_CHUNK_NAME  "h5_link_chunk_true"
#define LINK_FALSE_CHUNK_NAME "h5_link_chunk_false"
#define LINK_HARD_CHUNK_NAME  "h5_link_chunk_hard"
#define MULTI_HARD_CHUNK_NAME "h5_multi_chunk_hard"
#define MULTI_COLL_CHUNK_NAME "h5_multi_chunk_coll"
#define MULTI_INDP_CHUNK_NAME "h5_multi_chunk_indp"

#define DSET_COLLECTIVE_CHUNK_NAME "coll_chunk_name"

/*Constants for MPI derived data type generated from span tree */

#define MSPACE1_RANK 1     /* Rank of the first dataset in memory */
#define MSPACE1_DIM  27000 /* Dataset size in memory */
#define FSPACE_RANK  2     /* Dataset rank as it is stored in the file */
#define FSPACE_DIM1  9     /* Dimension sizes of the dataset as it is stored in the file */
#define FSPACE_DIM2  3600
/* We will read dataset back from the file to the dataset in memory with these dataspace parameters. */
#define MSPACE_RANK 2
#define MSPACE_DIM1 9
#define MSPACE_DIM2 3600
#define FHCOUNT0    1   /* Count of the first dimension of the first hyperslab selection*/
#define FHCOUNT1    768 /* Count of the second dimension of the first hyperslab selection*/
#define FHSTRIDE0   4   /* Stride of the first dimension of the first hyperslab selection*/
#define FHSTRIDE1   3   /* Stride of the second dimension of the first hyperslab selection*/
#define FHBLOCK0    3   /* Block of the first dimension of the first hyperslab selection*/
#define FHBLOCK1    2   /* Block of the second dimension of the first hyperslab selection*/
#define FHSTART0    0   /* start of the first dimension of the first hyperslab selection*/
#define FHSTART1    1   /* start of the second dimension of the first hyperslab selection*/

#define SHCOUNT0  1   /* Count of the first dimension of the first hyperslab selection*/
#define SHCOUNT1  1   /* Count of the second dimension of the first hyperslab selection*/
#define SHSTRIDE0 1   /* Stride of the first dimension of the first hyperslab selection*/
#define SHSTRIDE1 1   /* Stride of the second dimension of the first hyperslab selection*/
#define SHBLOCK0  3   /* Block of the first dimension of the first hyperslab selection*/
#define SHBLOCK1  768 /* Block of the second dimension of the first hyperslab selection*/
#define SHSTART0  4   /* start of the first dimension of the first hyperslab selection*/
#define SHSTART1  0   /* start of the second dimension of the first hyperslab selection*/

#define MHCOUNT0  6912 /* Count of the first dimension of the first hyperslab selection*/
#define MHSTRIDE0 1    /* Stride of the first dimension of the first hyperslab selection*/
#define MHBLOCK0  1    /* Block of the first dimension of the first hyperslab selection*/
#define MHSTART0  1    /* start of the first dimension of the first hyperslab selection*/

#define RFFHCOUNT0  3   /* Count of the first dimension of the first hyperslab selection*/
#define RFFHCOUNT1  768 /* Count of the second dimension of the first hyperslab selection*/
#define RFFHSTRIDE0 1   /* Stride of the first dimension of the first hyperslab selection*/
#define RFFHSTRIDE1 1   /* Stride of the second dimension of the first hyperslab selection*/
#define RFFHBLOCK0  1   /* Block of the first dimension of the first hyperslab selection*/
#define RFFHBLOCK1  1   /* Block of the second dimension of the first hyperslab selection*/
#define RFFHSTART0  1   /* start of the first dimension of the first hyperslab selection*/
#define RFFHSTART1  2   /* start of the second dimension of the first hyperslab selection*/

#define RFSHCOUNT0  3    /* Count of the first dimension of the first hyperslab selection*/
#define RFSHCOUNT1  1536 /* Count of the second dimension of the first hyperslab selection*/
#define RFSHSTRIDE0 1    /* Stride of the first dimension of the first hyperslab selection*/
#define RFSHSTRIDE1 1    /* Stride of the second dimension of the first hyperslab selection*/
#define RFSHBLOCK0  1    /* Block of the first dimension of the first hyperslab selection*/
#define RFSHBLOCK1  1    /* Block of the second dimension of the first hyperslab selection*/
#define RFSHSTART0  2    /* start of the first dimension of the first hyperslab selection*/
#define RFSHSTART1  4    /* start of the second dimension of the first hyperslab selection*/

#define RMFHCOUNT0  3   /* Count of the first dimension of the first hyperslab selection*/
#define RMFHCOUNT1  768 /* Count of the second dimension of the first hyperslab selection*/
#define RMFHSTRIDE0 1   /* Stride of the first dimension of the first hyperslab selection*/
#define RMFHSTRIDE1 1   /* Stride of the second dimension of the first hyperslab selection*/
#define RMFHBLOCK0  1   /* Block of the first dimension of the first hyperslab selection*/
#define RMFHBLOCK1  1   /* Block of the second dimension of the first hyperslab selection*/
#define RMFHSTART0  0   /* start of the first dimension of the first hyperslab selection*/
#define RMFHSTART1  0   /* start of the second dimension of the first hyperslab selection*/

#define RMSHCOUNT0  3    /* Count of the first dimension of the first hyperslab selection*/
#define RMSHCOUNT1  1536 /* Count of the second dimension of the first hyperslab selection*/
#define RMSHSTRIDE0 1    /* Stride of the first dimension of the first hyperslab selection*/
#define RMSHSTRIDE1 1    /* Stride of the second dimension of the first hyperslab selection*/
#define RMSHBLOCK0  1    /* Block of the first dimension of the first hyperslab selection*/
#define RMSHBLOCK1  1    /* Block of the second dimension of the first hyperslab selection*/
#define RMSHSTART0  1    /* start of the first dimension of the first hyperslab selection*/
#define RMSHSTART1  2    /* start of the second dimension of the first hyperslab selection*/

#define NPOINTS                                                                                              \
    4 /* Number of points that will be selected                                                              \
                                  and overwritten */

/* Definitions of the selection mode for the test_actual_io_function. */
#define TEST_ACTUAL_IO_NO_COLLECTIVE            0
#define TEST_ACTUAL_IO_RESET                    1
#define TEST_ACTUAL_IO_MULTI_CHUNK_IND          2
#define TEST_ACTUAL_IO_MULTI_CHUNK_COL          3
#define TEST_ACTUAL_IO_MULTI_CHUNK_MIX          4
#define TEST_ACTUAL_IO_MULTI_CHUNK_MIX_DISAGREE 5
#define TEST_ACTUAL_IO_DIRECT_MULTI_CHUNK_IND   6
#define TEST_ACTUAL_IO_DIRECT_MULTI_CHUNK_COL   7
#define TEST_ACTUAL_IO_LINK_CHUNK               8
#define TEST_ACTUAL_IO_CONTIGUOUS               9

/* Definitions of the selection mode for the no_collective_cause_tests function. */
#define TEST_COLLECTIVE                                 0x001
#define TEST_SET_INDEPENDENT                            0x002
#define TEST_DATATYPE_CONVERSION                        0x004
#define TEST_DATA_TRANSFORMS                            0x008
#define TEST_NOT_SIMPLE_OR_SCALAR_DATASPACES            0x010
#define TEST_NOT_CONTIGUOUS_OR_CHUNKED_DATASET_COMPACT  0x020
#define TEST_NOT_CONTIGUOUS_OR_CHUNKED_DATASET_EXTERNAL 0x040

/* Don't erase these lines, they are put here for debugging purposes */
/*
#define MSPACE1_RANK     1
#define MSPACE1_DIM      50
#define MSPACE2_RANK     1
#define MSPACE2_DIM      4
#define FSPACE_RANK      2
#define FSPACE_DIM1      8
#define FSPACE_DIM2      12
#define MSPACE_RANK      2
#define MSPACE_DIM1      8
#define MSPACE_DIM2      9
#define NPOINTS          4
*/ /* end of debugging macro */

/* type definitions */
typedef struct H5Ptest_param_t /* holds extra test parameters */
{
    char *name;
    int   count;
} H5Ptest_param_t;

/* Dataset data type.  Int's can be easily octo dumped. */
typedef int DATATYPE;

/* Shared global variables */
extern int dim0, dim1;           /*Dataset dimensions */
extern int chunkdim0, chunkdim1; /*Chunk dimensions */
extern int nerrors;              /*errors count */
extern int facc_type;            /*Test file access type */
extern int dxfer_coll_type;

/* Test program prototypes */
herr_t test_plist_ed(TestParams_t *params);
herr_t external_links(TestParams_t *params);
herr_t zero_dim_dset(TestParams_t *params);
herr_t test_file_properties(TestParams_t *params);
herr_t test_delete(TestParams_t *params);
herr_t multiple_dset_write(TestParams_t *params);
herr_t multiple_group_write(TestParams_t *params);
herr_t multiple_group_read(TestParams_t *params);
herr_t collective_group_write_independent_group_read(TestParams_t *params);
herr_t test_fapl_mpio_dup(TestParams_t *params);
herr_t test_split_comm_access(TestParams_t *params);
herr_t test_page_buffer_access(TestParams_t *params);
herr_t dataset_atomicity(TestParams_t *params);
herr_t dataset_writeInd(TestParams_t *params);
herr_t dataset_writeAll(TestParams_t *params);
herr_t extend_writeInd(TestParams_t *params);
herr_t extend_writeInd2(TestParams_t *params);
herr_t extend_writeAll(TestParams_t *params);
herr_t dataset_readInd(TestParams_t *params);
herr_t dataset_readAll(TestParams_t *params);
herr_t extend_readInd(TestParams_t *params);
herr_t extend_readAll(TestParams_t *params);
herr_t none_selection_chunk(TestParams_t *params);
herr_t actual_io_mode_tests(TestParams_t *params);
herr_t no_collective_cause_tests(TestParams_t *params);
herr_t test_chunk_alloc(TestParams_t *params);
herr_t test_filter_read(TestParams_t *params);
herr_t compact_dataset(TestParams_t *params);
herr_t null_dataset(TestParams_t *params);
herr_t big_dataset(TestParams_t *params);
herr_t dataset_fillvalue(TestParams_t *params);
herr_t coll_chunk1(TestParams_t *params);
herr_t coll_chunk2(TestParams_t *params);
herr_t coll_chunk3(TestParams_t *params);
herr_t coll_chunk4(TestParams_t *params);
herr_t coll_chunk5(TestParams_t *params);
herr_t coll_chunk6(TestParams_t *params);
herr_t coll_chunk7(TestParams_t *params);
herr_t coll_chunk8(TestParams_t *params);
herr_t coll_chunk9(TestParams_t *params);
herr_t coll_chunk10(TestParams_t *params);
herr_t coll_irregular_cont_read(TestParams_t *params);
herr_t coll_irregular_cont_write(TestParams_t *params);
herr_t coll_irregular_simple_chunk_read(TestParams_t *params);
herr_t coll_irregular_simple_chunk_write(TestParams_t *params);
herr_t coll_irregular_complex_chunk_read(TestParams_t *params);
herr_t coll_irregular_complex_chunk_write(TestParams_t *params);
herr_t io_mode_confusion(TestParams_t *params);
herr_t rr_obj_hdr_flush_confusion(TestParams_t *params);
herr_t chunk_align_bug_1(TestParams_t *params);
herr_t lower_dim_size_comp_test(TestParams_t *params);
herr_t link_chunk_collective_io_test(TestParams_t *params);
herr_t file_image_daisy_chain_test(TestParams_t *params);
#ifdef H5_HAVE_FILTER_DEFLATE
herr_t compress_readAll(TestParams_t *params);
#endif /* H5_HAVE_FILTER_DEFLATE */
herr_t test_dense_attr(TestParams_t *params);
herr_t test_partial_no_selection_coll_md_read(TestParams_t *params);
herr_t test_multi_chunk_io_addrmap_issue(TestParams_t *params);
herr_t test_link_chunk_io_sort_chunk_issue(TestParams_t *params);
herr_t test_collective_global_heap_write(TestParams_t *params);
herr_t test_oflush(TestParams_t *params);

/* commonly used prototypes */
MPI_Offset h5_mpi_get_file_size(const char *filename, MPI_Comm comm, MPI_Info info);
int dataset_vrfy(hsize_t start[], hsize_t count[], hsize_t stride[], hsize_t block[], DATATYPE *dataset,
                 DATATYPE *original);
#endif /* PHDF5TEST_H */
