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
 *              multi-threaded access to the HDF5 library. Does not actually
 *              interact with a real storage layer.
 */

/* For HDF5 plugin functionality */
#include "H5PLextern.h"

/* This connector's header */
#include "thread_test_vol_connector.h"

#include <time.h>
#include <pthread.h>
#include <stdatomic.h>

#ifdef H5_HAVE_MULTITHREAD 
const struct timespec sleep_time_g = {0L, (1000 * 1000 * 100)};

void *thread_test_file_open(const char *name, unsigned flags, hid_t fapl_id,
                          hid_t dxpl_id, void **req);

herr_t thread_test_file_close(void *file, hid_t dxpl_id, void **req);

herr_t thread_test_file_specific(void *obj, H5VL_file_specific_args_t *args,
                               hid_t dxpl_id, void **req);

herr_t thread_test_file_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

herr_t thread_test_introspect_opt_query(void *obj, H5VL_subclass_t subcls,
                                      int opt_type, uint64_t *flags);

typedef struct vlock_test_args_t {
    hid_t file_id;
    _Atomic int *vlock_test_flag;
} vlock_test_args_t;

/* The VOL class struct */
static const H5VL_class_t thread_test_vol_g = {
    H5VL_VERSION,                  /* VOL class struct version */
    THREAD_TEST_VOL_CONNECTOR_VALUE, /* value            */
    THREAD_TEST_VOL_CONNECTOR_NAME,  /* name             */
    0,                             /* connector version */
    H5VL_CAP_FLAG_FILE_BASIC | H5VL_CAP_FLAG_THREADSAFE, /* capability flags */
    NULL,                                                /* initialize       */
    NULL,                                                /* terminate */
    {
        /* info_cls */
        (size_t)0, /* size             */
        NULL,      /* copy             */
        NULL,      /* compare          */
        NULL,      /* free             */
        NULL,      /* to_str           */
        NULL,      /* from_str         */
    },
    {
        /* wrap_cls */
        NULL, /* get_object       */
        NULL, /* get_wrap_ctx     */
        NULL, /* wrap_object      */
        NULL, /* unwrap_object    */
        NULL, /* free_wrap_ctx    */
    },
    {
        /* attribute_cls */
        NULL, /* create           */
        NULL, /* open             */
        NULL, /* read             */
        NULL, /* write            */
        NULL, /* get              */
        NULL, /* specific         */
        NULL, /* optional         */
        NULL  /* close            */
    },
    {
        /* dataset_cls */
        NULL, /* create           */
        NULL, /* open             */
        NULL, /* read             */
        NULL, /* write            */
        NULL, /* get              */
        NULL, /* specific         */
        NULL, /* optional         */
        NULL  /* close            */
    },
    {
        /* datatype_cls */
        NULL, /* commit           */
        NULL, /* open             */
        NULL, /* get_size         */
        NULL, /* specific         */
        NULL, /* optional         */
        NULL  /* close            */
    },
    {
        /* file_cls */
        NULL,                    /* create           */
        thread_test_file_open,     /* open             */
        NULL,                    /* get              */
        thread_test_file_specific, /* specific         */
        thread_test_file_optional, /* optional         */
        thread_test_file_close     /* close            */
    },
    {
        /* group_cls */
        NULL, /* create           */
        NULL,                   /* open             */
        NULL,                   /* get              */
        NULL,                   /* specific         */
        NULL,                   /* optional         */
        NULL                    /* close            */
    },
    {
        /* link_cls */
        NULL, /* create           */
        NULL, /* copy             */
        NULL, /* move             */
        NULL, /* get              */
        NULL, /* specific         */
        NULL  /* optional         */
    },
    {
        /* object_cls */
        NULL, /* open             */
        NULL, /* copy             */
        NULL, /* get              */
        NULL, /* specific         */
        NULL  /* optional         */
    },
    {
        /* introspect_cls */
        NULL,                           /* get_conn_cls     */
        NULL,                           /* get_cap_flags    */
        thread_test_introspect_opt_query, /* opt_query        */
    },
    {
        /* request_cls */
        NULL, /* wait             */
        NULL, /* notify           */
        NULL, /* cancel           */
        NULL, /* specific         */
        NULL, /* optional         */
        NULL  /* free             */
    },
    {
        /* blob_cls */
        NULL, /* put              */
        NULL, /* get              */
        NULL, /* specific         */
        NULL  /* optional         */
    },
    {
        /* token_cls */
        NULL, /* cmp              */
        NULL, /* to_str           */
        NULL  /* from_str         */
    },
    NULL /* optional         */
};

/*--------------------------------------------------------------------------
 * Function: thread_test_file_open
 *
 * Purpose: Always return success to pretend to open a file
 *
 * Return: (void*)1
 *-------------------------------------------------------------------------
 */
void *thread_test_file_open(const char *name, unsigned flags, hid_t fapl_id,
                          hid_t dxpl_id, void **req) {
  /* Silence warnings */
  (void)name;
  (void)flags;
  (void)fapl_id;
  (void)dxpl_id;
  (void)req;

  return (void *)1;
} /* end thread_test_file_open() */

herr_t thread_test_file_close(void *file, hid_t dxpl_id, void **req) {
  /* Silence warnings */
  (void)file;
  (void)dxpl_id;
  (void)req;

  return 0;
} /* end thread_test_file_close() */

herr_t thread_test_file_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req) {
    H5VL_native_file_optional_args_t *opt_args = NULL;
    size_t sleep_count = 0;
    herr_t ret_value = 0;

    /* Silence compiler warnings */
    (void)obj;
    (void)dxpl_id;
    (void)req;

    switch (args->op_type) {
        /* Used for invalid API usage test */
        case H5VL_NATIVE_FILE_GET_FILE_IMAGE: {
            opt_args = (H5VL_native_file_optional_args_t *)args->args;
            vlock_test_args_t *vlock_test_args = (vlock_test_args_t *)opt_args->get_file_image.buf;
            _Atomic int *vlock_test_flag = vlock_test_args->vlock_test_flag;

            /* Wait until another thread raises the flag or timeout */
            do {
                nanosleep(&sleep_time_g, NULL);
                sleep_count++;

                if (sleep_count > 100) {
                    ret_value = -1;
                    goto error;
                }

            } while(atomic_load(vlock_test_flag) == 0);

            break;
        }

        default:
            break;
    }

error:
    return ret_value;
}
/*--------------------------------------------------------------------------
 * Function: thread_test_file_specific
 *
 * Purpose: Implement H5Fis_accessible() to pretend to check file accessibility
 *    to satisfy the check when loading a connector during file open failure.
 * 
 *    Other operations are overloaded for specific thread-related tests with no
 *    relation to the original intended operation.
 *
 * Return: 0 on success, -1 on failure
 *-------------------------------------------------------------------------
 */
herr_t thread_test_file_specific(void *obj, H5VL_file_specific_args_t *args,
                               hid_t dxpl_id, void **req) {
  herr_t ret_value = 0;

  /* Silence warnings */
  (void)obj;
  (void)dxpl_id;
  (void)req;

  switch (args->op_type) {
    case H5VL_FILE_IS_ACCESSIBLE: {
        *args->args.is_accessible.accessible = true;
        break;
    }

    default:
        break;
  }

  return ret_value;
} /* end thread_test_file_specific() */

herr_t thread_test_introspect_opt_query(void *obj, H5VL_subclass_t subcls,
                                      int opt_type, uint64_t *flags) {
  herr_t ret_value = 0;

  /* Silence warnings */
  (void)obj;
  (void)subcls;
  (void)opt_type;
  (void)flags;

  return ret_value;
} /* end thread_test_introspect_opt_query() */

/* These two functions are necessary to load this plugin using
 * the HDF5 library.
 */

H5PL_type_t H5PLget_plugin_type(void) { return H5PL_TYPE_VOL; }
const void *H5PLget_plugin_info(void) { return &thread_test_vol_g; }

#endif /* H5_HAVE_MULTITHREAD */