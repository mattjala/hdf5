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

/* Purpose:     A virtual object layer (VOL) connector used for testing
 *              multi-threaded access to the HDF5 library. Invokes
 *              the corresponding native VOL connector routines under the global lock.
 */

/* For HDF5 plugin functionality */
#include "H5PLextern.h"

/* This connector's header */
#include "mt_native_wrapper_vol_connector.h"

/* This connector eventually routes its operations back to the Native VOL */
#include "H5VLnative_private.h"

/* Attribute callbacks */
H5_DLL void  *mt_native_wrapper_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name,
                                       hid_t type_id, hid_t space_id, hid_t acpl_id, hid_t aapl_id,
                                       hid_t dxpl_id, void **req);
H5_DLL void *mt_native_wrapper_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name,
                                     hid_t aapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_attr_read(void *attr, hid_t dtype_id, void *buf, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_attr_write(void *attr, hid_t dtype_id, const void *buf, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_attr_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                         H5VL_attr_specific_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_attr_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_attr_close(void *attr, hid_t dxpl_id, void **req);

/* Dataset callbacks */
H5_DLL void  *mt_native_wrapper_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                          hid_t lcpl_id, hid_t type_id, hid_t space_id, hid_t dcpl_id,
                                          hid_t dapl_id, hid_t dxpl_id, void **req);
H5_DLL void  *mt_native_wrapper_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                        hid_t dapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_dataset_read(size_t count, void *obj[], hid_t mem_type_id[], hid_t mem_space_id[],
                                        hid_t file_space_id[], hid_t dxpl_id, void *buf[], void **req);
H5_DLL herr_t mt_native_wrapper_dataset_write(size_t count, void *obj[], hid_t mem_type_id[], hid_t mem_space_id[],
                                         hid_t file_space_id[], hid_t dxpl_id, const void *buf[], void **req);
H5_DLL herr_t mt_native_wrapper_dataset_get(void *dset, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_dataset_specific(void *dset, H5VL_dataset_specific_args_t *args, hid_t dxpl_id,
                                            void **req);
H5_DLL herr_t mt_native_wrapper_dataset_optional(void *dset, H5VL_optional_args_t *args, hid_t dxpl_id,
                                            void **req);
H5_DLL herr_t mt_native_wrapper_dataset_close(void *dset, hid_t dxpl_id, void **req);

/* Datatype callbacks */
H5_DLL void  *mt_native_wrapper_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                           hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id,
                                           hid_t dxpl_id, void **req);
H5_DLL void  *mt_native_wrapper_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                         hid_t tapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_datatype_get(void *dt, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_datatype_specific(void *dt, H5VL_datatype_specific_args_t *args, hid_t dxpl_id,
                                             void **req);
H5_DLL herr_t mt_native_wrapper_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* File callbacks */
H5_DLL void  *mt_native_wrapper_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id,
                                       hid_t dxpl_id, void **req);
H5_DLL void  *mt_native_wrapper_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id,
                                     void **req);
H5_DLL herr_t mt_native_wrapper_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_file_specific(void *file, H5VL_file_specific_args_t *args, hid_t dxpl_id,
                                         void **req);
H5_DLL herr_t mt_native_wrapper_file_optional(void *file, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_file_close(void *file, hid_t dxpl_id, void **req);

/* Group callbacks */
H5_DLL void  *mt_native_wrapper_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                        hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id,
                                        void **req);
H5_DLL void  *mt_native_wrapper_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                      hid_t gapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_group_specific(void *obj, H5VL_group_specific_args_t *args, hid_t dxpl_id,
                                          void **req);
H5_DLL herr_t mt_native_wrapper_group_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_group_close(void *grp, hid_t dxpl_id, void **req);

/* Link callbacks */
H5_DLL herr_t mt_native_wrapper_link_create(H5VL_link_create_args_t *args, void *obj,
                                       const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id,
                                       hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                                     const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id,
                                     hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                                     const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id,
                                     hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_link_get(void *obj, const H5VL_loc_params_t *loc_params,
                                    H5VL_link_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_link_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                         H5VL_link_specific_args_t *args, hid_t dxpl_id, void **req);

/* Object callbacks */
H5_DLL void *mt_native_wrapper_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type,
                                      hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_object_copy(void *src_obj, const H5VL_loc_params_t *loc_params1,
                                       const char *src_name, void *dst_obj,
                                       const H5VL_loc_params_t *loc_params2, const char *dst_name,
                                       hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_object_get(void *obj, const H5VL_loc_params_t *loc_params,
                                      H5VL_object_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_object_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                           H5VL_object_specific_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t mt_native_wrapper_object_optional(void *obj, const H5VL_loc_params_t *loc_params,
                                           H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Connector/container introspection functions */
H5_DLL herr_t mt_native_wrapper_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl,
                                                   const H5VL_class_t **conn_cls);
H5_DLL herr_t mt_native_wrapper_introspect_get_cap_flags(const void *info, uint64_t *cap_flags);
H5_DLL herr_t mt_native_wrapper_introspect_opt_query(void *obj, H5VL_subclass_t cls, int opt_type,
                                                uint64_t *flags);


/* Blob callbacks */
H5_DLL herr_t mt_native_wrapper_blob_put(void *obj, const void *buf, size_t size, void *blob_id, void *ctx);
H5_DLL herr_t mt_native_wrapper_blob_get(void *obj, const void *blob_id, void *buf, size_t size, void *ctx);
H5_DLL herr_t mt_native_wrapper_blob_specific(void *obj, void *blob_id, H5VL_blob_specific_args_t *args);

/* Token callbacks */
H5_DLL herr_t mt_native_wrapper_token_cmp(void *obj, const H5O_token_t *token1, const H5O_token_t *token2,
                                     int *cmp_value);
H5_DLL herr_t mt_native_wrapper_token_to_str(void *obj, H5I_type_t obj_type, const H5O_token_t *token,
                                        char **token_str);
H5_DLL herr_t mt_native_wrapper_str_to_token(void *obj, H5I_type_t obj_type, const char *token_str,
                                        H5O_token_t *token);

/* The VOL class struct */
static const H5VL_class_t mt_native_wrapper_vol_g = {
    H5VL_VERSION,                  /* VOL class struct version */
    MT_NATIVE_WRAPPER_VOL_CONNECTOR_VALUE, /* value            */
    MT_NATIVE_WRAPPER_VOL_CONNECTOR_NAME,  /* name             */
    0,                             /* connector version */
    H5VL_CAP_FLAG_FILE_BASIC | H5VL_CAP_FLAG_THREADSAFE, /* capability flags */
    NULL,                                                /* initialize       */
    NULL,                                                /* terminate */
    {
        /* info_cls */
        (size_t)0, /* info size    */
        NULL,      /* info copy    */
        NULL,      /* info compare */
        NULL,      /* info free    */
        NULL,      /* info to str  */
        NULL       /* str to info  */
    },
    {
        /* wrap_cls */
        NULL, /* get_object   */
        NULL, /* get_wrap_ctx */
        NULL, /* wrap_object  */
        NULL, /* unwrap_object */
        NULL  /* free_wrap_ctx */
    },
    {
        /* attribute_cls */
        mt_native_wrapper_attr_create,   /* create       */
        mt_native_wrapper_attr_open,     /* open         */
        mt_native_wrapper_attr_read,     /* read         */
        mt_native_wrapper_attr_write,    /* write        */
        mt_native_wrapper_attr_get,      /* get          */
        mt_native_wrapper_attr_specific, /* specific     */
        mt_native_wrapper_attr_optional, /* optional     */
        mt_native_wrapper_attr_close     /* close        */
    },
    {
        /* dataset_cls */
        mt_native_wrapper_dataset_create,   /* create       */
        mt_native_wrapper_dataset_open,     /* open         */
        mt_native_wrapper_dataset_read,     /* read         */
        mt_native_wrapper_dataset_write,    /* write        */
        mt_native_wrapper_dataset_get,      /* get          */
        mt_native_wrapper_dataset_specific, /* specific     */
        mt_native_wrapper_dataset_optional, /* optional     */
        mt_native_wrapper_dataset_close     /* close        */
    },
    {
        /* datatype_cls */
        mt_native_wrapper_datatype_commit,   /* commit       */
        mt_native_wrapper_datatype_open,     /* open         */
        mt_native_wrapper_datatype_get,      /* get          */
        mt_native_wrapper_datatype_specific, /* specific     */
        NULL,                           /* optional     */
        mt_native_wrapper_datatype_close     /* close        */
    },
    {
        /* file_cls */
        mt_native_wrapper_file_create,   /* create       */
        mt_native_wrapper_file_open,     /* open         */
        mt_native_wrapper_file_get,      /* get          */
        mt_native_wrapper_file_specific, /* specific     */
        mt_native_wrapper_file_optional, /* optional     */
        mt_native_wrapper_file_close     /* close        */
    },
    {
        /* group_cls */
        mt_native_wrapper_group_create,   /* create       */
        mt_native_wrapper_group_open,     /* open         */
        mt_native_wrapper_group_get,      /* get          */
        mt_native_wrapper_group_specific, /* specific     */
        mt_native_wrapper_group_optional, /* optional     */
        mt_native_wrapper_group_close     /* close        */
    },
    {
        /* link_cls */
        mt_native_wrapper_link_create,   /* create       */
        mt_native_wrapper_link_copy,     /* copy         */
        mt_native_wrapper_link_move,     /* move         */
        mt_native_wrapper_link_get,      /* get          */
        mt_native_wrapper_link_specific, /* specific     */
        NULL                        /* optional     */
    },
    {
        /* object_cls */
        mt_native_wrapper_object_open,     /* open         */
        mt_native_wrapper_object_copy,     /* copy         */
        mt_native_wrapper_object_get,      /* get          */
        mt_native_wrapper_object_specific, /* specific     */
        mt_native_wrapper_object_optional  /* optional     */
    },
    {
        /* introspect_cls */
        mt_native_wrapper_introspect_get_conn_cls,  /* get_conn_cls */
        mt_native_wrapper_introspect_get_cap_flags, /* get_cap_flags */
        mt_native_wrapper_introspect_opt_query,     /* opt_query    */
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
        mt_native_wrapper_blob_put,      /* put */
        mt_native_wrapper_blob_get,      /* get */
        mt_native_wrapper_blob_specific, /* specific */
        NULL                        /* optional */
    },
    {
        /* token_cls */
        mt_native_wrapper_token_cmp,    /* cmp            */
        mt_native_wrapper_token_to_str, /* to_str         */
        mt_native_wrapper_str_to_token  /* from_str       */
    },
    NULL /* optional     */
};

void *
mt_native_wrapper_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name, hid_t type_id, hid_t space_id, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_attr_create(obj, loc_params, attr_name, type_id, space_id, acpl_id, aapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

void *
mt_native_wrapper_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name, hid_t aapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_attr_open(obj, loc_params, attr_name, aapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_attr_read(void *attr, hid_t dtype_id, void *buf, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_attr_read(attr, dtype_id, buf, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_attr_write(void *attr, hid_t dtype_id, const void *buf, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_attr_write(attr, dtype_id, buf, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_attr_get(obj, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_attr_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_attr_specific_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_attr_specific(obj, loc_params, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_attr_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_attr_optional(obj, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_attr_close(void *attr, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_attr_close(attr, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

void *
mt_native_wrapper_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id, hid_t type_id, hid_t space_id, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_dataset_create(obj, loc_params, name, lcpl_id, type_id, space_id, dcpl_id, dapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

void *
mt_native_wrapper_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t dapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_dataset_open(obj, loc_params, name, dapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_dataset_read(size_t count, void *obj[], hid_t mem_type_id[], hid_t mem_space_id[], hid_t file_space_id[], hid_t dxpl_id, void *buf[], void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_dataset_read(count, obj, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_dataset_write(size_t count, void *obj[], hid_t mem_type_id[], hid_t mem_space_id[], hid_t file_space_id[], hid_t dxpl_id, const void *buf[], void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_dataset_write(count, obj, mem_type_id, mem_space_id, file_space_id, dxpl_id, buf, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_dataset_get(void *dset, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_dataset_get(dset, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_dataset_specific(void *dset, H5VL_dataset_specific_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_dataset_specific(dset, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_dataset_optional(void *dset, H5VL_optional_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_dataset_optional(dset, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_dataset_close(void *dset, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_dataset_close(dset, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

void *
mt_native_wrapper_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_datatype_commit(obj, loc_params, name, type_id, lcpl_id, tcpl_id, tapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

void *
mt_native_wrapper_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t tapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_datatype_open(obj, loc_params, name, tapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_datatype_get(void *dt, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_datatype_get(dt, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_datatype_specific(void *dt, H5VL_datatype_specific_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_datatype_specific(dt, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}


herr_t
mt_native_wrapper_datatype_close(void *dt, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_datatype_close(dt, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}


void *
mt_native_wrapper_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;
    
    H5_API_LOCK;
    ret_value = H5VL__native_file_create(name, flags, fcpl_id, fapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

void *
mt_native_wrapper_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_file_open(name, flags, fapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_file_get(file, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_file_specific(void *file, H5VL_file_specific_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_file_specific(file, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_file_optional(void *file, H5VL_optional_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_file_optional(file, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_file_close(void *file, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;


    H5_API_LOCK;
    ret_value = H5VL__native_file_close(file, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}


void *
mt_native_wrapper_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_group_create(obj, loc_params, name, lcpl_id, gcpl_id, gapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

void *
mt_native_wrapper_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t gapl_id, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_group_open(obj, loc_params, name, gapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_group_get(obj, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_group_specific(void *obj, H5VL_group_specific_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_group_specific(obj, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_group_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_group_optional(obj, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_group_close(void *grp, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_group_close(grp, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_link_create(H5VL_link_create_args_t *args, void *obj, const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_link_create(args, obj, loc_params, lcpl_id, lapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_link_copy(src_obj, loc_params1, dst_obj, loc_params2, lcpl_id, lapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_link_move(src_obj, loc_params1, dst_obj, loc_params2, lcpl_id, lapl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_link_get(obj, loc_params, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_link_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_specific_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_link_specific(obj, loc_params, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

void *
mt_native_wrapper_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type, hid_t dxpl_id, void **req) {
    void *ret_value = NULL;

    H5_API_LOCK;
    ret_value = H5VL__native_object_open(obj, loc_params, opened_type, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_object_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, const char *src_name, void *dst_obj, const H5VL_loc_params_t *loc_params2, const char *dst_name, hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_object_copy(src_obj, loc_params1, src_name, dst_obj, loc_params2, dst_name, ocpypl_id, lcpl_id, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_object_get(obj, loc_params, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_object_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_specific_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_object_specific(obj, loc_params, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_object_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args, hid_t dxpl_id, void **req) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_object_optional(obj, loc_params, args, dxpl_id, req);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl, const H5VL_class_t **conn_cls) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_introspect_get_conn_cls(obj, lvl, conn_cls);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_introspect_get_cap_flags(const void *info, uint64_t *cap_flags) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_introspect_get_cap_flags(info, cap_flags);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t mt_native_wrapper_introspect_opt_query(void *obj, H5VL_subclass_t cls, int opt_type, uint64_t *flags) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_introspect_opt_query(obj, cls, opt_type, flags);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_blob_put(void *obj, const void *buf, size_t size, void *blob_id, void *ctx) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_blob_put(obj, buf, size, blob_id, ctx);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_blob_get(void *obj, const void *blob_id, void *buf, size_t size, void *ctx) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_blob_get(obj, blob_id, buf, size, ctx);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_blob_specific(void *obj, void *blob_id, H5VL_blob_specific_args_t *args) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_blob_specific(obj, blob_id, args);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_token_cmp(void *obj, const H5O_token_t *token1, const H5O_token_t *token2, int *cmp_value) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_token_cmp(obj, token1, token2, cmp_value);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_token_to_str(void *obj, H5I_type_t obj_type, const H5O_token_t *token, char **token_str) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_token_to_str(obj, obj_type, token, token_str);
    H5_API_UNLOCK;

    return ret_value;
}

herr_t
mt_native_wrapper_str_to_token(void *obj, H5I_type_t obj_type, const char *token_str, H5O_token_t *token) {
    herr_t ret_value = SUCCEED;

    H5_API_LOCK;
    ret_value = H5VL__native_str_to_token(obj, obj_type, token_str, token);
    H5_API_UNLOCK;

    return ret_value;
}

/* These two functions are necessary to load this plugin using
 * the HDF5 library.
 */

H5PL_type_t H5PLget_plugin_type(void) { return H5PL_TYPE_VOL; }
const void *H5PLget_plugin_info(void) { return &mt_native_wrapper_vol_g; }