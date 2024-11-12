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

#ifndef TTSAFE_H
#define TTSAFE_H

/*
 * Include required headers.  This file tests internal library functions,
 * so we include the private headers here.
 */
#include "testhdf5.h"

/* Prototypes for the support routines */
extern char *gen_name(int);

/* Prototypes for the test routines */
herr_t tts_is_threadsafe(TestParams_t *);

#if defined H5_HAVE_THREADSAFE || defined H5_HAVE_MULTITHREAD
herr_t tts_errstk(TestParams_t *);
#endif

#ifdef H5_HAVE_THREADSAFE
herr_t tts_dcreate(TestParams_t *);
herr_t tts_error(TestParams_t *);
herr_t tts_cancel(TestParams_t *);
herr_t tts_acreate(TestParams_t *);
herr_t tts_attr_vlen(TestParams_t *);
herr_t tts_mutex(TestParams_t *);

/* Prototypes for the cleanup routines */
herr_t cleanup_dcreate(TestParams_t *);
herr_t cleanup_error(TestParams_t *);
herr_t cleanup_cancel(TestParams_t *);
herr_t cleanup_acreate(TestParams_t *);
herr_t cleanup_attr_vlen(TestParams_t *);
herr_t cleanup_mutex(TestParams_t *);

#endif /* H5_HAVE_THREADSAFE */
#endif /* TTSAFE_H */
