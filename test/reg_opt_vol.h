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
 * Purpose: The header file for the "register operation" VOL connector,
 *           which exists to test registration of dynamic optional
 *           connector operations.
 */

#include "H5VLpublic.h" /* Virtual Object Layer                 */

#ifndef REG_OPT_VOL_H
#define REG_OPT_VOL_H

#define REG_OPT_VOL_NAME  "reg_opt"
#define REG_OPT_VOL_VALUE ((H5VL_class_value_t)502)

H5TEST_DLL hid_t reg_opt_register(void);
H5TEST_DLL herr_t reg_opt_datatype_get(void *obj, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req);

/* Value of currently registered optional dynamic VOL operation */
int reg_opt_curr_op_val = 0;

/* Public identifier for the reg opt VOL connector */
#define REG_OPT_VOL (reg_opt_register())


#endif /* REG_OPT_VOL_H */