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
 * Purpose:     The Virtual Object Layer as described in documentation.
 *              The purpose is to provide an abstraction on how to access the
 *              underlying HDF5 container, whether in a local file with
 *              a specific file format, or remotely on other machines, etc...
 */

/****************/
/* Module Setup */
/****************/

#include "H5VLmodule.h" /* This source code file is part of the H5VL module */

/***********/
/* Headers */
/***********/

#include "H5private.h"   /* Generic Functions                    */
#include "H5Eprivate.h"  /* Error handling                       */
#include "H5FLprivate.h" /* Free lists                           */
#include "H5Iprivate.h"  /* IDs                                  */
#include "H5MMprivate.h" /* Memory management                    */
#include "H5SLprivate.h" /* Skip lists				 */
#include "H5VLpkg.h"     /* Virtual Object Layer                 */

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/* Dynamic operation info */
typedef struct H5VL_dyn_op_t {
    char *op_name; /* Name of operation */
    int   op_val;  /* Value of operation */
} H5VL_dyn_op_t;

/********************/
/* Local Prototypes */
/********************/
static void H5VL__release_dyn_op(H5VL_dyn_op_t *dyn_op);

/*********************/
/* Package Variables */
/*********************/

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

/* The current optional operation values */
#ifdef H5_HAVE_MULTITHREAD
/* Initialize in H5VL_init_opt_operation_table() */
static _Atomic(int) H5VL_opt_vals_g[H5VL_SUBCLS_TOKEN + 1];
#else
static int     H5VL_opt_vals_g[H5VL_SUBCLS_TOKEN + 1] =
    {
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_NONE */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_INFO */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_WRAP */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_ATTR */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_DATASET */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_DATATYPE */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_FILE */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_GROUP */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_LINK */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_OBJECT */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_REQUEST */
        H5VL_RESERVED_NATIVE_OPTIONAL, /* H5VL_SUBCLS_BLOB */
        H5VL_RESERVED_NATIVE_OPTIONAL  /* H5VL_SUBCLS_TOKEN */
};
#endif

/* The current optional operations' info */
#ifdef H5_HAVE_MULTITHREAD
static _Atomic(H5SL_t *) H5VL_opt_ops_g[H5VL_SUBCLS_TOKEN + 1] =
#else
static H5SL_t *H5VL_opt_ops_g[H5VL_SUBCLS_TOKEN + 1] =
#endif
    {
        NULL, /* H5VL_SUBCLS_NONE */
        NULL, /* H5VL_SUBCLS_INFO */
        NULL, /* H5VL_SUBCLS_WRAP */
        NULL, /* H5VL_SUBCLS_ATTR */
        NULL, /* H5VL_SUBCLS_DATASET */
        NULL, /* H5VL_SUBCLS_DATATYPE */
        NULL, /* H5VL_SUBCLS_FILE */
        NULL, /* H5VL_SUBCLS_GROUP */
        NULL, /* H5VL_SUBCLS_LINK */
        NULL, /* H5VL_SUBCLS_OBJECT */
        NULL, /* H5VL_SUBCLS_REQUEST */
        NULL, /* H5VL_SUBCLS_BLOB */
        NULL  /* H5VL_SUBCLS_TOKEN */
    };

/* Declare a free list to manage the H5VL_class_t struct */
H5FL_DEFINE_STATIC(H5VL_dyn_op_t);

/*---------------------------------------------------------------------------
 * Function:    H5VL__release_dyn_op
 *
 * Purpose:     Release a dynamic operation info node
 *
 * Return:      Success:    Non-negative
 *              Failure:    Negative
 *
 *---------------------------------------------------------------------------
 */
static void
H5VL__release_dyn_op(H5VL_dyn_op_t *dyn_op)
{
    FUNC_ENTER_PACKAGE_NOERR

    H5MM_xfree(dyn_op->op_name);
    H5FL_FREE(H5VL_dyn_op_t, dyn_op);

    FUNC_LEAVE_NOAPI_VOID
} /* H5VL__release_dyn_op() */

/*---------------------------------------------------------------------------
 * Function:    H5VL__term_opt_operation_cb
 *
 * Purpose:     Callback for releasing a dynamically registered operation info
 *
 * Return:      Success:    Non-negative
 *              Failure:    Negative
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL__term_opt_operation_cb(void *_item, void H5_ATTR_UNUSED *key, void H5_ATTR_UNUSED *op_data)
{
    H5VL_dyn_op_t *item = (H5VL_dyn_op_t *)_item; /* Item to release */

    FUNC_ENTER_PACKAGE_NOERR

    /* Release the dynamically registered operation info */
    H5VL__release_dyn_op(item);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* H5VL__term_opt_operation_cb() */

/*---------------------------------------------------------------------------
 * Function:    H5VL__term_opt_operation
 *
 * Purpose:     Terminate the dynamically registered optional operations,
 *              releasing all operations.
 *
 * Return:      Success:    Non-negative
 *              Failure:    Negative
 *
 *---------------------------------------------------------------------------
 */
herr_t
H5VL__term_opt_operation(void)
{
    size_t subcls; /* Index over the elements of operation array */

    FUNC_ENTER_PACKAGE_NOERR
    H5_API_LOCK

    /* Iterate over the VOL subclasses */
    for (subcls = 0; subcls < NELMTS(H5VL_opt_vals_g); subcls++) {
        if (H5VL_opt_ops_g[subcls]) {
            H5SL_destroy(H5VL_opt_ops_g[subcls], H5VL__term_opt_operation_cb, NULL);
            H5VL_opt_ops_g[subcls] = NULL;
        }
    }

    H5_API_UNLOCK
    FUNC_LEAVE_NOAPI(SUCCEED)
} /* H5VL__term_opt_operation() */

#ifdef H5_HAVE_MULTITHREAD
/*---------------------------------------------------------------------------
 * Function:    H5VL__init_opt_operation_table
 *
 * Purpose:     Initialize the atomic optional operation table for VOL objects.
 *
 *---------------------------------------------------------------------------
 */
void
H5VL__init_opt_operation_table(void) {
    size_t subcls = 0; /* Index over the elements of operation array */

    FUNC_ENTER_PACKAGE_NOERR

    /* Iterate over the VOL subclasses */
    for (subcls = 0; subcls < NELMTS(H5VL_opt_vals_g); subcls++)
        atomic_init(&H5VL_opt_vals_g[subcls], H5VL_RESERVED_NATIVE_OPTIONAL);

    FUNC_LEAVE_NOAPI_VOID
} /* H5VL__init_opt_operation_table() */
#endif
/*---------------------------------------------------------------------------
 * Function:    H5VL__register_opt_operation
 *
 * Purpose:     Register a new optional operation for a VOL object subclass.
 *
 * Return:      Success:    Non-negative
 *              Failure:    Negative
 *
 *---------------------------------------------------------------------------
 */
herr_t
H5VL__register_opt_operation(H5VL_subclass_t subcls, const char *op_name, int *op_val)
{
    H5VL_dyn_op_t *new_op            = NULL;    /* Info about new operation */
    herr_t         ret_value         = SUCCEED; /* Return value */
    H5SL_t        *new_op_list       = NULL;    /* Newly created skip list, if any */
    H5SL_t        *op_list           = NULL;    /* Existing skip list */
    bool           new_list_inserted = false;   /* Whether a new skip list was inserted into global array */

    FUNC_ENTER_PACKAGE
    H5_API_LOCK

    /* Sanity checks */
    assert(op_val);
    assert(op_name && *op_name);

    /* Check for duplicate operation */
#ifdef H5_HAVE_MULTITHREAD
    if ((op_list = atomic_load(&H5VL_opt_ops_g[subcls])) != NULL) {
        if (NULL != H5SL_search(op_list, op_name))
            HGOTO_ERROR(H5E_VOL, H5E_EXISTS, FAIL, "operation name already exists");
    } /* end if */
#else
    if ((op_list = H5VL_opt_ops_g[subcls]) != NULL) {
        if (NULL != H5SL_search(H5VL_opt_ops_g[subcls], op_name))
            HGOTO_ERROR(H5E_VOL, H5E_EXISTS, FAIL, "operation name already exists");
    } /* end if */
#endif

    /* List does not exist: Create skip list for operations of this subclass */
    else {
        if ((new_op_list = H5SL_create(H5SL_TYPE_STR, NULL)) == NULL)
            HGOTO_ERROR(H5E_VOL, H5E_CANTCREATE, FAIL, "can't create skip list for operations");

#ifdef H5_HAVE_MULTITHREAD
        H5SL_t *expected = NULL;

        /* If another thread has concurrently set up this op list, abort and use that instead */
        if ((new_list_inserted =
                 atomic_compare_exchange_strong(&H5VL_opt_ops_g[subcls], &expected, new_op_list))) {
            op_list = new_op_list;
        }
        else {
            /* Another thread initialized the list */
            if ((op_list = atomic_load(&H5VL_opt_ops_g[subcls])) == NULL)
                HGOTO_ERROR(H5E_VOL, H5E_CANTGET, FAIL, "can't get skip list for operations");
        }
#else
        H5VL_opt_ops_g[subcls] = new_op_list;
        op_list                = new_op_list;
        new_list_inserted      = true;
#endif
    } /* end else */

    /* Register new operation */
    if (NULL == (new_op = H5FL_CALLOC(H5VL_dyn_op_t)))
        HGOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "can't allocate memory for dynamic operation info");
    if (NULL == (new_op->op_name = H5MM_strdup(op_name)))
        HGOTO_ERROR(H5E_VOL, H5E_CANTALLOC, FAIL, "can't allocate name for dynamic operation info");

#ifdef H5_HAVE_MULTITHREAD
    new_op->op_val = atomic_fetch_add(&H5VL_opt_vals_g[subcls], 1);
#else
    new_op->op_val = H5VL_opt_vals_g[subcls]++;
#endif

    /* Insert into subclass's skip list */
    if (H5SL_insert(op_list, new_op, new_op->op_name) < 0)
        HGOTO_ERROR(H5E_VOL, H5E_CANTINSERT, FAIL, "can't insert operation info into skip list");

    /* Return the next operation value to the caller */
    *op_val = new_op->op_val;

done:
    if (ret_value < 0 && new_op) {
        if (new_op->op_name)
            H5MM_xfree(new_op->op_name);
        H5FL_FREE(H5VL_dyn_op_t, new_op);
    }

    if (new_op_list && !new_list_inserted)
        H5SL_close(new_op_list);
    
    H5_API_UNLOCK
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5VL__register_opt_operation() */

/*---------------------------------------------------------------------------
 * Function:    H5VL__num_opt_operation
 *
 * Purpose:     Returns the # of currently registered optional operations
 *
 * Return:      # of registered optional operations / <can't fail>
 *
 *---------------------------------------------------------------------------
 */
size_t
H5VL__num_opt_operation(void)
{
    size_t subcls;        /* Index over the elements of operation array */
    size_t ret_value = 0; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR
    H5_API_LOCK

    /* Iterate over the VOL subclasses */
    for (subcls = 0; subcls < NELMTS(H5VL_opt_vals_g); subcls++)
        if (H5VL_opt_ops_g[subcls])
            ret_value += H5SL_count(H5VL_opt_ops_g[subcls]);

    H5_API_UNLOCK
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5VL__num_opt_operation() */

/*---------------------------------------------------------------------------
 * Function:    H5VL__find_opt_operation
 *
 * Purpose:     Look up a optional operation for a VOL object subclass, by name.
 *
 * Return:      Success:    Non-negative
 *              Failure:    Negative
 *
 *---------------------------------------------------------------------------
 */
herr_t
H5VL__find_opt_operation(H5VL_subclass_t subcls, const char *op_name, int *op_val)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE
    H5_API_LOCK

    /* Sanity checks */
    assert(op_val);
    assert(op_name && *op_name);

    /* Check for dynamic operations in the VOL subclass */
    if (H5VL_opt_ops_g[subcls]) {
        H5VL_dyn_op_t *dyn_op; /* Info about operation */

        /* Search for dynamic operation with correct name */
        if (NULL == (dyn_op = H5SL_search(H5VL_opt_ops_g[subcls], op_name)))
            HGOTO_ERROR(H5E_VOL, H5E_NOTFOUND, FAIL, "operation name isn't registered");

        /* Set operation value for user */
        *op_val = dyn_op->op_val;
    } /* end if */
    else
        HGOTO_ERROR(H5E_VOL, H5E_NOTFOUND, FAIL, "operation name isn't registered");

done:
    H5_API_UNLOCK
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5VL__find_opt_operation() */

/*---------------------------------------------------------------------------
 * Function:    H5VL__unregister_opt_operation
 *
 * Purpose:     Unregister a optional operation for a VOL object subclass, by name.
 *
 * Return:      Success:    Non-negative
 *              Failure:    Negative
 *
 *---------------------------------------------------------------------------
 */
herr_t
H5VL__unregister_opt_operation(H5VL_subclass_t subcls, const char *op_name)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE
    H5_API_LOCK

    /* Sanity checks */
    assert(op_name && *op_name);

    /* Check for dynamic operations in the VOL subclass */
    if (H5VL_opt_ops_g[subcls]) {
        H5VL_dyn_op_t *dyn_op; /* Info about operation */

        /* Search for dynamic operation with correct name */
        if (NULL == (dyn_op = H5SL_remove(H5VL_opt_ops_g[subcls], op_name)))
            HGOTO_ERROR(H5E_VOL, H5E_NOTFOUND, FAIL, "operation name isn't registered");

        /* Release the info for the operation */
        H5VL__release_dyn_op(dyn_op);

        /* Close the skip list, if no more operations in it */
        if (0 == H5SL_count(H5VL_opt_ops_g[subcls])) {
            if (H5SL_close(H5VL_opt_ops_g[subcls]) < 0)
                HGOTO_ERROR(H5E_VOL, H5E_CANTCLOSEOBJ, FAIL, "can't close dyn op skip list");
            H5VL_opt_ops_g[subcls] = NULL;
        } /* end if */
    }     /* end if */
    else
        HGOTO_ERROR(H5E_VOL, H5E_NOTFOUND, FAIL, "operation name isn't registered");

done:
    H5_API_UNLOCK
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5VL__unregister_opt_operation() */
