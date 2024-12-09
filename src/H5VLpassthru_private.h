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
 * Purpose:	The private header file for the pass-through VOL connector.
 */
#ifndef H5VLpassthru_private_H
#define H5VLpassthru_private_H

/* Private headers needed by this file */
#include "H5VLpassthru.h" /* Pass-through VOL connector                 */

/************/
/* Typedefs */
/************/

/* The pass through VOL info object */
typedef struct H5VL_pass_through_t {
    hid_t under_vol_id; /* ID for underlying VOL connector */
    void *under_object; /* Info object for underlying VOL connector */
} H5VL_pass_through_t;

/* The pass through VOL wrapper context */
typedef struct H5VL_pass_through_wrap_ctx_t {
    hid_t under_vol_id;   /* VOL ID for under VOL */
    void *under_wrap_ctx; /* Object wrapping context for under VOL */
} H5VL_pass_through_wrap_ctx_t;

/******************************/
/* Library Private Prototypes */
/******************************/

#ifdef __cplusplus
extern "C" {
#endif

/* Helper routines */
H5_DLL H5VL_pass_through_t *H5VL_pass_through_new_obj(void *under_obj, hid_t under_vol_id);
H5_DLL herr_t               H5VL_pass_through_free_obj(H5VL_pass_through_t *obj);

/* "Management" callbacks */
H5_DLL herr_t H5VL_pass_through_init(hid_t vipl_id);
H5_DLL herr_t H5VL_pass_through_term(void);

/* VOL info callbacks */
H5_DLL void  *H5VL_pass_through_info_copy(const void *info);
H5_DLL herr_t H5VL_pass_through_info_cmp(int *cmp_value, const void *info1, const void *info2);
H5_DLL herr_t H5VL_pass_through_info_free(void *info);
H5_DLL herr_t H5VL_pass_through_info_to_str(const void *info, char **str);
H5_DLL herr_t H5VL_pass_through_str_to_info(const char *str, void **info);

/* VOL object wrap / retrieval callbacks */
H5_DLL void  *H5VL_pass_through_get_object(const void *obj);
H5_DLL herr_t H5VL_pass_through_get_wrap_ctx(const void *obj, void **wrap_ctx);
H5_DLL void  *H5VL_pass_through_wrap_object(void *obj, H5I_type_t obj_type, void *wrap_ctx);
H5_DLL void  *H5VL_pass_through_unwrap_object(void *obj);
H5_DLL herr_t H5VL_pass_through_free_wrap_ctx(void *obj);

/* Attribute callbacks */
H5_DLL void  *H5VL_pass_through_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name,
                                       hid_t type_id, hid_t space_id, hid_t acpl_id, hid_t aapl_id,
                                       hid_t dxpl_id, void **req);
H5_DLL void  *H5VL_pass_through_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *attr_name,
                                     hid_t aapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_attr_read(void *attr, hid_t dtype_id, void *buf, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_attr_write(void *attr, hid_t dtype_id, const void *buf, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_attr_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                         H5VL_attr_specific_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_attr_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_attr_close(void *attr, hid_t dxpl_id, void **req);

/* Dataset callbacks */
H5_DLL void  *H5VL_pass_through_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                          hid_t lcpl_id, hid_t type_id, hid_t space_id, hid_t dcpl_id,
                                          hid_t dapl_id, hid_t dxpl_id, void **req);
H5_DLL void  *H5VL_pass_through_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                        hid_t dapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_dataset_read(size_t count, void *obj[], hid_t mem_type_id[], hid_t mem_space_id[],
                                        hid_t file_space_id[], hid_t dxpl_id, void *buf[], void **req);
H5_DLL herr_t H5VL_pass_through_dataset_write(size_t count, void *obj[], hid_t mem_type_id[], hid_t mem_space_id[],
                                         hid_t file_space_id[], hid_t dxpl_id, const void *buf[], void **req);
H5_DLL herr_t H5VL_pass_through_dataset_get(void *dset, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_dataset_specific(void *dset, H5VL_dataset_specific_args_t *args, hid_t dxpl_id,
                                            void **req);
H5_DLL herr_t H5VL_pass_through_dataset_optional(void *dset, H5VL_optional_args_t *args, hid_t dxpl_id,
                                            void **req);
H5_DLL herr_t H5VL_pass_through_dataset_close(void *dset, hid_t dxpl_id, void **req);

/* Datatype callbacks */
H5_DLL void  *H5VL_pass_through_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                           hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id,
                                           hid_t dxpl_id, void **req);
H5_DLL void  *H5VL_pass_through_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                         hid_t tapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_datatype_get(void *dt, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_datatype_specific(void *dt, H5VL_datatype_specific_args_t *args, hid_t dxpl_id,
                                             void **req);
H5_DLL herr_t H5VL_pass_through_datatype_optional(void *dt, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* File callbacks */
H5_DLL void  *H5VL_pass_through_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id,
                                       hid_t dxpl_id, void **req);
H5_DLL void  *H5VL_pass_through_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id,
                                     void **req);
H5_DLL herr_t H5VL_pass_through_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_file_specific(void *file, H5VL_file_specific_args_t *args, hid_t dxpl_id,
                                         void **req);
H5_DLL herr_t H5VL_pass_through_file_optional(void *file, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_file_close(void *file, hid_t dxpl_id, void **req);

/* Group callbacks */
H5_DLL void  *H5VL_pass_through_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                        hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id,
                                        void **req);
H5_DLL void  *H5VL_pass_through_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                      hid_t gapl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_group_specific(void *obj, H5VL_group_specific_args_t *args, hid_t dxpl_id,
                                          void **req);
H5_DLL herr_t H5VL_pass_through_group_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_group_close(void *grp, hid_t dxpl_id, void **req);

/* Link callbacks */
H5_DLL herr_t H5VL_pass_through_link_create(H5VL_link_create_args_t *args, void *obj,
                                       const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id,
                                       hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                                     const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id,
                                     hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                                     const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id,
                                     hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_link_get(void *obj, const H5VL_loc_params_t *loc_params,
                                    H5VL_link_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_link_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                         H5VL_link_specific_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_link_optional(void *obj, const H5VL_loc_params_t *loc_params,
                                         H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Object callbacks */
H5_DLL void *H5VL_pass_through_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type,
                                      hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_object_copy(void *src_obj, const H5VL_loc_params_t *loc_params1,
                                       const char *src_name, void *dst_obj,
                                       const H5VL_loc_params_t *loc_params2, const char *dst_name,
                                       hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_object_get(void *obj, const H5VL_loc_params_t *loc_params,
                                      H5VL_object_get_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_object_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                           H5VL_object_specific_args_t *args, hid_t dxpl_id, void **req);
H5_DLL herr_t H5VL_pass_through_object_optional(void *obj, const H5VL_loc_params_t *loc_params,
                                           H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Connector/container introspection functions */
H5_DLL herr_t H5VL_pass_through_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl,
                                                   const H5VL_class_t **conn_cls);
H5_DLL herr_t H5VL_pass_through_introspect_get_cap_flags(const void *info, uint64_t *cap_flags);
H5_DLL herr_t H5VL_pass_through_introspect_opt_query(void *obj, H5VL_subclass_t cls, int opt_type,
                                                uint64_t *flags);

/* Request callbacks */
H5_DLL herr_t H5VL_pass_through_request_wait(void *req, uint64_t timeout, H5VL_request_status_t *status);
H5_DLL herr_t H5VL_pass_through_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx);
H5_DLL herr_t H5VL_pass_through_request_cancel(void *req, H5VL_request_status_t *status);
H5_DLL herr_t H5VL_pass_through_request_specific(void *req, H5VL_request_specific_args_t *args);
H5_DLL herr_t H5VL_pass_through_request_optional(void *req, H5VL_optional_args_t *args);
H5_DLL herr_t H5VL_pass_through_request_free(void *req);

/* Blob callbacks */
H5_DLL herr_t H5VL_pass_through_blob_put(void *obj, const void *buf, size_t size, void *blob_id, void *ctx);
H5_DLL herr_t H5VL_pass_through_blob_get(void *obj, const void *blob_id, void *buf, size_t size, void *ctx);
H5_DLL herr_t H5VL_pass_through_blob_specific(void *obj, void *blob_id, H5VL_blob_specific_args_t *args);
H5_DLL herr_t H5VL_pass_through_blob_optional(void *obj, void *blob_id, H5VL_optional_args_t *args);

/* Token callbacks */
H5_DLL herr_t H5VL_pass_through_token_cmp(void *obj, const H5O_token_t *token1, const H5O_token_t *token2,
                                     int *cmp_value);
H5_DLL herr_t H5VL_pass_through_token_to_str(void *obj, H5I_type_t obj_type, const H5O_token_t *token,
                                        char **token_str);
H5_DLL herr_t H5VL_pass_through_token_from_str(void *obj, H5I_type_t obj_type, const char *token_str,
                                        H5O_token_t *token);

/* Generic optional callback */
H5_DLL herr_t H5VL_pass_through_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

#ifdef __cplusplus
}
#endif


#endif /* H5VLpassthru_private_H */