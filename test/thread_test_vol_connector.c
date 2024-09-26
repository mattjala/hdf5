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

/* Purpose:     A simple virtual object layer (VOL) connector with almost no
 *              functionality that is used for testing basic plugin handling
 *              (registration, etc.).
 */

/* For HDF5 plugin functionality */
#include "H5PLextern.h"

/* This connector's header */
#include "thread_test_vol_connector.h"

void *fake_open_file_open(const char *name, unsigned flags, hid_t fapl_id,
                          hid_t dxpl_id, void **req);

herr_t fake_open_file_close(void *file, hid_t dxpl_id, void **req);

herr_t fake_open_file_specific(void *obj, H5VL_file_specific_args_t *args,
                               hid_t dxpl_id, void **req);

void *fake_open_group_create(void *obj, const H5VL_loc_params_t *loc_params,
                             const char *name, hid_t lcpl_id, hid_t gcpl_id,
                             hid_t gapl_id, hid_t dxpl_id, void **req);

herr_t fake_open_introspect_opt_query(void *obj, H5VL_subclass_t subcls,
                                      int opt_type, uint64_t *flags);

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
        fake_open_file_open,     /* open             */
        NULL,                    /* get              */
        fake_open_file_specific, /* specific         */
        NULL,                    /* optional         */
        fake_open_file_close     /* close            */
    },
    {
        /* group_cls */
        fake_open_group_create, /* create           */
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
        fake_open_introspect_opt_query, /* opt_query        */
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
 * Function: fake_open_file_open
 *
 * Purpose: Always return success to pretend to open a file
 *
 * Return: (void*)1
 *-------------------------------------------------------------------------
 */
void *fake_open_file_open(const char *name, unsigned flags, hid_t fapl_id,
                          hid_t dxpl_id, void **req) {
  /* Silence warnings */
  (void)name;
  (void)flags;
  (void)fapl_id;
  (void)dxpl_id;
  (void)req;

  return (void *)1;
} /* end fake_open_file_open() */

herr_t fake_open_file_close(void *file, hid_t dxpl_id, void **req) {
  /* Silence warnings */
  (void)file;
  (void)dxpl_id;
  (void)req;

  return 0;
} /* end fake_open_file_close() */

/*--------------------------------------------------------------------------
 * Function: fake_open_file_specific
 *
 * Purpose: Implement H5Fis_accessible() to pretend to check file accessibility
 *    to satisfy the check when loading a connector during file open failure.
 *
 * Return: TBD
 *-------------------------------------------------------------------------
 */
herr_t fake_open_file_specific(void *obj, H5VL_file_specific_args_t *args,
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
    ret_value = -1;
    break;
  }

  return ret_value;
} /* end fake_open_file_specific() */

void *fake_open_group_create(void *obj, const H5VL_loc_params_t *loc_params,
                             const char *name, hid_t lcpl_id, hid_t gcpl_id,
                             hid_t gapl_id, hid_t dxpl_id, void **req) {
  bool lock_acquired = false;
  unsigned int lock_count = 0;

  /* Silence warnings */
  (void)obj;
  (void)loc_params;
  (void)name;
  (void)lcpl_id;
  (void)gcpl_id;
  (void)gapl_id;
  (void)dxpl_id;
  (void)req;

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)
  /* Release the global mutex from the library */
  if (H5TSmutex_release(&lock_count) < 0) {
    printf("Failed to release mutex!\n");
    return NULL;
  }

  /* Re-acquire mutex before re-entering library */
  while (!lock_acquired) {
    if (H5TSmutex_acquire(lock_count, &lock_acquired) < 0) {
      printf("Failed to re-acquire mutex!\n");
      return NULL;
    }
  }
#else
  /* Silence warnings */
  (void)lock_acquired;
  (void)lock_count;
#endif

  return (void *)1;
} /* end fake_open_group_create() */

herr_t fake_open_introspect_opt_query(void *obj, H5VL_subclass_t subcls,
                                      int opt_type, uint64_t *flags) {
  herr_t ret_value = 0;

  /* Silence warnings */
  (void)obj;
  (void)subcls;
  (void)opt_type;
  (void)flags;

  return ret_value;
} /* end fake_open_introspect_opt_query() */

/* These two functions are necessary to load this plugin using
 * the HDF5 library.
 */

H5PL_type_t H5PLget_plugin_type(void) { return H5PL_TYPE_VOL; }
const void *H5PLget_plugin_info(void) { return &thread_test_vol_g; }
