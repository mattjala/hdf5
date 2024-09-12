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
 * Purpose: The header file for the 'fake open' testing VOL connector.
 * The 'fake open' connector is a VOL connector that always returns 'succeed'
 * from its file open callback in order to test VOL registration during file
 * open failure.
 */

#ifndef H5VL_fake_open_H
#define H5VL_fake_open_H

#define FAKE_OPEN_VOL_CONNECTOR_VALUE ((H5VL_class_value_t)161)
#define FAKE_OPEN_VOL_CONNECTOR_NAME "fake_open_vol_connector"

#endif /* H5VL_fake_open_H */