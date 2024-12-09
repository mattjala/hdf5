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
 * Purpose:     This is a "pass through" VOL connector, which forwards each
 *              VOL callback to an underlying connector.
 *
 *              It is designed as a test for multi-threaded VOL connector behavior.
 *              This is largely a clone of the pass-through VOL connector, but with
 *              minor changes to support multi-threaded access.
 *
 *              Note that the HDF5 error stack must be preserved on code paths
 *              that could be invoked when the underlying VOL connector's
 *              callback can fail.
 *
 */

/* Header files needed */
/* Do NOT include private HDF5 files here! */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public HDF5 file */
#include "hdf5.h"

/* This connector's headers */
#include "mt_passthru_wrapper_vol_connector.h"
#include "H5VLpassthru_private.h"

/**********/
/* Macros */
/**********/

/* Whether to display log message when callback is invoked */
/* (Uncomment to enable) */
/* #define ENABLE_PASSTHRU_LOGGING */

/* Hack for missing va_copy() in old Visual Studio editions
 * (from H5win2_defs.h - used on VS2012 and earlier)
 */
#if defined(_WIN32) && defined(_MSC_VER) && (_MSC_VER < 1800)
#define va_copy(D, S) ((D) = (S))
#endif

/********************* */
/* Function prototypes */
/********************* */

/* Dynamic plugin routines */
H5PL_type_t H5PLget_plugin_type(void);
const void *H5PLget_plugin_info(void);

/* VOL info callbacks */
static void  *mt_pass_through_wrapper_info_copy(const void *info);
static herr_t mt_pass_through_wrapper_info_cmp(int *cmp_value, const void *info1, const void *info2);
static herr_t mt_pass_through_wrapper_info_free(void *info);
static herr_t mt_pass_through_wrapper_info_to_str(const void *info, char **str);
static herr_t mt_pass_through_wrapper_str_to_info(const char *str, void **info);

/* VOL object wrap / retrieval callbacks */
static void  *mt_pass_through_wrapper_get_object(const void *obj);
static herr_t mt_pass_through_wrapper_get_wrap_ctx(const void *obj, void **wrap_ctx);
static void  *mt_pass_through_wrapper_wrap_object(void *obj, H5I_type_t obj_type, void *wrap_ctx);
static void  *mt_pass_through_wrapper_unwrap_object(void *obj);
static herr_t mt_pass_through_wrapper_free_wrap_ctx(void *obj);

/* Attribute callbacks */
static void  *mt_pass_through_wrapper_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                            hid_t type_id, hid_t space_id, hid_t acpl_id, hid_t aapl_id,
                                            hid_t dxpl_id, void **req);
static void  *mt_pass_through_wrapper_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                          hid_t aapl_id, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_attr_read(void *attr, hid_t mem_type_id, void *buf, hid_t dxpl_id,
                                          void **req);
static herr_t mt_pass_through_wrapper_attr_write(void *attr, hid_t mem_type_id, const void *buf, hid_t dxpl_id,
                                           void **req);
static herr_t mt_pass_through_wrapper_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_attr_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                              H5VL_attr_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_attr_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id,
                                              void **req);
static herr_t mt_pass_through_wrapper_attr_close(void *attr, hid_t dxpl_id, void **req);

/* Dataset callbacks */
static void  *mt_pass_through_wrapper_dataset_create(void *obj, const H5VL_loc_params_t *loc_params,
                                               const char *name, hid_t lcpl_id, hid_t type_id, hid_t space_id,
                                               hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req);
static void  *mt_pass_through_wrapper_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                             hid_t dapl_id, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_dataset_read(size_t count, void *dset[], hid_t mem_type_id[],
                                             hid_t mem_space_id[], hid_t file_space_id[], hid_t plist_id,
                                             void *buf[], void **req);
static herr_t mt_pass_through_wrapper_dataset_write(size_t count, void *dset[], hid_t mem_type_id[],
                                              hid_t mem_space_id[], hid_t file_space_id[], hid_t plist_id,
                                              const void *buf[], void **req);
static herr_t mt_pass_through_wrapper_dataset_get(void *dset, H5VL_dataset_get_args_t *args, hid_t dxpl_id,
                                            void **req);
static herr_t mt_pass_through_wrapper_dataset_specific(void *obj, H5VL_dataset_specific_args_t *args, hid_t dxpl_id,
                                                 void **req);
static herr_t mt_pass_through_wrapper_dataset_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id,
                                                 void **req);
static herr_t mt_pass_through_wrapper_dataset_close(void *dset, hid_t dxpl_id, void **req);

/* Datatype callbacks */
static void *mt_pass_through_wrapper_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params,
                                               const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id,
                                               hid_t tapl_id, hid_t dxpl_id, void **req);
static void *mt_pass_through_wrapper_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                             hid_t tapl_id, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_datatype_get(void *dt, H5VL_datatype_get_args_t *args, hid_t dxpl_id,
                                             void **req);
static herr_t mt_pass_through_wrapper_datatype_specific(void *obj, H5VL_datatype_specific_args_t *args,
                                                  hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_datatype_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id,
                                                  void **req);
static herr_t mt_pass_through_wrapper_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* File callbacks */
static void  *mt_pass_through_wrapper_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id,
                                            hid_t dxpl_id, void **req);
static void  *mt_pass_through_wrapper_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id,
                                          void **req);
static herr_t mt_pass_through_wrapper_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_file_specific(void *file, H5VL_file_specific_args_t *args, hid_t dxpl_id,
                                              void **req);
static herr_t mt_pass_through_wrapper_file_optional(void *file, H5VL_optional_args_t *args, hid_t dxpl_id,
                                              void **req);
static herr_t mt_pass_through_wrapper_file_close(void *file, hid_t dxpl_id, void **req);

/* Group callbacks */
static void  *mt_pass_through_wrapper_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                             hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id,
                                             void **req);
static void  *mt_pass_through_wrapper_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                           hid_t gapl_id, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_group_specific(void *obj, H5VL_group_specific_args_t *args, hid_t dxpl_id,
                                               void **req);
static herr_t mt_pass_through_wrapper_group_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id,
                                               void **req);
static herr_t mt_pass_through_wrapper_group_close(void *grp, hid_t dxpl_id, void **req);

/* Link callbacks */
static herr_t mt_pass_through_wrapper_link_create(H5VL_link_create_args_t *args, void *obj,
                                            const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id,
                                            hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                                          const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id,
                                          hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                                          const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id,
                                          hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_link_get(void *obj, const H5VL_loc_params_t *loc_params,
                                         H5VL_link_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_link_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                              H5VL_link_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_link_optional(void *obj, const H5VL_loc_params_t *loc_params,
                                              H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Object callbacks */
static void  *mt_pass_through_wrapper_object_open(void *obj, const H5VL_loc_params_t *loc_params,
                                            H5I_type_t *opened_type, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_object_copy(void *src_obj, const H5VL_loc_params_t *src_loc_params,
                                            const char *src_name, void *dst_obj,
                                            const H5VL_loc_params_t *dst_loc_params, const char *dst_name,
                                            hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_object_get(void *obj, const H5VL_loc_params_t *loc_params,
                                           H5VL_object_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_object_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                                H5VL_object_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t mt_pass_through_wrapper_object_optional(void *obj, const H5VL_loc_params_t *loc_params,
                                                H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Container/connector introspection callbacks */
static herr_t mt_pass_through_wrapper_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl,
                                                        const H5VL_class_t **conn_cls);
static herr_t mt_pass_through_wrapper_introspect_get_cap_flags(const void *info, uint64_t *cap_flags);
static herr_t mt_pass_through_wrapper_introspect_opt_query(void *obj, H5VL_subclass_t cls, int opt_type,
                                                     uint64_t *flags);

/* Async request callbacks */
static herr_t mt_pass_through_wrapper_request_wait(void *req, uint64_t timeout, H5VL_request_status_t *status);
static herr_t mt_pass_through_wrapper_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx);
static herr_t mt_pass_through_wrapper_request_cancel(void *req, H5VL_request_status_t *status);
static herr_t mt_pass_through_wrapper_request_specific(void *req, H5VL_request_specific_args_t *args);
static herr_t mt_pass_through_wrapper_request_optional(void *req, H5VL_optional_args_t *args);
static herr_t mt_pass_through_wrapper_request_free(void *req);

/* Blob callbacks */
static herr_t mt_pass_through_wrapper_blob_put(void *obj, const void *buf, size_t size, void *blob_id, void *ctx);
static herr_t mt_pass_through_wrapper_blob_get(void *obj, const void *blob_id, void *buf, size_t size, void *ctx);
static herr_t mt_pass_through_wrapper_blob_specific(void *obj, void *blob_id, H5VL_blob_specific_args_t *args);
static herr_t mt_pass_through_wrapper_blob_optional(void *obj, void *blob_id, H5VL_optional_args_t *args);

/* Token callbacks */
static herr_t mt_pass_through_wrapper_token_cmp(void *obj, const H5O_token_t *token1, const H5O_token_t *token2,
                                          int *cmp_value);
static herr_t mt_pass_through_wrapper_token_to_str(void *obj, H5I_type_t obj_type, const H5O_token_t *token,
                                             char **token_str);
static herr_t mt_pass_through_wrapper_token_from_str(void *obj, H5I_type_t obj_type, const char *token_str,
                                               H5O_token_t *token);

/* Generic optional callback */
static herr_t mt_pass_through_wrapper_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/*******************/
/* Local variables */
/*******************/

/* Pass through VOL connector class struct */
static const H5VL_class_t mt_pass_through_wrapper_g = {
    H5VL_VERSION,                            /* VOL class struct version */
    MT_PASSTHRU_WRAPPER_VALUE, /* value        */
    MT_PASSTHRU_WRAPPER_NAME,                      /* name         */
    0,                   /* connector version */
    H5VL_CAP_FLAG_THREADSAFE,  /* capability flags */
    NULL,                  /* initialize   */
    NULL,                  /* terminate    */
    {
        /* info_cls */
        sizeof(mt_pass_through_wrapper_info_t), /* size    */
        mt_pass_through_wrapper_info_copy,      /* copy    */
        mt_pass_through_wrapper_info_cmp,       /* compare */
        mt_pass_through_wrapper_info_free,      /* free    */
        mt_pass_through_wrapper_info_to_str,    /* to_str  */
        mt_pass_through_wrapper_str_to_info     /* from_str */
    },
    {
        /* wrap_cls */
        mt_pass_through_wrapper_get_object,    /* get_object   */
        mt_pass_through_wrapper_get_wrap_ctx,  /* get_wrap_ctx */
        mt_pass_through_wrapper_wrap_object,   /* wrap_object  */
        mt_pass_through_wrapper_unwrap_object, /* unwrap_object */
        mt_pass_through_wrapper_free_wrap_ctx  /* free_wrap_ctx */
    },
    {
        /* attribute_cls */
        mt_pass_through_wrapper_attr_create,   /* create */
        mt_pass_through_wrapper_attr_open,     /* open */
        mt_pass_through_wrapper_attr_read,     /* read */
        mt_pass_through_wrapper_attr_write,    /* write */
        mt_pass_through_wrapper_attr_get,      /* get */
        mt_pass_through_wrapper_attr_specific, /* specific */
        mt_pass_through_wrapper_attr_optional, /* optional */
        mt_pass_through_wrapper_attr_close     /* close */
    },
    {
        /* dataset_cls */
        mt_pass_through_wrapper_dataset_create,   /* create */
        mt_pass_through_wrapper_dataset_open,     /* open */
        mt_pass_through_wrapper_dataset_read,     /* read */
        mt_pass_through_wrapper_dataset_write,    /* write */
        mt_pass_through_wrapper_dataset_get,      /* get */
        mt_pass_through_wrapper_dataset_specific, /* specific */
        mt_pass_through_wrapper_dataset_optional, /* optional */
        mt_pass_through_wrapper_dataset_close     /* close */
    },
    {
        /* datatype_cls */
        mt_pass_through_wrapper_datatype_commit,   /* commit */
        mt_pass_through_wrapper_datatype_open,     /* open */
        mt_pass_through_wrapper_datatype_get,      /* get_size */
        mt_pass_through_wrapper_datatype_specific, /* specific */
        mt_pass_through_wrapper_datatype_optional, /* optional */
        mt_pass_through_wrapper_datatype_close     /* close */
    },
    {
        /* file_cls */
        mt_pass_through_wrapper_file_create,   /* create */
        mt_pass_through_wrapper_file_open,     /* open */
        mt_pass_through_wrapper_file_get,      /* get */
        mt_pass_through_wrapper_file_specific, /* specific */
        mt_pass_through_wrapper_file_optional, /* optional */
        mt_pass_through_wrapper_file_close     /* close */
    },
    {
        /* group_cls */
        mt_pass_through_wrapper_group_create,   /* create */
        mt_pass_through_wrapper_group_open,     /* open */
        mt_pass_through_wrapper_group_get,      /* get */
        mt_pass_through_wrapper_group_specific, /* specific */
        mt_pass_through_wrapper_group_optional, /* optional */
        mt_pass_through_wrapper_group_close     /* close */
    },
    {
        /* link_cls */
        mt_pass_through_wrapper_link_create,   /* create */
        mt_pass_through_wrapper_link_copy,     /* copy */
        mt_pass_through_wrapper_link_move,     /* move */
        mt_pass_through_wrapper_link_get,      /* get */
        mt_pass_through_wrapper_link_specific, /* specific */
        mt_pass_through_wrapper_link_optional  /* optional */
    },
    {
        /* object_cls */
        mt_pass_through_wrapper_object_open,     /* open */
        mt_pass_through_wrapper_object_copy,     /* copy */
        mt_pass_through_wrapper_object_get,      /* get */
        mt_pass_through_wrapper_object_specific, /* specific */
        mt_pass_through_wrapper_object_optional  /* optional */
    },
    {
        /* introspect_cls */
        mt_pass_through_wrapper_introspect_get_conn_cls,  /* get_conn_cls */
        mt_pass_through_wrapper_introspect_get_cap_flags, /* get_cap_flags */
        mt_pass_through_wrapper_introspect_opt_query,     /* opt_query */
    },
    {
        /* request_cls */
        mt_pass_through_wrapper_request_wait,     /* wait */
        mt_pass_through_wrapper_request_notify,   /* notify */
        mt_pass_through_wrapper_request_cancel,   /* cancel */
        mt_pass_through_wrapper_request_specific, /* specific */
        mt_pass_through_wrapper_request_optional, /* optional */
        mt_pass_through_wrapper_request_free      /* free */
    },
    {
        /* blob_cls */
        mt_pass_through_wrapper_blob_put,      /* put */
        mt_pass_through_wrapper_blob_get,      /* get */
        mt_pass_through_wrapper_blob_specific, /* specific */
        mt_pass_through_wrapper_blob_optional  /* optional */
    },
    {
        /* token_cls */
        mt_pass_through_wrapper_token_cmp,     /* cmp */
        mt_pass_through_wrapper_token_to_str,  /* to_str */
        mt_pass_through_wrapper_token_from_str /* from_str */
    },
    mt_pass_through_wrapper_optional /* optional */
};

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_info_copy
 *
 * Purpose:     Duplicate the connector's info object.
 *
 * Returns:     Success:    New connector info object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_info_copy(const void *_info)
{
    return H5VL_pass_through_info_copy(_info);
} /* end mt_pass_through_wrapper_info_copy() */


static herr_t
mt_pass_through_wrapper_info_cmp(int *cmp_value, const void *_info1, const void *_info2)
{
    return H5VL_pass_through_info_cmp(cmp_value, _info1, _info2);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_info_free
 *
 * Purpose:     Release an info object for the connector.
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_info_free(void *_info)
{
    return H5VL_pass_through_info_free(_info);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_info_to_str
 *
 * Purpose:     Serialize an info object for this connector into a string
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_info_to_str(const void *_info, char **str)
{
    return H5VL_pass_through_info_to_str(_info, str);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_str_to_info
 *
 * Purpose:     Deserialize a string into an info object for this connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_str_to_info(const char *str, void **_info)
{
    return H5VL_pass_through_str_to_info(str, _info);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_get_object
 *
 * Purpose:     Retrieve the 'data' for a VOL object.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_get_object(const void *obj)
{
    return H5VL_pass_through_get_object(obj);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_get_wrap_ctx
 *
 * Purpose:     Retrieve a "wrapper context" for an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_get_wrap_ctx(const void *obj, void **wrap_ctx)
{
        return H5VL_pass_through_get_wrap_ctx(obj, wrap_ctx);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_wrap_object
 *
 * Purpose:     Use a "wrapper context" to wrap a data object
 *
 * Return:      Success:    Pointer to wrapped object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_wrap_object(void *obj, H5I_type_t obj_type, void *_wrap_ctx)
{
    return H5VL_pass_through_wrap_object(obj, obj_type, _wrap_ctx);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_unwrap_object
 *
 * Purpose:     Unwrap a wrapped object, discarding the wrapper, but returning
 *		underlying object.
 *
 * Return:      Success:    Pointer to unwrapped object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_unwrap_object(void *obj)
{
    return H5VL_pass_through_unwrap_object(obj);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_free_wrap_ctx
 *
 * Purpose:     Release a "wrapper context" for an object
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_free_wrap_ctx(void *_wrap_ctx)
{
    return H5VL_pass_through_free_wrap_ctx(_wrap_ctx);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_attr_create
 *
 * Purpose:     Creates an attribute on an object.
 *
 * Return:      Success:    Pointer to attribute object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id,
                              hid_t space_id, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_attr_create(obj, loc_params, name, type_id, space_id, acpl_id, aapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_attr_open
 *
 * Purpose:     Opens an attribute on an object.
 *
 * Return:      Success:    Pointer to attribute object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t aapl_id,
                            hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_attr_open(obj, loc_params, name, aapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_attr_read
 *
 * Purpose:     Reads data from attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_attr_read(void *attr, hid_t mem_type_id, void *buf, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_attr_read(attr, mem_type_id, buf, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_attr_write
 *
 * Purpose:     Writes data to attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_attr_write(void *attr, hid_t mem_type_id, const void *buf, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_attr_write(attr, mem_type_id, buf, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_attr_get
 *
 * Purpose:     Gets information about an attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_attr_get(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_attr_specific
 *
 * Purpose:     Specific operation on attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_attr_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                H5VL_attr_specific_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_attr_specific(obj, loc_params, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_attr_optional
 *
 * Purpose:     Perform a connector-specific operation on an attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_attr_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_attr_optional(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_attr_close
 *
 * Purpose:     Closes an attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1, attr not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_attr_close(void *attr, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_attr_close(attr, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_dataset_create
 *
 * Purpose:     Creates a dataset in a container
 *
 * Return:      Success:    Pointer to a dataset object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                 hid_t lcpl_id, hid_t type_id, hid_t space_id, hid_t dcpl_id, hid_t dapl_id,
                                 hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_dataset_create(obj, loc_params, name, lcpl_id, type_id, space_id, dcpl_id, dapl_id,
                                         dxpl_id, req);
}
/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_dataset_open
 *
 * Purpose:     Opens a dataset in a container
 *
 * Return:      Success:    Pointer to a dataset object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                               hid_t dapl_id, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_dataset_open(obj, loc_params, name, dapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_dataset_read
 *
 * Purpose:     Reads data elements from a dataset into a buffer.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_dataset_read(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[],
                               hid_t file_space_id[], hid_t plist_id, void *buf[], void **req)
{
    return H5VL_pass_through_dataset_read(count, dset, mem_type_id, mem_space_id, file_space_id, plist_id, buf, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_dataset_write
 *
 * Purpose:     Writes data elements from a buffer into a dataset.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_dataset_write(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[],
                                hid_t file_space_id[], hid_t plist_id, const void *buf[], void **req)
{
    return H5VL_pass_through_dataset_write(count, dset, mem_type_id, mem_space_id, file_space_id, plist_id, buf, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_dataset_get
 *
 * Purpose:     Gets information about a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_dataset_get(void *dset, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_dataset_get(dset, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_dataset_specific
 *
 * Purpose:     Specific operation on a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_dataset_specific(void *obj, H5VL_dataset_specific_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_dataset_specific(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_dataset_optional
 *
 * Purpose:     Perform a connector-specific operation on a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_dataset_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_dataset_optional(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_dataset_close
 *
 * Purpose:     Closes a dataset.
 *
 * Return:      Success:    0
 *              Failure:    -1, dataset not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_dataset_close(void *dset, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_dataset_close(dset, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_datatype_commit
 *
 * Purpose:     Commits a datatype inside a container.
 *
 * Return:      Success:    Pointer to datatype object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                  hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id,
                                  void **req)
{
    return H5VL_pass_through_datatype_commit(obj, loc_params, name, type_id, lcpl_id, tcpl_id, tapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_datatype_open
 *
 * Purpose:     Opens a named datatype inside a container.
 *
 * Return:      Success:    Pointer to datatype object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                hid_t tapl_id, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_datatype_open(obj, loc_params, name, tapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_datatype_get
 *
 * Purpose:     Get information about a datatype
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_datatype_get(void *dt, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_datatype_get(dt, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_datatype_specific
 *
 * Purpose:     Specific operations for datatypes
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_datatype_specific(void *obj, H5VL_datatype_specific_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_datatype_specific(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_datatype_optional
 *
 * Purpose:     Perform a connector-specific operation on a datatype
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_datatype_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_datatype_optional(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_datatype_close
 *
 * Purpose:     Closes a datatype.
 *
 * Return:      Success:    0
 *              Failure:    -1, datatype not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_datatype_close(void *dt, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_datatype_close(dt, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_file_create
 *
 * Purpose:     Creates a container using this connector
 *
 * Return:      Success:    Pointer to a file object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id,
                              void **req)
{
    return H5VL_pass_through_file_create(name, flags, fcpl_id, fapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_file_open
 *
 * Purpose:     Opens a container created with this connector
 *
 * Return:      Success:    Pointer to a file object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_file_open(name, flags, fapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_file_get
 *
 * Purpose:     Get info about a file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_file_get(file, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_file_specific
 *
 * Purpose:     Specific operation on file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_file_specific(void *file, H5VL_file_specific_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_file_specific(file, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_file_optional
 *
 * Purpose:     Perform a connector-specific operation on a file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_file_optional(void *file, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_file_optional(file, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_file_close
 *
 * Purpose:     Closes a file.
 *
 * Return:      Success:    0
 *              Failure:    -1, file not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_file_close(void *file, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_file_close(file, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_group_create
 *
 * Purpose:     Creates a group inside a container
 *
 * Return:      Success:    Pointer to a group object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                               hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_group_create(obj, loc_params, name, lcpl_id, gcpl_id, gapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_group_open
 *
 * Purpose:     Opens a group inside a container
 *
 * Return:      Success:    Pointer to a group object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t gapl_id,
                             hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_group_open(obj, loc_params, name, gapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_group_get
 *
 * Purpose:     Get info about a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_group_get(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_group_specific
 *
 * Purpose:     Specific operation on a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_group_specific(void *obj, H5VL_group_specific_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_group_specific(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_group_optional
 *
 * Purpose:     Perform a connector-specific operation on a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_group_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_group_optional(obj, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_group_close
 *
 * Purpose:     Closes a group.
 *
 * Return:      Success:    0
 *              Failure:    -1, group not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_group_close(void *grp, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_group_close(grp, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_link_create
 *
 * Purpose:     Creates a hard / soft / UD / external link.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_link_create(H5VL_link_create_args_t *args, void *obj, const H5VL_loc_params_t *loc_params,
                              hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_link_create(args, obj, loc_params, lcpl_id, lapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_link_copy
 *
 * Purpose:     Renames an object within an HDF5 container and copies it to a new
 *              group.  The original name SRC is unlinked from the group graph
 *              and then inserted with the new name DST (which can specify a
 *              new path for the object) as an atomic operation. The names
 *              are interpreted relative to SRC_LOC_ID and
 *              DST_LOC_ID, which are either file IDs or group ID.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                            const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id,
                            void **req)
{
    return H5VL_pass_through_link_copy(src_obj, loc_params1, dst_obj, loc_params2, lcpl_id, lapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_link_move
 *
 * Purpose:     Moves a link within an HDF5 file to a new group.  The original
 *              name SRC is unlinked from the group graph
 *              and then inserted with the new name DST (which can specify a
 *              new path for the object) as an atomic operation. The names
 *              are interpreted relative to SRC_LOC_ID and
 *              DST_LOC_ID, which are either file IDs or group ID.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                            const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id,
                            void **req)
{
    return H5VL_pass_through_link_move(src_obj, loc_params1, dst_obj, loc_params2, lcpl_id, lapl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_link_get
 *
 * Purpose:     Get info about a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_args_t *args,
                           hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_link_get(obj, loc_params, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_link_specific
 *
 * Purpose:     Specific operation on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_link_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                H5VL_link_specific_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_link_specific(obj, loc_params, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_link_optional
 *
 * Purpose:     Perform a connector-specific operation on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_link_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args,
                                hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_link_optional(obj, loc_params, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_object_open
 *
 * Purpose:     Opens an object inside a container.
 *
 * Return:      Success:    Pointer to object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
mt_pass_through_wrapper_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type,
                              hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_object_open(obj, loc_params, opened_type, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_object_copy
 *
 * Purpose:     Copies an object inside a container.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_object_copy(void *src_obj, const H5VL_loc_params_t *src_loc_params, const char *src_name,
                              void *dst_obj, const H5VL_loc_params_t *dst_loc_params, const char *dst_name,
                              hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_object_copy(src_obj, src_loc_params, src_name, dst_obj, dst_loc_params, dst_name,
                                     ocpypl_id, lcpl_id, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_object_get
 *
 * Purpose:     Get info about an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_args_t *args,
                             hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_object_get(obj, loc_params, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_object_specific
 *
 * Purpose:     Specific operation on an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_object_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                  H5VL_object_specific_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_object_specific(obj, loc_params, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_object_optional
 *
 * Purpose:     Perform a connector-specific operation for an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_object_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args,
                                  hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_object_optional(obj, loc_params, args, dxpl_id, req);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_introspect_get_conn_cls
 *
 * Purpose:     Query the connector class.
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
mt_pass_through_wrapper_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl, const H5VL_class_t **conn_cls)
{
    return H5VL_pass_through_introspect_get_conn_cls(obj, lvl, conn_cls);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_introspect_get_cap_flags
 *
 * Purpose:     Query the capability flags for this connector and any
 *              underlying connector(s).
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
mt_pass_through_wrapper_introspect_get_cap_flags(const void *_info, uint64_t *cap_flags)
{
    return H5VL_pass_through_introspect_get_cap_flags(_info, cap_flags);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_introspect_opt_query
 *
 * Purpose:     Query if an optional operation is supported by this connector
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
mt_pass_through_wrapper_introspect_opt_query(void *obj, H5VL_subclass_t cls, int opt_type, uint64_t *flags)
{
    return H5VL_pass_through_introspect_opt_query(obj, cls, opt_type, flags);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_request_wait
 *
 * Purpose:     Wait (with a timeout) for an async operation to complete
 *
 * Note:        Releases the request if the operation has completed and the
 *              connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_request_wait(void *obj, uint64_t timeout, H5VL_request_status_t *status)
{
    return H5VL_pass_through_request_wait(obj, timeout, status);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_request_notify
 *
 * Purpose:     Registers a user callback to be invoked when an asynchronous
 *              operation completes
 *
 * Note:        Releases the request, if connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx)
{
    return H5VL_pass_through_request_notify(obj, cb, ctx);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_request_cancel
 *
 * Purpose:     Cancels an asynchronous operation
 *
 * Note:        Releases the request, if connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_request_cancel(void *obj, H5VL_request_status_t *status)
{
    return H5VL_pass_through_request_cancel(obj, status);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_request_specific
 *
 * Purpose:     Specific operation on a request
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_request_specific(void *obj, H5VL_request_specific_args_t *args)
{
    return H5VL_pass_through_request_specific(obj, args);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_request_optional
 *
 * Purpose:     Perform a connector-specific operation for a request
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_request_optional(void *obj, H5VL_optional_args_t *args)
{
    return H5VL_pass_through_request_optional(obj, args);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_request_free
 *
 * Purpose:     Releases a request, allowing the operation to complete without
 *              application tracking
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_request_free(void *obj)
{
    return H5VL_pass_through_request_free(obj);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_blob_put
 *
 * Purpose:     Handles the blob 'put' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
mt_pass_through_wrapper_blob_put(void *obj, const void *buf, size_t size, void *blob_id, void *ctx)
{
    return H5VL_pass_through_blob_put(obj, buf, size, blob_id, ctx);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_blob_get
 *
 * Purpose:     Handles the blob 'get' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
mt_pass_through_wrapper_blob_get(void *obj, const void *blob_id, void *buf, size_t size, void *ctx)
{
    return H5VL_pass_through_blob_get(obj, blob_id, buf, size, ctx);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_blob_specific
 *
 * Purpose:     Handles the blob 'specific' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
mt_pass_through_wrapper_blob_specific(void *obj, void *blob_id, H5VL_blob_specific_args_t *args)
{
    return H5VL_pass_through_blob_specific(obj, blob_id, args);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_blob_optional
 *
 * Purpose:     Handles the blob 'optional' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
mt_pass_through_wrapper_blob_optional(void *obj, void *blob_id, H5VL_optional_args_t *args)
{
    return H5VL_pass_through_blob_optional(obj, blob_id, args);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_token_cmp
 *
 * Purpose:     Compare two of the connector's object tokens, setting
 *              *cmp_value, following the same rules as strcmp().
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_token_cmp(void *obj, const H5O_token_t *token1, const H5O_token_t *token2, int *cmp_value)
{
    return H5VL_pass_through_token_cmp(obj, token1, token2, cmp_value);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_token_to_str
 *
 * Purpose:     Serialize the connector's object token into a string.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_token_to_str(void *obj, H5I_type_t obj_type, const H5O_token_t *token, char **token_str)
{
    return H5VL_pass_through_token_to_str(obj, obj_type, token, token_str);
}

/*---------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_token_from_str
 *
 * Purpose:     Deserialize the connector's object token from a string.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
mt_pass_through_wrapper_token_from_str(void *obj, H5I_type_t obj_type, const char *token_str, H5O_token_t *token)
{
    return H5VL_pass_through_token_from_str(obj, obj_type, token_str, token);
}

/*-------------------------------------------------------------------------
 * Function:    mt_pass_through_wrapper_optional
 *
 * Purpose:     Handles the generic 'optional' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
mt_pass_through_wrapper_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    return H5VL_pass_through_optional(obj, args, dxpl_id, req);
}

/* These two functions are necessary to load this plugin using
 * the HDF5 library.
 */

H5PL_type_t H5PLget_plugin_type(void) { return H5PL_TYPE_VOL; }
const void *H5PLget_plugin_info(void) { return &mt_pass_through_wrapper_g; }
