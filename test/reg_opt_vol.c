/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the LICENSE file, which can be found at the root of the source code       *
 * distribution tree, or in https://www.hdfgroup.org/licenses.               *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Purpose: The "register operation" VOL connector,
 *           which exists to test registration of dynamic optional
 *           connector operations.*/

/* Headers needed */
#define H5VL_FRIEND

#include "H5private.h"   /* Generic Functions      */
#include "H5Ppublic.h"   /* Property Lists         */
#include "H5VLpkg.h"     /* Virtual Object Layer  */
#include "reg_opt_vol.h" /* This VOL's header file */

static herr_t reg_opt_op_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t reg_opt_op_optional_verify(void *obj, H5VL_optional_args_t *args);
static herr_t reg_opt_link_optional(void *obj, const H5VL_loc_params_t *loc_params,
                                    H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* A test VOL connector to verify registration of dynamic operations */
const H5VL_class_t reg_opt_vol_g = {
    H5VL_VERSION,       /* VOL class struct version */
    REG_OPT_VOL_VALUE,  /* value        */
    REG_OPT_VOL_NAME,   /* name         */
    0,                  /* version      */
    H5VL_CAP_FLAG_NONE, /* capability flags */
    NULL,               /* initialize   */
    NULL,               /* terminate    */
    {
        /* info_cls */
        (size_t)0, /* size    */
        NULL,      /* copy    */
        NULL,      /* compare */
        NULL,      /* free    */
        NULL,      /* to_str  */
        NULL,      /* from_str */
    },
    {
        /* wrap_cls */
        NULL, /* get_object   */
        NULL, /* get_wrap_ctx */
        NULL, /* wrap_object  */
        NULL, /* unwrap_object */
        NULL, /* free_wrap_ctx */
    },
    {
        /* attribute_cls */
        NULL,                /* create       */
        NULL,                /* open         */
        NULL,                /* read         */
        NULL,                /* write        */
        NULL,                /* get          */
        NULL,                /* specific     */
        reg_opt_op_optional, /* optional     */
        NULL                 /* close        */
    },
    {
        /* dataset_cls */
        NULL,                /* create       */
        NULL,                /* open         */
        NULL,                /* read         */
        NULL,                /* write        */
        NULL,                /* get          */
        NULL,                /* specific     */
        reg_opt_op_optional, /* optional     */
        NULL                 /* close        */
    },
    {
        /* datatype_cls */
        NULL,                 /* commit       */
        NULL,                 /* open         */
        reg_opt_datatype_get, /* get          */
        NULL,                 /* specific     */
        reg_opt_op_optional,  /* optional     */
        NULL                  /* close        */
    },
    {
        /* file_cls */
        NULL,                /* create       */
        NULL,                /* open         */
        NULL,                /* get          */
        NULL,                /* specific     */
        reg_opt_op_optional, /* optional     */
        NULL                 /* close        */
    },
    {
        /* group_cls */
        NULL,                /* create       */
        NULL,                /* open         */
        NULL,                /* get          */
        NULL,                /* specific     */
        reg_opt_op_optional, /* optional     */
        NULL                 /* close        */
    },
    {
        /* link_cls */
        NULL,                 /* create       */
        NULL,                 /* copy         */
        NULL,                 /* move         */
        NULL,                 /* get          */
        NULL,                 /* specific     */
        reg_opt_link_optional /* optional     */
    },
    {
        /* object_cls */
        NULL,                 /* open         */
        NULL,                 /* copy         */
        NULL,                 /* get          */
        NULL,                 /* specific     */
        reg_opt_link_optional /* optional     */
    },
    {
        /* introspect_cls */
        NULL, /* get_conn_cls */
        NULL, /* get_cap_flags */
        NULL, /* opt_query    */
    },
    {
        /* request_cls */
        NULL, /* wait         */
        NULL, /* notify       */
        NULL, /* cancel       */
        NULL, /* specific     */
        NULL, /* optional     */
        NULL  /* free         */
    },
    {
        /* blob_cls */
        NULL, /* put          */
        NULL, /* get          */
        NULL, /* specific     */
        NULL  /* optional     */
    },
    {
        /* token_cls */
        NULL, /* cmp              */
        NULL, /* to_str           */
        NULL  /* from_str         */
    },
    NULL /* optional     */
};

/*-------------------------------------------------------------------------
 * Function:    reg_opt_op_optional_verify
 *
 * Purpose:     Common verification routine for dynamic optional operations
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
reg_opt_op_optional_verify(void *obj, H5VL_optional_args_t *args)
{
    int *o = (int *)obj;
    int *op_args;

    /* Check for receiving correct operation value */
    if (args->op_type != reg_opt_curr_op_val)
        return -1;

    /* Check that the object is correct */
    if ((-1) != *o)
        return -1;

    /* Update the object, with the operation value */
    *o = args->op_type;

    /* Check that the argument is correct */
    op_args = args->args;
    if (NULL == op_args)
        return -1;
    if ((-1) != *op_args)
        return -1;

    /* Update the argument return parameter */
    *op_args = args->op_type;

    return 0;
} /* end reg_opt_op_optional_verify() */

/*-------------------------------------------------------------------------
 * Function:    reg_opt_op_optional
 *
 * Purpose:     Common callback to perform a connector-specific operation
 *              on an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
reg_opt_op_optional(void *obj, H5VL_optional_args_t *args, hid_t H5_ATTR_UNUSED dxpl_id,
                    void H5_ATTR_UNUSED **req)
{
    /* Invoke the common value verification routine */
    return reg_opt_op_optional_verify(obj, args);
} /* end reg_opt_op_optional() */

/*-------------------------------------------------------------------------
 * Function:    reg_opt_link_optional
 *
 * Purpose:     Callback to perform a connector-specific operation
 *              on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
reg_opt_link_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args,
                      hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    /* Check for receiving correct loc_params info */
    if (loc_params->type != H5VL_OBJECT_BY_NAME)
        return -1;
    if (loc_params->obj_type != H5I_GROUP)
        return -1;
    if (HDstrcmp(loc_params->loc_data.loc_by_name.name, ".") != 0)
        return -1;
    if (loc_params->loc_data.loc_by_name.lapl_id != H5P_LINK_ACCESS_DEFAULT)
        return -1;

    /* Invoke the common value verification routine */
    return reg_opt_op_optional_verify(obj, args);
} /* end reg_opt_link_optional() */

/*-------------------------------------------------------------------------
 * Function:    reg_opt_datatype_get
 *
 * Purpose:     Handles the datatype get callback
 *
 * Note:        This is _strictly_ a testing fixture to support the
 *              exercise_reg_opt_oper() testing routine.  It fakes just
 *              enough of the named datatype VOL callback for the
 *              H5VL_register_using_vol_id() call in that test routine to
 *              succeed.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
reg_opt_datatype_get(void H5_ATTR_UNUSED *obj, H5VL_datatype_get_args_t *args, hid_t H5_ATTR_UNUSED dxpl_id,
                     void H5_ATTR_UNUSED **req)
{
    herr_t ret_value = SUCCEED; /* Return value */

    if (H5VL_DATATYPE_GET_BINARY_SIZE == args->op_type) {
        if (H5Tencode(H5T_NATIVE_INT, NULL, args->args.get_binary_size.size) < 0)
            ret_value = FAIL;
    } /* end if */
    else if (H5VL_DATATYPE_GET_BINARY == args->op_type) {
        if (H5Tencode(H5T_NATIVE_INT, args->args.get_binary.buf, &args->args.get_binary.buf_size) < 0)
            ret_value = FAIL;
    } /* end if */
    else
        ret_value = FAIL;

    return ret_value;
} /* end reg_opt_datatype_get() */

/*-------------------------------------------------------------------------
 * Function:    reg_opt_register
 *
 * Purpose:     Registers this connector with the library and returns an
 *              ID for it
 * 
 * Return:      Success:    ID for this connector
 *              Failure:    -1
 * 
 *-------------------------------------------------------------------------
 */
hid_t
reg_opt_register(void)
{
    hid_t ret_value = H5I_INVALID_HID;

    /* Register the connector with the library */
    if ((ret_value = H5VLregister_connector(&reg_opt_vol_g, H5P_DEFAULT)) < 0)
        ret_value = H5I_INVALID_HID;

    return ret_value;
}

