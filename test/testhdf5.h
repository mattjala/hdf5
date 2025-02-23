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

#ifndef TESTHDF5_H
#define TESTHDF5_H

/* Include generic testing header */
#include "h5test.h"

/* Include testing framework functionality */
#include "testframe.h"

/* Use %ld to print the value because long should cover most cases. */
/* Used to make certain a return value _is_not_ a value */
#define CHECK(ret, val, where)                                                                               \
    do {                                                                                                     \
        if (VERBOSE_HI) {                                                                                    \
            printf("   Call to routine: %15s at line %4d "                                                   \
                   "in %s returned %ld \n",                                                                  \
                   where, (int)__LINE__, __FILE__, (long)(ret));                                             \
        }                                                                                                    \
        if ((ret) == (val)) {                                                                                \
            TestErrPrintf("*** UNEXPECTED RETURN from %s is %ld at line %4d "                                \
                          "in %s\n",                                                                         \
                          where, (long)(ret), (int)__LINE__, __FILE__);                                      \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

#define CHECK_I(ret, where)                                                                                  \
    do {                                                                                                     \
        if (VERBOSE_HI) {                                                                                    \
            printf("   Call to routine: %15s at line %4d in %s returned %ld\n", (where), (int)__LINE__,      \
                   __FILE__, (long)(ret));                                                                   \
        }                                                                                                    \
        if ((ret) < 0) {                                                                                     \
            TestErrPrintf("*** UNEXPECTED RETURN from %s is %ld line %4d in %s\n", (where), (long)(ret),     \
                          (int)__LINE__, __FILE__);                                                          \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

/* Check that a pointer is valid (i.e.: not NULL) */
#define CHECK_PTR(ret, where)                                                                                \
    do {                                                                                                     \
        if (VERBOSE_HI) {                                                                                    \
            printf("   Call to routine: %15s at line %4d in %s returned %p\n", (where), (int)__LINE__,       \
                   __FILE__, ((const void *)ret));                                                           \
        }                                                                                                    \
        if (!(ret)) {                                                                                        \
            TestErrPrintf("*** UNEXPECTED RETURN from %s is NULL line %4d in %s\n", (where), (int)__LINE__,  \
                          __FILE__);                                                                         \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

/* Check that a pointer is NULL */
#define CHECK_PTR_NULL(ret, where)                                                                           \
    do {                                                                                                     \
        if (VERBOSE_HI) {                                                                                    \
            printf("   Call to routine: %15s at line %4d in %s returned %p\n", (where), (int)__LINE__,       \
                   __FILE__, ((const void *)ret));                                                           \
        }                                                                                                    \
        if (ret) {                                                                                           \
            TestErrPrintf("*** UNEXPECTED RETURN from %s is not NULL line %4d in %s\n", (where),             \
                          (int)__LINE__, __FILE__);                                                          \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

/* Check that two pointers are equal */
#define CHECK_PTR_EQ(ret, val, where)                                                                        \
    do {                                                                                                     \
        if (VERBOSE_HI) {                                                                                    \
            printf("   Call to routine: %15s at line %4d in %s returned %p\n", (where), (int)__LINE__,       \
                   __FILE__, (const void *)(ret));                                                           \
        }                                                                                                    \
        if (ret != val) {                                                                                    \
            TestErrPrintf(                                                                                   \
                "*** UNEXPECTED RETURN from %s: returned value of %p is not equal to %p line %4d in %s\n",   \
                (where), (const void *)(ret), (const void *)(val), (int)__LINE__, __FILE__);                 \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

/* Used to make certain a return value _is_ a value */
#define VERIFY(_x, _val, where)                                                                              \
    do {                                                                                                     \
        long __x = (long)_x, __val = (long)_val;                                                             \
        if (VERBOSE_HI) {                                                                                    \
            printf("   Call to routine: %15s at line %4d in %s had value "                                   \
                   "%ld \n",                                                                                 \
                   (where), (int)__LINE__, __FILE__, __x);                                                   \
        }                                                                                                    \
        if ((__x) != (__val)) {                                                                              \
            TestErrPrintf("*** UNEXPECTED VALUE from %s should be %ld, but is %ld at line %4d "              \
                          "in %s\n",                                                                         \
                          (where), __val, __x, (int)__LINE__, __FILE__);                                     \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

/* Used to make certain a (non-'long' type's) return value _is_ a value */
#define VERIFY_TYPE(_x, _val, _type, _format, where)                                                         \
    do {                                                                                                     \
        _type __x = (_type)_x, __val = (_type)_val;                                                          \
        if (VERBOSE_HI) {                                                                                    \
            printf("   Call to routine: %15s at line %4d in %s had value " _format " \n", (where),           \
                   (int)__LINE__, __FILE__, __x);                                                            \
        }                                                                                                    \
        if ((__x) != (__val)) {                                                                              \
            TestErrPrintf("*** UNEXPECTED VALUE from %s should be " _format ", but is " _format              \
                          " at line %4d "                                                                    \
                          "in %s\n",                                                                         \
                          (where), __val, __x, (int)__LINE__, __FILE__);                                     \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

/* Used to make certain a string return value _is_ a value */
#define VERIFY_STR(x, val, where)                                                                            \
    do {                                                                                                     \
        if (VERBOSE_HI) {                                                                                    \
            printf("   Call to routine: %15s at line %4d in %s had value "                                   \
                   "%s \n",                                                                                  \
                   (where), (int)__LINE__, __FILE__, x);                                                     \
        }                                                                                                    \
        if (HDstrcmp(x, val) != 0) {                                                                         \
            TestErrPrintf("*** UNEXPECTED VALUE from %s should be %s, but is %s at line %4d "                \
                          "in %s\n",                                                                         \
                          where, val, x, (int)__LINE__, __FILE__);                                           \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

/* Used to document process through a test and to check for errors */
#define RESULT(ret, func)                                                                                    \
    do {                                                                                                     \
        if (VERBOSE_MED) {                                                                                   \
            printf("   Call to routine: %15s at line %4d in %s returned "                                    \
                   "%ld\n",                                                                                  \
                   func, (int)__LINE__, __FILE__, (long)(ret));                                              \
        }                                                                                                    \
        if (VERBOSE_HI)                                                                                      \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        if ((ret) == FAIL) {                                                                                 \
            TestErrPrintf("*** UNEXPECTED RETURN from %s is %ld at line %4d "                                \
                          "in %s\n",                                                                         \
                          func, (long)(ret), (int)__LINE__, __FILE__);                                       \
            H5Eprint2(H5E_DEFAULT, stdout);                                                                  \
        }                                                                                                    \
    } while (0)

/* Used to indicate an error that is complex to check for */
#define ERROR(where)                                                                                         \
    do {                                                                                                     \
        if (VERBOSE_HI)                                                                                      \
            printf("   Call to routine: %15s at line %4d in %s returned "                                    \
                   "invalid result\n",                                                                       \
                   where, (int)__LINE__, __FILE__);                                                          \
        TestErrPrintf("*** UNEXPECTED RESULT from %s at line %4d in %s\n", where, (int)__LINE__, __FILE__);  \
    } while (0)

#ifdef __cplusplus
extern "C" {
#endif

/* Prototypes for the test routines */
herr_t test_metadata(TestParams_t *params);
herr_t test_checksum(TestParams_t *params);
herr_t test_refstr(TestParams_t *params);
herr_t test_file(TestParams_t *params);
herr_t test_h5o(TestParams_t *params);
herr_t test_h5t(TestParams_t *params);
herr_t test_h5s(TestParams_t *params);
herr_t test_coords(TestParams_t *params);
herr_t test_h5d(TestParams_t *params);
herr_t test_attr(TestParams_t *params);
herr_t test_select(TestParams_t *params);
herr_t test_time(TestParams_t *params);
herr_t test_reference(TestParams_t *params);
herr_t test_reference_deprec(TestParams_t *params);
herr_t test_vltypes(TestParams_t *params);
herr_t test_vlstrings(TestParams_t *params);
herr_t test_iterate(TestParams_t *params);
herr_t test_array(TestParams_t *params);
herr_t test_genprop(TestParams_t *params);
herr_t test_configure(TestParams_t *params);
herr_t test_h5_system(TestParams_t *params);
herr_t test_misc(TestParams_t *params);
herr_t test_ids(TestParams_t *params);
herr_t test_skiplist(TestParams_t *params);
herr_t test_sohm(TestParams_t *params);
herr_t test_unicode(TestParams_t *params);

/* Prototypes for the cleanup routines */
herr_t cleanup_metadata(TestParams_t *params);
herr_t cleanup_checksum(TestParams_t *params);
herr_t cleanup_file(TestParams_t *params);
herr_t cleanup_h5o(TestParams_t *params);
herr_t cleanup_h5s(TestParams_t *params);
herr_t cleanup_coords(TestParams_t *params);
herr_t cleanup_attr(TestParams_t *params);
herr_t cleanup_select(TestParams_t *params);
herr_t cleanup_time(TestParams_t *params);
herr_t cleanup_reference(TestParams_t *params);
herr_t cleanup_reference_deprec(TestParams_t *params);
herr_t cleanup_vltypes(TestParams_t *params);
herr_t cleanup_vlstrings(TestParams_t *params);
herr_t cleanup_iterate(TestParams_t *params);
herr_t cleanup_array(TestParams_t *params);
herr_t cleanup_genprop(TestParams_t *params);
herr_t cleanup_configure(TestParams_t *params);
herr_t cleanup_h5_system(TestParams_t *params);
herr_t cleanup_sohm(TestParams_t *params);
herr_t cleanup_misc(TestParams_t *params);
herr_t cleanup_unicode(TestParams_t *params);

#ifdef __cplusplus
}
#endif
#endif /* TESTHDF5_H */
