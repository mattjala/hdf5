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
 * H5Iint.c - Private routines for handling IDs
 */

/****************/
/* Module Setup */
/****************/

#include "H5Imodule.h" /* This source code file is part of the H5I module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions                        */
#include "H5Eprivate.h"  /* Error handling                           */
#include "H5FLprivate.h" /* Free Lists                               */
#include "H5Ipkg.h"      /* IDs                                      */
#include "H5MMprivate.h" /* Memory management                        */
#include "H5Tprivate.h"  /* Datatypes                                */
#include "H5VLprivate.h" /* Virtual Object Layer                     */

#ifdef H5_HAVE_MULTITHREAD 
#include "lfht.c" 
#endif /* H5_HAVE_MULTITHREAD */

/****************/
/* Local Macros */
/****************/

#ifdef H5_HAVE_MULTITHREAD

#define H5I_MT_DEBUG                    0
#define H5I_MT_DEBUG_DO_NOT_DISTURB     0

/* The multi-thread version of H5I uses the do_not_distub field in instances of 
 * H5I_mt_id_info_t to maintain mutual exclusion on kernels of the host instance
 * of H5I_mt_id_info_t while performing operations that can't be rolled back -- 
 * specifically user provided callbacks.  
 *
 * The correct solution is to require that user provided callbacks be multi-
 * thread safe, and be able to handle duplicate calls gracefully.  Were this
 * the case, the do_not_disturb flag sould not be necessary.
 *
 * Unfortunately, this solution can't be applied to the existing HDF5 library
 * without retro-fitting multi-thread support.  While this is desireable, the
 * necessary resources are nto available at present.
 *
 * To make matters worse, the HDF5 library makes recursive visits to index 
 * entries in some callback functions provided to H5I.  This results in 
 * deadlocks as the recursive call waits forever for the do_not_disturb flag
 * be reset.
 *
 * An obvious solution it to make the lock implemented with the do_not_disturb
 * flag recursive.  The easy way to do this would be to store the id of the
 * locking thread in the instance of H5I_mt_id_info_t.  While I may go this 
 * way eventually, C11 threads are not universally available yet,  Rather tnan
 * commit to a thread library, I decided to avoid this solution for now.
 *
 * Instead, observe that until the HDF5 library is made multithread safe, 
 * the global lock must be held by any thread that is active in any section
 * that is not multi-thread safe.  Thus, it should be safe to ignore the 
 * do_not_disturb flag whenever the global mutex is held.
 *
 * The H5I__HAVE_GLOBAL_MUTEX #define is set up to simulate this until such 
 * time as the mutex is moved below H5I.  In the serial build, the global 
 * mutex is held by default -- and thus for now the H5I__HAVE_GLOBAL_MUTEX
 * #define is set to TRUE.
 */
#define H5I__HAVE_GLOBAL_MUTEX          1

#endif /* H5_HAVE_MULTITHREAD */

/* Combine a Type number and an ID index into an ID */
#define H5I_MAKE(g, i) ((((hid_t)(g)&TYPE_MASK) << ID_BITS) | ((hid_t)(i)&ID_MASK))

/******************/
/* Local Typedefs */
/******************/

/* User data for iterator callback for retrieving an ID corresponding to an object pointer */
typedef struct {
    const void *object;   /* object pointer to search for */
    H5I_type_t  obj_type; /* type of object we are searching for */
    hid_t       ret_id;   /* ID returned */
} H5I_get_id_ud_t;

#ifdef H5_HAVE_MULTITHREAD

/* User data for iterator callback for ID iteration */
typedef struct {
    H5I_search_func_t user_func;  /* 'User' function to invoke */
    void             *user_udata; /* User data to pass to 'user' function */
    hbool_t           app_ref;    /* Whether this is an appl. ref. call */
    H5I_type_t        obj_type;   /* Type of object we are iterating over */
    hbool_t           have_global_mutex; /* whether the global mutex is held by this thread */
} H5I_iterate_ud_t;

#else /* H5_HAVE_MULTITHREAD */

/* User data for iterator callback for ID iteration */
typedef struct {
    H5I_search_func_t user_func;  /* 'User' function to invoke */
    void             *user_udata; /* User data to pass to 'user' function */
    hbool_t           app_ref;    /* Whether this is an appl. ref. call */
    H5I_type_t        obj_type;   /* Type of object we are iterating over */
} H5I_iterate_ud_t;

#endif /* H5_HAVE_MULTITHREAD */

/* User data for H5I__clear_type_cb */
#ifdef H5_HAVE_MULTITHREAD
typedef struct {
    H5I_mt_type_info_t *type_info; /* Pointer to the type's info to be cleared */
    hbool_t          force;     /* Whether to always remove the ID */
    hbool_t          app_ref;   /* Whether this is an appl. ref. call */
} H5I_mt_clear_type_ud_t;
#else /* H5_HAVE_MULTITHREAD */
typedef struct {
    H5I_type_info_t *type_info; /* Pointer to the type's info to be cleared */
    hbool_t          force;     /* Whether to always remove the ID */
    hbool_t          app_ref;   /* Whether this is an appl. ref. call */
} H5I_clear_type_ud_t;
#endif /* H5_HAVE_MULTITHREAD */

/********************/
/* Package Typedefs */
/********************/

/********************/
/* Local Prototypes */
/********************/

static herr_t H5I__mark_node(void *_id, void *key, void *udata);
static void  *H5I__remove_common(H5I_type_info_t *type_info, hid_t id);

#ifdef H5_HAVE_MULTITHREAD

static herr_t H5I__unwrap(void *object, H5I_type_t type, void **unwrapped_object_ptr);
static int    H5I__dec_ref(hid_t id, void **request, hbool_t app);

#else /* H5_HAVE_MULTITHREAD */

static void  *H5I__unwrap(void *object, H5I_type_t type);
static int    H5I__dec_ref(hid_t id, void **request);

#endif /* H5_HAVE_MULTITHREAD */

static int    H5I__dec_app_ref(hid_t id, void **request);
static int    H5I__dec_app_ref_always_close(hid_t id, void **request);
static int    H5I__find_id_cb(void *_item, void *_key, void *_udata);

#ifdef H5_HAVE_MULTITHREAD

static herr_t H5I__clear_mt_id_info_free_list(void);
static herr_t H5I__discard_mt_id_info(H5I_mt_id_info_t * id_info_ptr);
static H5I_mt_id_info_t * H5I__new_mt_id_info(hid_t id, unsigned count, unsigned app_count, const void * object, 
                                              hbool_t is_future, H5I_future_realize_func_t realize_cb, 
                                              H5I_future_discard_func_t discard_cb);
static herr_t H5I__clear_mt_type_info_free_list(void);
static herr_t H5I__discard_mt_type_info(H5I_mt_type_info_t * type_info_ptr);
static H5I_mt_type_info_t * H5I__new_mt_type_info(const H5I_class_t *cls, unsigned reserved);
#endif /* H5_HAVE_MULTITHREAD */

/*********************/
/* Package Variables */
/*********************/

/* Declared extern in H5Ipkg.h and documented there */
#ifdef H5_HAVE_MULTITHREAD

H5I_mt_t              H5I_mt_g;

#else /* H5_HAVE_MULTITHREAD */

H5I_type_info_t *H5I_type_info_array_g[H5I_MAX_NUM_TYPES];
int              H5I_next_type_g = (int)H5I_NTYPES;

/* Declare a free list to manage the H5I_id_info_t struct */
H5FL_DEFINE_STATIC(H5I_id_info_t);

/* Whether deletes are actually marks (for mark-and-sweep) */
static hbool_t H5I_marking_s = FALSE;

#endif /* H5_HAVE_MULTITHREAD */

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

/*-------------------------------------------------------------------------
 * Function:    H5I_init
 *
 * Purpose:     Initialize the interface from some other layer.
 *
 *              At present, this function performs initializations needed
 *              for the multi-thread build of H5I.  Thus it need not be 
 *              called in other contexts.
 *
 * Return:      Success:    Positive if any action was taken that might
 *                          affect some other interface; zero otherwise.
 *
 *              Failure:    Negative
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_init(void)
{
    herr_t     ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_MULTITHREAD
    int i;
    H5I_mt_id_info_sptr_t init_id_sptr = {NULL, 0ULL};
    H5I_mt_id_info_sptr_t id_sptr;
    H5I_mt_id_info_t * id_info_ptr;
    H5I_mt_type_info_sptr_t init_type_sptr = {NULL, 0ULL};
    H5I_mt_type_info_sptr_t type_sptr;
    H5I_mt_type_info_t * type_info_ptr;

    FUNC_ENTER_NOAPI(FAIL)

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__init() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* initialize cognates of existing globals in H5I_mt_g */

    for ( i = 0; i < H5I_MAX_NUM_TYPES; i++) {

        atomic_init(&(H5I_mt_g.type_info_array[i]), NULL);

        if ( i < H5I_NTYPES ) {

            atomic_init(&(H5I_mt_g.type_info_allocation_table[i]), TRUE);

        } else {

            atomic_init(&(H5I_mt_g.type_info_allocation_table[i]), FALSE);
        }

        atomic_init(&(H5I_mt_g.marking_array[i]), 0);
    }
    atomic_init(&(H5I_mt_g.next_type), (int)H5I_NTYPES);


    /* initialized new globals needed by the MT version of H5I */

    atomic_init(&(H5I_mt_g.active_threads), 0);


    /* initialize the id info free list */

    atomic_init(&(H5I_mt_g.id_info_fl_shead), init_id_sptr);
    atomic_init(&(H5I_mt_g.id_info_fl_stail), init_id_sptr);
    atomic_init(&(H5I_mt_g.id_info_fl_len), 0ULL);
    atomic_init(&(H5I_mt_g.max_desired_id_info_fl_len), H5I__MAX_DESIRED_ID_INFO_FL_LEN);
    atomic_init(&(H5I_mt_g.num_id_info_fl_entries_reallocable), 0ULL);

    /* allocate the initial entry in the id info free list and initialize the id info free list */
    id_info_ptr = H5I__new_mt_id_info(0, 0, 0, NULL, FALSE, NULL, NULL);
    if ( NULL == id_info_ptr) 
        HGOTO_ERROR(H5E_ID, H5E_CANTINIT, FAIL, "Can't initialize id info free list");

    atomic_store(&(id_info_ptr->on_fl), TRUE);

    id_sptr.ptr = id_info_ptr;
    id_sptr.sn = 1ULL;

    atomic_store(&(H5I_mt_g.id_info_fl_shead), id_sptr);
    atomic_store(&(H5I_mt_g.id_info_fl_stail), id_sptr);
    atomic_store(&(H5I_mt_g.id_info_fl_len), 1ULL);


    /* allocate the initial entry in the type info free list and initialize the type info free list */

    atomic_init(&(H5I_mt_g.type_info_fl_shead), init_type_sptr);
    atomic_init(&(H5I_mt_g.type_info_fl_stail), init_type_sptr);
    atomic_init(&(H5I_mt_g.type_info_fl_len), 0ULL);
    atomic_init(&(H5I_mt_g.max_desired_type_info_fl_len), H5I__MAX_DESIRED_TYPE_INFO_FL_LEN);
#if 1
    atomic_init(&(H5I_mt_g.num_type_info_fl_entries_reallocable), 0ULL);
#else
    H5I_suint64_t init_suint64 = {0ULL, 0ULL};
    atomic_init(&(H5I_mt_g.snum_type_info_fl_entries_reallocable), init_suint64);
#endif

    /* allocate the initial entry in the id info free list and initialize the id info free list */
    type_info_ptr = H5I__new_mt_type_info(NULL, 0);
    if ( NULL == type_info_ptr) 
        HGOTO_ERROR(H5E_ID, H5E_CANTINIT, FAIL, "Can't initialize type info free list");

    /* H5I__new_mt_type_info() sets up the lock free hash table -- must take it 
     * back down before we insert the new instance of H5I_mt_type_info_t on the
     * type info free list.
     */
    lfht_clear(&(type_info_ptr->lfht));
    atomic_store(&(type_info_ptr->lfht_cleared), TRUE);

    atomic_store(&(type_info_ptr->on_fl), TRUE);

    type_sptr.ptr = type_info_ptr;
    type_sptr.sn = 1ULL;

    atomic_store(&(H5I_mt_g.type_info_fl_shead), type_sptr);
    atomic_store(&(H5I_mt_g.type_info_fl_stail), type_sptr);
    atomic_store(&(H5I_mt_g.type_info_fl_len), 1ULL);


    /* initialize stats */

    atomic_init(&(H5I_mt_g.dump_stats_on_shutdown), FALSE);

    atomic_init(&(H5I_mt_g.init_type_registrations), 0ULL);
    atomic_init(&(H5I_mt_g.duplicate_type_registrations), 0ULL);
    atomic_init(&(H5I_mt_g.type_registration_collisions), 0ULL);

    atomic_init(&(H5I_mt_g.max_id_info_fl_len), 1ULL);
    atomic_init(&(H5I_mt_g.num_id_info_structs_alloced_from_heap), 1ULL); 
    atomic_init(&(H5I_mt_g.num_id_info_structs_alloced_from_fl), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_structs_freed), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_structs_added_to_fl), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_head_update_cols), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_tail_update_cols), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_append_cols), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_structs_marked_reallocatable), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_empty), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_fl_too_small), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_no_reallocable_entries), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_aborts), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_noops), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_num_reallocable_updates), 0ULL);
    atomic_init(&(H5I_mt_g.num_id_info_fl_num_reallocable_total), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__discard_mt_id_info__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__new_mt_id_info__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__clear_mt_id_info_free_list__num_calls), 0ULL);


    atomic_init(&(H5I_mt_g.max_type_info_fl_len), 1ULL);
    atomic_init(&(H5I_mt_g.num_type_info_structs_alloced_from_heap), 1ULL); 
    atomic_init(&(H5I_mt_g.num_type_info_structs_alloced_from_fl), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_structs_freed), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_structs_added_to_fl), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_head_update_cols), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_tail_update_cols), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_append_cols), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_structs_marked_reallocatable), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_empty), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_fl_too_small), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_no_reallocable_entries), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_aborts), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_noops), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_num_reallocable_updates), 0ULL);
    atomic_init(&(H5I_mt_g.num_type_info_fl_num_reallocable_total), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__discard_mt_type_info__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__new_mt_type_info__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__clear_mt_type_info_free_list__num_calls), 0ULL);


    atomic_init(&(H5I_mt_g.H5I__mark_node__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__num_calls_with_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__num_calls_without_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__already_marked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__marked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__marked_by_another_thread), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__no_ops), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_discard_cb), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_discard_cb), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_marked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_unmarked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__discard_cb_successes), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_free_func), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_free_func), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__free_func_failures_marked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__free_func_failures_unmarked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__free_func_successes), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__mark_node__retries), 0ULL);

    atomic_init(&(H5I_mt_g.H5I__remove_common__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__remove_common__already_marked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__remove_common__marked_by_another_thread), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__remove_common__marked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__remove_common__target_not_in_lfht), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__remove_common__retries), 0ULL);

    atomic_init(&(H5I_mt_g.H5I__find_id__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__num_calls_with_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__num_calls_with_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__ids_found), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__num_calls_to_realize_cb), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_realize_cb), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_realize_cb), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__num_calls_to_H5I__remove_common), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__num_calls_to_discard_cb), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_discard_cb), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_discard_cb), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__future_id_conversions_attempted), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__future_id_conversions_completed), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__find_id__retries), 0ULL);

    atomic_init(&(H5I_mt_g.H5I_register_using_existing_id__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_register_using_existing_id__num_failures), 0ULL);

    atomic_init(&(H5I_mt_g.H5I_subst__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_subst__num_calls__with_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_subst__num_calls__without_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_subst__marked_on_entry), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_subst__marked_during_call), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_subst__retries), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_subst__failures), 0ULL);

    atomic_init(&(H5I_mt_g.H5I__dec_ref__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__num_app_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__num_calls_with_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__num_calls_without_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__marked_on_entry), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__marked_during_call), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__marked), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__decremented), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__app_decremented), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__calls_to_free_func), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__global_mutex_locks_for_free_func), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__global_mutex_unlocks_for_free_func), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__free_func_failed), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__dec_ref__retries), 0ULL);

    atomic_init(&(H5I_mt_g.H5I__inc_ref__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__inc_ref__num_app_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__inc_ref__marked_on_entry), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__inc_ref__marked_during_call), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__inc_ref__incremented), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__inc_ref__app_incremented), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__inc_ref__retries), 0ULL);

    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_calls__with_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_calls__without_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__marked_during_call), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_user_func_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__global_mutex_locks_for_user_func), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__global_mutex_unlocks_for_user_func), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_user_func_successes), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_user_func_iter_stops), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_user_func_fails), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_user_func_skips), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__iterate_cb__num_retries), 0ULL);

    atomic_init(&(H5I_mt_g.H5I__unwrap__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__unwrap__num_calls_with_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__unwrap__num_calls_without_global_mutex), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5VL), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5VL), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5T), 0ULL);
    atomic_init(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5T), 0ULL);

    atomic_init(&(H5I_mt_g.H5I_is_file_object__num_calls), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_is_file_object__num_calls_to_H5T_is_named), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_is_file_object__global_mutex_locks_for_H5T_is_named), 0ULL);
    atomic_init(&(H5I_mt_g.H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named), 0ULL);

    atomic_init(&(H5I_mt_g.num_do_not_disturb_yields), 0ULL);
    atomic_init(&(H5I_mt_g.num_successful_do_not_disturb_sets), 0ULL);
    atomic_init(&(H5I_mt_g.num_failed_do_not_disturb_sets), 0ULL);
    atomic_init(&(H5I_mt_g.num_do_not_disturb_resets), 0ULL);
    atomic_init(&(H5I_mt_g.num_do_not_disturb_bypasses), 0ULL);

    atomic_init(&(H5I_mt_g.num_H5I_entries_via_public_API), 0ULL);
    atomic_init(&(H5I_mt_g.num_H5I_entries_via_internal_API), 0ULL);
    atomic_init(&(H5I_mt_g.num_H5I_entries_via_internal_API), 0ULL);
    atomic_init(&(H5I_mt_g.times_active_threads_is_zero), 0ULL);

done:

    FUNC_LEAVE_NOAPI(ret_value)

#else /* H5I__ MT */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    FUNC_LEAVE_NOAPI(ret_value)

#endif /* H5_HAVE_MULTITHREAD */

} /* H5I_init() */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I_term_package
 *
 * Purpose:     Terminate the H5I interface: release all memory, reset all
 *              global variables to initial values. This only happens if all
 *              types have been destroyed from other interfaces.
 *
 *              Modified for MT operation
 *
 * Return:      Success:    Positive if any action was taken that might
 *                          affect some other interface; zero otherwise.
 *
 *              Failure:    Negative
 *
 *-------------------------------------------------------------------------
 */
int
H5I_term_package(void)
{
    int in_use = 0; /* Number of ID types still in use */
    herr_t result;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_term_package() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    H5I_mt_type_info_t *type_info_ptr = NULL; /* Pointer to ID type */
    int              i;

    /* Count the number of types still in use */

    for (i = 0; i < atomic_load(&(H5I_mt_g.next_type)); i++) {
        if ((type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[i]))) && ! atomic_load(&(type_info_ptr->lfht_cleared))) {

            in_use++;
        }
    }

    /* If no types are still being used then clean up */
    if (0 == in_use) {
        for (i = 0; i <  atomic_load(&(H5I_mt_g.next_type)); i++) {
            type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[i]));
            if (type_info_ptr) {
                assert(atomic_load(&(type_info_ptr->lfht_cleared)));
#if 1 /* JRM */
                H5I__discard_mt_type_info(type_info_ptr);
                type_info_ptr = NULL;
#else /* JRM */
                type_info_ptr                = H5MM_xfree(type_info_ptr);
#endif /* JRM */
                atomic_store(&(H5I_mt_g.type_info_array[i]), NULL);
                in_use++;
            }
        }

        /* discard the contents of the id and type info free lists */
        result = H5I__clear_mt_id_info_free_list();
        assert( result >= 0 );

        result = H5I__clear_mt_type_info_free_list();
        assert( result >= 0 );

        if ( atomic_load(&(H5I_mt_g.dump_stats_on_shutdown)) ) {

            H5I_dump_stats(stdout);
        }

        H5I_clear_stats();
    }

    FUNC_LEAVE_NOAPI(in_use)
} /* end H5I_term_package() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_term_package
 *
 * Purpose:     Terminate the H5I interface: release all memory, reset all
 *              global variables to initial values. This only happens if all
 *              types have been destroyed from other interfaces.
 *
 * Return:      Success:    Positive if any action was taken that might
 *                          affect some other interface; zero otherwise.
 *
 *              Failure:    Negative
 *
 *-------------------------------------------------------------------------
 */
int
H5I_term_package(void)
{
    int in_use = 0; /* Number of ID types still in use */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    H5I_type_info_t *type_info = NULL; /* Pointer to ID type */
    int              i;

    /* Count the number of types still in use */
    for (i = 0; i < H5I_next_type_g; i++)
        if ((type_info = H5I_type_info_array_g[i]) && type_info->hash_table)
            in_use++;

    /* If no types are still being used then clean up */
    if (0 == in_use) {
        for (i = 0; i < H5I_next_type_g; i++) {
            type_info = H5I_type_info_array_g[i];
            if (type_info) {
                assert(NULL == type_info->hash_table);
                type_info                = H5MM_xfree(type_info);
                H5I_type_info_array_g[i] = NULL;
                in_use++;
            }
        }
    }

    FUNC_LEAVE_NOAPI(in_use)
} /* end H5I_term_package() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_clear_stats
 *
 * Purpose:     Reset the stats maintained in the H5I_mt_g global structure.
 *
 *              Note that these statistics are only maintained in the multi-
 *              thread implementation of H5I.
 *
 * Return:      void
 *
 * Changes:     None.
 *
 *-------------------------------------------------------------------------
 */
void
H5I_clear_stats(void)
{
    FUNC_ENTER_NOAPI_NOERR

    atomic_store(&(H5I_mt_g.init_type_registrations), 0ULL);
    atomic_store(&(H5I_mt_g.duplicate_type_registrations), 0ULL);
    atomic_store(&(H5I_mt_g.type_registration_collisions), 0ULL);

    atomic_store(&(H5I_mt_g.max_id_info_fl_len), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_structs_alloced_from_heap), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_structs_alloced_from_fl), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_structs_freed), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_structs_added_to_fl), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_head_update_cols), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_tail_update_cols), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_append_cols), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_structs_marked_reallocatable), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_empty), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_fl_too_small), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_no_reallocable_entries), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_aborts), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_noops), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_num_reallocable_updates), 0ULL);
    atomic_store(&(H5I_mt_g.num_id_info_fl_num_reallocable_total), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__discard_mt_id_info__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__new_mt_id_info__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__clear_mt_id_info_free_list__num_calls), 0ULL);

    atomic_store(&(H5I_mt_g.max_type_info_fl_len), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_structs_alloced_from_heap), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_structs_alloced_from_fl), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_structs_freed), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_structs_added_to_fl), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_head_update_cols), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_tail_update_cols), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_append_cols), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_structs_marked_reallocatable), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_empty), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_fl_too_small), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_no_reallocable_entries), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_aborts), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_noops), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_num_reallocable_updates), 0ULL);
    atomic_store(&(H5I_mt_g.num_type_info_fl_num_reallocable_total), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__discard_mt_type_info__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__new_mt_type_info__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__clear_mt_type_info_free_list__num_calls), 0ULL);

    atomic_store(&(H5I_mt_g.H5I__mark_node__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__num_calls_with_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__num_calls_without_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__already_marked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__marked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__marked_by_another_thread), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__no_ops), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_discard_cb), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_discard_cb), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_marked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_unmarked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__discard_cb_successes), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_free_func), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_free_func), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__free_func_failures_marked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__free_func_failures_unmarked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__free_func_successes), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__mark_node__retries), 0ULL);

    atomic_store(&(H5I_mt_g.H5I__remove_common__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__remove_common__already_marked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__remove_common__marked_by_another_thread), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__remove_common__marked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__remove_common__target_not_in_lfht), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__remove_common__retries), 0ULL);

    atomic_store(&(H5I_mt_g.H5I__find_id__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__num_calls_with_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__num_calls_without_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__ids_found), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__num_calls_to_realize_cb), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_realize_cb), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_realize_cb), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__num_calls_to_H5I__remove_common), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__num_calls_to_discard_cb), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_discard_cb), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_discard_cb), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__future_id_conversions_attempted), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__future_id_conversions_completed), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__find_id__retries), 0ULL);

    atomic_store(&(H5I_mt_g.H5I_register_using_existing_id__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_register_using_existing_id__num_failures), 0ULL);

    atomic_store(&(H5I_mt_g.H5I_subst__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_subst__num_calls__with_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_subst__num_calls__without_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_subst__marked_on_entry), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_subst__marked_during_call), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_subst__retries), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_subst__failures), 0ULL);

    atomic_store(&(H5I_mt_g.H5I__dec_ref__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__num_app_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__num_calls_with_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__num_calls_without_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__marked_on_entry), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__marked_during_call), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__marked), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__decremented), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__app_decremented), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__calls_to_free_func), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__global_mutex_locks_for_free_func), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__global_mutex_unlocks_for_free_func), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__free_func_failed), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__dec_ref__retries), 0ULL);

    atomic_store(&(H5I_mt_g.H5I__inc_ref__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__inc_ref__num_app_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__inc_ref__marked_on_entry), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__inc_ref__marked_during_call), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__inc_ref__incremented), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__inc_ref__app_incremented), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__inc_ref__retries), 0ULL);

    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_calls__with_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_calls__without_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__marked_during_call), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_user_func_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__global_mutex_locks_for_user_func), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__global_mutex_unlocks_for_user_func), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_user_func_successes), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_user_func_iter_stops), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_user_func_fails), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_user_func_skips), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__iterate_cb__num_retries), 0ULL);

    atomic_store(&(H5I_mt_g.H5I__unwrap__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__unwrap__num_calls_with_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__unwrap__num_calls_without_global_mutex), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5VL), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5VL), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5T), 0ULL);
    atomic_store(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5T), 0ULL);

    atomic_store(&(H5I_mt_g.H5I_is_file_object__num_calls), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_is_file_object__num_calls_to_H5T_is_named), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_is_file_object__global_mutex_locks_for_H5T_is_named), 0ULL);
    atomic_store(&(H5I_mt_g.H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named), 0ULL);

    atomic_store(&(H5I_mt_g.num_do_not_disturb_yields), 0ULL);
    atomic_store(&(H5I_mt_g.num_successful_do_not_disturb_sets), 0ULL);
    atomic_store(&(H5I_mt_g.num_failed_do_not_disturb_sets), 0ULL);
    atomic_store(&(H5I_mt_g.num_do_not_disturb_resets), 0ULL);
    atomic_store(&(H5I_mt_g.num_do_not_disturb_bypasses), 0ULL);

    atomic_store(&(H5I_mt_g.num_H5I_entries_via_public_API), 0ULL);
    atomic_store(&(H5I_mt_g.num_H5I_entries_via_internal_API), 0ULL);
    atomic_store(&(H5I_mt_g.max_active_threads), 0ULL);
    atomic_store(&(H5I_mt_g.times_active_threads_is_zero), 0ULL);

    FUNC_LEAVE_NOAPI_VOID;

} /* H5I_clear_stats() */

#endif /* H5I__INIT() */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_dump_stats
 *
 * Purpose:     Dump the stats maintained in the H5I_mt_g global structure
 *              to the specified file.
 *
 *              Note that these statistics are only maintained in the multi-
 *              thread implementation of H5I.
 *
 * Return:      void
 *
 * Changes:     None.
 *
 *-------------------------------------------------------------------------
 */
void
H5I_dump_stats(FILE * file_ptr)
{
    FUNC_ENTER_NOAPI_NOERR

    fprintf(file_ptr, "\n\nH5I Multi-Thread STATS:\n\n");

    fprintf(file_ptr, "H5I_mt_g.init_type_registrations                                       = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.init_type_registrations))));
    fprintf(file_ptr, "H5I_mt_g.duplicate_type_registrations                                  = %lld\n",
            (unsigned long long)(atomic_load(&(H5I_mt_g.duplicate_type_registrations))));
    fprintf(file_ptr, "H5I_mt_g.type_registration_collisions                                  = %lld\n\n",
            (unsigned long long)(atomic_load(&(H5I_mt_g.type_registration_collisions))));

    fprintf(file_ptr, "H5I_mt_g.id_info_fl_len                                                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.id_info_fl_len))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_entries_reallocable                            = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable))));
    fprintf(file_ptr, "H5I_mt_g.max_id_info_fl_len                                            = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.max_id_info_fl_len))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_alloced_from_heap                         = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_alloced_from_fl                           = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_freed                                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_freed))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_added_to_fl                               = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_head_update_cols                               = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_head_update_cols))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_tail_update_cols                               = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_tail_update_cols))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_append_cols                                    = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_append_cols))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_marked_reallocatable                      = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_marked_reallocatable))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty                  = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_frees_skipped_due_to_empty                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_empty))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_frees_skipped_due_to_fl_too_small              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_fl_too_small))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_frees_skipped_due_to_no_reallocable_entries    = %lld\n", 
            (unsigned long long) 
            (atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_no_reallocable_entries))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_update_aborts                  = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_aborts))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_update_noops                   = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_noops))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions              = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_updates                        = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_updates))));
    fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_total                          = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total))));
    fprintf(file_ptr, "H5I_mt_g.H5I__discard_mt_id_info__num_calls                            = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__discard_mt_id_info__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__new_mt_id_info__num_calls                                = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__new_mt_id_info__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__clear_mt_id_info_free_list__num_calls                    = %lld\n\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__clear_mt_id_info_free_list__num_calls))));

    fprintf(file_ptr, "H5I_mt_g.type_info_fl_len                                              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.type_info_fl_len))));
#if 1
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_entries_reallocable                          = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable))));
#else
    H5I_suint64_t snum_type_info_fl_entries_reallocable;
    snum_type_info_fl_entries_reallocable = atomic_load(&(H5I_mt_g.snum_type_info_fl_entries_reallocable));
    fprintf(file_ptr, "H5I_mt_g.snum_type_info_fl_entries_reallocable                         = (%lld, %lld)\n", 
            (unsigned long long)snum_type_info_fl_entries_reallocable.val,
            (unsigned long long)snum_type_info_fl_entries_reallocable.sn);
#endif
    fprintf(file_ptr, "H5I_mt_g.max_type_info_fl_len                                          = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.max_type_info_fl_len))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_alloced_from_heap                       = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_alloced_from_fl                         = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_freed                                   = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_freed))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_added_to_fl                             = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_head_update_cols                             = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_head_update_cols))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_tail_update_cols                             = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_tail_update_cols))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_append_cols                                  = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_append_cols))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_marked_reallocatable                    = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_marked_reallocatable))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_frees_skipped_due_to_empty                   = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_empty))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_frees_skipped_due_to_fl_too_small            = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_fl_too_small))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_frees_skipped_due_to_no_reallocable_entries  = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_no_reallocable_entries))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_update_aborts                = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_aborts))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_update_noops                 = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_noops))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions            = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_updates                      = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_updates))));
    fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_total                        = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total))));
    fprintf(file_ptr, "H5I_mt_g.H5I__discard_type_id_info__num_calls                          = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__discard_mt_type_info__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__new_mt_type_info__num_calls                              = %lld\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__new_mt_type_info__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__clear_mt_type_info_free_list__num_calls                  = %lld\n\n", 
            (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__clear_mt_type_info_free_list__num_calls))));

    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__num_calls                                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__num_calls_with_global_mutex                   = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls_with_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__num_calls_without_global_mutex                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls_without_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__already_marked                                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__already_marked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__marked                                        = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__marked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__marked_by_another_thread                      = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__marked_by_another_thread))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__no_ops                                        = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__no_ops))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__global_mutex_locks_for_discard_cb             = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_discard_cb))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_discard_cb           = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_discard_cb))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__discard_cb_failures_marked                    = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_marked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__discard_cb_failures_unmarked                  = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_unmarked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__discard_cb_successes                          = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_successes))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__global_mutex_locks_for_free_func              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_free_func))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_free_func            = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_free_func))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__free_func_failures_marked                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_failures_marked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__free_func_failures_unmarked                   = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_failures_unmarked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__free_func_successes                           = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_successes))));
    fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__retries                                       = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__retries))));

    fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__num_calls                                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__already_marked                            = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__already_marked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__marked_by_another_thread                  = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__marked_by_another_thread))));
    fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__marked                                    = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__marked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__target_not_in_lfht                        = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__target_not_in_lfht))));
    fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__retries                                   = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__retries))));

    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls                                       = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_with_global_mutex                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_with_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_without_global_mutex                  = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_without_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__ids_found                                       = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__ids_found))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_to_realize_cb                         = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_realize_cb))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__global_mutex_locks_for_realize_cb               = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_realize_cb))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_realize_cb             = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_realize_cb))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_to_H5I__remove_common                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_H5I__remove_common))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_to_discard_cb                         = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_discard_cb))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__global_mutex_locks_for_discard_cb               = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_discard_cb))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_discard_cb             = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_discard_cb))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__future_id_conversions_attempted                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__future_id_conversions_attempted))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__future_id_conversions_completed                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__future_id_conversions_completed))));
    fprintf(file_ptr, "H5I_mt_g.H5I__find_id__retries                                         = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__retries))));

    fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_calls                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_marked_only               = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only))));
    fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use         = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use))));
    fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_failures                  = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_failures))));

    fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_calls                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_marked_only               = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only))));
    fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use         = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use))));
    fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_failures                  = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_failures))));

    fprintf(file_ptr, "H5I_mt_g.H5I_subst__num_calls                                          = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I_subst__num_calls__with_global_mutex                       = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls__with_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I_subst__num_calls__without_global_mutex                    = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls__without_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I_subst__marked_on_entry                                    = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__marked_on_entry))));
    fprintf(file_ptr, "H5I_mt_g.H5I_subst__marked_during_call                                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__marked_during_call))));
    fprintf(file_ptr, "H5I_mt_g.H5I_subst__retries                                            = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__retries))));
    fprintf(file_ptr, "H5I_mt_g.H5I_subst__failures                                           = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__failures))));

    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__num_calls                                       = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__num_app_calls                                   = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_app_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__num_calls_with_global_mutex                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls_with_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__num_calls_without_global_mutex                  = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls_without_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__marked_on_entry                                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked_on_entry))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__marked_during_call                              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked_during_call))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__marked                                          = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__decremented                                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__decremented))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__app_decremented                                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__app_decremented))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__calls_to_free_func                              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__calls_to_free_func))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__global_mutex_locks_for_free_func                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__global_mutex_locks_for_free_func))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__global_mutex_unlocks_for_free_func              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__global_mutex_unlocks_for_free_func))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__free_func_failed                                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__free_func_failed))));
    fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__retries                                         = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__retries))));

    fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__num_calls                                       = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__num_app_calls                                   = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__num_app_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__marked_on_entry                                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__marked_on_entry))));
    fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__marked_during_call                              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__marked_during_call))));
    fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__incremented                                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__incremented))));
    fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__app_incremented                                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__app_incremented))));
    fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__retries                                         = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__retries))));

    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_calls                                    = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_calls__with_global_mutex                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls__with_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_calls__without_global_mutex              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls__without_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__marked_during_call                           = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__marked_during_call))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_calls                          = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__global_mutex_locks_for_user_func             = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__global_mutex_locks_for_user_func))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__global_mutex_unlocks_for_user_func           = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I__iterate_cb__global_mutex_unlocks_for_user_func))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_successes                      = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_successes))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_iter_stops                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_iter_stops))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_fails                          = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_fails))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_skips                          = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_skips))));
    fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_retries                                  = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_retries))));

    fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__num_calls                                        = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__num_calls_with_global_mutex                      = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls_with_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__num_calls_without_global_mutex                   = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls_without_global_mutex))));
    fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5VL               = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5VL))));
    fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5VL             = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5VL))));
    fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5T                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5T))));
    fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5T              = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5T))));

    fprintf(file_ptr, "H5I_mt_g.H5I_is_file_object__num_calls                                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_is_file_object__num_calls))));
    fprintf(file_ptr, "H5I_mt_g.H5I_is_file_object__num_calls_to_H5T_is_named                 = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_is_file_object__num_calls_to_H5T_is_named))));
    fprintf(file_ptr, "H5I_mt_g.H5I_is_file_object__global_mutex_locks_for_H5T_is_named       = %lld\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I_is_file_object__global_mutex_locks_for_H5T_is_named))));
    fprintf(file_ptr, "H5I_mt_g.H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named     = %lld\n\n", 
            (unsigned long long)
            (atomic_load(&(H5I_mt_g.H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named))));

    fprintf(file_ptr, "H5I_mt_g.num_do_not_disturb_yields                                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_yields))));
    fprintf(file_ptr, "H5I_mt_g.num_successful_do_not_disturb_sets                            = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_successful_do_not_disturb_sets))));
    fprintf(file_ptr, "H5I_mt_g.num_failed_do_not_disturb_sets                                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_failed_do_not_disturb_sets))));
    fprintf(file_ptr, "H5I_mt_g.num_do_not_disturb_resets                                     = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_resets))));
    fprintf(file_ptr, "H5I_mt_g.num_do_not_disturb_bypasses                                   = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_bypasses))));

    fprintf(file_ptr, "H5I_mt_g.num_H5I_entries_via_public_API                                = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_H5I_entries_via_public_API))));
    fprintf(file_ptr, "H5I_mt_g.num_H5I_entries_via_internal_API                              = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.num_H5I_entries_via_internal_API))));
    fprintf(file_ptr, "H5I_mt_g.max_active_threads                                            = %lld\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.max_active_threads))));
    fprintf(file_ptr, "H5I_mt_g.times_active_threads_is_zero                                  = %lld\n\n", 
            (unsigned long long)(atomic_load(&(H5I_mt_g.times_active_threads_is_zero))));

#if 0
    fprintf(file_ptr, " = %lld\n", 
            (unsigned long long)(atomic_load(&())));
#endif

    FUNC_LEAVE_NOAPI_VOID;

} /* H5I_dump_stats() */

/*-------------------------------------------------------------------------
 * Function:    H5I_dump_nz_stats
 *
 * Purpose:     Dump the stats maintained in the H5I_mt_g global structure
 *              that have non-zero values to the specified file.
 *
 *              Note that these statistics are only maintained in the multi-
 *              thread implementation of H5I.
 *
 * Return:      void
 *
 * Changes:     None.
 *
 *-------------------------------------------------------------------------
 */
void
H5I_dump_nz_stats(FILE * file_ptr, const char * tag)
{
    FUNC_ENTER_NOAPI_NOERR

    fprintf(file_ptr, "\n\nH5I Multi-Thread Non-Zero STATS: (%s)\n\n", tag);


    /* type registration stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.init_type_registrations))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.init_type_registrations                                       = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.init_type_registrations))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.duplicate_type_registrations))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.duplicate_type_registrations                                  = %lld\n",
                (unsigned long long)(atomic_load(&(H5I_mt_g.duplicate_type_registrations))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.type_registration_collisions))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.type_registration_collisions                                  = %lld\n",
                (unsigned long long)(atomic_load(&(H5I_mt_g.type_registration_collisions))));


    /* ID info free list stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.max_id_info_fl_len))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.max_id_info_fl_len                                            = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.max_id_info_fl_len))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_alloced_from_heap                         = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_alloced_from_fl                           = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_freed))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_freed                                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_freed))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_added_to_fl                               = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_head_update_cols))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_head_update_cols                               = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_head_update_cols))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_tail_update_cols))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_tail_update_cols                               = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_tail_update_cols))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_append_cols))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_append_cols                                    = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_append_cols))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_marked_reallocatable))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_structs_marked_reallocatable                      = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_structs_marked_reallocatable))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries)))
         > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_empty))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_frees_skipped_due_to_empty                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_empty))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_fl_too_small))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_frees_skipped_due_to_fl_too_small              = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_fl_too_small))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_no_reallocable_entries))) 
         > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_frees_skipped_due_to_no_reallocable_entries    = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_no_reallocable_entries))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_aborts))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_update_aborts                  = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_aborts))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_noops))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_update_noops                   = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_noops))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions              = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_updates))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_updates                        = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_updates))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_id_info_fl_num_reallocable_total                          = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__discard_mt_id_info__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__discard_mt_id_info__num_calls                            = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__discard_mt_id_info__num_calls))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__new_mt_id_info__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__new_mt_id_info__num_calls                                = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__new_mt_id_info__num_calls))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__clear_mt_id_info_free_list__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__clear_mt_id_info_free_list__num_calls                    = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__clear_mt_id_info_free_list__num_calls))));


    /* type info free list stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.max_type_info_fl_len))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.max_type_info_fl_len                                          = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.max_type_info_fl_len))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_alloced_from_heap                       = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_alloced_from_fl                         = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_freed))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_freed                                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_freed))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_added_to_fl                             = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_head_update_cols))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_head_update_cols                             = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_head_update_cols))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_tail_update_cols))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_tail_update_cols                             = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_tail_update_cols))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_append_cols))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_append_cols                                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_append_cols))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_marked_reallocatable))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_structs_marked_reallocatable                    = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_structs_marked_reallocatable))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty                = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty))));

    if ( (unsigned long long)
         (atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_empty))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_frees_skipped_due_to_empty                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_empty))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_fl_too_small))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_frees_skipped_due_to_fl_too_small            = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_fl_too_small))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_no_reallocable_entries)))
         > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_frees_skipped_due_to_no_reallocable_entries    = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_no_reallocable_entries))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_aborts))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_update_aborts                = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_aborts))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_noops))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_update_noops                 = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_noops))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions            = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_updates))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_updates                      = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_updates))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_type_info_fl_num_reallocable_total                        = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__discard_mt_type_info__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__discard_mt_type_info__num_calls                          = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__discard_mt_type_info__num_calls))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__new_mt_type_info__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__new_mt_type_info__num_calls                              = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__new_mt_type_info__num_calls))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__clear_mt_type_info_free_list__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__clear_mt_type_info_free_list__num_calls                  = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__clear_mt_type_info_free_list__num_calls))));



    /* H5I__mark_node() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__num_calls                                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls_with_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__num_calls_with_global_mutex                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls_with_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls_without_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__num_calls_without_global_mutex                = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__num_calls_without_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__already_marked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__already_marked                                = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__already_marked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__marked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__marked                                        = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__marked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__marked_by_another_thread))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__marked_by_another_thread                      = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__marked_by_another_thread))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__no_ops))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__no_ops                                        = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__no_ops))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_discard_cb))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__global_mutex_locks_for_discard_cb             = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_discard_cb))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_discard_cb))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_discard_cb           = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_discard_cb))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_marked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__discard_cb_failures_marked                    = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_marked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_unmarked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__discard_cb_failures_unmarked                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_unmarked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_successes))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__discard_cb_successes                          = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__discard_cb_successes))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_free_func))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__global_mutex_locks_for_free_func              = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_free_func))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_free_func))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_free_func            = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_free_func))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_failures_marked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__free_func_failures_marked                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_failures_marked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_failures_unmarked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__free_func_failures_unmarked                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_failures_unmarked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_successes))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__free_func_successes                           = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__free_func_successes))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__retries))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__mark_node__retries                                       = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__mark_node__retries))));


    /* H5I__remove_common() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__num_calls                                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__already_marked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__already_marked                            = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__already_marked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__marked_by_another_thread))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__marked_by_another_thread                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__marked_by_another_thread))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__marked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__marked                                    = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__marked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__target_not_in_lfht))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__target_not_in_lfht                        = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__target_not_in_lfht))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__retries))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__remove_common__retries                                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__remove_common__retries))));


    /* H5I__find_id() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls                                       = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_with_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_with_global_mutex                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_with_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_without_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_without_global_mutex                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_without_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__ids_found))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__ids_found                                       = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__ids_found))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_realize_cb))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_to_realize_cb                         = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_realize_cb))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_realize_cb))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__global_mutex_locks_for_realize_cb               = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_realize_cb))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_realize_cb))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_realize_cb             = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_realize_cb))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_H5I__remove_common))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_to_H5I__remove_common                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_H5I__remove_common))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_discard_cb))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__num_calls_to_discard_cb                         = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__num_calls_to_discard_cb))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_discard_cb))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__global_mutex_locks_for_discard_cb               = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_discard_cb))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_discard_cb))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_discard_cb             = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_discard_cb))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__future_id_conversions_attempted))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__future_id_conversions_attempted                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__future_id_conversions_attempted))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__future_id_conversions_completed))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__future_id_conversions_completed                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__future_id_conversions_completed))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__retries))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__find_id__retries                                         = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__find_id__retries))));


    /* H5I_register() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_calls                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_calls))));

    if ( (unsigned long long) (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_marked_only               = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use)))
         > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use         = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_failures))) > 0ULL ) 
        fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_failures                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_failures))));


    /* H5I_register_using_existing_id() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_calls                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only))) > 0ULL )
         fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_marked_only               = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use)))
          > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use         = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_failures))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_register_using_existing_id__num_failures                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_register_using_existing_id__num_failures))));


    /* H5I_subst() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_subst__num_calls                                          = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls__with_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_subst__num_calls__with_global_mutex                       = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls__with_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls__without_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_subst__num_calls__without_global_mutex                    = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__num_calls__without_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__marked_on_entry))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_subst__marked_on_entry                                    = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__marked_on_entry))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__marked_during_call))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_subst__marked_during_call                                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__marked_during_call))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__retries))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_subst__retries                                            = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__retries))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__failures))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_subst__failures                                           = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_subst__failures))));


    /* H5I__dec_ref() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__num_calls                                       = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_app_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__num_app_calls                                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_app_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls_with_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__num_calls_with_global_mutex                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls_with_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls_without_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__num_calls_without_global_mutex                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__num_calls_without_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked_on_entry))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__marked_on_entry                                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked_on_entry))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked_during_call))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__marked_during_call                              = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked_during_call))));
    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__marked                                          = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__marked))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__decremented))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__decremented                                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__decremented))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__app_decremented))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__app_decremented                                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__app_decremented))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__calls_to_free_func))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__calls_to_free_func                              = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__calls_to_free_func))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__global_mutex_locks_for_free_func))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__global_mutex_locks_for_free_func                = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__global_mutex_locks_for_free_func))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__global_mutex_unlocks_for_free_func))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__global_mutex_unlocks_for_free_func              = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__global_mutex_unlocks_for_free_func))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__free_func_failed))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__free_func_failed                                = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__free_func_failed))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__retries))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__dec_ref__retries                                         = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__dec_ref__retries))));


    /* H5I__inc_ref() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__num_calls))) > 0ULL ) 
        fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__num_calls                                       = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__num_app_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__num_app_calls                                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__num_app_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__marked_on_entry))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__marked_on_entry                                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__marked_on_entry))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__marked_during_call))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__marked_during_call                              = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__marked_during_call))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__incremented))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__incremented                                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__incremented))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__app_incremented))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__app_incremented                                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__app_incremented))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__retries))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__inc_ref__retries                                         = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__inc_ref__retries))));


    /* H5I__iterate_cb_stats() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_calls                                    = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls__with_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_calls__with_global_mutex                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls__with_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls__without_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_calls__without_global_mutex              = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_calls__without_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__marked_during_call))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__marked_during_call                           = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__marked_during_call))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_calls                          = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__global_mutex_locks_for_user_func))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__global_mutex_locks_for_user_func             = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__global_mutex_locks_for_user_func))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__global_mutex_unlocks_for_user_func))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__global_mutex_unlocks_for_user_func           = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I__iterate_cb__global_mutex_unlocks_for_user_func))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_successes))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_successes                      = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_successes))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_iter_stops))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_iter_stops                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_iter_stops))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_fails))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_fails                          = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_fails))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_skips))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_user_func_skips                          = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_user_func_skips))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_retries))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__iterate_cb__num_retries                                  = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__iterate_cb__num_retries))));


    /* H5I__unwrap() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__num_calls                                        = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls_with_global_mutex))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__num_calls_with_global_mutex                      = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls_with_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls_without_global_mutex))) > 0 )
        fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__num_calls_without_global_mutex                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__num_calls_without_global_mutex))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5VL))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5VL               = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5VL))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5VL))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5VL             = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5VL))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5T))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5T                = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5T))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5T))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5T              = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5T))));


    /* H5I_is_file_object() stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_is_file_object__num_calls))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_is_file_object__num_calls                                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_is_file_object__num_calls))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_is_file_object__num_calls_to_H5T_is_named))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_is_file_object__num_calls_to_H5T_is_named                 = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_is_file_object__num_calls_to_H5T_is_named))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_is_file_object__global_mutex_locks_for_H5T_is_named))) 
         > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_is_file_object__global_mutex_locks_for_H5T_is_named       = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I_is_file_object__global_mutex_locks_for_H5T_is_named))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named)))
         > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named     = %lld\n", 
                (unsigned long long)
                (atomic_load(&(H5I_mt_g.H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named))));


    /* do_not_disturb stats */

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_yields))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_do_not_disturb_yields                                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_yields))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_successful_do_not_disturb_sets))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_successful_do_not_disturb_sets                            = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_successful_do_not_disturb_sets))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_failed_do_not_disturb_sets))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_failed_do_not_disturb_sets                                = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_failed_do_not_disturb_sets))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_resets))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_do_not_disturb_resets                                     = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_resets))));

    if ( (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_bypasses))) > 0ULL )
        fprintf(file_ptr, "H5I_mt_g.num_do_not_disturb_bypasses                                   = %lld\n", 
                (unsigned long long)(atomic_load(&(H5I_mt_g.num_do_not_disturb_bypasses))));

    FUNC_LEAVE_NOAPI_VOID;

} /* H5I_dump_nz_stats() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_register_type
 *
 * Purpose:     Creates a new type of ID's to give out.
 *              The class is initialized or its reference count is incremented
 *              (if it is already initialized).
 *
 * Return:      SUCCEED/FAIL
 *
 * Changes:     Modified to support multi-thread operation in H5I.
 *
 *                                            JRM -- 08/26/23
 *
 *              To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_register_type() function to H5I_register_type_internal()
 *              and created a new version of H5I_register_type() that 
 *              simply calls H5I__enter(), H5I_register_type_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_register_type(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_register_type(const H5I_class_t *cls)
{
    herr_t              ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_register_type_internal(cls);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_register_type() */

herr_t
H5I_register_type_internal(const H5I_class_t *cls)
{
    H5I_mt_type_info_t *type_info_ptr  = NULL;    /* Pointer to the ID type*/
    H5I_mt_type_info_t *expected_ptr   = NULL;    /* Pointer to the ID type*/
    herr_t              result;                   /* for sanity checking */
    herr_t              ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

#if H5I_MT_DEBUG
    fprintf(stdout, "\n   H5I_register_type() called. cls->type = %d\n", (int)(cls->type));
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(cls);
    assert(cls->type > 0);
    assert((int)cls->type < H5I_MAX_NUM_TYPES);
    assert(atomic_load(&(H5I_mt_g.type_info_allocation_table[(int)(cls->type)])));

    /* Initialize the type */
    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[cls->type]));

    if ( NULL == type_info_ptr ) {

        /* allocate and initialize an instance of H5I_type_info_t */
#if 1 /* JRM */
        if (NULL == (type_info_ptr = H5I__new_mt_type_info(cls, 0)))

            HGOTO_ERROR(H5E_ID, H5E_CANTALLOC, FAIL, "ID type allocation failed");

        atomic_fetch_add(&(type_info_ptr->init_count), 1);
#else /* JRM  */
        if (NULL == (type_info_ptr = (H5I_mt_type_info_t *)H5MM_calloc(sizeof(H5I_mt_type_info_t))))
            HGOTO_ERROR(H5E_ID, H5E_CANTALLOC, FAIL, "ID type allocation failed");

        type_info_ptr->cls = cls;
        atomic_init(&(type_info_ptr->init_count), 0);
        atomic_init(&(type_info_ptr->id_count), 0ULL);
        atomic_init(&(type_info_ptr->nextid), cls->reserved);
        atomic_init(&(type_info_ptr->last_id_info), NULL);
        atomic_init(&(type_info_ptr->lfht_cleared), FALSE);
        lfht_init(&(type_info_ptr->lfht));
#endif /* JRM */

        /* now attempt to insert it into H5I_mt_g.type_info_array_[cls->type].  It is possible
         * that another thread has done the initialization while we were allocating and 
         * and initializing the instance of H5I_type_info_t.  If so, we will discard the
         * instance just initialized and simply increment the init_count on the instance 
         * that was created, initialized, and inserted by another thread.
         *
         * Recall that expected_ptr is initialized to NULL, but will be set to the current
         * value of H5I_mt_g.type_info_array[class->type] if it is not NULL.
         */
        if ( atomic_compare_exchange_strong(&(H5I_mt_g.type_info_array[cls->type]), &expected_ptr, type_info_ptr) ) {

            /* We inserted the new instance of H5I_type_info_t into H5I_mt_g.type_info_array[cls->type].
             * Update stats and goto done.
             */
            atomic_fetch_add(&(H5I_mt_g.init_type_registrations), 1);
            HGOTO_DONE(SUCCEED);

        } else {

            /* the atomic_compare_exchange_strong() failed because H5I_mt_g.type_info_array[cls->type] is
             * no longer NULL -- which means that another thread beat us to creating and installing 
             * the new instance of H5I_type_info_t.
             *
             * Thus we must discard the instance we just created, and increment the init_count field
             * of the existing instance of H5I_type_info_t.
             */
            assert(type_info_ptr != expected_ptr);

            atomic_fetch_sub(&(type_info_ptr->init_count), 1);

            lfht_clear(&(type_info_ptr->lfht));
            atomic_store(&(type_info_ptr->lfht_cleared), TRUE);
#if 1
            result = H5I__discard_mt_type_info(type_info_ptr);
            assert(result >= 0);
#else 
            H5MM_free(type_info_ptr);
#endif

            /* If I read the specs on atomic_compare_exchange_strong() correctly, expected_ptr should 
             * equal H5I_mt_g.type_info_array[cls->type] at this point.  Verify this.
             */
            assert(expected_ptr);
            assert(expected_ptr == atomic_load(&(H5I_mt_g.type_info_array[cls->type])));

            /* Having looked at the higher level code, I expect this will fail, although a deep
             * comparison of type_info->cls and cls should succeed.  Leave it for now to see
             * what happens.
             */
            assert(expected_ptr->cls == cls);

            /* Increment the number of type registration collisions.  Note that
             * we will also increment the number of duplicate type registrations.
             */
            atomic_fetch_add(&(H5I_mt_g.type_registration_collisions), 1);

            /* set type_info_ptr to expected_ptr so that we can increment the number
             * of registrations below.
             */
            type_info_ptr = expected_ptr;
        }
    }

    assert(type_info_ptr);
    assert(H5I__TYPE_INFO == type_info_ptr->tag);

    atomic_fetch_add(&(type_info_ptr->init_count), 1);

    /* update stats for a duplicate type registration*/
    atomic_fetch_add(&(H5I_mt_g.duplicate_type_registrations), 1);

done:

#if H5I_MT_DEBUG
    fprintf(stdout, "\n   H5I_register_type() returns %d\n", (int)(ret_value));
#endif /* H5I_MT_DEBUG */

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_register_type_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_register_type
 *
 * Purpose:     Creates a new type of ID's to give out.
 *              The class is initialized or its reference count is incremented
 *              (if it is already initialized).
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_register_type(const H5I_class_t *cls)
{
    H5I_type_info_t *type_info = NULL;    /* Pointer to the ID type*/
    herr_t           ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(cls);
    assert(cls->type > 0 && (int)cls->type < H5I_MAX_NUM_TYPES);

    /* Initialize the type */
    if (NULL == H5I_type_info_array_g[cls->type]) {
        /* Allocate the type information for new type */
        if (NULL == (type_info = (H5I_type_info_t *)H5MM_calloc(sizeof(H5I_type_info_t))))
            HGOTO_ERROR(H5E_ID, H5E_CANTALLOC, FAIL, "ID type allocation failed");
        H5I_type_info_array_g[cls->type] = type_info;
    }
    else {
        /* Get the pointer to the existing type */
        type_info = H5I_type_info_array_g[cls->type];
    }

    /* Initialize the ID type structure for new types */
    if (type_info->init_count == 0) {
        type_info->cls          = cls;
        type_info->id_count     = 0;
        type_info->nextid       = cls->reserved;
        type_info->last_id_info = NULL;
        type_info->hash_table   = NULL;
    }

    /* Increment the count of the times this type has been initialized */
    type_info->init_count++;

done:
    /* Clean up on error */
    if (ret_value < 0)
        if (type_info)
            H5MM_free(type_info);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_register_type() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_nmembers
 *
 * Purpose:     Returns the number of members in a type.
 *
 *              Updated for MT operation.
 *
 * Return:      Success:    Number of members; zero if the type is empty
 *                          or has been deleted.
 *
 *              Failure:    Negative
 *
 * Programmer:  Robb Matzke
 *              Wednesday, March 24, 1999
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_nmemgers() function to H5I_nmembers_internal()
 *              and created a new version of H5I_nmembers() that 
 *              simply calls H5I__enter(), H5I_nmembers_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_nmembers(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
int64_t
H5I_nmembers(H5I_type_t type)
{
    int64_t             ret_value = 0;    /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_nmembers_internal(type);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_nmembers() */

int64_t
H5I_nmembers_internal(H5I_type_t type)
{
    H5I_mt_type_info_t *type_info = NULL; /* Pointer to the ID type */
    int64_t             ret_value = 0;    /* Return value */

    FUNC_ENTER_NOAPI((-1))

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_nmembers() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Validate parameter */

    if ( ( type <= H5I_BADID ) || ( ((int)type) >= atomic_load(&H5I_mt_g.next_type) ) )
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    if ( ( NULL == (type_info = atomic_load(&(H5I_mt_g.type_info_array[type]))) ) || 
         ( atomic_load(&(type_info->init_count)) <= 0 ) )
        HGOTO_DONE(0);

    /* Set return value */
    H5_CHECKED_ASSIGN(ret_value, int64_t, atomic_load(&(type_info->id_count)), uint64_t);


done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_nmembers_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_nmembers
 *
 * Purpose:     Returns the number of members in a type.
 *
 * Return:      Success:    Number of members; zero if the type is empty
 *                          or has been deleted.
 *
 *              Failure:    Negative
 *
 * Programmer:  Robb Matzke
 *              Wednesday, March 24, 1999
 *
 *-------------------------------------------------------------------------
 */
int64_t
H5I_nmembers(H5I_type_t type)
{
    H5I_type_info_t *type_info = NULL; /* Pointer to the ID type */
    int64_t          ret_value = 0;    /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Validate parameter */
    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");
    if (NULL == (type_info = H5I_type_info_array_g[type]) || type_info->init_count <= 0)
        HGOTO_DONE(0);

    /* Set return value */
    H5_CHECKED_ASSIGN(ret_value, int64_t, type_info->id_count, uint64_t);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_nmembers() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I__unwrap
 *
 * Purpose:     Unwraps the object pointer for the 'item' that corresponds
 *              to an ID.
 *
 *              For the multi-thread case, it may be necessary for us to 
 *              grab the global mutex before invoking either 
 *              H5VL_object_data(), or H5T_get_actual_type().  This creates
 *              at least the technical possibility of flagging an error,
 *              which in turns requires a rework of the function call.
 *
 *              As a result, the un-wrapped pointer is returned in 
 *              *unwrapped_object_ptr, and the function returns either 
 *              SUCCEED or FAIL.  In the latter case, *unwrapped_object_ptr
 *              is undefined.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Quincey Koziol
 *              Friday, October 19, 2018
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__unwrap(void *object, H5I_type_t type, void **unwrapped_object_ptr)
{
    hbool_t have_global_mutex = TRUE; /* Trivially true in single thread builds */
    void *unwrapped_object;
    herr_t ret_value = SUCCEED;; /* Return value */

    FUNC_ENTER_PACKAGE

    atomic_fetch_add(&(H5I_mt_g.H5I__unwrap__num_calls), 1);

    /* Sanity checks */
    assert(object);
    assert(unwrapped_object_ptr);

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)

    if ( H5TS_have_mutex(&H5_g.init_lock, &have_global_mutex) < 0 )

        HGOTO_ERROR(H5E_LIB, H5E_CANTGET, FAIL, "Can't determine whether we have the global mutex");
        
#endif /* defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD) */

    if ( have_global_mutex ) {

        atomic_fetch_add(&(H5I_mt_g.H5I__unwrap__num_calls_with_global_mutex), 1);

    } else {

        atomic_fetch_add(&(H5I_mt_g.H5I__unwrap__num_calls_without_global_mutex), 1);
    }

    /* The stored object pointer might be an H5VL_object_t, in which
     * case we'll need to get the wrapped object struct (H5F_t *, etc.).
     */
    if (H5I_FILE == type || H5I_GROUP == type || H5I_DATASET == type || H5I_ATTR == type) {

        const H5VL_object_t *vol_obj;

        vol_obj   = (const H5VL_object_t *)object;

        if ( ! have_global_mutex ) {

            /* must wrap call to H5VL_object_data() in global mutex */

            atomic_fetch_add(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5VL), 1);
            H5_API_LOCK
            unwrapped_object = H5VL_object_data(vol_obj);
            H5_API_UNLOCK
            atomic_fetch_add(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5VL), 1);

        } else {

            unwrapped_object = H5VL_object_data(vol_obj);
        }
    } else if (H5I_DATATYPE == type) {

        H5T_t *dt = (H5T_t *)object;

        if ( ! have_global_mutex ) {

            /* must wrap call to H5T_get_actual_type() in global mutex */

            atomic_fetch_add(&(H5I_mt_g.H5I__unwrap__times_global_mutex_locked_for_H5T), 1);
            H5_API_LOCK
            unwrapped_object = (void *)H5T_get_actual_type(dt);
            H5_API_UNLOCK
            atomic_fetch_add(&(H5I_mt_g.H5I__unwrap__times_global_mutex_unlocked_for_H5T), 1);

        } else {

            unwrapped_object = (void *)H5T_get_actual_type(dt);
        }
    }
    else {

        unwrapped_object = object;
    }

    *unwrapped_object_ptr = unwrapped_object;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__unwrap() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__unwrap
 *
 * Purpose:     Unwraps the object pointer for the 'item' that corresponds
 *              to an ID.
 *
 * Return:      Pointer to the unwrapped pointer (can't fail)
 *
 * Programmer:  Quincey Koziol
 *              Friday, October 19, 2018
 *
 *-------------------------------------------------------------------------
 */
static void *
H5I__unwrap(void *object, H5I_type_t type)
{
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity checks */
    assert(object);

    /* The stored object pointer might be an H5VL_object_t, in which
     * case we'll need to get the wrapped object struct (H5F_t *, etc.).
     */
    if (H5I_FILE == type || H5I_GROUP == type || H5I_DATASET == type || H5I_ATTR == type) {
        const H5VL_object_t *vol_obj;

        vol_obj   = (const H5VL_object_t *)object;
        ret_value = H5VL_object_data(vol_obj); 
    }
    else if (H5I_DATATYPE == type) {
        H5T_t *dt = (H5T_t *)object;

        ret_value = (void *)H5T_get_actual_type(dt);
    }
    else
        ret_value = object;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__unwrap() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_clear_type
 *
 * Purpose:     Removes all objects from the type, calling the free
 *              function for each object regardless of the reference count.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Wednesday, March 24, 1999
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_clear_type() function to H5I_clear_type_internal()
 *              and created a new version of H5I_clear_type() that 
 *              simply calls H5I__enter(), H5I_clear_type_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_clear_type(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_clear_type(H5I_type_t type, hbool_t force, hbool_t app_ref)
{
    herr_t              ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_clear_type_internal(type, force, app_ref);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_clear_type() */

herr_t
H5I_clear_type_internal(H5I_type_t type, hbool_t force, hbool_t app_ref)
{
    H5I_mt_clear_type_ud_t     udata; /* udata struct for callback */
    H5I_mt_id_info_kernel_t    info_k;
    H5I_mt_id_info_t          *id_info_ptr = NULL;
    unsigned long long         id;
    void                      *value;
    herr_t                     ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_clear_type() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Validate parameters */
    if (type <= H5I_BADID || (int)type >= atomic_load(&(H5I_mt_g.next_type)))

        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    udata.type_info = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if ( ( udata.type_info == NULL ) || ( atomic_load(&(udata.type_info->init_count)) <= 0 ) )

        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Finish constructing udata */
    udata.force   = force;
    udata.app_ref = app_ref;

    /* Clearing a type is done in two phases (mark-and-sweep). This is because
     * the type's free callback can free other IDs, potentially corrupting
     * the data structure during the traversal.
     */

    /* Set marking flag */
    atomic_fetch_add(&(H5I_mt_g.marking_array[type]), 1);

    /* Mark nodes for deletion */
    if ( lfht_get_first(&(udata.type_info->lfht), &id, &value) ) {

        do {
            /* the single thread version of the code checks to see if the instance of 
             * H5I_id_info_t returned by either lfht_get_first() or lfht_get_next() is 
             * marked, and only calls H5I__mark_node() it it is not.  
             *
             * However, checking to see if *id_info_ptr is marked has become more expensive, 
             * as we must do an atomic_load to obtain the kernel, and then read the marked field.
             *
             * Further, H5I__mark_node() has to check the marked field anyway in its 
             * do-while loop.  Thus we now call H5I__mark_node() unconditionally.
             *
             * Recall that value is a pointer to H5I_id_info_t which has been cast to void *.
             */
            if (H5I__mark_node(value, NULL, (void *)&udata) < 0) {

                HGOTO_ERROR(H5E_ID, H5E_BADITER, FAIL, "iteration failed while clearing the ID type");
            }
        } while (lfht_get_next(&(udata.type_info->lfht), id, &id, &value));
    }

    /* Unset marking flag */
    atomic_fetch_sub(&(H5I_mt_g.marking_array[type]), 1);
    assert(atomic_load(&(H5I_mt_g.marking_array[type])) >= 0);

    /* Perform sweep */
    if ( lfht_get_first(&(udata.type_info->lfht), &id, &value) ) {

        do {
            id_info_ptr = (H5I_mt_id_info_t *)value;

            info_k = atomic_load(&(id_info_ptr->k));

            /* Only delete the id from the hash table if H5I_mt_g.marking_array[type] is zero ond the id is marked
             * for deletion.  Note that it is possible that another  thread will increment or decrement 
             * H5I_mt_g.marking_array[type] while this loop is running.  However, this should not matter since 
             * marking an id for deletion is a one way process, and the operations will appear to have
             * been executed in some order.
             */
            if ( ( 0 == atomic_load(&(H5I_mt_g.marking_array[type]) ) && ( info_k.marked ) ) ) {

                /* this delete may fail, as it is possible that another thread will have beaten 
                 * us to the actual deletion of the entry from the lock free hash table.  Thus 
                 * don't flag an error if lfht_delete() fails, but don't discard *id_info_ptr 
                 * unless it succeeds.
                 */
                if ( lfht_delete(&(udata.type_info->lfht), id) ) {

                    if ( H5I__discard_mt_id_info(id_info_ptr) < 0 )

                        HGOTO_ERROR(H5E_ID, H5E_CANTFREE, FAIL, "Can't add id info to free list");
                }
            }
        } while (lfht_get_next(&(udata.type_info->lfht), id, &id, &value));
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_clear_type_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_clear_type
 *
 * Purpose:     Removes all objects from the type, calling the free
 *              function for each object regardless of the reference count.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Wednesday, March 24, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_clear_type(H5I_type_t type, hbool_t force, hbool_t app_ref)
{
    H5I_clear_type_ud_t udata; /* udata struct for callback */
    H5I_id_info_t      *item      = NULL;
    H5I_id_info_t      *tmp       = NULL;
    herr_t              ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Validate parameters */
    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    udata.type_info = H5I_type_info_array_g[type];
    if (udata.type_info == NULL || udata.type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Finish constructing udata */
    udata.force   = force;
    udata.app_ref = app_ref;

    /* Clearing a type is done in two phases (mark-and-sweep). This is because
     * the type's free callback can free other IDs, potentially corrupting
     * the data structure during the traversal.
     */

    /* Set marking flag */
    H5I_marking_s = TRUE;

    /* Mark nodes for deletion */
    HASH_ITER(hh, udata.type_info->hash_table, item, tmp)
    {
        if (!item->marked)
            if (H5I__mark_node((void *)item, NULL, (void *)&udata) < 0)
                HGOTO_ERROR(H5E_ID, H5E_BADITER, FAIL, "iteration failed while clearing the ID type");
    }

    /* Unset marking flag */
    H5I_marking_s = FALSE;

    /* Perform sweep */
    HASH_ITER(hh, udata.type_info->hash_table, item, tmp)
    {
        if (item->marked) {
            HASH_DELETE(hh, udata.type_info->hash_table, item);
            item = H5FL_FREE(H5I_id_info_t, item);
        }
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_clear_type() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I__mark_node
 *
 * Purpose:     Attempts to mark the node for freeing and calls the free
 *              function for the object, if any
 *
 *              Addendum 9/6/23:
 *
 *              A more detailed description of the action of this function
 *              is necessary for the multi-thread conversion.  From reading 
 *              the code of the single thread version, I get the following.
 *
 *              if udata->force is set, or 
 *
 *                 udata->app_ref is TRUE and info_ptr->count <= 1, or
 *
 *                 udata->app_ref is FALSE and 
 *                  info_ptr->count - info_ptr->count <= 1
 *
 *              *info_ptr (the node in the original comment) is considered
 *              for marking.
 *
 *              If an instance of *info_ptr is considered for marking, it 
 *              will actually be marked if either:
 *
 *              1) info_ptr->is_future is TRUE, and either 
 *                 (info_ptr->discard_cb)((void *)info->object) succeeds or
 *                 udata->force is TRUE.
 *
 *              2) udata->type_info->cls->free_func is NULL, or either
 *                 (udata->type_info->cls->free_func)((void *)info_ptr->object, 
 *                 H5_REQUEST_NULL) succeeds or udata->force is TRUE.
 *
 *              Note that in both cases, the failed call to the discard_cb 
 *              or free_func is ignored.  Further, if udata->force is false, 
 *              *info_ptr with its possibly corrupted *object is left in 
 *              the index.
 *
 *              This seems questionable to me, but since this is what the
 *              single thread version of the code does, we will stay with
 *              it for now.  Obviously, this decision should be revisited
 *              once the prototype is up and running.
 *
 *                                                   -- JRM
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Neil Fortner
 *              Friday, July 10, 2015
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__mark_node(void *_info, void H5_ATTR_UNUSED *key, void *_udata)
{
    hbool_t                 is_candidate;
    hbool_t                 cant_roll_back;
    hbool_t                 do_not_disturb_set;
    hbool_t                 mark;
    hbool_t                 done = FALSE;
    hbool_t                 have_global_mutex = TRUE; /*trivially so for single thread builds */
    hbool_t                 cls_is_mt_safe;
    hbool_t                 bool_result;
    int                     pass = 0;
    H5I_mt_id_info_kernel_t info_k;
    H5I_mt_id_info_kernel_t mod_info_k;
    H5I_mt_id_info_t       *id_info_ptr  = (H5I_mt_id_info_t *)_info;        /* Current ID info being worked with */
    H5I_mt_clear_type_ud_t *udata        = (H5I_mt_clear_type_ud_t *)_udata; /* udata struct */
    herr_t                  result;
    herr_t                  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    memset(&info_k, 0, sizeof(H5I_mt_id_info_kernel_t));
    memset(&mod_info_k, 0, sizeof(H5I_mt_id_info_kernel_t));

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__mark_node() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity checks */
    assert(id_info_ptr);
    assert(udata);
    assert(udata->type_info);

    atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__num_calls), 1ULL);

    cls_is_mt_safe = ((udata->type_info->cls->flags & H5I_CLASS_IS_MT_SAFE) != 0);

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)

    if ( H5TS_have_mutex(&H5_g.init_lock, &have_global_mutex) < 0 )

        HGOTO_ERROR(H5E_LIB, H5E_CANTGET, FAIL, "Can't determine whether we have the global mutex");
        
#endif /* defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD) */

    if ( have_global_mutex ) {

        atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__num_calls_with_global_mutex), 1ULL);

    } else {

        atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__num_calls_without_global_mutex), 1ULL);
    }

    do {

        /* If another thread modified id_info_ptr-k while we are preparing our modified copy,
         * or if we need to set the do not disturb flag to prevent simultaneous calls to 
         * the future id discard_cb callback or the regular id free_func, we will have to 
         * re-run this do-while loop.  Since we start each pass fresh, start by reseting 
         * all the flags to their initial values.
         */
        is_candidate       = FALSE;
        cant_roll_back     = FALSE;
        do_not_disturb_set = FALSE;
        mark               = FALSE;

        /* increment the pass and log retries */
        if ( pass++ >= 1 ) {

            atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__retries), 1ULL);
        }

        /* load the atomic kernel from *id_info_ptr into info_k.  Note that this is a snapshot of the 
         * state of *id_info_ptr, and can be changed before we get to writing it back.
         */
        info_k = atomic_load(&(id_info_ptr->k));

        if ( info_k.marked ) {

            /* this is is already marked for deletion -- nothing to do here */

            /* update stats */
            if ( pass <= 1 ) {

                atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__already_marked), 1ULL);

            } else {

                atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__marked_by_another_thread), 1ULL);
            }
            break;
        }

        if ( info_k.do_not_disturb ) {
#if 0 
            if ( ( have_global_mutex ) && ( info_k.have_global_mutex ) ) {

                bypass_do_not_disturb = TRUE;

                atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_bypasses), 1ULL);

            } else {
#endif

                /* Another thread is in the process of performing an operation on the info kernel
                 * that can't be rolled back -- either a future id realize_cb or discard_cb, or a 
                 * regular id free_func.  
                 *
                 * Thus we must wait until that thread is done and then re-start the operation -- which
                 * may be moot by that point.
                 */

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_yields), 1ULL);

                /* need to do better than this.  Want to call pthread_yield(),
                 * but that call doesn't seem to be supported anymore.
                 */
                sleep(1);

                continue;
#if 0
            }
#endif
        }

        if ( ( udata->force ) || ( (info_k.count - ((!udata->app_ref) * info_k.app_count)) <= 1 ) ) {

            is_candidate = TRUE;

            if ( ( info_k.is_future ) || ( udata->type_info->cls->free_func ) ) {

                cant_roll_back = TRUE;
            }
        }

        if ( ! is_candidate ) {

            /* we have nothing to do -- just break out of the while loop */

            /* update stats */
            atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__no_ops), 1ULL);

            break;
        }

        if ( cant_roll_back ) {

            mod_info_k.count             = info_k.count;
            mod_info_k.app_count         = info_k.app_count;
            mod_info_k.object            = info_k.object;

            mod_info_k.marked            = info_k.marked;
            mod_info_k.do_not_disturb    = TRUE;
            mod_info_k.is_future         = info_k.is_future;
            mod_info_k.have_global_mutex = have_global_mutex;

            /* We don't want multiple threads trying to either realize or dispose of the 
             * data associated with the future id or trying to free the data associated 
             * with a regular id at the same time.
             *
             * To serialize such actions, we will attempt to set the do not disturb
             * flag.  If successful, this will prevent any other threads from modifying 
             * id_info_ptr->k until after it is set back to FALSE.
             */
            if ( ! atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k ) ) {

                /* Some other thread changed the value of id_info_ptr->k since we last read
                 * it.  Thus we must return to the beginning of the do loop and start 
                 * again.  Note that it is possible that by that time, there will be 
                 * nothing left to do.
                 */

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_failed_do_not_disturb_sets), 1ULL);

                continue;

            } else {

                do_not_disturb_set = TRUE;

#if 0 /* JRM */
                /* make info_k into a copy of the global kernel */
                info_k.do_not_disturb = TRUE;
#else /* JTM */
                /* On the face of it, it would seem that we could just update info_k
                 * to match mod_info_k, and use it in the next atomic_compare_exchange_strong()
                 * call.  However, for reason or reasons unknown, this doesn't work.
                 *
                 * Instead, we reload info_k after the atomic_compare_exchange_strong(),
                 * and verify that it contains the expected values.
                 */
                info_k = atomic_load(&(id_info_ptr->k));

                assert(info_k.count             == mod_info_k.count);
                assert(info_k.app_count         == mod_info_k.app_count);
                assert(info_k.object            == mod_info_k.object);

                assert(info_k.marked            == mod_info_k.marked);
                assert(info_k.do_not_disturb    == mod_info_k.do_not_disturb);
                assert(info_k.is_future         == mod_info_k.is_future);
                assert(info_k.have_global_mutex == mod_info_k.have_global_mutex);
#endif /* JRM */

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_successful_do_not_disturb_sets), 1ULL);
#if H5I_MT_DEBUG_DO_NOT_DISTURB
                fprintf(stdout, "H5I__mark_node() set do not disturb on id = 0x%llx.\n",
                          (unsigned long long)(id_info_ptr->id));
#endif /* H5I_MT_DEBUG_DO_NOT_DISTURB */
            }
        }

        assert( ( do_not_disturb_set ) || ( ! cant_roll_back ) );

        if ( info_k.is_future ) {

            assert(do_not_disturb_set);

            /* Discard the future object */
            if ( ( ! have_global_mutex ) && ( ! cls_is_mt_safe ) ) {

                atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_discard_cb), 1ULL);
                H5_API_LOCK
                H5_GCC_CLANG_DIAG_OFF("cast-qual")
                result = (id_info_ptr->discard_cb)((void *)info_k.object);
                H5_GCC_CLANG_DIAG_ON("cast-qual")
                H5_API_UNLOCK
                atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_discard_cb), 1ULL);

            } else {

                H5_GCC_CLANG_DIAG_OFF("cast-qual")
                result = (id_info_ptr->discard_cb)((void *)info_k.object);
                H5_GCC_CLANG_DIAG_ON("cast-qual")
            }

            if ( result < 0 ) {

                /* discard_cb has failed -- ignore the failure -- but update stats below*/

                if (udata->force) {

                    /* if the force flag is set, mark the *id_info_ptr for deletion */
#ifdef H5I_DEBUG
                    if (H5DEBUG(I)) {
                        fprintf(H5DEBUG(I),
                                  "H5I: discard type=%d obj=0x%08lx "
                                  "failure ignored\n",
                                  (int)udata->type_info->cls->type, (unsigned long)(info_k.object));
                    }
#endif /* H5I_DEBUG */

                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_marked), 1ULL);

                    /* Indicate node should be removed from list */
                    mark = TRUE;

                } else {

                    /* If the force flag is not set, we leave *info_ptr alone and don't mark it 
                     * for deletion.  
                     *
                     * This seems questionable to me, since now info_ptr->object is potentially 
                     * corrupted.  However, that is what the single thread code does, so keep 
                     * it that way for now.  Obviously, this decision should be reviewed once 
                     * we have the prototype up and running.
                     *                                                JRM -- 9/8/23
                     */
                    atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__discard_cb_failures_unmarked), 1ULL);
                }
            }
            else { /* discard_cb succeeded */
                
                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__discard_cb_successes), 1ULL);

                /* Indicate node should be removed from list */
                mark = TRUE;
            }
        }
        else { /* it is a regular ID */

            /* Check for a 'free' function and call it, if it exists */
            if ( udata->type_info->cls->free_func ) {

                assert(do_not_disturb_set);

                if ( ( ! have_global_mutex ) && ( ! cls_is_mt_safe ) ) {

                    atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__global_mutex_locks_for_free_func), 1ULL);
                    H5_API_LOCK
                    H5_GCC_CLANG_DIAG_OFF("cast-qual")
                    result = (udata->type_info->cls->free_func)((void *)info_k.object, H5_REQUEST_NULL);
                    H5_GCC_CLANG_DIAG_ON("cast-qual")
                    H5_API_UNLOCK
                    atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__global_mutex_unlocks_for_free_func), 1ULL);

                } else {

                    H5_GCC_CLANG_DIAG_OFF("cast-qual")
                    result = (udata->type_info->cls->free_func)((void *)info_k.object, H5_REQUEST_NULL);
                    H5_GCC_CLANG_DIAG_ON("cast-qual")
                }

                if ( result < 0 ) {

                    /* the free function failed */

#if 0 /* JRM */
                    fprintf(stdout, "H5I__mark_node(): The free function failed.\n");
#endif /* JRM */

                    if (udata->force) {
#ifdef H5I_DEBUG
                        if (H5DEBUG(I)) {
                            fprintf(H5DEBUG(I),
                                      "H5I: free type=%d obj=0x%08lx "
                                      "failure ignored\n",
                                      (int)udata->type_info->cls->type, (unsigned long)(info_k.object));
                        }
#endif /* H5I_DEBUG */

                        /* update stats */
                        atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__free_func_failures_marked), 1ULL);

                        /* Indicate node should be removed from list */
                        mark = TRUE;

                    } else {

                        /* If the force flag is not set, we leave *info_ptr alone and don't mark it 
                         * for deletion.  
                         *
                         * This seems questionable to me, since now info_ptr->object is potentially 
                         * corrupted.  However, that is what the single thread code does, so keep 
                         * it that way for now.  Obviously, this decision should be reviewed once 
                         * we have the prototype up and running.
                         *                                                JRM -- 9/8/23
                         */
                        atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__free_func_failures_unmarked), 1ULL);
                    }
                }
                else { /* free function succeeded */

                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__free_func_successes), 1ULL);

                    /* Indicate node should be removed from list */
                    mark = TRUE;
#if 0 /* JRM */
                    fprintf(stdout, "H5I__mark_node(): The free function succeeded -- mark = %d.\n", (int)mark);
#endif /* JRM */
                }
            }
        }

        if ( ( mark ) || ( do_not_disturb_set ) ) {

            /* If we have set marked to TRUE, or if we have set the do_not_disturb flag, we 
             * must attempt to replace the current value of info_ptr->k with our modified 
             * version.
             *
             * First setup mod_info_k.  The only fields we will touch are do_not_disturb
             * and / or marked.  All other value are drawn from info_k that we read at the
             * top of the do/while loop.
             *
             * If mark == TRUE, set mod_info_k.count and mod_info_k.app_count to zero, and 
             * set info_k.object to FALSE. Similarly, set info_k.is_future to FALSE.  Do this
             * because the instance of H5I_mt_id_info_t and its associated id are effectively
             * deleted as soon as id_info_ptr->k.marked is set to TRUE.
             */
            if ( mark ) {

                mod_info_k.count             = 0;
                mod_info_k.app_count         = 0;
                mod_info_k.object            = NULL;

                mod_info_k.marked            = TRUE;
                mod_info_k.do_not_disturb    = FALSE;  
                mod_info_k.is_future         = FALSE;
                mod_info_k.have_global_mutex = FALSE;

            } else {

                mod_info_k.count             = info_k.count;
                mod_info_k.app_count         = info_k.app_count;
                mod_info_k.object            = info_k.object;

                mod_info_k.marked            = info_k.marked;
                mod_info_k.do_not_disturb    = FALSE;  
                mod_info_k.is_future         = info_k.is_future;
                mod_info_k.have_global_mutex = FALSE;
            }

            /* now attempt to overwrite the value of info_ptr->k.  If do_not_disturb_set is TRUE,
             * this must succeed -- hence we do it in an assert.  If do_not_disturb_set is FALSE,
             * it may or may not succeed.  On success we set done to TRUE.  Otherwise, some other 
             * therad has modified id_info_ptr->k since we read it, and we must try again.
             */
            if ( do_not_disturb_set ) {

                assert( info_k.do_not_disturb );
                assert( ! mod_info_k.do_not_disturb );

                bool_result = atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k);
                assert(bool_result);

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_resets), 1ULL);

                done = TRUE;

#if H5I_MT_DEBUG_DO_NOT_DISTURB
                fprintf(stdout, "H5I__mark_node() reset do not disturb on id = 0x%llx.\n",
                          (unsigned long long)(id_info_ptr->id));
#endif /* H5I_MT_DEBUG_DO_NOT_DISTURB */
        
            } else if ( atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k) ) {

                /* no need to update update stats here -- will increment H5I_mt_g.H5I__mark_node__marked
                 * after we exit the do/while loop
                 */

                done = TRUE;

            } else {

                /* the atomic compare exchange strong failed -- try again */

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__retries), 1ULL);
            }
        } else {

            /* no action required -- just set done to TRUE */

            /* update stats */

            done = TRUE;
        }
    } while ( ! done );

    if ( mark ) {

        /* update stats */
        atomic_fetch_add(&(H5I_mt_g.H5I__mark_node__marked), 1ULL);

        /* Decrement the number of IDs in the type */
        atomic_fetch_sub(&(udata->type_info->id_count), 1);
    }

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__mark_node() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__mark_node
 *
 * Purpose:     Attempts to mark the node for freeing and calls the free
 *              function for the object, if any
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Neil Fortner
 *              Friday, July 10, 2015
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__mark_node(void *_info, void H5_ATTR_UNUSED *key, void *_udata)
{
    H5I_id_info_t       *info  = (H5I_id_info_t *)_info;        /* Current ID info being worked with */
    H5I_clear_type_ud_t *udata = (H5I_clear_type_ud_t *)_udata; /* udata struct */
    hbool_t              mark  = FALSE;

    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity checks */
    assert(info);
    assert(udata);
    assert(udata->type_info);

    /* Do nothing to the object if the reference count is larger than
     * one and forcing is off.
     */
    if (udata->force || (info->count - (!udata->app_ref * info->app_count)) <= 1) {
        /* Check if this is an un-realized future object */
        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        if (info->is_future) {
            /* Discard the future object */
            if ((info->discard_cb)((void *)info->object) < 0) {
                if (udata->force) {
#ifdef H5I_DEBUG
                    if (H5DEBUG(I)) {
                        fprintf(H5DEBUG(I),
                                  "H5I: discard type=%d obj=0x%08lx "
                                  "failure ignored\n",
                                  (int)udata->type_info->cls->type, (unsigned long)(info->object));
                    }
#endif /* H5I_DEBUG */

                    /* Indicate node should be removed from list */
                    mark = TRUE;
                }
            }
            else {
                /* Indicate node should be removed from list */
                mark = TRUE;
            }
        }
        else {
            /* Check for a 'free' function and call it, if it exists */
            if (udata->type_info->cls->free_func &&
                (udata->type_info->cls->free_func)((void *)info->object, H5_REQUEST_NULL) < 0) {
                if (udata->force) {
#ifdef H5I_DEBUG
                    if (H5DEBUG(I)) {
                        fprintf(H5DEBUG(I),
                                  "H5I: free type=%d obj=0x%08lx "
                                  "failure ignored\n",
                                  (int)udata->type_info->cls->type, (unsigned long)(info->object));
                    }
#endif /* H5I_DEBUG */

                    /* Indicate node should be removed from list */
                    mark = TRUE;
                }
            }
            else {
                /* Indicate node should be removed from list */
                mark = TRUE;
            }
        }
        H5_GCC_CLANG_DIAG_ON("cast-qual")

        /* Remove ID if requested */
        if (mark) {
            /* Mark ID for deletion */
            info->marked = TRUE;

            /* Decrement the number of IDs in the type */
            udata->type_info->id_count--;
        }
    }

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5I__mark_node() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I__destroy_type
 *
 * Purpose:     Destroys a type along with all IDs in that type
 *              regardless of their reference counts. Destroying IDs
 *              involves calling the free-func for each ID's object and
 *              then adding the ID struct to the ID free list.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Nathaniel Furrer
 *              James Laird
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__destroy_type(H5I_type_t type)
{
    hbool_t             expected  = TRUE;
    hbool_t             result;
    H5I_mt_type_info_t *type_info_ptr = NULL;    /* Pointer to the ID type */
    herr_t              ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_PACKAGE

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_destroy_type() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Validate parameter */
    if (type <= H5I_BADID || (int)type >= atomic_load(&(H5I_mt_g.next_type)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));
    
    if (type_info_ptr == NULL || atomic_load(&(type_info_ptr->init_count)) <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Close/clear/destroy all IDs for this type */
    H5E_BEGIN_TRY
    {
        H5I_clear_type_internal(type, TRUE, FALSE);
    }
    H5E_END_TRY /* don't care about errors */

    /* Check if we should release the ID class */
    if (type_info_ptr->cls->flags & H5I_CLASS_IS_APPLICATION)
        type_info_ptr->cls = H5MM_xfree_const(type_info_ptr->cls);

    atomic_store(&(type_info_ptr->init_count), 0);

    lfht_clear(&(type_info_ptr->lfht));

    atomic_store(&(type_info_ptr->lfht_cleared), TRUE);

    result = atomic_compare_exchange_strong(&(H5I_mt_g.type_info_array[type]), &type_info_ptr, NULL);
    assert(result);

    result = atomic_compare_exchange_strong(&(H5I_mt_g.type_info_allocation_table[type]), &expected, FALSE);
    assert(result);

    H5I__discard_mt_type_info(type_info_ptr);
    type_info_ptr = NULL;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__destroy_type() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__destroy_type
 *
 * Purpose:     Destroys a type along with all IDs in that type
 *              regardless of their reference counts. Destroying IDs
 *              involves calling the free-func for each ID's object and
 *              then adding the ID struct to the ID free list.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Nathaniel Furrer
 *              James Laird
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__destroy_type(H5I_type_t type)
{
    H5I_type_info_t *type_info = NULL;    /* Pointer to the ID type */
    herr_t           ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Validate parameter */
    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    type_info = H5I_type_info_array_g[type];
    if (type_info == NULL || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Close/clear/destroy all IDs for this type */
    H5E_BEGIN_TRY
    {
        H5I_clear_type(type, TRUE, FALSE);
    }
    H5E_END_TRY /* don't care about errors */

        /* Check if we should release the ID class */
        if (type_info->cls->flags & H5I_CLASS_IS_APPLICATION)
            type_info->cls = H5MM_xfree_const(type_info->cls);

    HASH_CLEAR(hh, type_info->hash_table);

    type_info->hash_table = NULL;

    type_info = H5MM_xfree(type_info);

    H5I_type_info_array_g[type] = NULL;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__destroy_type() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I__register
 *
 * Purpose:     Registers an OBJECT in a TYPE and returns an ID for it.
 *              This routine does _not_ check for unique-ness of the objects,
 *              if you register an object twice, you will get two different
 *              IDs for it.  This routine does make certain that each ID in a
 *              type is unique.  IDs are created by getting a unique number
 *              for the type the ID is in and incorporating the TYPE into
 *              the ID which is returned to the user.
 *
 *              IDs are marked as "future" if the realize_cb and discard_cb
 *              parameters are non-NULL.
 *
 * Return:      Success:    New object ID
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5I__register(H5I_type_t type, const void *object, hbool_t app_ref, H5I_future_realize_func_t realize_cb,
              H5I_future_discard_func_t discard_cb)
{
    hbool_t             result;
    H5I_mt_type_info_t *type_info_ptr = NULL;            /* Pointer to the type */
    H5I_mt_id_info_t   *id_info_ptr   = NULL;            /* Pointer to the new ID information */
    hid_t               new_id        = H5I_INVALID_HID; /* New ID */
    hid_t               ret_value     = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_PACKAGE

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__register() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Check arguments */
    if ( ( type <= H5I_BADID ) || ( (int)type >= atomic_load(&(H5I_mt_g.next_type)) ) )

        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, H5I_INVALID_HID, "invalid type number");

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if ( ( NULL == type_info_ptr ) || ( atomic_load(&(type_info_ptr->init_count)) <= 0 ) )

        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, H5I_INVALID_HID, "invalid type");

    new_id = H5I_MAKE(type, atomic_fetch_add(&(type_info_ptr->nextid), 1ULL));

    /* strictly speaking there is a race condition here, but it doesn't matter which thread 
     * incremented nextid beyond its limit as long as we catch it.
     */
    assert(atomic_load(&(type_info_ptr->nextid)) <= ID_MASK); 

    id_info_ptr = H5I__new_mt_id_info(new_id, 1, !!app_ref, object, 
                                      (NULL != realize_cb), realize_cb, discard_cb);

    if ( NULL == id_info_ptr )

        HGOTO_ERROR(H5E_ID, H5E_NOSPACE, H5I_INVALID_HID, "allocation and init of new H5I_mt_type_info_t failed");

    /* Note that the insertion if the new ID is not completely atomic -- as we have three 
     * operations:
     *
     * 1) increment type_info_ptr->id_count.
     *
     * 2) insert into the lock free hash table.
     *
     * 3) set type_info_ptr->last_id_info.
     *
     * At present, these actions are performed in the above order.  
     *
     * The rational for incrementing the id_count first is that it will keep the index from 
     * being closed in some corner cases.
     *
     * I can't make a strong arguement for either ordering of the second two items, so please
     * view it as arbitrary until we come up with an argument to the contrary.
     */

    /* increment the id_count */
    atomic_fetch_add(&(type_info_ptr->id_count), 1ULL);

    /* Insert into the lock free hash table */
    /* todo -- make this throw and error */
    result = lfht_add(&(type_info_ptr->lfht), (unsigned long long int)new_id, (void *)id_info_ptr);
    assert(result);

    /* Set the most recent ID to this object */
    atomic_store(&(type_info_ptr->last_id_info), id_info_ptr);

    /* Set return value */
    ret_value = new_id;

done:

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__register() exiting. \n\n\n");
#endif /* H5I_MT_DEBUG */

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__register() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__register
 *
 * Purpose:     Registers an OBJECT in a TYPE and returns an ID for it.
 *              This routine does _not_ check for unique-ness of the objects,
 *              if you register an object twice, you will get two different
 *              IDs for it.  This routine does make certain that each ID in a
 *              type is unique.  IDs are created by getting a unique number
 *              for the type the ID is in and incorporating the TYPE into
 *              the ID which is returned to the user.
 *
 *              IDs are marked as "future" if the realize_cb and discard_cb
 *              parameters are non-NULL.
 *
 * Return:      Success:    New object ID
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5I__register(H5I_type_t type, const void *object, hbool_t app_ref, H5I_future_realize_func_t realize_cb,
              H5I_future_discard_func_t discard_cb)
{
    H5I_type_info_t *type_info = NULL;            /* Pointer to the type */
    H5I_id_info_t   *info      = NULL;            /* Pointer to the new ID information */
    hid_t            new_id    = H5I_INVALID_HID; /* New ID */
    hid_t            ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Check arguments */
    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, H5I_INVALID_HID, "invalid type number");
    type_info = H5I_type_info_array_g[type];
    if ((NULL == type_info) || (type_info->init_count <= 0))
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, H5I_INVALID_HID, "invalid type");
    if (NULL == (info = H5FL_CALLOC(H5I_id_info_t)))
        HGOTO_ERROR(H5E_ID, H5E_NOSPACE, H5I_INVALID_HID, "memory allocation failed");

    /* Create the struct & its ID */
    new_id           = H5I_MAKE(type, type_info->nextid);
    info->id         = new_id;
    info->count      = 1; /* initial reference count */
    info->app_count  = !!app_ref;
    info->object     = object;
    info->is_future  = (NULL != realize_cb);
    info->realize_cb = realize_cb;
    info->discard_cb = discard_cb;
    info->marked     = FALSE;

    /* Insert into the type */
    HASH_ADD(hh, type_info->hash_table, id, sizeof(hid_t), info);

    type_info->id_count++;
    type_info->nextid++;

    /* Sanity check for the 'nextid' getting too large and wrapping around */
    assert(type_info->nextid <= ID_MASK);

    /* Set the most recent ID to this object */
    type_info->last_id_info = info;

    /* Set return value */
    ret_value = new_id;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__register() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_register
 *
 * Purpose:     Library-private wrapper for H5I__register.
 *
 *              No changes required for multi-thread.
 *
 * Return:      Success:    New object ID
 *              Failure:    H5I_INVALID_HID
 *
 * Changes:     Added calls to H5I__enter() and H5I__exit() to track 
 *              the number of threads in H5I.  If 
 *              H5I_register() is ever called from within 
 *              H5I, we will need to add a boolean prameter to control 
 *              the H5I__enter/exit calls.
 *
 *                                          JRM -- 7/5/24
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5I_register(H5I_type_t type, const void *object, hbool_t app_ref)
{
    hid_t ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_NOAPI(H5I_INVALID_HID)

    H5I__enter(FALSE);

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I_register(type = %d, object = 0x%llx, app_ref = %d) called. \n", 
              (int)type, (unsigned long long)object, (int)app_ref);
#endif /* H5I_MT_DEBUG */

    /* Sanity checks */
    assert(type >= H5I_FILE && type < H5I_NTYPES);
    assert(object);

    /* Retrieve ID for object */
    if (H5I_INVALID_HID == (ret_value = H5I__register(type, object, app_ref, NULL, NULL)))
        HGOTO_ERROR(H5E_ID, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to register object");

done:

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I_register(type = %d, object = 0x%llx, app_ref = %d) returns %llx. \n", 
              (int)type, (unsigned long long)object, (int)app_ref, (unsigned long long)ret_value);
#endif /* H5I_MT_DEBUG */

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_register() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_register
 *
 * Purpose:     Library-private wrapper for H5I__register.
 *
 *              No changes required for multi-thread.
 *
 * Return:      Success:    New object ID
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5I_register(H5I_type_t type, const void *object, hbool_t app_ref)
{
    hid_t ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_NOAPI(H5I_INVALID_HID)

    /* Sanity checks */
    assert(type >= H5I_FILE && type < H5I_NTYPES);
    assert(object);

    /* Retrieve ID for object */
    if (H5I_INVALID_HID == (ret_value = H5I__register(type, object, app_ref, NULL, NULL)))
        HGOTO_ERROR(H5E_ID, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to register object");

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_register() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_register_using_existing_id
 *
 * Purpose:     Registers an OBJECT in a TYPE with the supplied ID for it.
 *              This routine will check to ensure the supplied ID is not already
 *              in use, and ensure that it is a valid ID for the given type,
 *              but will NOT check to ensure the OBJECT is not already
 *              registered (thus, it is possible to register one object under
 *              multiple IDs).
 *
 *              Re-worked for multi-thread.
 *
 * NOTE:        Intended for use in refresh calls, where we have to close
 *              and re-open the underlying data, then hook the object back
 *              up to the original ID.
 *
 * Return:      SUCCEED/FAIL
 *
 * Changes:     Added calls to H5I__enter() and H5I__exit() to track 
 *              the number of threads in H5I.  If 
 *              H5I_register_using_exiting_id() is ever called from within 
 *              H5I, we will need to add a boolean prameter to control 
 *              the H5I__enter/exit calls.
 *
 *                                          JRM -- 7/5/24
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_register_using_existing_id(H5I_type_t type, void *object, hbool_t app_ref, hid_t existing_id)
{
    hbool_t                 result;
    H5I_mt_id_info_kernel_t info_k;
    H5I_mt_type_info_t     *type_info_ptr    = NULL;    /* Pointer to the type */
    H5I_mt_id_info_t       *old_id_info_ptr  = NULL;    /* Pointer to the old ID information */
    H5I_mt_id_info_t       *new_id_info_ptr  = NULL;    /* Pointer to the new ID information */
    herr_t                  ret_value        = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    H5I__enter(FALSE);

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_register_using_existing_id() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    atomic_fetch_add(&(H5I_mt_g.H5I_register_using_existing_id__num_calls), 1ULL);

    /* Check arguments */
    assert(object);

    /* Make sure type number is valid */
    if ( ( type <= H5I_BADID ) || ( (int)type >= atomic_load(&(H5I_mt_g.next_type)) ) )

        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    /* Make sure ID is not already in use.
     *
     * Because of the H5I_mt_g.marking_array[type] flag, it is possible that an entry with the 
     * specified ID will exist in the lock free hash table, but be marked as deleted.
     *
     * This couldn't happen in the single thread version, but it becomes possible in multi-thread.
     *
     * The correct solution is probably to get rid of the H5I_mt_g.marking_array[] -- however that
     * doesn't seem prudent until the initial version is up and running, and I have a good 
     * understanding of why the mark and sweep approach was thought necessary.
     *
     * Thus, at present, it seems best to code around the issue, and be able to handle IDs that 
     * are still in the index but are marked as deleted.
     */
    if (NULL != (old_id_info_ptr = H5I__find_id(existing_id))) {

        info_k = atomic_load(&(old_id_info_ptr->k));

        if ( info_k.marked ) {

            atomic_fetch_add(&(H5I_mt_g.H5I_register_using_existing_id__num_marked_only), 1ULL);

        } else {

            atomic_fetch_add(&(H5I_mt_g.H5I_register_using_existing_id__num_id_already_in_use), 1ULL);
            HGOTO_ERROR(H5E_ID, H5E_BADRANGE, FAIL, "ID already in use");
        }
    }

    /* Get type pointer from list of types */
    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if ( ( NULL == type_info_ptr ) || ( atomic_load(&(type_info_ptr->init_count)) <= 0 ) )

        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Make sure requested ID belongs to object's type */
    if ( H5I_TYPE(existing_id) != type )

        HGOTO_ERROR(H5E_ID, H5E_BADRANGE, FAIL, "invalid type for provided ID");

    /* This API call is only used by the native VOL connector, which is not 
     * asynchronous -- for now at least.
     *
     * Hence is_future is FALSE, and both realize_cb and discard_cb are NULL
     */
    new_id_info_ptr = H5I__new_mt_id_info(existing_id, 1, !!app_ref, object, FALSE, NULL, NULL);

    if ( ! new_id_info_ptr )

        HGOTO_ERROR(H5E_ID, H5E_NOSPACE, FAIL, "memory allocation and init of new id info failed");

    /* Now insert the ID into the index.  The H5I_mt_g.marking_array[type] flag makes this more 
     * painful than it should be as it is possible that the id info for the existing ID
     * has only been marked for deletion, but not actually deleted from the lock free hash
     * table.  To make things more interesting, in the multi-thread case, it is possible that
     * another thread has beaten us to the insertion.
     *
     * Supposedly, this API call is used only by the native VOL.  If so, this latter item
     * is not an issue until the relevant portions of the native VOL are made multi-thread.
     *
     * Thus for now, it should be sufficient to delete the marked ID info from the lock free 
     * hash table before inserting the new ID info.  However, for the long term, we need 
     * a compare and swap call for the lock free hash table instead of the existing 
     * unconditional swap value call.
     *
     * In the absence of the compare and swap for the lock free hash table, we will 
     * simply try to delete the existing ID from the lock free hash table, and then 
     * insert the new id info with the same ID.
     *
     * Note that the delete from the hash table may fail, as it is possible that some
     * other thread will have swept the marked IDs in the time since we looked it up.
     */
    if ( old_id_info_ptr ) {

        /* no point in checking the return value here, as it is possible 
         * that another thread has deleted the lock free hash table entry
         * in the time since we looked up the old_id_info_ptr.
         *
         * As discussed above, this really should be a compare and swap, 
         * but is should be safe for now.
         */
        lfht_delete(&(type_info_ptr->lfht), (unsigned long long)existing_id);
    } 

    /* return an error on failure here */
    result = lfht_add(&(type_info_ptr->lfht), (unsigned long long int)existing_id, (void *)new_id_info_ptr);
    assert(result);
     
    atomic_fetch_add(&(type_info_ptr->id_count), 1);

    atomic_store(&(type_info_ptr->last_id_info), new_id_info_ptr);

done:

    if ( FAIL == ret_value ) {

        atomic_fetch_add(&(H5I_mt_g.H5I_register_using_existing_id__num_failures), 1ULL);
    }

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_register_using_existing_id() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_register_using_existing_id
 *
 * Purpose:     Registers an OBJECT in a TYPE with the supplied ID for it.
 *              This routine will check to ensure the supplied ID is not already
 *              in use, and ensure that it is a valid ID for the given type,
 *              but will NOT check to ensure the OBJECT is not already
 *              registered (thus, it is possible to register one object under
 *              multiple IDs).
 *
 * NOTE:        Intended for use in refresh calls, where we have to close
 *              and re-open the underlying data, then hook the object back
 *              up to the original ID.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_register_using_existing_id(H5I_type_t type, void *object, hbool_t app_ref, hid_t existing_id)
{
    hbool_t          result;
    H5I_type_info_t *type_info = NULL;    /* Pointer to the type */
    H5I_id_info_t   *info      = NULL;    /* Pointer to the new ID information */
    herr_t           ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Check arguments */
    assert(object);

    /* Make sure ID is not already in use */
    if (NULL != (info = H5I__find_id(existing_id)))
        HGOTO_ERROR(H5E_ID, H5E_BADRANGE, FAIL, "ID already in use");

    /* Make sure type number is valid */
    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    /* Get type pointer from list of types */
    type_info = H5I_type_info_array_g[type];

    if (NULL == type_info || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Make sure requested ID belongs to object's type */
    if (H5I_TYPE(existing_id) != type)
        HGOTO_ERROR(H5E_ID, H5E_BADRANGE, FAIL, "invalid type for provided ID");

    /* Allocate new structure to house this ID */
    if (NULL == (info = H5FL_CALLOC(H5I_id_info_t)))
        HGOTO_ERROR(H5E_ID, H5E_NOSPACE, FAIL, "memory allocation failed");

    /* Create the struct & insert requested ID */
    info->id        = existing_id;
    info->count     = 1; /* initial reference count*/
    info->app_count = !!app_ref;
    info->object    = object;
    /* This API call is only used by the native VOL connector, which is
     * not asynchronous.
     */
    info->is_future  = FALSE;
    info->realize_cb = NULL;
    info->discard_cb = NULL;
    info->marked     = FALSE;

    /* Insert into the type */
#ifdef H5_HAVE_MULTITHREAD

    /* todo -- make this throw an error */
    result = lfht_add(&(type_info->lfht), (unsigned long long int)existing_id, (void *)info);
    assert(result);

#else /* H5_HAVE_MULTITHREAD */
    HASH_ADD(hh, type_info->hash_table, id, sizeof(hid_t), info);
#endif /* H5_HAVE_MULTITHREAD */
    type_info->id_count++;

    /* Set the most recent ID to this object */
    type_info->last_id_info = info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_register_using_existing_id() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_subst
 *
 * Purpose:     Substitute a new object pointer for the specified ID.
 *
 *              Re-written for multi-thread.
 *
 * Return:      Success:    Non-NULL previous object pointer associated
 *                          with the specified ID.
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Saturday, February 27, 2010
 *
 * Changes:     Added calls to H5I__enter() and H5I__exit() to track 
 *              the number of threads in H5I.  If 
 *              H5I_subst() is ever called from within 
 *              H5I, we will need to add a boolean prameter to control 
 *              the H5I__enter/exit calls.
 *
 *                                          JRM -- 7/5/24
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_subst(hid_t id, const void *new_object)
{
    hbool_t                 done = FALSE;
    hbool_t                 have_global_mutex = TRUE; /* trivially true in the single thread case */
    int                     pass = 0;
    H5I_mt_id_info_kernel_t info_k;
    H5I_mt_id_info_kernel_t mod_info_k;
    H5I_mt_id_info_t       *id_info_ptr  = NULL; /* Pointer to the ID's info */
    const void             *old_object;
    void                   *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    H5I__enter(FALSE);

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_subst() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    atomic_fetch_add(&(H5I_mt_g.H5I_subst__num_calls), 1ULL);

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)

    if ( H5TS_have_mutex(&H5_g.init_lock, &have_global_mutex) < 0 )

        HGOTO_ERROR(H5E_LIB, H5E_CANTGET, NULL, "Can't determine whether we have the global mutex");
        
#endif /* defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD) */

    if ( have_global_mutex ) {

        atomic_fetch_add(&(H5I_mt_g.H5I_subst__num_calls__with_global_mutex), 1ULL);

    } else {
        
        atomic_fetch_add(&(H5I_mt_g.H5I_subst__num_calls__without_global_mutex), 1ULL);
    }

    do {

        id_info_ptr = NULL;
        old_object = NULL;

        /* increment the pass and log retries */
        if ( pass++ >= 1 ) {

            atomic_fetch_add(&(H5I_mt_g.H5I_subst__retries), 1ULL);
        }

        if ( NULL == (id_info_ptr = H5I__find_id(id)) )

            HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, NULL, "can't find ID");

        info_k = atomic_load(&(id_info_ptr->k));

        if ( info_k.marked ) {

            /* this is is already marked for deletion -- nothing to do here */

            /* update stats */
            if ( pass <= 1 ) {

                atomic_fetch_add(&(H5I_mt_g.H5I_subst__marked_on_entry), 1ULL);

            } else {

                atomic_fetch_add(&(H5I_mt_g.H5I_subst__marked_during_call), 1ULL);
            }
            break;
        }

        if ( info_k.do_not_disturb ) {
#if 0
            if ( ( have_global_mutex ) && ( info_k.have_global_mutex ) ) {

                bypass_do_not_disturb = TRUE;

                atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_bypasses), 1ULL);

            } else {
#endif 
                /* Another thread is in the process of performing an operation on the info kernel
                 * that can't be rolled back -- either a future id realize_cb or discard_cb, or a
                 * regular id free_func.
                 *
                 * Thus we must wait until that thread is done and then re-start the operation -- which
                 * may be moot by that point.
                 */

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_yields), 1ULL);

                /* need to do better than this.  Want to call pthread_yield(),
                 * but that call doesn't seem to be supported anymore.
                 */
                sleep(1);

                continue;
#if 0
            }
#endif
        }

        old_object = info_k.object;

        /* setup the modified version of the id info kernel */
        mod_info_k.count          = info_k.count;
        mod_info_k.app_count      = info_k.app_count;
        mod_info_k.object         = new_object;

        mod_info_k.marked         = info_k.marked;;
        mod_info_k.do_not_disturb = info_k.do_not_disturb;;
        mod_info_k.is_future      = info_k.is_future;

        mod_info_k.object = new_object;

        if ( atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k) ) {

            done = TRUE;

        } else {

            /* the atomic compare exchange strong failed -- try again */

        }
    } while ( ! done );

    if ( done ) {

        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        ret_value = (void *)old_object;
        H5_GCC_CLANG_DIAG_ON("cast-qual")

    } else {

        atomic_fetch_add(&(H5I_mt_g.H5I_subst__failures), 1ULL);
    } 

done:

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_subst() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_subst
 *
 * Purpose:     Substitute a new object pointer for the specified ID.
 *
 * Return:      Success:    Non-NULL previous object pointer associated
 *                          with the specified ID.
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Saturday, February 27, 2010
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_subst(hid_t id, const void *new_object)
{
    H5I_id_info_t *info      = NULL; /* Pointer to the ID's info */
    void          *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* General lookup of the ID */
    if (NULL == (info = H5I__find_id(id)))
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, NULL, "can't get ID ref count");

    /* Get the old object pointer to return */
    H5_GCC_CLANG_DIAG_OFF("cast-qual")
    ret_value = (void *)info->object;
    H5_GCC_CLANG_DIAG_ON("cast-qual")

    /* Set the new object pointer for the ID */
    info->object = new_object;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_subst() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_object
 *
 * Purpose:     Find an object pointer for the specified ID.
 *
 *              Modified for multi-thread.
 *
 * Return:      Success:    Non-NULL object pointer associated with the
 *                          specified ID
 *
 *              Failure:    NULL
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_object() function to H5I_object_internal()
 *              and created a new version of H5I_object() that 
 *              simply calls H5I__enter(), H5I_object_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_object(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_object(hid_t id)
{
    void                   *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_object_internal(id);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_object() */

void *
H5I_object_internal(hid_t id)
{
    H5I_mt_id_info_kernel_t info_k;
    H5I_mt_id_info_t       *info_ptr      = NULL; /* Pointer to the ID info */
    void                   *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

#if H5I_MT_DEBUG
    fprintf(stderr, "   H5I_object(0x%llx) called. \n", (unsigned long long)id);
#endif /* H5I_MT_DEBUG */

    /* General lookup of the ID */
    if (NULL != (info_ptr = H5I__find_id(id))) {

        /* Get the object pointer to return */

        info_k = atomic_load(&(info_ptr->k));

        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        ret_value = (void *)info_k.object;
        H5_GCC_CLANG_DIAG_ON("cast-qual")
    }

#if H5I_MT_DEBUG
    fprintf(stderr, "   H5I_object(0x%llx) returns 0x%llx. \n", 
              (unsigned long long)id, (unsigned long long)ret_value);
#endif /* H5I_MT_DEBUG */

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_object() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_object
 *
 * Purpose:     Find an object pointer for the specified ID.
 *
 * Return:      Success:    Non-NULL object pointer associated with the
 *                          specified ID
 *
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_object(hid_t id)
{
    H5I_id_info_t *info      = NULL; /* Pointer to the ID info */
    void          *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    /* General lookup of the ID */
    if (NULL != (info = H5I__find_id(id))) {
        /* Get the object pointer to return */
        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        ret_value = (void *)info->object;
        H5_GCC_CLANG_DIAG_ON("cast-qual")
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_object() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I_object_verify
 *
 * Purpose:     Find an object pointer for the specified ID, verifying that
 *              its in a particular type.
 *
 *              Updated for multi-thread.
 *
 * Return:      Success:    Non-NULL object pointer associated with the
 *                          specified ID.
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, July 31, 2002
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_object_verify() function to H5I_object_verify_internal()
 *              and created a new version of H5I_object_verify() that 
 *              simply calls H5I__enter(), H5I_object_verify_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_object_verify(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_object_verify(hid_t id, H5I_type_t type)
{
    void                   *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_object_verify_internal(id, type);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_object_verify() */

void *
H5I_object_verify_internal(hid_t id, H5I_type_t type)
{
    H5I_mt_id_info_kernel_t  info_k;
    H5I_mt_id_info_t        *info_ptr      = NULL; /* Pointer to the ID info */
    void                    *ret_value     = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I_object_verify(id = 0x%llx, type = %d) called. \n", 
              (unsigned long long)id, (int)type);
#endif /* H5I_MT_DEBUG */

    assert( ( type >= 1 ) && ( (int)type < atomic_load(&(H5I_mt_g.next_type)) ) );

    /* Verify that the type of the ID is correct & lookup the ID */
    if ( ( type == H5I_TYPE(id) ) && ( NULL != (info_ptr = H5I__find_id(id)) ) ) {

        /* Get the object pointer to return */

        info_k = atomic_load(&(info_ptr->k));

        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        ret_value = (void *)info_k.object;
        H5_GCC_CLANG_DIAG_ON("cast-qual")
    }

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I_object_verify(id = 0x%llx, type = %d) returns 0x%llx. \n", 
              (unsigned long long)id, (int)type, (unsigned long long)ret_value);
#endif /* H5I_MT_DEBUG */

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I_object_verify_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_object_verify
 *
 * Purpose:     Find an object pointer for the specified ID, verifying that
 *              its in a particular type.
 *
 * Return:      Success:    Non-NULL object pointer associated with the
 *                          specified ID.
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, July 31, 2002
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_object_verify(hid_t id, H5I_type_t type)
{
    H5I_id_info_t *info      = NULL; /* Pointer to the ID info */
    void          *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    assert(type >= 1 && (int)type < H5I_next_type_g);

    /* Verify that the type of the ID is correct & lookup the ID */
    if (type == H5I_TYPE(id) && NULL != (info = H5I__find_id(id))) {
        /* Get the object pointer to return */
        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        ret_value = (void *)info->object;
        H5_GCC_CLANG_DIAG_ON("cast-qual")
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* H5I_object_verify() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_get_type
 *
 * Purpose:     Given an object ID return the type to which it
 *              belongs.  The ID need not be the ID of an object which
 *              currently exists because the type number is encoded
 *              in the object ID.
 *
 * Return:      Success:    A positive integer (corresponding to an H5I_type_t
 *                          enum value for library ID types, but not for user
 *                          ID types).
 *              Failure:    H5I_BADID
 *
 * Programmer:  Robb Matzke
 *              Friday, February 19, 1999
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_get_type() function to H5I_getr_type_internal()
 *              and created a new version of H5I_get_type() that 
 *              simply calls H5I__enter(), H5I_get_type_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_get_type(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
H5I_type_t
H5I_get_type(hid_t id)
{
    H5I_type_t ret_value = H5I_BADID; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_get_type_internal(id);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_get_type() */

H5I_type_t
H5I_get_type_internal(hid_t id)
{
    H5I_type_t ret_value = H5I_BADID; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_get_type() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    if (id > 0)
        ret_value = H5I_TYPE(id);

    assert(ret_value >= H5I_BADID && (int)ret_value < H5I_mt_g.next_type);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_get_type_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_get_type
 *
 * Purpose:     Given an object ID return the type to which it
 *              belongs.  The ID need not be the ID of an object which
 *              currently exists because the type number is encoded
 *              in the object ID.
 *
 * Return:      Success:    A positive integer (corresponding to an H5I_type_t
 *                          enum value for library ID types, but not for user
 *                          ID types).
 *              Failure:    H5I_BADID
 *
 * Programmer:  Robb Matzke
 *              Friday, February 19, 1999
 *
 *-------------------------------------------------------------------------
 */
H5I_type_t
H5I_get_type(hid_t id)
{
    H5I_type_t ret_value = H5I_BADID; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    if (id > 0)
        ret_value = H5I_TYPE(id);

    assert(ret_value >= H5I_BADID && (int)ret_value < H5I_next_type_g);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_get_type() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_is_file_object
 *
 * Purpose:     Convenience function to determine if an ID represents
 *              a file object.
 *
 *              In H5O calls, you can't use object_verify to ensure
 *              the ID was of the correct class since there's no
 *              H5I_OBJECT ID class.
 *
 *              Updated for multi-thread.
 *
 * Return:      Success:    TRUE/FALSE
 *              Failure:    FAIL
 *
 * Changes:     Added calls to H5I__enter() and H5I__exit() to track 
 *              the number of threads in H5I.  If 
 *              H5I_is_file_object() is ever called from within 
 *              H5I, we will need to add a boolean prameter to control 
 *              the H5I__enter/exit calls.
 *
 *                                          JRM -- 7/5/24
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5I_is_file_object(hid_t id)
{
    H5I_type_t          type      = H5I_get_type_internal(id);
    htri_t              ret_value = FAIL;

    FUNC_ENTER_NOAPI(FAIL)

    H5I__enter(FALSE);

    atomic_fetch_add(&(H5I_mt_g.H5I_is_file_object__num_calls), 1ULL);

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_is_file_object() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Fail if the ID type is out of range */
    if (type < 1 || type >= H5I_NTYPES)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "ID type out of range");

    /* Return TRUE if the ID is a file object (dataset, group, map, or committed
     * datatype), FALSE otherwise.
     */
    if ( ( H5I_DATASET == type ) || ( H5I_GROUP == type ) || ( H5I_MAP == type ) ) {

        ret_value = TRUE;

    } else if ( H5I_DATATYPE == type ) {

        hbool_t             have_global_mutex = TRUE; /*trivially so for single thread builds */
        H5T_t              *dt = NULL;

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)
        /* test to see whether this thread currently holds the global mutex.  Store the
         * the result for later use.
         */
        if ( H5TS_have_mutex(&H5_g.init_lock, &have_global_mutex) < 0 )

            HGOTO_ERROR(H5E_LIB, H5E_CANTGET, FAIL, "Can't determine whether we have the global mutex");

#endif /* defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD) */

        /* In the multi-thread case, it is possible that id could be deleted between 
         * the call to H5I_object() and the call to H5T_is_named().  
         *
         * The correct way of solving this is to modify H5T to be multi-thread, and 
         * in particular to keep deleted data types on a free list until all references
         * to the datatype have been deleted.
         *
         * However, that isn't an option for now.  Thus, to prevent this, increment the 
         * reference count on id before we call H5I_object() and decrement it after 
         * H5T_is_named() returns.
         *
         * Note that this isn't bullet proof at present -- there are routines that delete
         * IDs without checking the ID reference counts.  These should only be run on 
         * shutdown when there is only one thread active, but this is a point to consider
         * in debugging.
         *
         * Further, note that the current implementation is very in-efficient due to the
         * ref count increment and decrement.  Think on converting this to a single 
         * function using the do not disturb flag to avoid the possiblity of the ID
         * being deleted out from under the H5T_id_named() call.
         */

        if ( -1 == H5I_inc_ref_internal(id, FALSE) )

            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "unable to increment id ref count");

        if ( NULL == ( dt = (H5T_t *)H5I_object_internal(id) ) ) 

            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "unable to get underlying datatype struct");

        atomic_fetch_add(&(H5I_mt_g.H5I_is_file_object__num_calls_to_H5T_is_named), 1ULL);

        /* If this thread doesn't alread have the global mutex, we must grab it before 
         * the call to H5T_is_named() and drop it afterwards.
         */
        if ( ! have_global_mutex ) {

            atomic_fetch_add(&(H5I_mt_g.H5I_is_file_object__global_mutex_locks_for_H5T_is_named), 1ULL);
            H5_API_LOCK
            ret_value = H5T_is_named(dt); 
            H5_API_UNLOCK
            atomic_fetch_add(&(H5I_mt_g.H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named), 1ULL);

        } else {

            ret_value = H5T_is_named(dt); 
        }

        if ( -1 == H5I_dec_ref_internal(id) ) 

            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "unable to decrement id ref count");

    } else {

        ret_value = FALSE;
    }

done:

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I_is_file_object() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_is_file_object
 *
 * Purpose:     Convenience function to determine if an ID represents
 *              a file object.
 *
 *              In H5O calls, you can't use object_verify to ensure
 *              the ID was of the correct class since there's no
 *              H5I_OBJECT ID class.
 *
 * Return:      Success:    TRUE/FALSE
 *              Failure:    FAIL
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5I_is_file_object(hid_t id)
{
    H5I_type_t type      = H5I_get_type(id);
    htri_t     ret_value = FAIL;

    FUNC_ENTER_NOAPI(FAIL)

    /* Fail if the ID type is out of range */
    if (type < 1 || type >= H5I_NTYPES)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "ID type out of range");

    /* Return TRUE if the ID is a file object (dataset, group, map, or committed
     * datatype), FALSE otherwise.
     */
    if (H5I_DATASET == type || H5I_GROUP == type || H5I_MAP == type)
        ret_value = TRUE;
    else if (H5I_DATATYPE == type) {

        H5T_t *dt = NULL;

        if (NULL == (dt = (H5T_t *)H5I_object(id)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "unable to get underlying datatype struct");

        ret_value = H5T_is_named(dt); /* will hit global mutex -- JRM */
    }
    else
        ret_value = FALSE;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5I_is_file_object() */

#endif /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__remove_verify
 *
 * Purpose:     Removes the specified ID from its type, first checking that
 *              the ID's type is the same as the ID type supplied as an argument
 *
 * Return:      Success:    A pointer to the object that was removed, the
 *                          same pointer which would have been found by
 *                          calling H5I_object().
 *              Failure:    NULL
 *
 * Programmer:  James Laird
 *              Nat Furrer
 *
 *-------------------------------------------------------------------------
 */
void *
H5I__remove_verify(hid_t id, H5I_type_t type)
{
    void *ret_value = NULL; /*return value            */

    FUNC_ENTER_PACKAGE_NOERR

#ifdef H5_HAVE_MULTITHREAD 
#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__remove_verify() called. \n\n\n");
#endif /* H5I_MT_DEBUG */
#endif /* H5_HAVE_MULTITHREAD */

    /* Argument checking will be performed by H5I_remove() */

    /* Verify that the type of the ID is correct */
    if (type == H5I_TYPE(id)) {
#ifdef H5_HAVE_MULTITHREAD 
        ret_value = H5I_remove_internal(id);
#else /* H5_HAVE_MULTITHREAD */
        ret_value = H5I_remove(id);
#endif /* H5_HAVE_MULTITHREAD */
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__remove_verify() */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I__remove_common
 *
 * Purpose:     Common code to remove a specified ID from its type.
 *
 *              Modified for MT.
 *
 * Return:      Success:    A pointer to the object that was removed, the
 *                          same pointer which would have been found by
 *                          calling H5I_object().
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              October 3, 2013
 *
 *-------------------------------------------------------------------------
 */
static void *
H5I__remove_common(H5I_type_info_t *type_info_ptr, hid_t id)
{
    hbool_t                 done = FALSE;
    int                     pass = 0;
    H5I_type_t              type;
    H5I_mt_id_info_t       *id_info_ptr  = NULL; /* Pointer to the current ID */
    H5I_mt_id_info_t       *dup_id_info_ptr;     /* Pointer to the current ID */
    H5I_mt_id_info_kernel_t info_k;
    H5I_mt_id_info_kernel_t mod_info_k;
    void                   *ret_value = NULL;    /* Return value */

    FUNC_ENTER_PACKAGE

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__remove_common() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(type_info_ptr);
    assert(type_info_ptr->cls);
    type = type_info_ptr->cls->type;
    assert(type == H5I_TYPE(id));
    assert(atomic_load(&(type_info_ptr->init_count)) > 0);

    atomic_fetch_add(&(H5I_mt_g.H5I__remove_common__num_calls), 1ULL);

    /* Delete or mark the node */
    do {

        /* increment the pass and log retries */
        if ( pass++ >= 1 ) {

            atomic_fetch_add(&(H5I_mt_g.H5I__remove_common__retries), 1ULL);
        }

        lfht_find(&(type_info_ptr->lfht), (unsigned long long int)id, (void **)&id_info_ptr);

        if ( id_info_ptr ) {

            info_k = atomic_load(&(id_info_ptr->k)); 

            if ( info_k.marked ) {

                /* update stats */
                if ( pass <= 1 ) {

                    atomic_fetch_add(&(H5I_mt_g.H5I__remove_common__already_marked), 1ULL);

                } else {

                    atomic_fetch_add(&(H5I_mt_g.H5I__remove_common__marked_by_another_thread), 1ULL);
                }

                /* the target ID has been logically deleted from the index.
                 * Thus set id_info_ptr = NULL, and flag an error.
                 *
                 * Note that for now at least, we don't distinguish between the case in which the 
                 * ID is logically deleted on entry vs. the case in which the ID is logically 
                 * deleted by another thread at a later point.
                 */
                id_info_ptr = NULL;
                done = TRUE;

            } else if ( info_k.do_not_disturb ) {

                /* Another thread is in the process of performing an operation on the info kernel
                 * that can't be rolled back -- either a future id realize_cb or discard_cb, or a
                 * regular id callback that must be serialized.
                 *
                 * Thus we must wait until that thread is done and then re-start the operation -- which
                 * may be moot by that point.
                 */

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_yields), 1ULL);

                /* need to do better than this.  Want to call pthread_yield(),
                 * but that call doesn't seem to be supported anymore.
                 */
                sleep(1);

                continue;

            } else {

                /* The id and the associated instance of H5I_mt_id_info_t is logically deleted 
                 * as soon as we set id_info_ptr->k.marked to TRUE -- thus update the remaining 
                 * fields of id_info_ptr->k accordingly.
                 */

                mod_info_k.count             = 0;
                mod_info_k.app_count         = 0;
                mod_info_k.object            = NULL;

                mod_info_k.marked            = TRUE;
                mod_info_k.do_not_disturb    = FALSE;
                mod_info_k.is_future         = FALSE;
                mod_info_k.have_global_mutex = FALSE;

               if ( atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k) ) {

                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.H5I__remove_common__marked), 1ULL);

                    done = TRUE;

                } else {

                    /* the atomic compare exchange strong failed -- try again */

                    /* done is false, so nothing to do to trigger the retry */
                }
            }
        } else { /* id_info_ptr is NULL */

            /* target entry doesn't exist in the lock free hash table, so can't proceed.
             * will flag an error later.
             */
            /* update stats */
            atomic_fetch_add(&(H5I_mt_g.H5I__remove_common__target_not_in_lfht), 1ULL);

            done = TRUE;
        }
    } while ( ! done );

    if ( ! id_info_ptr ) {

        HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, NULL, "can't mark ID for removal from hash table");

    } else {

        /* if this was the last ID accessed, set type_info_ptr->last_id_info to NULL.
         * Do this with a call to atomic_compare_exchange_strong().  This call will NULL
         * type_info_ptr->last_id_info if it is currently set to id_info_ptr, and leave it 
         * unchanged otherwise.  If type_info_ptr->last_id_info is not id_info_ptr, 
         * atomic_compare_exchange_strong() will return its current value in the second 
         * parameter -- hence the need for dup_id_info_ptr.
         */
        dup_id_info_ptr = id_info_ptr;
        atomic_compare_exchange_strong(&(type_info_ptr->last_id_info), &dup_id_info_ptr, NULL);

        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        ret_value = (void *)info_k.object;
        H5_GCC_CLANG_DIAG_ON("cast-qual")

        atomic_fetch_sub(&(type_info_ptr->id_count), 1ULL);

        if ( 0 == atomic_load(&(H5I_mt_g.marking_array[type])) ) {
            
            if ( ( ! lfht_delete(&(type_info_ptr->lfht), (unsigned long long int)id)) )

                HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, NULL, "can't remove ID node from hash table");

            if ( H5I__discard_mt_id_info(id_info_ptr) < 0 )

                HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, NULL, "can't release ID info to free list");
        }
    }

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__remove_common() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__remove_common
 *
 * Purpose:     Common code to remove a specified ID from its type.
 *
 * Return:      Success:    A pointer to the object that was removed, the
 *                          same pointer which would have been found by
 *                          calling H5I_object().
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              October 3, 2013
 *
 *-------------------------------------------------------------------------
 */
static void *
H5I__remove_common(H5I_type_info_t *type_info, hid_t id)
{
    H5I_id_info_t *info      = NULL; /* Pointer to the current ID */
    void          *ret_value = NULL; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(type_info);

    /* Delete or mark the node */
    HASH_FIND(hh, type_info->hash_table, &id, sizeof(hid_t), info);
    if (info) {
        assert(!info->marked);
        if (!H5I_marking_s)
            HASH_DELETE(hh, type_info->hash_table, info);
        else
            info->marked = TRUE;
    }
    else
        HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, NULL, "can't remove ID node from hash table");

    /* Check if this ID was the last one accessed */
    if (type_info->last_id_info == info)
        type_info->last_id_info = NULL;

    H5_GCC_CLANG_DIAG_OFF("cast-qual")
    ret_value = (void *)info->object;
    H5_GCC_CLANG_DIAG_ON("cast-qual")

    if (!H5I_marking_s)
        info = H5FL_FREE(H5I_id_info_t, info);

    /* Decrement the number of IDs in the type */
    (type_info->id_count)--;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__remove_common() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_remove
 *
 * Purpose:     Removes the specified ID from its type.
 *
 *              Updated for multi-thread
 *
 * Return:      Success:    A pointer to the object that was removed, the
 *                          same pointer which would have been found by
 *                          calling H5I_object().
 *              Failure:    NULL
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_remove() function to H5I_remove_internal()
 *              and created a new version of H5I_remove() that 
 *              simply calls H5I__enter(), H5I_remove_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_remove(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_remove(hid_t id)
{
    void               *ret_value     = NULL;      /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_remove_internal(id);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_remove() */

void *
H5I_remove_internal(hid_t id)
{
    H5I_mt_type_info_t *type_info_ptr = NULL;      /* Pointer to the ID type */
    H5I_type_t          type          = H5I_BADID; /* ID's type */
    void               *ret_value     = NULL;      /* Return value */

    FUNC_ENTER_NOAPI(NULL)

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_remove() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Check arguments */
    type = H5I_TYPE(id);

    if (type <= H5I_BADID || (int)type >= atomic_load(&(H5I_mt_g.next_type)) )
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, NULL, "invalid type number");

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if (type_info_ptr == NULL || atomic_load(&(type_info_ptr->init_count)) <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, NULL, "invalid type");

    /* Remove the node from the type */
    if (NULL == (ret_value = H5I__remove_common(type_info_ptr, id)))
        HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, NULL, "can't remove ID node");

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_remove_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_remove
 *
 * Purpose:     Removes the specified ID from its type.
 *
 * Return:      Success:    A pointer to the object that was removed, the
 *                          same pointer which would have been found by
 *                          calling H5I_object().
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_remove(hid_t id)
{
    H5I_type_info_t *type_info = NULL;      /* Pointer to the ID type */
    H5I_type_t       type      = H5I_BADID; /* ID's type */
    void            *ret_value = NULL;      /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* Check arguments */
    type = H5I_TYPE(id);
    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, NULL, "invalid type number");
    type_info = H5I_type_info_array_g[type];
    if (type_info == NULL || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, NULL, "invalid type");

    /* Remove the node from the type */
    if (NULL == (ret_value = H5I__remove_common(type_info, id)))
        HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, NULL, "can't remove ID node");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_remove() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I__dec_ref
 *
 * Purpose:     This will fail if the type is not a reference counted type.
 *              The ID type's 'free' function will be called for the ID
 *              if the reference count for the ID reaches 0 and a free
 *              function has been defined at type creation time.
 *
 *              Reworked for multi-thread.  Changes were major, as to 
 *              preserve atomicity, I had two options -- either greatly 
 *              extend H5I__remove_common(), or incorporate it 
 *              functionality into this function.
 *
 *              For now at least, the latter seems the most appropriate,
 *              althought refactoring will be in order once the prototype
 *              is up and running.
 *
 *              Further, to make app_count and count decrements atomic, 
 *              added the app boolean parameter.  When set, both 
 *              id_info_ptr->k.count and id_info_ptr->k.app_count fields
 *              are decrementd, and, if id_info_ptr->k.count is still 
 *              positive, the new value of id_info_ptr->k.app_count is 
 *              returned.  Note that the ID is still marked for 
 *              deletion if id_info_ptr->k.count drops to zero, and 
 *              in that case, 0 is returned unless an error is detected.
 *
 *                                              JRM -- 9/18/23
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              request != H5_REQUEST_NULL.
 *
 * Return:      Success:    New reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__dec_ref(hid_t id, void **request, hbool_t app)
{
    hbool_t                  done                = FALSE;
    hbool_t                  do_not_disturb_set;
    hbool_t                  marked_for_deletion;
    hbool_t                  have_global_mutex = TRUE; /* trivially so in single thread builds */
    hbool_t                  cls_is_mt_safe;
    hbool_t                  bool_result;
    int                      pass                = 0;
    H5I_mt_id_info_kernel_t  info_k;
    H5I_mt_id_info_kernel_t  mod_info_k;
    H5I_mt_id_info_t        *id_info_ptr         = NULL; /* Pointer to the ID */
    H5I_mt_type_info_t      *type_info_ptr;              /* ptr to the type   */
    herr_t                   result;
    int                      ret_value           = 0;    /* Return value */

    FUNC_ENTER_PACKAGE

    memset(&info_k, 0, sizeof(H5I_mt_id_info_kernel_t));
    memset(&mod_info_k, 0, sizeof(H5I_mt_id_info_kernel_t));

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I__dec_ref(0x%llx, reguest, app) called. \n", (unsigned long long)id);
#endif /* H5I_MT_DEBUG */
#if 0 /* JRM */
    if (id == 0x1300000000000000 )
        fprintf(stderr, "   H5I__dec_ref(0x%llx, reguest, app) entering. \n", (unsigned long long)id);
#endif /* JRM */

    atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__num_calls), 1ULL);

    if ( app ) {

        atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__num_app_calls), 1ULL);
    }

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)

    if ( H5TS_have_mutex(&H5_g.init_lock, &have_global_mutex) < 0 )

        HGOTO_ERROR(H5E_LIB, H5E_CANTGET, FAIL, "Can't determine whether we have the global mutex");
        
#endif /* defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD) */

    if ( have_global_mutex ) {

        atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__num_calls_with_global_mutex), 1ULL);

    } else {

        atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__num_calls_without_global_mutex), 1ULL);
    }

    /* Get the ID's type */
    if ( NULL == (type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[H5I_TYPE(id)]))) )

        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID type");

    /* test the class flags to see if the class is multi-thread safe, and make note of the result */
    cls_is_mt_safe = ((type_info_ptr->cls->flags & H5I_CLASS_IS_MT_SAFE) != 0);

    /* General lookup of the ID -- note that if successful, this call will convert 
     * future IDs to regular IDs.
     *
     * Note that there is no need to repeat this search at the beginning of each 
     * pass through the do/while loop, as any changes will be reflected in *id_info_ptr.
     */
    if (NULL == (id_info_ptr = H5I__find_id(id)))

        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");

    /* Sanity check */
    assert(id >= 0);

    do {

        do_not_disturb_set  = FALSE;
        marked_for_deletion = FALSE;

        /* increment the pass and log retries */
        if ( pass++ >= 1 ) {

            atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__retries), 1ULL);
        }

        info_k = atomic_load(&(id_info_ptr->k));

        if ( info_k.marked ) {

            /* this is is already marked for deletion -- nothing to do here */

            /* update stats */
            if ( pass <= 1 ) {

                atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__marked_on_entry), 1ULL);

            } else {

                atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__marked_during_call), 1ULL);
            }

            HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");
        }

        if ( info_k.do_not_disturb ) {

            /* Another thread is in the process of performing an operation on the info kernel
             * that can't be rolled back -- either a future id realize_cb or discard_cb, or a
             * regular id callback that must be serialized.
             *
             * Thus we must wait until that thread is done and then re-start the operation -- which
             * may be moot by that point.
             */

            /* update stats */
            atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_yields), 1ULL);

            /* need to do better than this.  Want to call pthread_yield(),
             * but that call doesn't seem to be supported anymore.
             */
            sleep(1);

            continue;
        }

        if ( ( info_k.count > 1 ) || ( NULL == type_info_ptr->cls->free_func ) ) {

            /* Either count > 1 or the free function for the class is undefined.
             * In either case, we can roll back the operation an re-try if the 
             * global copy of the kernel has changed since we read it at the 
             * top of the do/while loop. 
             */
            mod_info_k.count             = info_k.count;
            mod_info_k.app_count         = info_k.app_count;
            mod_info_k.object            = info_k.object;

            mod_info_k.marked            = info_k.marked;
            mod_info_k.do_not_disturb    = info_k.do_not_disturb;
            mod_info_k.is_future         = info_k.is_future;
            mod_info_k.have_global_mutex = FALSE;

            if ( info_k.count > 1 ) {

                mod_info_k.count--;

                if ( app ) {

                    mod_info_k.app_count--;

                    assert(mod_info_k.count >= mod_info_k.app_count);
                }
            } else {
                
                assert( NULL == type_info_ptr->cls->free_func );

                /* id_info_ptr->k.count is about to drop to zero, and as a result, the 
                 * the ID and *id_info_ptr are about to be removed from the idex at least
                 * logically, and probably physically as well.  Since the free function 
                 * is undefined, all we need to do is setup mod_info_k accordingly and 
                 * try to replace id_info_ptr->k with mod_info_k.
                 */
                mod_info_k.count             = 0;
                mod_info_k.app_count         = 0;
                mod_info_k.object            = NULL;

                mod_info_k.marked            = TRUE;
                mod_info_k.do_not_disturb    = FALSE;
                mod_info_k.is_future         = FALSE;
                mod_info_k.have_global_mutex = FALSE;

                marked_for_deletion = TRUE;
            }

            if ( atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k) ) {

                if ( marked_for_deletion ) {

                    atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__marked), 1ULL);
                    ret_value = 0;

                } else {

                    atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__decremented), 1ULL);

                    if ( app ) {

                        atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__app_decremented), 1ULL);

                        H5_GCC_CLANG_DIAG_OFF("cast-qual")
                        ret_value = (int)(mod_info_k.app_count);
                        H5_GCC_CLANG_DIAG_ON("cast-qual")

                    } else {

                        H5_GCC_CLANG_DIAG_OFF("cast-qual")
                        ret_value = (int)(mod_info_k.count);
                        H5_GCC_CLANG_DIAG_ON("cast-qual")
                    }
                }

                done = TRUE;

            } else {

                /* the atomic compare exchange strong failed -- try again */

                /* done is false, so nothing to do to trigger the retry */
                assert( ! done );
            }
        } else {

            assert(info_k.count <= 1);
            assert(type_info_ptr->cls->free_func);

            cls_is_mt_safe = ((type_info_ptr->cls->flags & H5I_CLASS_IS_MT_SAFE) != 0);

            /* The ref count has dropped to 1, and the class free_func is defined.  
             * Proceed as follows:
             *
             *    1) Set the do_not_disturb_flag
             *
             *       In passing set the have_global_mutex flag to true if either we currently
             *       have the global mutex, or if the H5I_CLASS_IS_MT_SAFE is set in 
             *       type_info_ptr->cls->flags.  Do this because we must obtain the global 
             *       mutex before calling the free_func() and drop if after the call if we 
             *       don't have the mutex already.
             *
             *    2) Call the free_func().  If the class is not multi-thread safe and
             *       we don't already hold the globla mutex, we must obtain it before the 
             *       call, and drop it afterwards.
             * 
             *       On success, go on to 3) below.  
             *
             *       On failure, reset the do_not_disturb flag and return -1.  Do
             *       not flag an error
             *
             *    3) Set the marked flag, reset the do_not_disturb flag, and set
             *       the return value to zero.
             * 
             *    4) If H5I_mt_g.marking_array[H5I_TYPE(id)] is zero, remove the ID from 
             *       the lock free hash table, and release the associated instance of 
             *       H5I_mt_id_info_t to the free list.
             *
             * Note the failure to flag an error if the free function fails.
             * This is the same behaviour seen in the single thread version of
             * H5I__mark_node().  While the notion of leaving an entry in the 
             * index after its free function has failed seems questionable at 
             * best, as per H5I__mark_node, I have chosen to follow this lead
             * at least for the initial prototype.
             */

            /* attempt to set the do_not_disturb flag */
            mod_info_k.count             = info_k.count;
            mod_info_k.app_count         = info_k.app_count;
            mod_info_k.object            = info_k.object;

            mod_info_k.marked            = info_k.marked;
            mod_info_k.do_not_disturb    = TRUE;
            mod_info_k.is_future         = info_k.is_future;
            mod_info_k.have_global_mutex = ((have_global_mutex) || (! cls_is_mt_safe));

            /* We want to call the free function, and then mark the id for deletion.  
             * Since we can't roll this action back, we need exclusive access to the 
             * kernel of the instance of H5I_mt_id_info_t associated with the ID.
             *
             * To get this, try to set the do_not_disturb flag in the kernl.   If 
             * successful, this will prevent any other threads from modifying
             * id_info_ptr->k until after it is set back to FALSE.
             */
            if ( ! atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k) ) {

                /* Some other thread changed the value of id_info_ptr->k since we last read
                 * it.  Thus we must return to the beginning of the do loop and start
                 * again.  Note that it is possible that by that time, there will be
                 * nothing left to do.
                 */

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_failed_do_not_disturb_sets), 1ULL);

                continue;

            } else {

                do_not_disturb_set = TRUE;

#if 0 /* JRM */
                /* make info_k into a copy of the global kernel */
                info_k.do_not_disturb = TRUE;
#else /* JTM */
                /* On the face of it, it would seem that we could just update info_k
                 * to match mod_info_k, and use it in the next atomic_compare_exchange_strong()
                 * call.  However, for reason or reasons unknown, this doesn't work.  
                 *
                 * Instead, we reload info_k after the atomic_compare_exchange_strong(),
                 * and verify that it contains the expected values.
                 */
                info_k = atomic_load(&(id_info_ptr->k));

                assert(info_k.count             == mod_info_k.count);
                assert(info_k.app_count         == mod_info_k.app_count);
                assert(info_k.object            == mod_info_k.object);

                assert(info_k.marked            == mod_info_k.marked);
                assert(info_k.do_not_disturb    == mod_info_k.do_not_disturb);
                assert(info_k.is_future         == mod_info_k.is_future);
                assert(info_k.have_global_mutex == mod_info_k.have_global_mutex);
#endif /* JRM */

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_successful_do_not_disturb_sets), 1ULL);

#if H5I_MT_DEBUG_DO_NOT_DISTURB
                fprintf(stdout, "H5I__dec_ref() set do not disturb on id = 0x%llx.\n",
                          (unsigned long long)(id_info_ptr->id));
#endif /* H5I_MT_DEBUG_DO_NOT_DISTURB */
            }

            assert( do_not_disturb_set );

            atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__calls_to_free_func), 1ULL);

            /* Note that the free_func may call back into H5I.  As long as it doesn't try
             * to access this ID, either directly or indirectly, there shouldn't be a problem.
             *
             * In the case of indexes maintained by the HDF5 library proper, this should be
             * manageable as we have access to the code.  For external users (either user 
             * programmer or VOL connectors), we must document this.
             */
            if ( ( ! have_global_mutex ) && ( ! cls_is_mt_safe ) ) {

                atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__global_mutex_locks_for_free_func), 1ULL);
                H5_API_LOCK
                H5_GCC_CLANG_DIAG_OFF("cast-qual")
                result = type_info_ptr->cls->free_func((void *)info_k.object, request);
                H5_GCC_CLANG_DIAG_ON("cast-qual")
                H5_API_UNLOCK
                atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__global_mutex_unlocks_for_free_func), 1ULL);

            } else {

                H5_GCC_CLANG_DIAG_OFF("cast-qual")
                result = type_info_ptr->cls->free_func((void *)info_k.object, request);
                H5_GCC_CLANG_DIAG_ON("cast-qual")
            }

            if ( result >= 0 ) {

                /* The free_func() succeeded -- reset the do_not_disturb flag, and set marked to TRUE.
                 * Since the ID and the associated instance of H5I_mt_id_info_t will be logically 
                 * deleted as soon as we overwrite id_info_ptr->k with mod_info_k, set the remaining 
                 * fields to reflect this.
                 */
                mod_info_k.count             = 0;
                mod_info_k.app_count         = 0;
                mod_info_k.object            = NULL;

                mod_info_k.marked            = TRUE;
                mod_info_k.do_not_disturb    = FALSE;
                mod_info_k.is_future         = FALSE;
                mod_info_k.have_global_mutex = FALSE;

                marked_for_deletion       = TRUE;

                ret_value = 0;

            } else {

                /* The free_func() failed -- just update stats, reset the do not disturb flag, 
                 * and set ret_value = -1 
                 */
                atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__free_func_failed), 1ULL);

                mod_info_k.do_not_disturb    = FALSE;
                mod_info_k.have_global_mutex = FALSE;
                ret_value = -1;

            }

            /* since we have the do_not_disturb flag, the following atomic_compare_exchange_strong()
             * must succeed.
             */
            bool_result = atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k);
            assert(bool_result);

            atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_resets), 1ULL);

#if H5I_MT_DEBUG_DO_NOT_DISTURB
            fprintf(stdout, "H5I__dec_ref() reset do not disturb on id = 0x%llx.\n",
                      (unsigned long long)(id_info_ptr->id));
#endif /* H5I_MT_DEBUG_DO_NOT_DISTURB */
 
            /* Whether we succeeded or failed, we are done with the do/while loop */
            done = TRUE;
        }
    } while ( ! done );

    if ( marked_for_deletion ) {

        assert( 0 == ret_value );
        assert( id_info_ptr );

        atomic_fetch_sub(&(type_info_ptr->id_count), 1ULL);

        if ( 0 == atomic_load(&(H5I_mt_g.marking_array[H5I_TYPE(id)])) ) {

            /* attempt to remove the ID from the lock free hash table and release the 
             * instance of H5I_mt_id_info_t to the free list.
             */

            if ( ( ! lfht_delete(&(type_info_ptr->lfht), (unsigned long long int)id)) )

                HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, (-1), "can't remove ID node from hash table");

            if ( H5I__discard_mt_id_info(id_info_ptr) < 0 )

                HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, (-1), "can't release ID info to free list");
        }
    }

    assert ( ( ret_value >= 1 ) || ( marked_for_deletion && ( 0 == ret_value ) ) || ( -1 == ret_value ) ||
             ( ( app ) && ( 0 == ret_value ) && ( mod_info_k.count >= 1 ) ) );

done:

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I__dec_ref(0x%llx, reguest, app) returns %d. \n", (unsigned long long)id, ret_value);
#endif /* H5I_MT_DEBUG */

#if 0 /* JRM */
    if ( ret_value < 0 ) 
        fprintf(stderr, "   H5I__dec_ref(0x%llx, reguest, app) returns %d. \n", (unsigned long long)id, ret_value);
#endif /* JRM */

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__dec_ref */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__dec_ref
 *
 * Purpose:     This will fail if the type is not a reference counted type.
 *              The ID type's 'free' function will be called for the ID
 *              if the reference count for the ID reaches 0 and a free
 *              function has been defined at type creation time.
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              request != H5_REQUEST_NULL.
 *
 * Return:      Success:    New reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__dec_ref(hid_t id, void **request)
{
    H5I_id_info_t *info      = NULL; /* Pointer to the ID */
    int            ret_value = 0;    /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(id >= 0);

    /* General lookup of the ID */
    if (NULL == (info = H5I__find_id(id)))
        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");

    /* If this is the last reference to the object then invoke the type's
     * free method on the object. If the free method is undefined or
     * successful then remove the object from the type; otherwise leave
     * the object in the type without decrementing the reference
     * count. If the reference count is more than one then decrement the
     * reference count without calling the free method.
     *
     * Beware: the free method may call other H5I functions.
     *
     * If an object is closing, we can remove the ID even though the free
     * method might fail.  This can happen when a mandatory filter fails to
     * write when a dataset is closed and the chunk cache is flushed to the
     * file.  We have to close the dataset anyway. (SLU - 2010/9/7)
     */
    if (1 == info->count) {
        H5I_type_info_t *type_info; /*ptr to the type    */

        /* Get the ID's type */
        type_info = H5I_type_info_array_g[H5I_TYPE(id)];

        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        if (!type_info->cls->free_func || (type_info->cls->free_func)((void *)info->object, request) >= 0) {
            /* Remove the node from the type */
            if (NULL == H5I__remove_common(type_info, id))
                HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, (-1), "can't remove ID node");
            ret_value = 0;
        } /* end if */
        else
            ret_value = -1;
        H5_GCC_CLANG_DIAG_ON("cast-qual")
    } /* end if */
    else {
        --(info->count);
        ret_value = (int)info->count;
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__dec_ref */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_ref
 *
 * Purpose:     Decrements the number of references outstanding for an ID.
 *
 *              Updated for multi-thread
 *
 * Return:      Success:    New reference count
 *              Failure:    -1
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_dec_ref() function to H5I_dec_ref_internal()
 *              and created a new version of H5I_dec_ref() that 
 *              simply calls H5I__enter(), H5I_dec_ref_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_dec_ref(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_ref(hid_t id)
{
    int                      ret_value = 0;      /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_dec_ref_internal(id);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_dec_ref() */

int
H5I_dec_ref_internal(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I_dec_ref(0x%llx) called. \n", (unsigned long long)id);
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_ref(id, H5_REQUEST_NULL, FALSE)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I_dec_ref(0x%llx) returns %d. \n", (unsigned long long)id, ret_value);
#endif /* H5I_MT_DEBUG */

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_dec_ref_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_ref
 *
 * Purpose:     Decrements the number of references outstanding for an ID.
 *
 * Return:      Success:    New reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_ref(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_ref(id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_ref() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I__dec_app_ref
 *
 * Purpose:     Wrapper for case of modifying the application ref.
 *              count for an ID as well as normal reference count.
 *
 *              Updated for multi-thread.  To maintain atomicity, 
 *              decrement of the app_count was moved to H5I__dec_ref()
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              request != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__dec_app_ref(hid_t id, void **request)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_PACKAGE

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__dec_app_ref() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(id >= 0);

    /* Call regular decrement reference count routine */
    if ((ret_value = H5I__dec_ref(id, request, TRUE)) < 0)

        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__dec_app_ref() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__dec_app_ref
 *
 * Purpose:     Wrapper for case of modifying the application ref.
 *              count for an ID as well as normal reference count.
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              request != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__dec_app_ref(hid_t id, void **request)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(id >= 0);

    /* Call regular decrement reference count routine */
    if ((ret_value = H5I__dec_ref(id, request)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

    /* Check if the ID still exists */
    if (ret_value > 0) {
        H5I_id_info_t *info = NULL; /* Pointer to the ID info */

        /* General lookup of the ID */
        if (NULL == (info = H5I__find_id(id)))
            HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");

        /* Adjust app_ref */
        --(info->app_count);
        assert(info->count >= info->app_count);

        /* Set return value */
        ret_value = (int)info->app_count;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__dec_app_ref() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref
 *
 * Purpose:     Wrapper for case of modifying the application ref. count for
 *              an ID as well as normal reference count.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Sept 16, 2010
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_dec_app_ref() function to H5I_dec_app_ref_internal()
 *              and created a new version of H5I_dec_all_ref() that 
 *              simply calls H5I__enter(), H5I_dec_app_ref_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_dec_app_ref(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref(hid_t id)
{
    int                      ret_value = 0;      /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_dec_app_ref_internal(id);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_dec_app_ref() */

int
H5I_dec_app_ref_internal(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_dec_app_ref() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref(id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref_internal() */


#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref
 *
 * Purpose:     Wrapper for case of modifying the application ref. count for
 *              an ID as well as normal reference count.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Sept 16, 2010
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref(id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_async
 *
 * Purpose:     Asynchronous wrapper for case of modifying the application ref.
 *              count for an ID as well as normal reference count.
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              token != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 * Programmer:  Houjun Tang
 *              Oct 21, 2019
 *
 * Changes:     Added calls to H5I__enter() and H5I__exit() to track 
 *              the number of threads in H5I.  If H5I_dec_app_ref_async()
 *              is ever called from within H5I, we will need to add a 
 *              boolean prameter to control the H5I__enter/exit calls.
 *
 *                                          JRM -- 7/5/24
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_async(hid_t id, void **token)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    H5I__enter(FALSE);

    /* Sanity check */
    assert(id >= 0);

    /* [Possibly] aynchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref(id, token)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't asynchronously decrement ID ref count");

done:

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_dec_app_ref_async() */


#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_async
 *
 * Purpose:     Asynchronous wrapper for case of modifying the application ref.
 *              count for an ID as well as normal reference count.
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              token != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 * Programmer:  Houjun Tang
 *              Oct 21, 2019
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_async(hid_t id, void **token)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* [Possibly] aynchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref(id, token)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't asynchronously decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref_async() */

#endif /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__dec_app_ref_always_close
 *
 * Purpose:     Wrapper for case of always closing the ID, even when the free
 *              routine fails
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              request != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__dec_app_ref_always_close(hid_t id, void **request)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_PACKAGE

#ifdef H5_HAVE_MULTITHREAD
#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__dec_app_ref_always_close() called. \n\n\n");
#endif /* H5I_MT_DEBUG */
#endif /* H5_HAVE_MULTITHREAD */

    /* Sanity check */
    assert(id >= 0);

    /* Call application decrement reference count routine */
    ret_value = H5I__dec_app_ref(id, request);

    /* Check for failure */
    if (ret_value < 0) {
        /*
         * If an object is closing, we can remove the ID even though the free
         * method might fail.  This can happen when a mandatory filter fails to
         * write when a dataset is closed and the chunk cache is flushed to the
         * file.  We have to close the dataset anyway. (SLU - 2010/9/7)
         */
#ifdef H5_HAVE_MULTITHREAD
        H5I_remove_internal(id);
#else /* H5_HAVE_MULTITHREAD */
        H5I_remove(id);
#endif /* H5_HAVE_MULTITHREAD */

        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__dec_app_ref_always_close() */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_always_close
 *
 * Purpose:     Wrapper for case of always closing the ID, even when the free
 *              routine fails.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 * Changes:     Added calls to H5I__enter() and H5I__exit() to track 
 *              the number of threads in H5I.  If 
 *              H5I_dec_app_always_close() is ever called from within 
 *              H5I, we will need to add a boolean prameter to control 
 *              the H5I__enter/exit calls.
 *
 *                                          JRM -- 7/5/24
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_always_close(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    H5I__enter(FALSE);

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_dec_app_ref_always_close() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref_always_close(id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_dec_app_ref_always_close() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_always_close
 *
 * Purpose:     Wrapper for case of always closing the ID, even when the free
 *              routine fails.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_always_close(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref_always_close(id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref_always_close() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_always_close_async
 *
 * Purpose:     Asynchronous wrapper for case of always closing the ID, even
 *              when the free routine fails
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              token != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 * Changes:     Added calls to H5I__enter() and H5I__exit() to track 
 *              the number of threads in H5I.  If 
 *              H5I_dec_app_always_close_async() is ever called from 
 *              within  H5I, we will need to add a boolean prameter to 
 *              control the H5I__enter/exit calls.
 *
 *                                          JRM -- 7/5/24
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_always_close_async(hid_t id, void **token)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    H5I__enter(FALSE);

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_dec_app_ref_always_close_async() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(id >= 0);

    /* [Possibly] aynchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref_always_close(id, token)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't asynchronously decrement ID ref count");

done:

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_dec_app_ref_always_close_async() */


#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_always_close_async
 *
 * Purpose:     Asynchronous wrapper for case of always closing the ID, even
 *              when the free routine fails
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              token != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_always_close_async(hid_t id, void **token)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* [Possibly] aynchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref_always_close(id, token)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't asynchronously decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref_always_close_async() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_inc_ref
 *
 * Purpose:     Increment the reference count for an object.
 *
 *              Modified for multi-thread
 *
 * Return:      Success:    The new reference count
 *              Failure:    -1
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_inc_ref() function to H5I_inc_ref_internal()
 *              and created a new version of H5I_inc_ref() that 
 *              simply calls H5I__enter(), H5I_ind_ref_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_inc_ref(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
int
H5I_inc_ref(hid_t id, hbool_t app_ref)
{
    int                      ret_value = 0;      /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_inc_ref_internal(id, app_ref);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_inc_ref() */

int
H5I_inc_ref_internal(hid_t id, hbool_t app_ref)
{
    hbool_t                  done                = FALSE;
    int                      pass                = 0;
    H5I_mt_id_info_kernel_t  info_k;
    H5I_mt_id_info_kernel_t  mod_info_k;
    H5I_mt_id_info_t        *id_info_ptr = NULL; /* Pointer to the ID info */
    int                      ret_value = 0;      /* Return value */

    FUNC_ENTER_NOAPI((-1))

    memset(&info_k, 0, sizeof(H5I_mt_id_info_kernel_t));
    memset(&mod_info_k, 0, sizeof(H5I_mt_id_info_kernel_t));

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I_inc_ref((id = 0x%llx, app_ref = %d) called. \n", 
              (unsigned long long)id, (int)app_ref);
#endif /* H5I_MT_DEBUG */

    atomic_fetch_add(&(H5I_mt_g.H5I__inc_ref__num_calls), 1ULL);

    if ( app_ref ) {

        atomic_fetch_add(&(H5I_mt_g.H5I__inc_ref__num_app_calls), 1ULL);
    }

    /* Sanity check */
    assert(id >= 0);

    /* General lookup of the ID -- note that if successful, this call will convert
     * future IDs to regular IDs.
     *
     * Note that there is no need to repeat this search at the beginning of each
     * pass through the do/while loop, as any changes will be reflected in *id_info_ptr.
     */
    if (NULL == (id_info_ptr = H5I__find_id(id)))

        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");

    do {

        /* increment the pass and log retries */
        if ( pass++ >= 1 ) {

            atomic_fetch_add(&(H5I_mt_g.H5I__inc_ref__retries), 1ULL);
        }

        info_k = atomic_load(&(id_info_ptr->k));

        if ( info_k.marked ) {

            /* this is is already marked for deletion -- nothing to do here */

            /* update stats */
            if ( pass <= 1 ) {

                atomic_fetch_add(&(H5I_mt_g.H5I__inc_ref__marked_on_entry), 1ULL);

            } else {

                atomic_fetch_add(&(H5I_mt_g.H5I__inc_ref__marked_during_call), 1ULL);
            }

            HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");
        }

        if ( info_k.do_not_disturb ) {

            /* Another thread is in the process of performing an operation on the info kernel
             * that can't be rolled back -- either a future id realize_cb or discard_cb, or a
             * regular id callback that must be serialized.
             *
             * Thus we must wait until that thread is done and then re-start the operation -- which
             * may be moot by that point.
             */

            /* update stats */
            atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_yields), 1ULL);

            /* need to do better than this.  Want to call pthread_yield(),
             * but that call doesn't seem to be supported anymore.
             */
            sleep(1);

            continue;
        }

        /* Set mod_info_k to reflect the ref_count increment */
        mod_info_k.count             = info_k.count + 1;
        mod_info_k.app_count         = info_k.app_count;
        mod_info_k.object            = info_k.object;

        mod_info_k.marked            = info_k.marked;
        mod_info_k.do_not_disturb    = info_k.do_not_disturb;
        mod_info_k.is_future         = info_k.is_future;
        mod_info_k.have_global_mutex = info_k.have_global_mutex;

        if ( app_ref ) {

            mod_info_k.app_count++;
        }

        if ( atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k) ) {

            /* Update stats and set return value*/

            atomic_fetch_add(&(H5I_mt_g.H5I__inc_ref__incremented), 1ULL);

            if ( app_ref ) {

                atomic_fetch_add(&(H5I_mt_g.H5I__inc_ref__app_incremented), 1ULL);

                H5_GCC_CLANG_DIAG_OFF("cast-qual")
                ret_value = (int)(mod_info_k.app_count);
                H5_GCC_CLANG_DIAG_ON("cast-qual")

            } else {

                H5_GCC_CLANG_DIAG_OFF("cast-qual")
                ret_value = (int)(mod_info_k.count);
                H5_GCC_CLANG_DIAG_ON("cast-qual")
            }

            done = TRUE;

        } else {

            /* the atomic compare exchange strong failed -- try again */

            /* done is false, so nothing to do to trigger the retry */
        }
    } while ( ! done );

done:

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I_inc_ref((id = 0x%llx, app_ref = %d) returns %d. \n", 
              (unsigned long long)id, (int)app_ref, (int)ret_value);
#endif /* H5I_MT_DEBUG */

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_inc_ref_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_inc_ref
 *
 * Purpose:     Increment the reference count for an object.
 *
 * Return:      Success:    The new reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_inc_ref(hid_t id, hbool_t app_ref)
{
    H5I_id_info_t *info      = NULL; /* Pointer to the ID info */
    int            ret_value = 0;    /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* General lookup of the ID */
    if (NULL == (info = H5I__find_id(id)))
        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");

    /* Adjust reference counts */
    ++(info->count);
    if (app_ref)
        ++(info->app_count);

    /* Set return value */
    ret_value = (int)(app_ref ? info->app_count : info->count);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_inc_ref() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I_get_ref
 *
 * Purpose:     Retrieve the reference count for an object.
 * 
 *              Updated for multi-thread.
 *
 * Return:      Success:    The reference count
 *              Failure:    -1
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_get_ref() function to H5I_get_ref_internal()
 *              and created a new version of H5I_get_ref() that 
 *              simply calls H5I__enter(), H5I_get_ref_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_get_ref(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
int
H5I_get_ref(hid_t id, hbool_t app_ref)
{
    herr_t                   ret_value     = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_get_ref_internal(id, app_ref);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_get_ref() */

int
H5I_get_ref_internal(hid_t id, hbool_t app_ref)
{
    H5I_mt_id_info_t           *id_info_ptr      = NULL; /* Pointer to the ID */
    H5I_mt_id_info_kernel_t  info_k;
    int                      ret_value = 0;    /* Return value */

    FUNC_ENTER_NOAPI((-1))

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_get_ref() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(id >= 0);

    /* General lookup of the ID */
    if (NULL == (id_info_ptr = H5I__find_id(id)))
        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");

    info_k = atomic_load(&(id_info_ptr->k));

    /* Set return value */
    ret_value = (int)(app_ref ? info_k.app_count : info_k.count);

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_get_ref_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_get_ref
 *
 * Purpose:     Retrieve the reference count for an object.
 *
 * Return:      Success:    The reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_get_ref(hid_t id, hbool_t app_ref)
{
    H5I_id_info_t *info      = NULL; /* Pointer to the ID */
    int            ret_value = 0;    /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* General lookup of the ID */
    if (NULL == (info = H5I__find_id(id)))
        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");

    /* Set return value */
    ret_value = (int)(app_ref ? info->app_count : info->count);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_get_ref() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I__inc_type_ref
 *
 * Purpose:     Increment the reference count for an ID type.
 *
 *              Updated for multi-thread.
 *
 * Return:      Success:    The new reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I__inc_type_ref(H5I_type_t type)
{
    H5I_mt_type_info_t *type_info_ptr = NULL; /* Pointer to the type */
    int                 ret_value     = -1;   /* Return value */

    FUNC_ENTER_PACKAGE

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__inc_type_ref() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert( ( type > 0 ) && ( (int)type < atomic_load(&(H5I_mt_g.next_type)) ) );

    /* Check arguments */
    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if ( NULL == type_info_ptr )

        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Set return value -- atomic_fetch_add() returns the old value, hence the plus 1 */
    ret_value = 1 + (int)(atomic_fetch_add(&(type_info_ptr->init_count), 1));

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__inc_type_ref() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__inc_type_ref
 *
 * Purpose:     Increment the reference count for an ID type.
 *
 * Return:      Success:    The new reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I__inc_type_ref(H5I_type_t type)
{
    H5I_type_info_t *type_info = NULL; /* Pointer to the type */
    int              ret_value = -1;   /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(type > 0 && (int)type < H5I_next_type_g);

    /* Check arguments */
    type_info = H5I_type_info_array_g[type];
    if (NULL == type_info)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Set return value */
    ret_value = (int)(++(type_info->init_count));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__inc_type_ref() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_type_ref
 *
 * Purpose:     Decrements the reference count on an entire type of IDs.
 *              If the type reference count becomes zero then the type is
 *              destroyed along with all IDs in that type regardless of
 *              their reference counts. Destroying IDs involves calling
 *              the free-func for each ID's object and then adding the ID
 *              struct to the ID free list.
 *              Returns the number of references to the type on success; a
 *              return value of 0 means that the type will have to be
 *              re-initialized before it can be used again (and should probably
 *              be set to H5I_UNINIT).
 *
 * Return:      Success:    Number of references to type
 *              Failure:    -1
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_dec_type_ref() function to H5I_dec_type_ref_internal()
 *              and created a new version of H5I_dec_type_ref() that 
 *              simply calls H5I__enter(), H5I_dec_type_ref_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_dec_type_ref(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_type_ref(H5I_type_t type)
{
    int                      ret_value = 0;      /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_dec_type_ref_internal(type);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_dec_type_ref() */

int
H5I_dec_type_ref_internal(H5I_type_t type)
{
    H5I_mt_type_info_t *type_info_ptr = NULL; /* Pointer to the ID type */
    herr_t              ret_value = 0;    /* Return value */

    FUNC_ENTER_NOAPI((-1))

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_dec_type_ref() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    if (type <= H5I_BADID || (int)type >= atomic_load(&(H5I_mt_g.next_type)))

        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, (-1), "invalid type number");

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if ( ( type_info_ptr == NULL ) || ( atomic_load(&(type_info_ptr->init_count)) <= 0 ) )

        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Decrement the number of users of the ID type.  If this is the
     * last user of the type then release all IDs from the type and
     * free all memory it used.  The free function is invoked for each ID
     * being freed.
     */
    if ( 1 == atomic_load(&(type_info_ptr->init_count)) ) {

        H5I__destroy_type(type);
        ret_value = 0;
    
    } else {

        /* atomic_fetch_sub() returns the original value of the atomic variable -- hence the minus 1 */
        ret_value = (int)(atomic_fetch_sub(&(type_info_ptr->init_count), 1)) - 1;
    }

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_dec_type_ref_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_type_ref
 *
 * Purpose:     Decrements the reference count on an entire type of IDs.
 *              If the type reference count becomes zero then the type is
 *              destroyed along with all IDs in that type regardless of
 *              their reference counts. Destroying IDs involves calling
 *              the free-func for each ID's object and then adding the ID
 *              struct to the ID free list.
 *              Returns the number of references to the type on success; a
 *              return value of 0 means that the type will have to be
 *              re-initialized before it can be used again (and should probably
 *              be set to H5I_UNINIT).
 *
 * Return:      Success:    Number of references to type
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_type_ref(H5I_type_t type)
{
    H5I_type_info_t *type_info = NULL; /* Pointer to the ID type */
    herr_t           ret_value = 0;    /* Return value */

    FUNC_ENTER_NOAPI((-1))

    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, (-1), "invalid type number");

    type_info = H5I_type_info_array_g[type];
    if (type_info == NULL || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Decrement the number of users of the ID type.  If this is the
     * last user of the type then release all IDs from the type and
     * free all memory it used.  The free function is invoked for each ID
     * being freed.
     */
    if (1 == type_info->init_count) {
        H5I__destroy_type(type);
        ret_value = 0;
    }
    else {
        --(type_info->init_count);
        ret_value = (herr_t)type_info->init_count;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_type_ref() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I__get_type_ref
 *
 * Purpose:     Retrieve the reference count for an ID type.
 *
 *              Updated for multi-thread
 *
 * Return:      Success:    The reference count
 *
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I__get_type_ref(H5I_type_t type)
{
    H5I_mt_type_info_t *type_info_ptr = NULL; /* Pointer to the type  */
    int                 ret_value = -1;   /* Return value         */

    FUNC_ENTER_PACKAGE

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__get_type_ref() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(type >= 0);

    /* Check arguments */

    if ( ( type <= H5I_BADID ) || ( (int)type >= atomic_load(&(H5I_mt_g.next_type)) ) )

        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, (-1), "invalid type number");

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if ( ! type_info_ptr )

        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Set return value */
    ret_value = (int)atomic_load(&(type_info_ptr->init_count));

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__get_type_ref() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__get_type_ref
 *
 * Purpose:     Retrieve the reference count for an ID type.
 *
 * Return:      Success:    The reference count
 *
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I__get_type_ref(H5I_type_t type)
{
    H5I_type_info_t *type_info = NULL; /* Pointer to the type  */
    int              ret_value = -1;   /* Return value         */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(type >= 0);

    /* Check arguments */
    type_info = H5I_type_info_array_g[type];
    if (!type_info)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Set return value */
    ret_value = (int)type_info->init_count;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__get_type_ref() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I__iterate_cb
 *
 * Purpose:     Callback routine for H5I_iterate, invokes "user" callback
 *              function, and then sets return value, based on the result of
 *              that callback.
 *
 *              Updated for multi-thread.
 *
 * Return:      Success:    H5_ITER_CONT (0) or H5_ITER_STOP (1)
 *              Failure:    H5_ITER_ERROR (-1)
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__iterate_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    hbool_t                  have_global_mutex;
    hbool_t                  bool_result;
    H5I_mt_id_info_t        *id_info_ptr       = (H5I_mt_id_info_t *)_item;  /* Pointer to the ID info */
    H5I_iterate_ud_t        *udata             = (H5I_iterate_ud_t *)_udata; /* User data for callback */
    H5I_mt_id_info_kernel_t  info_k;
    herr_t                   result;
    int                      ret_value         = H5_ITER_CONT;               /* Callback return value */

    FUNC_ENTER_PACKAGE_NOERR

    memset(&info_k, 0, sizeof(H5I_mt_id_info_kernel_t));

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__iterate_cb() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__num_calls), 1ULL);

    have_global_mutex = udata->have_global_mutex;

    if ( have_global_mutex ) {

        atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__num_calls__with_global_mutex), 1ULL);

    } else {

        atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__num_calls__without_global_mutex), 1ULL);
    }

    /* read the current value of the id info kernel */
    info_k = atomic_load(&(id_info_ptr->k));

    /* Only invoke the callback function if this ID has not been marked for deletion, is visible 
     * externally and its reference count is positive.
     *
     * While the user_func (and all the callbacks defined in the type) should be thread safe,
     * for now, use the do_not_disturb flag to ensure that user_func has exclusive access
     * to the object -- at least from within H5I.  (Note, however, that the object can still 
     * be looked up by the user and accessed outside the H5I code.  Similarly, the user 
     * may have a copy of the pointer, and be able to access its data structure at will 
     * directly))  
     *
     * If the limited protection given to the object associated with the ID is not sufficient, 
     * the client object will have to be made multi-thread safe.  Indeed, this should be 
     * the end state -- but unless and until the native VOL is made thread safe, this 
     * limited protection seems a reasonable middle ground 
     * 
     * As per the other uses of the do_not_disturb flag, it is possible for the user_func to 
     * trigger a deadlock if it attempts to access the current ID via H5I either directly 
     * or through some sequence of calls.
     */
    if ( ( ! info_k.marked ) && ( ( ( ! udata->app_ref ) || ( info_k.app_count > 0 ) ) ) ) {

        hbool_t                  done = FALSE;
        hbool_t                  bypass_do_not_disturb;
        hbool_t                  do_not_disturb_set = FALSE;
        int                      pass                = 0;
        H5I_type_t               type                = udata->obj_type;
        H5I_mt_id_info_kernel_t  mod_info_k;
        void                    *object;
        herr_t                   cb_ret_val;

        memset(&mod_info_k, 0, sizeof(H5I_mt_id_info_kernel_t));

        do {
            bypass_do_not_disturb = FALSE;

            /* increment the pass and log retries */
            if ( pass++ >= 1 ) {

                atomic_fetch_add(&(H5I_mt_g.H5I__dec_ref__retries), 1ULL);
            }

            info_k = atomic_load(&(id_info_ptr->k));

            if ( info_k.marked ) {

                /* the ID has been marked for deletion since we started, update stats 
                 * and return without calling the user_func()
                 */
                atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__marked_during_call), 1ULL);

                break;
            }

            if ( info_k.do_not_disturb ) {

                if ( ( have_global_mutex ) && ( info_k.have_global_mutex ) ) {

                    bypass_do_not_disturb = TRUE;

                    atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_bypasses), 1ULL);

                } else {

                    /* Another thread is in the process of performing an operation on the info kernel
                     * that can't be rolled back -- either a future id realize_cb or discard_cb, or a
                     * regular id callback that must be serialized.
                     *
                     * Thus we must wait until that thread is done and then re-start the operation -- which
                     * may be moot by that point.
                     */

                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_yields), 1ULL);

                    /* need to do better than this.  Want to call pthread_yield(),
                     * but that call doesn't seem to be supported anymore.
                     */
                    sleep(1);

                    continue;
                }
            }

            if ( ! bypass_do_not_disturb ) {

                /* attempt to set the do_not_disturb flag */
                mod_info_k.count             = info_k.count;
                mod_info_k.app_count         = info_k.app_count;
                mod_info_k.object            = info_k.object;

                mod_info_k.marked            = info_k.marked;
                mod_info_k.do_not_disturb    = TRUE;
                mod_info_k.is_future         = info_k.is_future;

                /* set mod_inf_k.have_global_mutex to TRUE since if we don't have the global
                 * mutext, we will grab it before calling the user function, and drop it as soon
                 * as it returns.
                 */
                mod_info_k.have_global_mutex = have_global_mutex;

                /* We want to ensure that no other thread inside H5I does anything with 
                 * the object while we call the user_func on the objec on the object.  
                 * Note that this is only a partial solution, but it is the best we can 
                 * do in H5I.
                 *
                 * To do this, try to set the do_not_disturb flag in the kernl.   If
                 * successful, this will prevent any other threads from modifying
                 * id_info_ptr->k until after it is set back to FALSE.  In particluar,
                 * no thread in H5I will call any function on the object associated 
                 * with the ID until it successfully sets the do_not_disturb flag.
                 */
                if ( ! atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k) ) {

                    /* Some other thread changed the value of id_info_ptr->k since we last read
                     * it.  Thus we must return to the beginning of the do loop and start
                     * again.  Note that it is possible that by that time, there will be
                     * nothing left to do.
                     */
    
                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.num_failed_do_not_disturb_sets), 1ULL);
    
                    continue;
    
                } else {
    
                    do_not_disturb_set = TRUE;
    
#if 0 /* JRM */
                    /* make info_k into a copy of the global kernel */
                    info_k.do_not_disturb = TRUE;
#else /* JTM */
                    /* On the face of it, it would seem that we could just update info_k
                     * to match mod_info_k, and use it in the next atomic_compare_exchange_strong()
                     * call.  However, for reason or reasons unknown, this doesn't work.
                     *
                     * Instead, we reload info_k after the atomic_compare_exchange_strong(),
                     * and verify that it contains the expected values.
                     */
                    info_k = atomic_load(&(id_info_ptr->k));

                    assert(info_k.count             == mod_info_k.count);
                    assert(info_k.app_count         == mod_info_k.app_count);
                    assert(info_k.object            == mod_info_k.object);

                    assert(info_k.marked            == mod_info_k.marked);
                    assert(info_k.do_not_disturb    == mod_info_k.do_not_disturb);
                    assert(info_k.is_future         == mod_info_k.is_future);
                    assert(info_k.have_global_mutex == mod_info_k.have_global_mutex);
#endif /* JRM */

                    /* prepare to reset the do_not_disturb flag */
                    mod_info_k.do_not_disturb    = FALSE;
                    mod_info_k.have_global_mutex = FALSE;

                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.num_successful_do_not_disturb_sets), 1ULL);

#if H5I_MT_DEBUG_DO_NOT_DISTURB
                    fprintf(stdout, "H5I__iterate_cb() set do not disturb on id = 0x%llx.\n",
                            (unsigned long long)(id_info_ptr->id));
#endif /* H5I_MT_DEBUG_DO_NOT_DISTURB */
                }
            } /* if ( ! bypass_do_not_disturb ) */

            assert( ( do_not_disturb_set ) || ( bypass_do_not_disturb ) );

            /* The stored object pointer might be an H5VL_object_t, in which
             * case we'll need to get the wrapped object struct (H5F_t *, etc.).
             */
#if 0 
            H5_GCC_CLANG_DIAG_OFF("cast-qual")
            object = H5I__unwrap((void *)info_k.object, type, &object); /* may hit global mutex */
            H5_GCC_CLANG_DIAG_ON("cast-qual")
#endif 
            /* H5I__unwrap() can fail -- for now at least.  Handle this by treating any 
             * failure as a callback failure.  
             */
            H5_GCC_CLANG_DIAG_OFF("cast-qual")
            result = H5I__unwrap((void *)info_k.object, type, &object);
            H5_GCC_CLANG_DIAG_ON("cast-qual")

            if ( result < 0 ) {

                cb_ret_val = -1;

            } else {

                atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__num_user_func_calls), 1ULL);

                /* Invoke callback function.  Grab the global mutex if we don't have it already */
                if ( ! have_global_mutex ) {

                    atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__global_mutex_locks_for_user_func), 1ULL);
                    H5_API_LOCK
                    cb_ret_val = (*udata->user_func)((void *)object, id_info_ptr->id, udata->user_udata);
                    H5_API_UNLOCK
                    atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__global_mutex_unlocks_for_user_func), 1ULL);

                } else {

                    cb_ret_val = (*udata->user_func)((void *)object, id_info_ptr->id, udata->user_udata);
                }
            }

            /* Set the return value based on the callback's return value */
            if (cb_ret_val > 0) {

                atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__num_user_func_iter_stops), 1ULL);

                ret_value = H5_ITER_STOP; /* terminate iteration early */

            } else if (cb_ret_val < 0) {

                atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__num_user_func_fails), 1ULL);

                ret_value = H5_ITER_ERROR; /* indicate failure (which terminates iteration) */

            } else {

                atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__num_user_func_successes), 1ULL);
            }

            if ( ! bypass_do_not_disturb ) {

                /* since we have the do_not_disturb flag, the following atomic_compare_exchange_strong()
                 * must succeed.
                 */
                assert(info_k.do_not_disturb);

                assert( ! mod_info_k.do_not_disturb );

                bool_result = atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k);
                assert(bool_result);

                atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_resets), 1ULL);

#if H5I_MT_DEBUG_DO_NOT_DISTURB
                fprintf(stdout, "H5I__iterate_cb() reset do not disturb on id = 0x%llx.\n",
                        (unsigned long long)(id_info_ptr->id));
#endif /* H5I_MT_DEBUG_DO_NOT_DISTURB */
            }

            /* If execution gets this far, we are done with the do/while loop */
            done = TRUE;

        } while ( ! done );
    } else {

        atomic_fetch_add(&(H5I_mt_g.H5I__iterate_cb__num_user_func_skips), 1ULL);
    }

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__iterate_cb() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__iterate_cb
 *
 * Purpose:     Callback routine for H5I_iterate, invokes "user" callback
 *              function, and then sets return value, based on the result of
 *              that callback.
 *
 * Return:      Success:    H5_ITER_CONT (0) or H5_ITER_STOP (1)
 *              Failure:    H5_ITER_ERROR (-1)
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__iterate_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    H5I_id_info_t    *info      = (H5I_id_info_t *)_item;     /* Pointer to the ID info */
    H5I_iterate_ud_t *udata     = (H5I_iterate_ud_t *)_udata; /* User data for callback */
    int               ret_value = H5_ITER_CONT;               /* Callback return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Only invoke the callback function if this ID is visible externally and
     * its reference count is positive.
     */
    if ((!udata->app_ref) || (info->app_count > 0)) {
        H5I_type_t type = udata->obj_type;
        void      *object;
        herr_t     cb_ret_val;

        /* The stored object pointer might be an H5VL_object_t, in which
         * case we'll need to get the wrapped object struct (H5F_t *, etc.).
         */
        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        object = H5I__unwrap((void *)info->object, type);
        H5_GCC_CLANG_DIAG_ON("cast-qual")

        /* Invoke callback function */
        cb_ret_val = (*udata->user_func)((void *)object, info->id, udata->user_udata);

        /* Set the return value based on the callback's return value */
        if (cb_ret_val > 0)
            ret_value = H5_ITER_STOP; /* terminate iteration early */
        else if (cb_ret_val < 0)
            ret_value = H5_ITER_ERROR; /* indicate failure (which terminates iteration) */
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__iterate_cb() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_iterate
 *
 * Purpose:     Apply function FUNC to each member of type TYPE (with
 *              non-zero application reference count if app_ref is TRUE).
 *              Stop if FUNC returns a non zero value (i.e. anything
 *              other than H5_ITER_CONT).
 *
 *              If FUNC returns a positive value (i.e. H5_ITER_STOP),
 *              return SUCCEED.
 *
 *              If FUNC returns a negative value (i.e. H5_ITER_ERROR),
 *              return FAIL.
 *
 *              The FUNC should take a pointer to the object and the
 *              udata as arguments and return non-zero to terminate
 *              siteration, and zero to continue.
 *
 *              Updated for multi-thread.
 *
 * Limitation:  Currently there is no way to start the iteration from
 *              where a previous iteration left off.
 *
 * Return:      SUCCEED/FAIL
 *
 * Changes:     To track threads entering and exiting H5I (needed for free 
 *              list management), changed the name of the existing 
 *              H5I_iterate() function to H5I_iterate_internal()
 *              and created a new version of H5I_iterate() that 
 *              simply calls H5I__enter(), H5I_iterate_internal(),
 *              and then H5I__exit().
 *
 *              It would make more sense to just add another parameter to 
 *              H5I_iterate(), but until we have a single version 
 *              of the H5I code, this will be complicated.  Make this 
 *              change when we get to the production version.
 * 
 *                                              JRM -- 07/04/24
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_iterate(H5I_type_t type, H5I_search_func_t func, void *udata, hbool_t app_ref)
{
    herr_t                   ret_value     = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    H5I__enter(FALSE);

    ret_value = H5I_iterate_internal(type, func, udata, app_ref);

    H5I__exit();

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_iterate() */

herr_t
H5I_iterate_internal(H5I_type_t type, H5I_search_func_t func, void *udata, hbool_t app_ref)
{
    hbool_t                  have_global_mutex = TRUE; /* trivially true in the single thread case */
    H5I_mt_type_info_t      *type_info_ptr = NULL;    /* Pointer to the type */
    H5I_mt_id_info_kernel_t  info_k;
    herr_t                   ret_value     = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_iterate() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)

    if ( H5TS_have_mutex(&H5_g.init_lock, &have_global_mutex) < 0 ) {
        
        ret_value = H5_ITER_ERROR;
    
    } 
#endif /* defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD) */


    /* Check arguments */
    if (type <= H5I_BADID || (int)type >= atomic_load(&(H5I_mt_g.next_type)))

        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    /* Only iterate through ID list if it is initialized and there are IDs in type */
    if ( ( type_info_ptr )  && ( atomic_load(&(type_info_ptr->init_count)) > 0 ) && 
         ( atomic_load(&(type_info_ptr->id_count)) > 0 ) ) {

        H5I_iterate_ud_t       iter_udata; /* User data for iteration callback */
        H5I_mt_id_info_t      *id_info_ptr = NULL;
        unsigned long long int id;
        void * value;

        /* Set up iterator user data */
        iter_udata.user_func         = func;
        iter_udata.user_udata        = udata;
        iter_udata.app_ref           = app_ref;
        iter_udata.obj_type          = type;
        iter_udata.have_global_mutex = have_global_mutex;

        /* Iterate over IDs */
        if ( lfht_get_first(&(type_info_ptr->lfht), &id, &value) ) {

            do {
                id_info_ptr = (H5I_mt_id_info_t *)value;

                info_k = atomic_load(&(id_info_ptr->k));

                if (! info_k.marked) {

                    int ret = H5I__iterate_cb((void *)id_info_ptr, NULL, (void *)&iter_udata);

                    if (H5_ITER_ERROR == ret)
                        HGOTO_ERROR(H5E_ID, H5E_BADITER, FAIL, "iteration failed");

                    if (H5_ITER_STOP == ret)
                        break;
                }
            } while (lfht_get_next(&(type_info_ptr->lfht), id, &id, &value));
        }
    }

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_iterate_internal() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_iterate
 *
 * Purpose:     Apply function FUNC to each member of type TYPE (with
 *              non-zero application reference count if app_ref is TRUE).
 *              Stop if FUNC returns a non zero value (i.e. anything
 *              other than H5_ITER_CONT).
 *
 *              If FUNC returns a positive value (i.e. H5_ITER_STOP),
 *              return SUCCEED.
 *
 *              If FUNC returns a negative value (i.e. H5_ITER_ERROR),
 *              return FAIL.
 *
 *              The FUNC should take a pointer to the object and the
 *              udata as arguments and return non-zero to terminate
 *              siteration, and zero to continue.
 *
 * Limitation:  Currently there is no way to start the iteration from
 *              where a previous iteration left off.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_iterate(H5I_type_t type, H5I_search_func_t func, void *udata, hbool_t app_ref)
{
    H5I_type_info_t *type_info = NULL;    /* Pointer to the type */
    herr_t           ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Check arguments */
    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");
    type_info = H5I_type_info_array_g[type];

    /* Only iterate through ID list if it is initialized and there are IDs in type */
    if (type_info && type_info->init_count > 0 && type_info->id_count > 0) {
        H5I_iterate_ud_t iter_udata; /* User data for iteration callback */
        H5I_id_info_t   *item = NULL;
        H5I_id_info_t   *tmp  = NULL;

        /* Set up iterator user data */
        iter_udata.user_func  = func;
        iter_udata.user_udata = udata;
        iter_udata.app_ref    = app_ref;
        iter_udata.obj_type   = type;

        /* Iterate over IDs */
        HASH_ITER(hh, type_info->hash_table, item, tmp)
        {
            if (!item->marked) {
                int ret = H5I__iterate_cb((void *)item, NULL, (void *)&iter_udata);
                if (H5_ITER_ERROR == ret)
                    HGOTO_ERROR(H5E_ID, H5E_BADITER, FAIL, "iteration failed");
                if (H5_ITER_STOP == ret)
                    break;
            }
        }
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_iterate() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_get_first
 *
 * Purpose:     Given a type ID, find the first ID in the given type, and 
 *              return that ID and its associated un-wrapped object pointer
 *              in *id_ptr and *object_ptr respectively.
 *
 *              If the type is empty, *id_ptr is set to zero, and 
 *              *object_ptr is set to NULL.  Recall that since type 0 is 
 *              not used, and since the type is encoded in the id, an 
 *              id of zero cannot occur.
 *
 *              Note that the itteration supported by the H5I_get_first()
 *              and H5I_get_next() is neither id nor insertion order.
 *
 *              On failure, *id_ptr and *object_ptr are undefined.
 *
 * Return:      Success:    SUCCEED
 *
 *              Failure:    FAIL
 *
 * Changes:     Added the called_from_H5I paramter, which must be set 
 *              to TRUE if the function is called withing the H5I package,
 *              and FALSE otherwise.  This is needed to allow  tracking
 *              the number of threads inside H5I, which is in turn used
 *              in free list management.
 *                                              JRM -- 7/6/24
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_get_first(H5I_type_t type, hid_t *id_ptr, void ** object_ptr, hbool_t called_from_H5I)
{
    H5I_mt_type_info_t      *type_info_ptr    = NULL;    /* Pointer to the type */
    unsigned long long int   id               = 0;
    void                    *value            = NULL;
    void                    *object           = NULL;
    H5I_mt_id_info_t        *id_info_ptr      = NULL;
    H5I_mt_id_info_kernel_t  info_k;
    herr_t                   result;
    herr_t                   ret_value        = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    if ( ! called_from_H5I ) {

        /* if H5I_get_first() is not called from withing H5I, it must have been called
         * from somewhere within the HDF5 library proper -- hence we set the public_api
         * parameter of H5I__enter() to FALSE.
         */
        H5I__enter(FALSE);
    }

    /* Check arguments */
    if (type <= H5I_BADID || (int)type >= atomic_load(&(H5I_mt_g.next_type)))

        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    if ( ( ! id_ptr ) || ( ! object_ptr ) ) 

        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bad id or object ptr");

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    /* Only iterate through ID list if it is initialized and there are IDs in type */
    if ( ( type_info_ptr )  && ( atomic_load(&(type_info_ptr->init_count)) > 0 ) && 
         ( atomic_load(&(type_info_ptr->id_count)) > 0 ) ) {

        /* Even though we have just tested to see if the type is non-empty, it is 
         * possible that it will be emptied during the following do-while loop.
         * Thus set *id_ptr and *object_ptr to values indicating that the type is
         * empty before starting our search for the first entry in the type.
         * Typically, the following assignments will be overwritten.
         */
        *id_ptr     = (hid_t)0;
        *object_ptr = NULL;

        /* Iterate over IDs */
        if ( lfht_get_first(&(type_info_ptr->lfht), &id, &value) ) {

            do {
                id_info_ptr = (H5I_mt_id_info_t *)value;

                info_k = atomic_load(&(id_info_ptr->k));

                if ( ! info_k.marked ) {

                    /* The stored object pointer might be an H5VL_object_t, in which
                     * case we'll need to get the wrapped object struct (H5F_t *, etc.).
                     */
#if 0 
                    H5_GCC_CLANG_DIAG_OFF("cast-qual")
                    object = H5I__unwrap((void *)info_k.object, type);
                    H5_GCC_CLANG_DIAG_ON("cast-qual")
#endif 
                    H5_GCC_CLANG_DIAG_OFF("cast-qual")
                    result = H5I__unwrap((void *)info_k.object, type, &object);
                    H5_GCC_CLANG_DIAG_ON("cast-qual")

                    if ( result < 0 )

                        HGOTO_ERROR(H5E_LIB, H5E_CANTGET, FAIL, "Can't get unwrapped object");

                    *id_ptr     = (hid_t)id;
                    *object_ptr = object;
                    break;
                }
            } while (lfht_get_next(&(type_info_ptr->lfht), id, &id, &value));
        }
    } else {

        *id_ptr     = (hid_t)0;
        *object_ptr = NULL;
    }

done:

    if ( ! called_from_H5I ) {

        H5I__exit();
    }

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_get_first() */

/*-------------------------------------------------------------------------
 * Function:    H5I_get_next
 *
 * Purpose:     Given a type ID, and the last id visited in an itteration 
 *              through the specified index, return the next ID in the 
 *              itteration and its associated un-wrapped object pointer 
 *              in *next_id_ptr and *object_ptr respectively.
 *
 *              If there are no further IDs remaining in the type, 
 *              *id_ptr is set to zero, and *object_ptr is set to NULL.  
 *              Recall that since type 0 is not used, and since the type 
 *              is encoded in the id, an id of zero cannot occur.
 *
 *              Note that the itteration supported by the H5I_get_first()
 *              and H5I_get_next() is neither id nor insertion order.
 *
 *              Further, note that the index may be modified during the
 *              itteration.  Deletions, additions, and modifications to
 *              the object associated with an ID may or may not be 
 *              reflected in the itterations.
 *
 *              On failure, *id_ptr and *object_ptr are undefined.
 *
 * Return:      Success:    SUCCEED
 *
 *              Failure:    FAIL
 *
 * Changes:     Added the called_from_H5I paramter, which must be set 
 *              to TRUE if the function is called withing the H5I package,
 *              and FALSE otherwise.  This is needed to allow  tracking
 *              the number of threads inside H5I, which is in turn used
 *              in free list management.
 *                                              JRM -- 7/6/24
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_get_next(H5I_type_t type, hid_t last_id, hid_t *next_id_ptr, void ** next_object_ptr, hbool_t called_from_H5I)
{
    H5I_mt_type_info_t      *type_info_ptr = NULL;    /* Pointer to the type */
    unsigned long long int   id            = 0;
    void                    *value         = NULL;
    void                    *object        = NULL;
    H5I_mt_id_info_t        *id_info_ptr   = NULL;
    H5I_mt_id_info_kernel_t  info_k;
    herr_t                   result;
    herr_t                   ret_value     = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    if ( ! called_from_H5I ) {

        /* if H5I_get_first() is not called from withing H5I, it must have been called
         * from somewhere within the HDF5 library proper -- hence we set the public_api
         * parameter of H5I__enter() to FALSE.
         */
        H5I__enter(FALSE);
    }

    /* Check arguments */
    if (type <= H5I_BADID || (int)type >= atomic_load(&(H5I_mt_g.next_type)))

        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    if ( ( last_id == 0 ) || ( type != H5I_TYPE(last_id) ) ) 

        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid last_id");

    if ( ( ! next_id_ptr ) || ( ! next_object_ptr ) ) 

        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bad next id or next object ptr");

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    /* Only iterate through ID list if it is initialized and there are IDs in type */
    if ( ( type_info_ptr )  && ( atomic_load(&(type_info_ptr->init_count)) > 0 ) && 
         ( atomic_load(&(type_info_ptr->id_count)) > 0 ) ) {

        id = (unsigned long long int)last_id;

        /* While we know that the target index is not empty, it is possible that 
         * last_id is the last id in the itteration through theindex, or that the 
         * next id will be deleted before we get to it.  
         * 
         * Thus set *next_id_ptr and *next_object_ptr to values indicating that we 
         * have completed the itteration before we start searcing for the next 
         * id in the indexxthe type is
         * 
         * Usually, the following assignments will be overwritten.
         */
        *next_id_ptr     = (hid_t)0;
        *next_object_ptr = NULL;

        /* Iterate over IDs starting just after last_id */
        while ( lfht_get_next(&(type_info_ptr->lfht), id, &id, &value) ) {

            id_info_ptr = (H5I_mt_id_info_t *)value;

            info_k = atomic_load(&(id_info_ptr->k));

            if ( ! info_k.marked ) {

                /* The stored object pointer might be an H5VL_object_t, in which
                 * case we'll need to get the wrapped object struct (H5F_t *, etc.).
                 */
#if 0 
                H5_GCC_CLANG_DIAG_OFF("cast-qual")
                object = H5I__unwrap((void *)info_k.object, type);
                H5_GCC_CLANG_DIAG_ON("cast-qual")
#endif 
                H5_GCC_CLANG_DIAG_OFF("cast-qual")
                result = H5I__unwrap((void *)info_k.object, type, &object);
                H5_GCC_CLANG_DIAG_ON("cast-qual")

                if ( result < 0 )

                    HGOTO_ERROR(H5E_LIB, H5E_CANTGET, FAIL, "Can't get unwrapped object");

                *next_id_ptr     = (hid_t)id;
                *next_object_ptr = object;
                break;
            }
        }
    } else {

        *next_id_ptr     = (hid_t)0;
        *next_object_ptr = NULL;
    }

done:

    if ( ! called_from_H5I ) {

        H5I__exit();
    }

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_get_next() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I__find_id
 *
 * Purpose:     Given an object ID find the info struct that describes the
 *              object.
 *
 * Return:      Success:    A pointer to the object's info struct.
 *
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
H5I_mt_id_info_t *
H5I__find_id(hid_t id)
{
    hbool_t                 do_not_disturb_set;
    hbool_t                 done = FALSE;
    hbool_t                 have_global_mutex = TRUE; /* trivially true in the serial case */
    hbool_t                 cls_is_mt_safe;
    hbool_t                 bool_result;
    int                     pass = 0;
    herr_t                  result;
    H5I_type_t              type;                      /* ID's type */
    H5I_mt_type_info_t     *type_info_ptr      = NULL; /* Pointer to the type */
    H5I_mt_id_info_t       *id_info_ptr        = NULL; /* ID's info */
    H5I_mt_id_info_t       *dup_id_info_ptr;
    H5I_mt_id_info_t       *last_id_info_ptr   = NULL; /* ID's info */
    H5I_mt_id_info_kernel_t info_k;
    H5I_mt_id_info_kernel_t mod_info_k;
    H5I_mt_id_info_t       *ret_value          = NULL; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    atomic_fetch_add(&(H5I_mt_g.H5I__find_id__num_calls), 1ULL);

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I__find_id(0x%llx) called. \n", (unsigned long long)id);
#endif /* H5I_MT_DEBUG */

#if defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD)

    if ( H5TS_have_mutex(&H5_g.init_lock, &have_global_mutex) < 0 )

        HGOTO_DONE(NULL);

#endif /* defined(H5_HAVE_THREADSAFE) || defined(H5_HAVE_MULTITHREAD) */

    if ( have_global_mutex ) {

        atomic_fetch_add(&(H5I_mt_g.H5I__find_id__num_calls_with_global_mutex), 1ULL);

    } else {

        atomic_fetch_add(&(H5I_mt_g.H5I__find_id__num_calls_without_global_mutex), 1ULL);
    }

    /* Check arguments */
    type = H5I_TYPE(id);
    if (type <= H5I_BADID || (int)type >= atomic_load(&(H5I_mt_g.next_type)))
        HGOTO_DONE(NULL);

    do {

        do_not_disturb_set = FALSE;
        type_info_ptr = NULL;
        id_info_ptr = NULL;

        /* increment the pass and log retries */
        if ( pass++ >= 1 ) {

            atomic_fetch_add(&(H5I_mt_g.H5I__find_id__retries), 1ULL);
        }

        type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

        if  ( ( ! type_info_ptr ) || ( atomic_load(&(type_info_ptr->init_count)) <= 0) ) {

            /* type doesn't exist, or has been logically deleted.  No point in
             * in retrying, so just return NULL.
             */
            HGOTO_DONE(NULL);
        }

        cls_is_mt_safe = ((type_info_ptr->cls->flags & H5I_CLASS_IS_MT_SAFE) != 0);

        /* Check for same ID as we have looked up last time */
        last_id_info_ptr = atomic_load(&(type_info_ptr->last_id_info));

        if ( ( last_id_info_ptr ) && ( last_id_info_ptr->id == id ) ) {

            id_info_ptr = last_id_info_ptr;

        } else {

            if ( ! lfht_find(&(type_info_ptr->lfht), (unsigned long long int)id, (void **)&id_info_ptr) ) {

                assert(NULL == id_info_ptr);
            }

            /* Remember this ID */
            atomic_store(&(type_info_ptr->last_id_info), id_info_ptr);

        }

        if ( id_info_ptr ) {

            info_k = atomic_load(&(id_info_ptr->k));

            if ( info_k.marked ) {

                /* the ID is marked for deletion -- nothing to do here.  Set
                 * id_info_ptr to NULL and break out of the loop
                 */
                id_info_ptr = NULL;

                break;
            }

            /* In principle, as long as we don't modify it, we can read an id whose do not disturb 
             * flag is set.  This suggests that we only need to do a thread yield and continue if 
             * the is_future flag is set.  However, this happens infrequently, and it triggers false
             * negatives in the test bed.  Thus do the thread yield if info_k.do_not_disturb is TRUE,
             * regardless of the value of info_k.is_future.
             */
            if ( info_k.do_not_disturb ) {

                /* It is possible that this call into H5I is recursive.  If so, it is possible to
                 * deadlock on the do_not_discurb flag.  To avoid this, we check to see if the 
                 * the global lock was helf when the have_global_mutex flag was set, and if it is 
                 * is held by this thread -- if so, we can ignore the do_not_disturb flag, since 
                 * we are the same thread.  
                 * 
                 * Note that this is a temporary hack that works with the existing library to pass
                 * the regression tests.  A more general solution is needed for the production 
                 * version.
                 */
                if ( ( have_global_mutex ) && ( info_k.have_global_mutex ) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_bypasses), 1ULL);

                } else {

                    /* Another thread is in the process of performing an operation on the info kernel
                     * that can't be rolled back -- either a future id realize_cb or discard_cb, or a
                     * regular id callback that must be serialized.
                     *
                     * Thus we must wait until that thread is done and then re-start the operation -- which
                     * may be moot by that point.
                     */

                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_yields), 1ULL);

                    /* need to do better than this.  Want to call pthread_yield(),
                     * but that call doesn't seem to be supported anymore.
                     */
                    sleep(1);

                    continue;
                }
            }

            if ( info_k.is_future ) {

                /* we must try to resolve the future ID.  This requires 
                 * the following three operations:
                 *
                 * 1) Call the realize callback on id_info_ptr->k.object
                 *    This will return the actual ID and the actual object
                 *
                 * 2) Remove the actual ID from the index.
                 *
                 * 3) Discard id_info_ptr->k.object via a call to the
                 *    discard callback
                 *
                 * This done, we must set id_info_ptr->k.is_future to FALSE,
                 * and set id_info_ptr->k.object to point to the actual 
                 * object.
                 *
                 * All this must be done as a single operation, with no 
                 * other thread allowed into this critical region until 
                 * we are done.
                 *
                 * The obvious way of doing this is with a mutex -- however,
                 * the is_future flag only set when the async VOL is in use.
                 * Thus in the overwelming majority of cases, this would 
                 * impose significant overhead to no purpose.
                 *
                 * Instead, use the do_not_disturb flag in the kernel.
                 *
                 *
                 * NOTE THAT THE DO_NOT_DISTURB FLAG MAY ALREADY BE SET if 
                 * this is a recursive call and this is the thread
                 * which set the flag earlier in the call stack.  
                 *
                 * For now, assert that this is not the case.  This makes 
                 * sense, as the future ID capability will have to be 
                 * reworked, likely making any effort expended on this 
                 * point moot.
                 *
                 *
                 * If this flag is set, no other thread will begin an attempt
                 * modify id_info_ptr->k until it is reset, and once it is set, 
                 * any attempt to modify the kernel by a thread that is already
                 * in progress will fail -- prompting a retry and a wait on 
                 * the do_not_disturb flag.
                 *
                 * This has the advantage of adding only the cost of testing 
                 * a flag in the kernel and then proceeding in the typical 
                 * case -- acceptable overhead I hope.
                 *
                 * Note however, that in effect, I am using atomics to 
                 * construct my own lock, and thus I am creating the 
                 * possibility of a deadlock if either the realize_cb
                 * or the discard_cb attempts to access this ID and 
                 * modify its kernel.  Since I have no control over the
                 * the async VOL, this is possible, and will have to be
                 * dealt with if the situation arrises.
                 *
                 * Note also that the do_not_disturb flag is also used 
                 * to serialize calls to ID callbacks in the HDF5 library --
                 * creating the same potential for deadlocks.  However, these
                 * calls are in the library, and thus any such misbehaviour 
                 * can be addressed directly.
                 */

                assert( ! info_k.do_not_disturb ); /* temporary check until future IDs are reworked */

                if ( pass == 1 ) {

                    atomic_fetch_add(&(H5I_mt_g.H5I__find_id__future_id_conversions_attempted), 1ULL);
                }

                /* attempt to set the do_not_disturb flag.  If we fail, return 
                 * to the beginning of the do/while loop and retry.  Note that 
                 * circumstances may have changed -- in particular, some other 
                 * thread may have realized the ID.
                 */

                mod_info_k.count             = info_k.count;
                mod_info_k.app_count         = info_k.app_count;
                mod_info_k.object            = info_k.object;

                mod_info_k.marked            = info_k.marked;
                mod_info_k.do_not_disturb    = TRUE;
                mod_info_k.is_future         = info_k.is_future;

                /* set mod_info_k.have_global_mutex to TRUE if either this thread has the 
                 * global mutex or the class is not multi-thread safe.  Set mod_info_k.have_global_mutex
                 * to TRUE in the latter case since we must grab the global mutex before calling
                 * the realize callback and drop it when it returns.
                 */
                mod_info_k.have_global_mutex = ((have_global_mutex) || (! cls_is_mt_safe));

                if ( ! atomic_compare_exchange_strong(&(id_info_ptr->k), &info_k, mod_info_k ) ) {

                    /* Some other thread changed the value of id_info_ptr->k since we last read
                     * it.  Thus we must return to the beginning of the do loop and start
                     * again.  Note that it is possible that by that time, there will be
                     * nothing left to do.
                     */

                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.num_failed_do_not_disturb_sets), 1ULL);

                    continue;

                } else {

                    do_not_disturb_set = TRUE;

#if 0 /* JRM */
                    /* make info_k into a copy of the global kernel */
                    info_k.do_not_disturb = TRUE;
#else /* JTM */
                    /* On the face of it, it would seem that we could just update info_k
                     * to match mod_info_k, and use it in the next atomic_compare_exchange_strong()
                     * call.  However, for reason or reasons unknown, this doesn't work.
                     *
                     * Instead, we reload info_k after the atomic_compare_exchange_strong(),
                     * and verify that it contains the expected values.
                     */
                    info_k = atomic_load(&(id_info_ptr->k));

                    assert(info_k.count             == mod_info_k.count);
                    assert(info_k.app_count         == mod_info_k.app_count);
                    assert(info_k.object            == mod_info_k.object);

                    assert(info_k.marked            == mod_info_k.marked);
                    assert(info_k.do_not_disturb    == mod_info_k.do_not_disturb);
                    assert(info_k.is_future         == mod_info_k.is_future);
                    assert(info_k.have_global_mutex == mod_info_k.have_global_mutex);
#endif /* JRM */

                    /* setup mod_info_k to reset the do_not_disturb flag.  If we are successful
                     * at realizing the future ID, we will make further changes to mod_info_k
                     * before we use it to overwrite id_info_ptr->k.
                     */
                    mod_info_k.do_not_disturb    = FALSE;
                    mod_info_k.have_global_mutex = FALSE;

                    /* update stats */
                    atomic_fetch_add(&(H5I_mt_g.num_successful_do_not_disturb_sets), 1ULL);

#if H5I_MT_DEBUG_DO_NOT_DISTURB
                    fprintf(stdout, "H5I__find_id() set do not disturb on id = 0x%llx.\n",
                              (unsigned long long)(id_info_ptr->id));
#endif /* H5I_MT_DEBUG_DO_NOT_DISTURB */
                }
            }

            assert( ( ! info_k.is_future ) || ( do_not_disturb_set ) );

            /* save a copy of id_info_ptr for use when we reset the do_not_disturb flag. */
            dup_id_info_ptr = id_info_ptr;

            if ( info_k.is_future ) {

                hid_t actual_id;
                const void * actual_object = NULL;
                const void * future_object = NULL;

                atomic_fetch_add(&(H5I_mt_g.H5I__find_id__num_calls_to_realize_cb), 1ULL);
                    
                /* Invoke the realize callback, to get the actual object.  If this
                 * call fails, we must reset the do_not_disturb flag and return NULL
                 *
                 * If we don't have the global mutex, and the class is not multi-thread
                 * safe, grab the global mutex before the call and drop it immediately 
                 * afterwards.
                 */
                if ( ( ! have_global_mutex ) && ( ! cls_is_mt_safe ) ) {

                    atomic_fetch_add(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_realize_cb), 1ULL);
                    H5_API_LOCK
                    H5_GCC_CLANG_DIAG_OFF("cast-qual")
                    result = (id_info_ptr->realize_cb)((void *)info_k.object, &actual_id);
                    H5_GCC_CLANG_DIAG_ON("cast-qual")
                    H5_API_UNLOCK
                    atomic_fetch_add(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_realize_cb), 1ULL);

                } else {

                    H5_GCC_CLANG_DIAG_OFF("cast-qual")
                    result = (id_info_ptr->realize_cb)((void *)info_k.object, &actual_id);
                    H5_GCC_CLANG_DIAG_ON("cast-qual")
                }

                if ( result < 0 ) {

                    id_info_ptr = NULL;
                    done = TRUE;

                } 

                if ( ( ! done ) && ( ( H5I_INVALID_HID == actual_id ) || ( H5I_TYPE(id) != H5I_TYPE(actual_id) ) ) ) {

                    /* either we received an invalid ID from the realize_cb(), or that ID 
                     * is not of the same type as the id passed into this function.  In either 
                     * case, we must reset the do_not_disturb flag and return NULL.
                     */

                    id_info_ptr = NULL;
                    done = TRUE;

                } 

                if ( ! done ) {

                    /* Swap the actual object in for the future object */

                    future_object = info_k.object;

                    /* The call to H5I__remove_common() simply marks the actual 
                     * id as deleted, and if H5I_mt_g.marking_array[H5I_TYPE(id)] is zero, 
                     * deletes it from the lock free hash table.
                     *
                     * Thus there shouldn't be any potential for dead lock here.
                     */
                    actual_object = H5I__remove_common(type_info_ptr, actual_id);

                    atomic_fetch_add(&(H5I_mt_g.H5I__find_id__num_calls_to_H5I__remove_common), 1ULL);

                    if ( NULL == actual_object ) {

                        /* According to the documentation, this means that H5I__remove_common() has failed.
                         * However, if one examines the code, it is also possible that no object is 
                         * associated with the real ID.  
                         *
                         * The single thread code contains an assertion that actual_ovject is not NULL -- 
                         * from which I infer that a NULL actual_object should be treated as an error.
                         *
                         * For now at least, rather than re-work H5I__find_id() to report an error in 
                         * this case, we will simply cause the realization of the future ID to fail.
                         */
                        id_info_ptr = NULL;
                        done = TRUE;
                    } 
                }

                if ( ! done ) {

                    atomic_fetch_add(&(H5I_mt_g.H5I__find_id__num_calls_to_discard_cb), 1ULL);

                    /* Discard the future object.  If we don't hold the global mutex and 
                     * the class is not multi-thread safe, grab the global mutex before 
                     * the call to the discard_cb, and drop it immediately on return.
                     */
                    if ( ( ! have_global_mutex ) && ( ! cls_is_mt_safe ) ) {

                        atomic_fetch_add(&(H5I_mt_g.H5I__find_id__global_mutex_locks_for_discard_cb), 1ULL);
                        H5_API_LOCK
                        H5_GCC_CLANG_DIAG_OFF("cast-qual")
                        result = (id_info_ptr->discard_cb)((void *)future_object);
                        H5_GCC_CLANG_DIAG_ON("cast-qual")
                        H5_API_UNLOCK
                        atomic_fetch_add(&(H5I_mt_g.H5I__find_id__global_mutex_unlocks_for_discard_cb), 1ULL);

                    } else {

                        H5_GCC_CLANG_DIAG_OFF("cast-qual")
                        result = (id_info_ptr->discard_cb)((void *)future_object);
                        H5_GCC_CLANG_DIAG_ON("cast-qual")
                    }

                    if ( result < 0 ) {

                        /* The discard callback has failed.  We must reset the do_not_disturb flag
                         * and return NULL.
                         */
                        id_info_ptr = NULL;
                        done = TRUE;

                    } else {

                        /* we have successfully realized the future ID.  Set up mod_info_k
                         * to reflect this.
                         *
                         * Note that unlike the serial version of H5I, we do not set the 
                         * realize_cb and discard_cb fields to NULL.  They are not accessed 
                         * unless is_future is TRUE, and by not modifying them after the 
                         * the instance of H5I_mt_id_info_t is allocated, there is no need
                         * to make them atomic -- at least until compiliers start optimizing 
                         * across function boundaries.
                         */
                        mod_info_k.is_future = FALSE;
                        mod_info_k.object = actual_object;

                        /* update stats */
                        atomic_fetch_add(&(H5I_mt_g.H5I__find_id__future_id_conversions_completed), 1ULL);

                        done = TRUE;
                    }

                    future_object = NULL;
                }
            }

            if ( do_not_disturb_set ) {

                /* we must reset the do_not_disturb flag, and possibly make other changes to the 
                 * id info kernel as well.  Do this with a call to atomic_compare_exchange_strong().
                 * This call must succeed, so simply assert that it does.
                 *
                 * In the event of failure in realizing the future id, id_info_ptr will have 
                 * been set to NULL -- hence the use of dup_id_info_ptr below.
                 */

                assert( ! mod_info_k.do_not_disturb );

                bool_result = atomic_compare_exchange_strong(&(dup_id_info_ptr->k), &info_k, mod_info_k);
                assert(bool_result);

                atomic_fetch_add(&(H5I_mt_g.num_do_not_disturb_resets), 1ULL);

#if H5I_MT_DEBUG_DO_NOT_DISTURB
                fprintf(stdout, "H5I__find_id() reset do not disturb on id = 0x%llx.\n",
                          (unsigned long long)(dup_id_info_ptr->id));
#endif /* H5I_MT_DEBUG_DO_NOT_DISTURB */
            }

            done = TRUE;

        } else {

            /* target ID doesn't appear to exist */
            done = TRUE;
        }
    } while ( ! done );

    if ( id_info_ptr ) {

        atomic_fetch_add(&(H5I_mt_g.H5I__find_id__ids_found), 1ULL);
    }

    /* Set return value */
    ret_value = id_info_ptr;

done:

#if H5I_MT_DEBUG
    fprintf(stdout, "   H5I__find_id(0x%llx) returns 0x%llx. \n", 
              (unsigned long long)id, (unsigned long long) ret_value);
#endif /* H5I_MT_DEBUG */

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__find_id() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__find_id
 *
 * Purpose:     Given an object ID find the info struct that describes the
 *              object.
 *
 * Return:      Success:    A pointer to the object's info struct.
 *
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
H5I_id_info_t *
H5I__find_id(hid_t id)
{
    H5I_type_t       type;             /* ID's type */
    H5I_type_info_t *type_info = NULL; /* Pointer to the type */
    H5I_id_info_t   *id_info   = NULL; /* ID's info */
    H5I_id_info_t   *ret_value = NULL; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Check arguments */
    type = H5I_TYPE(id);
    if (type <= H5I_BADID || (int)type >= H5I_next_type_g)
        HGOTO_DONE(NULL);
    type_info = H5I_type_info_array_g[type];
    if (!type_info || type_info->init_count <= 0)
        HGOTO_DONE(NULL);


    /* Check for same ID as we have looked up last time */
    if (type_info->last_id_info && type_info->last_id_info->id == id)
        id_info = type_info->last_id_info;
    else {
        HASH_FIND(hh, type_info->hash_table, &id, sizeof(hid_t), id_info);

        /* Remember this ID */
        type_info->last_id_info = id_info;
    }

    /* Check if this is a future ID */
    H5_GCC_CLANG_DIAG_OFF("cast-qual")
    if (id_info && id_info->is_future) {
        hid_t actual_id = H5I_INVALID_HID; /* ID for actual object */
        void *future_object;               /* Pointer to the future object */
        void *actual_object;               /* Pointer to the actual object */

        /* Invoke the realize callback, to get the actual object */
        if ((id_info->realize_cb)((void *)id_info->object, &actual_id) < 0)
            HGOTO_DONE(NULL);

        /* Verify that we received a valid ID, of the same type */
        if (H5I_INVALID_HID == actual_id)
            HGOTO_DONE(NULL);
        if (H5I_TYPE(id) != H5I_TYPE(actual_id))
            HGOTO_DONE(NULL);

        /* Swap the actual object in for the future object */
        future_object = (void *)id_info->object;
        actual_object = H5I__remove_common(type_info, actual_id);
        assert(actual_object);
        id_info->object = actual_object;

        /* Discard the future object */
        if ((id_info->discard_cb)(future_object) < 0)
            HGOTO_DONE(NULL);
        future_object = NULL;

        /* Change the ID from 'future' to 'actual' */
        id_info->is_future  = FALSE;
        id_info->realize_cb = NULL;
        id_info->discard_cb = NULL;
    }
    H5_GCC_CLANG_DIAG_ON("cast-qual")

    /* Set return value */
    ret_value = id_info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__find_id() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD 

/*-------------------------------------------------------------------------
 * Function:    H5I__find_id_cb
 *
 * Purpose:     Callback for searching for an ID with a specific pointer
 *
 *              Updated for multi-thread.
 *
 * Return:      Success:    H5_ITER_CONT (0) or H5_ITER_STOP (1)
 *              Failure:    H5_ITER_ERROR (-1)
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__find_id_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    H5I_mt_id_info_t        *id_info_ptr      = (H5I_mt_id_info_t *)_item; /* Pointer to the ID info */
    H5I_mt_id_info_kernel_t  info_k;
    H5I_get_id_ud_t         *udata            = (H5I_get_id_ud_t *)_udata; /* Pointer to user data */
    H5I_type_t               type             = udata->obj_type;
    void                    *object           = NULL;
    herr_t                   result;
    int                      ret_value        = H5_ITER_CONT; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I__find_id_cb() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    /* Sanity check */
    assert(id_info_ptr);
    assert(udata);

    info_k = atomic_load(&(id_info_ptr->k));

    /* ignore entries that are marked for deletion */
    if ( ! info_k.marked ) {

        /* Get a pointer to the VOL connector's data */
#if 0 
        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        object = H5I__unwrap((void *)info_k.object, type); /* will hit global mutex */
        H5_GCC_CLANG_DIAG_ON("cast-qual")
#endif
        H5_GCC_CLANG_DIAG_OFF("cast-qual")
        result = H5I__unwrap((void *)info_k.object, type, &object);
        H5_GCC_CLANG_DIAG_ON("cast-qual")

        if ( result < 0 ) {

            ret_value = H5_ITER_ERROR;

        } else {

            /* Check for a match */
            if (object == udata->object) {

                udata->ret_id = id_info_ptr->id;
                ret_value     = H5_ITER_STOP;
            }
        }
    }

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I__find_id_cb() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I__find_id_cb
 *
 * Purpose:     Callback for searching for an ID with a specific pointer
 *
 * Return:      Success:    H5_ITER_CONT (0) or H5_ITER_STOP (1)
 *              Failure:    H5_ITER_ERROR (-1)
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__find_id_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    H5I_id_info_t   *info      = (H5I_id_info_t *)_item;    /* Pointer to the ID info */
    H5I_get_id_ud_t *udata     = (H5I_get_id_ud_t *)_udata; /* Pointer to user data */
    H5I_type_t       type      = udata->obj_type;
    const void      *object    = NULL;
    int              ret_value = H5_ITER_CONT; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(info);
    assert(udata);

    /* Get a pointer to the VOL connector's data */
    H5_GCC_CLANG_DIAG_OFF("cast-qual")
    object = H5I__unwrap((void *)info->object, type);
    H5_GCC_CLANG_DIAG_ON("cast-qual")

    /* Check for a match */
    if (object == udata->object) {
        udata->ret_id = info->id;
        ret_value     = H5_ITER_STOP;
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__find_id_cb() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/*-------------------------------------------------------------------------
 * Function:    H5I_find_id
 *
 * Purpose:     Return the ID of an object by searching through the ID list
 *              for the type.
 *
 *              Updated for multi-thread.
 *
 * Return:      SUCCEED/FAIL
 *              (id will be set to H5I_INVALID_HID on errors or not found)
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_find_id(const void *object, H5I_type_t type, hid_t *id)
{
    H5I_mt_type_info_t *type_info_ptr = NULL;    /* Pointer to the type */
    herr_t              ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

#if H5I_MT_DEBUG
    fprintf(stdout, "\n\n   H5I_find_id() called. \n\n\n");
#endif /* H5I_MT_DEBUG */

    assert(id);

    *id = H5I_INVALID_HID;

    type_info_ptr = atomic_load(&(H5I_mt_g.type_info_array[type]));

    if ( ( ! type_info_ptr ) || ( atomic_load(&(type_info_ptr->init_count)) <= 0 ) )

        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Only iterate through ID list if it is initialized and there are IDs in type */
    if ( ( atomic_load(&(type_info_ptr->init_count)) > 0 ) && ( atomic_load(&(type_info_ptr->id_count)) > 0 ) ) {

        H5I_get_id_ud_t         udata; /* User data */
        H5I_mt_id_info_t       *id_info_ptr = NULL;
        unsigned long long int  scan_id;
        void                   *value;

        /* Set up iterator user data */
        udata.object   = object;
        udata.obj_type = type;
        udata.ret_id   = H5I_INVALID_HID;

        /* Iterate over IDs for the ID type */
        if ( lfht_get_first(&(type_info_ptr->lfht), &scan_id, &value) ) {

            int ret;

            do {
                id_info_ptr = (H5I_mt_id_info_t *)value;

                ret = H5I__find_id_cb((void *)id_info_ptr, NULL, (void *)&udata);

                if (H5_ITER_ERROR == ret)

                    HGOTO_ERROR(H5E_ID, H5E_BADITER, FAIL, "iteration failed");

                if (H5_ITER_STOP == ret)

                    break;

            } while (lfht_get_next(&(type_info_ptr->lfht), scan_id, &scan_id, &value));
        }

        *id = udata.ret_id;
    }

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* end H5I_find_id() */

#else /* H5_HAVE_MULTITHREAD */

/*-------------------------------------------------------------------------
 * Function:    H5I_find_id
 *
 * Purpose:     Return the ID of an object by searching through the ID list
 *              for the type.
 *
 * Return:      SUCCEED/FAIL
 *              (id will be set to H5I_INVALID_HID on errors or not found)
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_find_id(const void *object, H5I_type_t type, hid_t *id)
{
    H5I_type_info_t *type_info = NULL;    /* Pointer to the type */
    herr_t           ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    assert(id);

    *id = H5I_INVALID_HID;

    type_info = H5I_type_info_array_g[type];
    if (!type_info || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Only iterate through ID list if it is initialized and there are IDs in type */
    if (type_info->init_count > 0 && type_info->id_count > 0) {
        H5I_get_id_ud_t udata; /* User data */
        H5I_id_info_t  *item = NULL;
        H5I_id_info_t  *tmp  = NULL;

        /* Set up iterator user data */
        udata.object   = object;
        udata.obj_type = type;
        udata.ret_id   = H5I_INVALID_HID;

        /* Iterate over IDs for the ID type */
        HASH_ITER(hh, type_info->hash_table, item, tmp)
        {
            int ret = H5I__find_id_cb((void *)item, NULL, (void *)&udata);
            if (H5_ITER_ERROR == ret)
                HGOTO_ERROR(H5E_ID, H5E_BADITER, FAIL, "iteration failed");
            if (H5_ITER_STOP == ret)
                break;
        }

        *id = udata.ret_id;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_find_id() */

#endif /* H5_HAVE_MULTITHREAD */

#ifdef H5_HAVE_MULTITHREAD

/************************************************************************
 *
 * H5I__clear_mt_id_info_free_list
 *
 *     Discard all entries on the id info free list in preparation for 
 *     shutdown.  
 *
 *     Note that this function assumes that no other threads are active 
 *     in H5I, and that it is therefore safe to ignore 
 *     H5I_mt_g.num_id_info_fl_entries_reallocable. 
 *
 *                                          JRM -- 10/24/23
 *
 ************************************************************************/

static herr_t
H5I__clear_mt_id_info_free_list(void)
{
    uint64_t              test_val;
    H5I_mt_id_info_sptr_t fl_head;
    H5I_mt_id_info_sptr_t null_snext = {NULL, 0ULL};
    H5I_mt_id_info_t    * fl_head_ptr;
    H5I_mt_id_info_t    * id_info_ptr;;
    herr_t                ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    atomic_fetch_add(&(H5I_mt_g.H5I__clear_mt_id_info_free_list__num_calls), 1ULL);

    fl_head = atomic_load(&(H5I_mt_g.id_info_fl_shead));
    fl_head_ptr = fl_head.ptr;

    if ( ( ! fl_head_ptr ) ||  ( 0ULL == atomic_load(&(H5I_mt_g.id_info_fl_len)) ) )

        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "H5I_mt_g.id_info_fl_shead.ptr == NULL -- H5I_mt_g not initialized?");


    while ( fl_head_ptr ) {

        id_info_ptr = fl_head_ptr;

        assert(H5I__ID_INFO == id_info_ptr->tag);
        assert(id_info_ptr->on_fl);

        fl_head = atomic_load(&(id_info_ptr->fl_snext));
        fl_head_ptr = fl_head.ptr;

        /* prepare *if_info_ptr for discard */
        id_info_ptr->tag = H5I__ID_INFO_INVALID;
        id_info_ptr->id  = (hid_t)0;
        atomic_store(&(id_info_ptr->fl_snext), null_snext);

        free(id_info_ptr);

        atomic_fetch_add(&(H5I_mt_g.num_id_info_structs_freed), 1ULL);
        test_val = atomic_fetch_sub(&(H5I_mt_g.id_info_fl_len), 1ULL);
        assert( test_val > 0ULL);
    }
    atomic_store(&(H5I_mt_g.id_info_fl_shead), null_snext);
    atomic_store(&(H5I_mt_g.id_info_fl_stail), null_snext);

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__clear_mt_id_info_free_list() */

#if 0 /* old version */

/************************************************************************
 *
 * H5I__discard_mt_id_info
 *
 *     Append the supplied instance of H5I_mt_id_info_t on the id info
 *     free list and increment H5I_mt_t.id_info_fl_len.
 *
 *     If the free list length exceeds 
 *     H5I_mt_t.max_desired_id_info_fl_len, attempt the remove the node 
 *     at the head of the id info free list from the free list, and 
 *     discard it and decrement lfht_ptr->fl_len if successful.
 *     ---- skip for now ---
 *
 *                                          JRM -- 9/1/23
 *
 ************************************************************************/

static herr_t 
H5I__discard_mt_id_info(H5I_mt_id_info_t * id_info_ptr)
{
    hbool_t done = FALSE;
    hbool_t on_fl = FALSE;
    hbool_t result;
    uint64_t fl_len;
    uint64_t max_fl_len;
    H5I_mt_id_info_sptr_t snext = {NULL, 0ULL};
    H5I_mt_id_info_sptr_t new_snext;
    H5I_mt_id_info_sptr_t fl_stail;
    H5I_mt_id_info_sptr_t fl_snext;
    H5I_mt_id_info_sptr_t new_fl_snext;
    H5I_mt_id_info_sptr_t new_fl_stail;
    H5I_mt_id_info_sptr_t test_fl_stail;
    H5I_mt_id_info_kernel_t info_k;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    assert(id_info_ptr);
    assert(H5I__ID_INFO == id_info_ptr->tag);

    info_k = atomic_load(&(id_info_ptr->k));

    assert(0 == info_k.count);
    assert(0 == info_k.app_count);
    assert(NULL == info_k.object);
    assert(TRUE == info_k.marked);
    assert(FALSE == info_k.do_not_disturb);
    assert(FALSE == info_k.is_future);
    assert(FALSE == info_k.have_global_mutex);

    assert(!atomic_load(&(id_info_ptr->on_fl)));
    assert(!atomic_load(&(id_info_ptr->re_allocable)));

    snext = atomic_load(&(id_info_ptr->fl_snext));
    new_snext.ptr = NULL;
    new_snext.sn = snext.sn + 1;

    atomic_store(&(id_info_ptr->fl_snext), new_snext);

    result = atomic_compare_exchange_strong(&(id_info_ptr->on_fl), &on_fl, TRUE);
    assert( result );

#if 1 /* JRM */
    result = atomic_compare_exchange_strong(&(id_info_ptr->re_allocable), &on_fl, TRUE);
    assert( result );
#endif /* JRM */

    while ( ! done ) {

        fl_stail = atomic_load(&(H5I_mt_g.id_info_fl_stail));

        assert(fl_stail.ptr);

        /* it is possible that *fl_tail.ptr has passed through the free list
         * and been re-allocated between the time we loaded it, and now.
         * If so, fl_stail_ptr->on_fl will no longer be TRUE.
         * This isn't a problem, but if so, the following if statement will fail.
         */
        // assert(atomic_load(&(fl_stail.ptr->on_fl)));

        fl_snext = atomic_load(&(fl_stail.ptr->fl_snext));

        test_fl_stail = atomic_load(&(H5I_mt_g.id_info_fl_stail));

        if ( ( test_fl_stail.ptr == fl_stail.ptr ) && ( test_fl_stail.sn == fl_stail.sn ) ) {

            if ( NULL == fl_snext.ptr ) {

                /* attempt to append id_info_ptr by setting fl_tail->fl_snext.ptr to id_info_ptr.
                 * If this succeeds, update stats and attempt to set H5I_mt_g.id_info_fl_stail.ptr
                 * to id_info_ptr as well.  This may or may not succeed, but in either
                 * case we are done.
                 */
                new_fl_snext.ptr = id_info_ptr;
                new_fl_snext.sn  = fl_snext.sn + 1;
                if ( atomic_compare_exchange_strong(&(fl_stail.ptr->fl_snext), &fl_snext, new_fl_snext) ) {

                    atomic_fetch_add(&(H5I_mt_g.id_info_fl_len), 1);
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_structs_added_to_fl), 1);

                    new_fl_stail.ptr = id_info_ptr;
                    new_fl_stail.sn  = fl_stail.sn + 1;
                    if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_stail), 
                                                          &fl_stail, new_fl_stail) ) {

                        atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_head_update_cols), 1);
                    }

                    /* if appropriate, attempt to update H5I_mt_g.max_id_info_fl_len.  In the
                     * event of a collision, just ignore it and go on, as I don't see any
                     * reasonable way to recover.
                     */
                    if ( (fl_len = atomic_load(&(H5I_mt_g.id_info_fl_len))) >
                         (max_fl_len = atomic_load(&(H5I_mt_g.max_id_info_fl_len))) ) {

                        atomic_compare_exchange_strong(&(H5I_mt_g.max_id_info_fl_len), &max_fl_len, fl_len);
                    }

                    done = true;

                } else {

                    /* append failed -- update stats and try again */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_append_cols), 1);

                }
            } else {

                // assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_next->tag)));

                /* attempt to set lfht_ptr->fl_stail to fl_next.  It doesn't
                 * matter whether we succeed or fail, as if we fail, it
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                new_fl_stail.ptr = fl_snext.ptr;
                new_fl_stail.sn  = fl_stail.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_stail), &fl_stail, new_fl_stail) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_tail_update_cols), 1);
                }
            }
        }
    }

    /* don't implement frees for now -- may deal with this in H5I_mt_enter/exit() */

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__discard_mt_id_info() */

#else /* new version */

/************************************************************************
 *
 * H5I__discard_mt_id_info
 *
 *     Append the supplied instance of H5I_mt_id_info_t on the id info
 *     free list and increment H5I_mt_t.id_info_fl_len.
 *
 *     If the free list length exceeds 
 *     H5I_mt_t.max_desired_id_info_fl_len, attempt the remove the node 
 *     at the head of the id info free list from the free list, and 
 *     discard it and decrement lfht_ptr->fl_len if successful.
 *     ---- skip for now ---
 *
 *                                          JRM -- 9/1/23
 *
 ************************************************************************/

static herr_t 
H5I__discard_mt_id_info(H5I_mt_id_info_t * id_info_ptr)
{
    hbool_t done = FALSE;
    hbool_t on_fl = FALSE;
    hbool_t try_to_free_an_entry = FALSE;
    hbool_t reallocable_entry_available = FALSE;
    hbool_t result;
    uint64_t fl_len;
    uint64_t max_fl_len;
    uint64_t test_val;
    H5I_mt_id_info_sptr_t snext = {NULL, 0ULL};
    H5I_mt_id_info_sptr_t new_snext;
    H5I_mt_id_info_sptr_t fl_shead;
    H5I_mt_id_info_sptr_t fl_stail;
    H5I_mt_id_info_sptr_t fl_snext;
    H5I_mt_id_info_sptr_t new_fl_snext;
    H5I_mt_id_info_sptr_t new_fl_shead;
    H5I_mt_id_info_sptr_t new_fl_stail;
    H5I_mt_id_info_sptr_t test_fl_shead;
    H5I_mt_id_info_sptr_t test_fl_stail;
    H5I_mt_id_info_kernel_t info_k;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    atomic_fetch_add(&(H5I_mt_g.H5I__discard_mt_id_info__num_calls), 1ULL);

    assert(id_info_ptr);
    assert(H5I__ID_INFO == id_info_ptr->tag);

    info_k = atomic_load(&(id_info_ptr->k));

    assert(0 == info_k.count);
    assert(0 == info_k.app_count);
    assert(NULL == info_k.object);
    assert(TRUE == info_k.marked);
    assert(FALSE == info_k.do_not_disturb);
    assert(FALSE == info_k.is_future);
    assert(FALSE == info_k.have_global_mutex);

    assert(!atomic_load(&(id_info_ptr->on_fl)));

    snext = atomic_load(&(id_info_ptr->fl_snext));
    new_snext.ptr = NULL;
    new_snext.sn = snext.sn + 1;

    atomic_store(&(id_info_ptr->fl_snext), new_snext);

    result = atomic_compare_exchange_strong(&(id_info_ptr->on_fl), &on_fl, TRUE);
    assert( result );


    while ( ! done ) {

        fl_stail = atomic_load(&(H5I_mt_g.id_info_fl_stail));

        assert(fl_stail.ptr);

        /* it is possible that *fl_tail.ptr has passed through the free list
         * and been re-allocated between the time we loaded it, and now.
         * If so, fl_stail_ptr->on_fl will no longer be TRUE.
         * This isn't a problem, but if so, the following if statement will fail.
         */
        // assert(atomic_load(&(fl_stail.ptr->on_fl)));

        fl_snext = atomic_load(&(fl_stail.ptr->fl_snext));

        test_fl_stail = atomic_load(&(H5I_mt_g.id_info_fl_stail));

        if ( ( test_fl_stail.ptr == fl_stail.ptr ) && ( test_fl_stail.sn == fl_stail.sn ) ) {

            if ( NULL == fl_snext.ptr ) {

                /* attempt to append id_info_ptr by setting fl_tail->fl_snext.ptr to id_info_ptr.
                 * If this succeeds, update stats and attempt to set H5I_mt_g.id_info_fl_stail.ptr
                 * to id_info_ptr as well.  This may or may not succeed, but in either
                 * case we are done.
                 */
                new_fl_snext.ptr = id_info_ptr;
                new_fl_snext.sn  = fl_snext.sn + 1;
                if ( atomic_compare_exchange_strong(&(fl_stail.ptr->fl_snext), &fl_snext, new_fl_snext) ) {

                    atomic_fetch_add(&(H5I_mt_g.id_info_fl_len), 1);
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_structs_added_to_fl), 1);

                    new_fl_stail.ptr = id_info_ptr;
                    new_fl_stail.sn  = fl_stail.sn + 1;
                    if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_stail), 
                                                          &fl_stail, new_fl_stail) ) {

                        atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_head_update_cols), 1);
                    }

                    /* if appropriate, attempt to update H5I_mt_g.max_id_info_fl_len.  In the
                     * event of a collision, just ignore it and go on, as I don't see any
                     * reasonable way to recover.
                     */
                    if ( (fl_len = atomic_load(&(H5I_mt_g.id_info_fl_len))) >
                         (max_fl_len = atomic_load(&(H5I_mt_g.max_id_info_fl_len))) ) {

                        atomic_compare_exchange_strong(&(H5I_mt_g.max_id_info_fl_len), &max_fl_len, fl_len);
                    }

                    done = true;

                } else {

                    /* append failed -- update stats and try again */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_append_cols), 1);

                }
            } else {

                // assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_next->tag)));

                /* attempt to set lfht_ptr->fl_stail to fl_next.  It doesn't
                 * matter whether we succeed or fail, as if we fail, it
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                new_fl_stail.ptr = fl_snext.ptr;
                new_fl_stail.sn  = fl_stail.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_stail), &fl_stail, new_fl_stail) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_tail_update_cols), 1);
                }
            }
        }
    }

    /* test to see if both H5I_mt_g.id_info_fl_len and H5I_mt_g.num_id_info_fl_entries_reallocable 
     * are greater than H5I_mt_g.max_desired_id_info_fl_len.  Check both, as while we want to 
     * keep the free list length around H5I_mt_g.max_desired_id_info_fl_len, we also want to 
     * have an ample supply of reallocable free list entries on hand.
     */
    assert(atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable)) <= 
           atomic_load(&(H5I_mt_g.id_info_fl_len)));

    if ( ( atomic_load(&(H5I_mt_g.id_info_fl_len)) > atomic_load(&(H5I_mt_g.max_desired_id_info_fl_len)) ) &&
         ( atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable)) > 
           atomic_load(&(H5I_mt_g.max_desired_id_info_fl_len)) ) ) {

        try_to_free_an_entry = TRUE;

    } else {

        atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_fl_too_small), 1ULL);
    }

    if ( try_to_free_an_entry ) {

        uint64_t num_fl_entries_reallocable;

        /* While a reallocable entry is almost certainly available, there
         * is the possibility that other threads have snapped up all 
         * the reallocable entries in the time since we determined that 
         * we should try to free an entry.  Hence the following:
         */
        while ( ( ! reallocable_entry_available ) &&
                ( 0 < (num_fl_entries_reallocable = atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable))) ) ) {

            assert( 0 < num_fl_entries_reallocable );

            if ( atomic_compare_exchange_strong(&(H5I_mt_g.num_id_info_fl_entries_reallocable),
                                                &num_fl_entries_reallocable, (num_fl_entries_reallocable - 1)) ) {

                reallocable_entry_available = TRUE;

            } else {

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions), 1ULL);
            }
        } /* end while */

        if ( ! reallocable_entry_available ) {

            /* No reallocable entries available -- just update stats and quit */

            atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_no_reallocable_entries), 1ULL);

        } else {

            done = FALSE;

            while ( ! done ) {

                fl_shead = atomic_load(&(H5I_mt_g.id_info_fl_shead));
                fl_stail = atomic_load(&(H5I_mt_g.id_info_fl_stail));

                assert(fl_shead.ptr);
                assert(fl_stail.ptr);

                fl_snext = atomic_load(&(fl_shead.ptr->fl_snext));

                test_fl_shead = atomic_load(&(H5I_mt_g.id_info_fl_shead));

                if ( ( test_fl_shead.ptr == fl_shead.ptr ) && ( test_fl_shead.sn == fl_shead.sn ) ) {

                    if ( fl_shead.ptr == fl_stail.ptr ) {

                        if ( NULL == fl_snext.ptr ) {

                            /* the free list is empty */
                            atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_frees_skipped_due_to_empty), 1);
                            done = TRUE;
                            break;
                        }

                        /* attempt to set H5I_mt_g.id_info_fl_stail to fl_snext.  It doesn't
                         * matter whether we succeed or fail, as if we fail, it
                         * just means that some other thread beat us to it.
                         *
                         * that said, it doesn't hurt to collect stats
                         */
                        new_fl_stail.ptr = fl_snext.ptr;
                        new_fl_stail.sn  = fl_stail.sn + 1;
                        if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_stail), &fl_stail, 
                                                              new_fl_stail) ) {

                            atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_tail_update_cols), 1ULL);
                        }
                    } else {

                        /* set up new_fl_shead */
                        assert(fl_snext.ptr);
                        new_fl_shead.ptr = fl_snext.ptr;
                        new_fl_shead.sn  = fl_shead.sn + 1;

                        if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_shead), 
                                                              &fl_shead, new_fl_shead) ) {

                            /* the attempt to remove the first item from the free list
                             * failed.  Update stats and try again.
                             */
                            atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_head_update_cols), 1ULL);

                        } else {

                            H5I_mt_id_info_sptr_t null_snext = {NULL, 0ULL};

                            /* first has been removed from the free list.  Set id_info_ptr to fl_shead.ptr,
                             * discard *id_info_ptr, update stats, and exit the loop by setting done to true.
                             */
                            id_info_ptr = fl_shead.ptr;

                            assert(H5I__ID_INFO == id_info_ptr->tag);
                            assert(id_info_ptr->on_fl);

                            info_k = atomic_load(&(id_info_ptr->k));

                            assert(0 == info_k.count);
                            assert(0 == info_k.app_count);
                            assert(NULL == info_k.object);

                            /* prepare *if_info_ptr for discard */
                            id_info_ptr->tag = H5I__ID_INFO_INVALID;
                            id_info_ptr->id  = (hid_t)0;
                            atomic_store(&(id_info_ptr->fl_snext), null_snext);
                            id_info_ptr->realize_cb = NULL;
                            id_info_ptr->discard_cb = NULL;

                            free(id_info_ptr);

                            /* update stats */
                            atomic_fetch_add(&(H5I_mt_g.num_id_info_structs_freed), 1ULL);
                            test_val = atomic_fetch_sub(&(H5I_mt_g.id_info_fl_len), 1ULL);
                            assert( test_val > 0ULL);

                            done = true;
                        }
                    }
                }
            } /* while ( ! done ) */
        }
    } /* if ( try_to_free_entry ) */

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__discard_mt_id_info() */

#endif /* new version */

#if 0 /* old version */

/************************************************************************
 *
 * H5I__new_mt_id_info
 *
 *     Test to see if an instance of H5I_mt_id_info_t is available on the
 *     id info free list.  If there is, remove it from the free list, 
 *     re-initialize it, and return a pointer to it.
 *
 *     Otherwise, allocate and initialize an instance of struct
 *     lfht_fl_node_t and return a pointer to the included instance of
 *     lfht_node_t to the caller.
 *
 *     Return a pointer to the new instance on success, and NULL on
 *     failure.
 *
 *                                          JRM -- 8/30/23
 *
 ************************************************************************/

static H5I_mt_id_info_t * 
H5I__new_mt_id_info(hid_t id, unsigned count, unsigned app_count, const void * object, hbool_t is_future, 
                    H5I_future_realize_func_t realize_cb, H5I_future_discard_func_t discard_cb)
{
    hbool_t fl_search_done = FALSE;;
    hbool_t result;
    H5I_mt_id_info_t * id_info_ptr = NULL;
    H5I_mt_id_info_sptr_t sfirst;
    H5I_mt_id_info_sptr_t new_sfirst;
    H5I_mt_id_info_sptr_t test_sfirst;
    H5I_mt_id_info_sptr_t slast;
    H5I_mt_id_info_sptr_t new_slast;
    H5I_mt_id_info_sptr_t snext;
    H5I_mt_id_info_sptr_t new_snext;
    H5I_mt_id_info_kernel_t new_k = {count, app_count, object, FALSE, FALSE, is_future, FALSE};
    H5I_mt_id_info_kernel_t old_k;
    H5I_mt_id_info_t * ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    sfirst = atomic_load(&(H5I_mt_g.id_info_fl_shead));

    if ( NULL == sfirst.ptr ) {

        /* free list is not yet initialized */
        fl_search_done = TRUE;
    }

    while ( ! fl_search_done ) {

        sfirst = atomic_load(&(H5I_mt_g.id_info_fl_shead));
        slast = atomic_load(&(H5I_mt_g.id_info_fl_stail));

        assert(sfirst.ptr);
        assert(slast.ptr);

        snext = atomic_load(&(sfirst.ptr->fl_snext));

        test_sfirst = atomic_load(&(H5I_mt_g.id_info_fl_shead));

        if ( ( test_sfirst.ptr == sfirst.ptr ) && ( test_sfirst.sn == sfirst.sn ) ) {

            if ( sfirst.ptr == slast.ptr ) {

                if ( NULL == snext.ptr ) {

                    /* the free list is empty */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty), 1);
                    fl_search_done = TRUE;
                    break;
                }

                /* attempt to set H5I_mt_g.id_info_fl_stail to snext.  It doesn't
                 * matter whether we succeed or fail, as if we fail, it
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                new_slast.ptr = snext.ptr;
                new_slast.sn  = slast.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_stail), &slast, new_slast) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_tail_update_cols), 1ULL);
                }
            } else {

                /* set up new_sfirst now in case we need it later.  */
                assert(snext.ptr);
                new_sfirst.ptr = snext.ptr;
                new_sfirst.sn  = sfirst.sn + 1;

                if ( ! atomic_load(&(sfirst.ptr->re_allocable)) ) {

                    /* The entry at the head of the free list is not re allocable,
                     * which means that there may be a pointer to it somewhere.  
                     * Rather than take the risk, let it sit on the free list until 
                     * is is marked as re allocable.
                     */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_head_not_reallocable), 1ULL);
                    fl_search_done = true;

                } else if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_shead), &sfirst, new_sfirst) ) {

                    /* the attempt to remove the first item from the free list
                     * failed.  Update stats and try again.
                     */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_head_update_cols), 1ULL);

                } else {

                    /* first has been removed from the free list.  Set fl_node_ptr to first,
                     * update stats, and exit the loop by setting fl_search_done to true.
                     */
                    id_info_ptr = sfirst.ptr;

                    assert(H5I__ID_INFO == id_info_ptr->tag);

                    id_info_ptr->id = id;

                    assert(atomic_load(&(id_info_ptr->on_fl)));
                    atomic_store(&(id_info_ptr->on_fl), FALSE);

                    assert(atomic_load(&(id_info_ptr->re_allocable)));
                    atomic_store(&(id_info_ptr->re_allocable), FALSE);

                    new_snext.ptr = NULL;
                    new_snext.sn  = snext.sn + 1;

                    result = atomic_compare_exchange_strong(&(id_info_ptr->fl_snext), &snext, new_snext);
                    assert(result);

                    old_k = atomic_load(&(id_info_ptr->k));

                    assert(0 == old_k.count);
                    assert(0 == old_k.app_count);
                    assert(NULL == old_k.object);

                    atomic_store(&(id_info_ptr->k), new_k);

                    id_info_ptr->realize_cb = realize_cb;
                    id_info_ptr->discard_cb = discard_cb;

                    atomic_fetch_sub(&(H5I_mt_g.id_info_fl_len), 1ULL);
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_structs_alloced_from_fl), 1ULL);

                    fl_search_done = true;

                    assert(id_info_ptr);
                }
            }
        }
    } /* while ( ! fl_search_done ) */

    if ( NULL == id_info_ptr ) {

        id_info_ptr = (H5I_mt_id_info_t *)malloc(sizeof(H5I_mt_id_info_t));

        if ( NULL == id_info_ptr )
            HGOTO_ERROR(H5E_ID, H5E_CANTALLOC, NULL, "ID info allocation failed");

        atomic_fetch_add(&(H5I_mt_g.num_id_info_structs_alloced_from_heap), 1ULL);

        id_info_ptr->tag = H5I__ID_INFO;
        id_info_ptr->id = id;
        atomic_init(&(id_info_ptr->k), new_k);
        id_info_ptr->realize_cb = realize_cb;
        id_info_ptr->discard_cb = discard_cb;
        atomic_init(&(id_info_ptr->on_fl), FALSE);
        atomic_init(&(id_info_ptr->re_allocable), FALSE);
        snext.ptr = NULL;
        snext.sn = 0ULL;
        atomic_init(&(id_info_ptr->fl_snext), snext);
    }

    assert(id_info_ptr);

    /* Set return value */
    ret_value = id_info_ptr;

    if ( ( atomic_load(

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__new_mt_id_info() */

#else /* new version */

/************************************************************************
 *
 * H5I__new_mt_id_info
 *
 *     Test to see if an instance of H5I_mt_id_info_t is available on the
 *     id info free list.  If there is, remove it from the free list, 
 *     re-initialize it, and return a pointer to it.
 *
 *     Otherwise, allocate and initialize an instance of struct
 *     lfht_fl_node_t and return a pointer to the included instance of
 *     lfht_node_t to the caller.
 *
 *     Return a pointer to the new instance on success, and NULL on
 *     failure.
 *
 *                                          JRM -- 8/30/23
 *
 ************************************************************************/

static H5I_mt_id_info_t * 
H5I__new_mt_id_info(hid_t id, unsigned count, unsigned app_count, const void * object, hbool_t is_future, 
                    H5I_future_realize_func_t realize_cb, H5I_future_discard_func_t discard_cb)
{
    hbool_t fl_search_done = FALSE;;
    hbool_t result;
    hbool_t reallocable_entry_available = FALSE;
    uint64_t num_fl_entries_reallocable;
    H5I_mt_id_info_t * id_info_ptr = NULL;
    H5I_mt_id_info_sptr_t sfirst;
    H5I_mt_id_info_sptr_t new_sfirst;
    H5I_mt_id_info_sptr_t test_sfirst;
    H5I_mt_id_info_sptr_t slast;
    H5I_mt_id_info_sptr_t new_slast;
    H5I_mt_id_info_sptr_t snext;
    H5I_mt_id_info_sptr_t new_snext;
    H5I_mt_id_info_kernel_t new_k;
    H5I_mt_id_info_kernel_t old_k;
    H5I_mt_id_info_t * ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    memset(&new_k, 0, sizeof(H5I_mt_id_info_kernel_t));
    new_k.count = count;
    new_k.app_count = app_count;
    new_k.object = object;
    new_k.marked = FALSE;
    new_k.do_not_disturb = FALSE;
    new_k.is_future = is_future;
    new_k.have_global_mutex = FALSE;



    atomic_fetch_add(&(H5I_mt_g.H5I__new_mt_id_info__num_calls), 1ULL);

    sfirst = atomic_load(&(H5I_mt_g.id_info_fl_shead));

    /* test to see if the free list has been initialized */

    if ( NULL == sfirst.ptr ) {

        /* free list is not yet initialized */
        fl_search_done = TRUE;
    }

    /* Test to see if there is a re-allocable entry on the free list.  Conceptually, we test to see
     * if H5I_mt_g.num_id_info_fl_entries_reallocable  is positive, we decrement it and set entry_reallocable
     * to TRUE.  Practically, it is a bit more complicated, as it is possible that 
     * H5I_mt_g.num_id_info_fl_entries_reallocable between the time that we read it to see if it is 
     * positive, and we decrement it.  To deal with this, we use an atomic compare exchange to set 
     * the new value, and retry if there is a collision.
     */
    if ( ! fl_search_done ) {

        while ( ( ! reallocable_entry_available ) && 
                ( 0 < (num_fl_entries_reallocable = atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable))) ) ) {

            assert( 0 < num_fl_entries_reallocable );

            if ( atomic_compare_exchange_strong(&(H5I_mt_g.num_id_info_fl_entries_reallocable),
                                                &num_fl_entries_reallocable, (num_fl_entries_reallocable - 1)) ) {

                reallocable_entry_available = TRUE;
            
            } else { 

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions), 1ULL);
            }
        } /* end while */

        if ( ! reallocable_entry_available ) {

            /* No reallocable entries available, so set fl_search_done = TRUE */

            fl_search_done = TRUE;

            /* update stats */
            atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries), 1ULL);

        }
    } /* end if ( ! fl_search_done ) */


    while ( ! fl_search_done ) {

        sfirst = atomic_load(&(H5I_mt_g.id_info_fl_shead));
        slast = atomic_load(&(H5I_mt_g.id_info_fl_stail));

        assert(sfirst.ptr);
        assert(slast.ptr);

        snext = atomic_load(&(sfirst.ptr->fl_snext));

        test_sfirst = atomic_load(&(H5I_mt_g.id_info_fl_shead));

        if ( ( test_sfirst.ptr == sfirst.ptr ) && ( test_sfirst.sn == sfirst.sn ) ) {

            if ( sfirst.ptr == slast.ptr ) {

                if ( NULL == snext.ptr ) {

                    /* the free list is empty */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_empty), 1);
                    fl_search_done = TRUE;
                    break;
                }

                /* attempt to set H5I_mt_g.id_info_fl_stail to snext.  It doesn't
                 * matter whether we succeed or fail, as if we fail, it
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                new_slast.ptr = snext.ptr;
                new_slast.sn  = slast.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_stail), &slast, new_slast) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_tail_update_cols), 1ULL);
                }
            } else {

                /* set up new_sfirst now in case we need it later.  */
                assert(snext.ptr);
                new_sfirst.ptr = snext.ptr;
                new_sfirst.sn  = sfirst.sn + 1;

                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.id_info_fl_shead), &sfirst, new_sfirst) ) {

                    /* the attempt to remove the first item from the free list
                     * failed.  Update stats and try again.
                     */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_head_update_cols), 1ULL);

                } else {

                    /* first has been removed from the free list.  Set fl_node_ptr to first,
                     * update stats, and exit the loop by setting fl_search_done to true.
                     */
                    id_info_ptr = sfirst.ptr;

                    assert(H5I__ID_INFO == id_info_ptr->tag);

                    id_info_ptr->id = id;

                    assert(atomic_load(&(id_info_ptr->on_fl)));
                    atomic_store(&(id_info_ptr->on_fl), FALSE);

                    new_snext.ptr = NULL;
                    new_snext.sn  = snext.sn + 1;

                    result = atomic_compare_exchange_strong(&(id_info_ptr->fl_snext), &snext, new_snext);
                    assert(result);

                    old_k = atomic_load(&(id_info_ptr->k));

                    assert(0 == old_k.count);
                    assert(0 == old_k.app_count);
                    assert(NULL == old_k.object);

                    atomic_store(&(id_info_ptr->k), new_k);

                    id_info_ptr->realize_cb = realize_cb;
                    id_info_ptr->discard_cb = discard_cb;

                    atomic_fetch_sub(&(H5I_mt_g.id_info_fl_len), 1ULL);
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_structs_alloced_from_fl), 1ULL);

                    fl_search_done = true;
                }
            }
        }
    } /* while ( ! fl_search_done ) */

    if ( NULL == id_info_ptr ) {

        id_info_ptr = (H5I_mt_id_info_t *)malloc(sizeof(H5I_mt_id_info_t));

        if ( NULL == id_info_ptr )
            HGOTO_ERROR(H5E_ID, H5E_CANTALLOC, NULL, "ID info allocation failed");

        atomic_fetch_add(&(H5I_mt_g.num_id_info_structs_alloced_from_heap), 1ULL);

        id_info_ptr->tag = H5I__ID_INFO;
        id_info_ptr->id = id;
        atomic_init(&(id_info_ptr->k), new_k);
        id_info_ptr->realize_cb = realize_cb;
        id_info_ptr->discard_cb = discard_cb;
        atomic_init(&(id_info_ptr->on_fl), FALSE);
        snext.ptr = NULL;
        snext.sn = 0ULL;
        atomic_init(&(id_info_ptr->fl_snext), snext);
    }

    assert(id_info_ptr);

    /* Set return value */
    ret_value = id_info_ptr;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__new_mt_id_info() */

#endif /* new version */

/************************************************************************
 *
 * H5I__clear_mt_type_info_free_list
 *
 *     Discard all entries on the type info free list in preparation for 
 *     shutdown.  
 *
 *     Note that this function assumes that no other threads are active 
 *     in H5I, and that it is therefore safe to ignore the 
 *     H5I_mt_g.num_type_info_fl_entries_reallocable.val.
 *
 *                                          JRM -- 10/24/23
 *
 ************************************************************************/

static herr_t
H5I__clear_mt_type_info_free_list(void)
{
    uint64_t                test_val;
    H5I_mt_type_info_sptr_t fl_head;
    H5I_mt_type_info_sptr_t null_snext = {NULL, 0ULL};
    H5I_mt_type_info_t    * fl_head_ptr;
    H5I_mt_type_info_t    * type_info_ptr;;
    herr_t                  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    fl_head = atomic_load(&(H5I_mt_g.type_info_fl_shead));
    fl_head_ptr = fl_head.ptr;

    if ( ! fl_head_ptr )

        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "H5I_mt_g.type_info_fl_shead.ptr == NULL -- H5I_mt_g not initialized?");


    while ( fl_head_ptr ) {

        type_info_ptr = fl_head_ptr;

        assert(H5I__TYPE_INFO == type_info_ptr->tag);
        assert(0 == atomic_load(&(type_info_ptr->init_count)));
        assert(0 == atomic_load(&(type_info_ptr->id_count)));
        assert(atomic_load(&(type_info_ptr->lfht_cleared)));
        assert(atomic_load(&(type_info_ptr->on_fl)));

        fl_head = atomic_load(&(type_info_ptr->fl_snext));
        fl_head_ptr = fl_head.ptr;

        /* prepare *if_info_ptr for discard */
        type_info_ptr->tag = H5I__TYPE_INFO_INVALID;
        type_info_ptr->cls = NULL;
        atomic_store(&(type_info_ptr->fl_snext), null_snext);

        free(type_info_ptr);

        atomic_fetch_add(&(H5I_mt_g.num_type_info_structs_freed), 1ULL);
        test_val = atomic_fetch_sub(&(H5I_mt_g.type_info_fl_len), 1ULL);
        assert(test_val > 0ULL);
    }
    atomic_store(&(H5I_mt_g.type_info_fl_shead), null_snext);
    atomic_store(&(H5I_mt_g.type_info_fl_stail), null_snext);

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__clear_mt_type_info_free_list() */

#if 0 /* old version */

/************************************************************************
 *
 * H5I__discard_mt_type_info
 *
 *     Append the supplied instance of H5I_mt_type_info_t on the type info
 *     free list and increment H5I_mt_t.type_info_fl_len.
 *
 *     If the free list length exceeds 
 *     H5I_mt_t.max_desired_type_info_fl_len, attempt the remove the node 
 *     at the head of the type info free list from the free list, and 
 *     discard it and decrement lfht_ptr->fl_len if successful.
 *     ---- skip for now ---
 *
 *                                          JRM -- 9/1/23
 *
 ************************************************************************/

static herr_t 
H5I__discard_mt_type_info(H5I_mt_type_info_t * type_info_ptr)
{
    hbool_t done = FALSE;
    hbool_t on_fl = FALSE;
    hbool_t result;
    uint64_t fl_len;
    uint64_t max_fl_len;
    H5I_mt_type_info_sptr_t snext = {NULL, 0ULL};
    H5I_mt_type_info_sptr_t new_snext;
    H5I_mt_type_info_sptr_t fl_stail;
    H5I_mt_type_info_sptr_t fl_snext;
    H5I_mt_type_info_sptr_t new_fl_snext;
    H5I_mt_type_info_sptr_t new_fl_stail;
    H5I_mt_type_info_sptr_t test_fl_stail;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    assert(type_info_ptr);
    assert(H5I__TYPE_INFO == type_info_ptr->tag);

    assert(0 == atomic_load(&(type_info_ptr->init_count)));
    assert(0 == atomic_load(&(type_info_ptr->id_count)));

    assert(atomic_load(&(type_info_ptr->lfht_cleared)));

    assert(!atomic_load(&(type_info_ptr->on_fl)));

    snext = atomic_load(&(type_info_ptr->fl_snext));

    new_snext.ptr = NULL;
    new_snext.sn = snext.sn + 1;

    atomic_store(&(type_info_ptr->fl_snext), new_snext);

    result = atomic_compare_exchange_strong(&(type_info_ptr->on_fl), &on_fl, TRUE);
    assert(result);


    while ( ! done ) {

        fl_stail = atomic_load(&(H5I_mt_g.type_info_fl_stail));

        assert(fl_stail.ptr);

        /* it is possible that *fl_tail.ptr has passed through the free list
         * and been re-allocated between the time we loaded it, and now.
         * If so, fl_stail_ptr->on_fl will no longer be TRUE.
         * This isn't a problem, but if so, the following if statement will fail.
         */
        // assert(atomic_load(&(fl_stail.ptr->on_fl)));

        fl_snext = atomic_load(&(fl_stail.ptr->fl_snext));

        test_fl_stail = atomic_load(&(H5I_mt_g.type_info_fl_stail));

        if ( ( test_fl_stail.ptr == fl_stail.ptr ) && ( test_fl_stail.sn == fl_stail.sn ) ) {

            if ( NULL == fl_snext.ptr ) {

                /* attempt to append type_info_ptr by setting fl_tail->fl_snext.ptr to type_info_ptr.
                 * If this succeeds, update stats and attempt to set H5I_mt_g.type_info_fl_stail.ptr
                 * to type_info_ptr as well.  This may or may not succeed, but in either
                 * case we are done.
                 */
                new_fl_snext.ptr = type_info_ptr;
                new_fl_snext.sn  = fl_snext.sn + 1;
                if ( atomic_compare_exchange_strong(&(fl_stail.ptr->fl_snext), &fl_snext, new_fl_snext) ) {

                    atomic_fetch_add(&(H5I_mt_g.type_info_fl_len), 1);
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_structs_added_to_fl), 1);

                    new_fl_stail.ptr = type_info_ptr;
                    new_fl_stail.sn  = fl_stail.sn + 1;
                    if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_stail), 
                                                          &fl_stail, new_fl_stail) ) {

                        atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_head_update_cols), 1);
                    }

                    /* if appropriate, attempt to update H5I_mt_g.max_type_info_fl_len.  In the
                     * event of a collision, just ignore it and go on, as I don't see any
                     * reasonable way to recover.
                     */
                    if ( (fl_len = atomic_load(&(H5I_mt_g.type_info_fl_len))) >
                         (max_fl_len = atomic_load(&(H5I_mt_g.max_type_info_fl_len))) ) {

                        atomic_compare_exchange_strong(&(H5I_mt_g.max_type_info_fl_len), &max_fl_len, fl_len);
                    }

                    done = true;

                } else {

                    /* append failed -- update stats and try again */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_append_cols), 1);

                }
            } else {

                // assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_next->tag)));

                /* attempt to set lfht_ptr->fl_stail to fl_next.  It doesn't
                 * matter whether we succeed or fail, as if we fail, it
                 * just means that some other thread beat us to it.
                 *
                 * that satype, it doesn't hurt to collect stats
                 */
                new_fl_stail.ptr = fl_snext.ptr;
                new_fl_stail.sn  = fl_stail.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_stail), &fl_stail, new_fl_stail) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_tail_update_cols), 1);
                }
            }
        }
    }

    /* don't implement frees for now -- may deal with this in H5I_mt_enter/exit() */

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__discard_mt_type_info() */

#else /* new version */

/************************************************************************
 *
 * H5I__discard_mt_type_info
 *
 *     Append the supplied instance of H5I_mt_type_info_t on the type info
 *     free list and increment H5I_mt_t.type_info_fl_len.
 *
 *     If the free list length exceeds 
 *     H5I_mt_t.max_desired_type_info_fl_len, attempt the remove the node 
 *     at the head of the type info free list from the free list, and 
 *     discard it and decrement lfht_ptr->fl_len if successful.
 *
 *                                          JRM -- 9/1/23
 *
 ************************************************************************/

static herr_t 
H5I__discard_mt_type_info(H5I_mt_type_info_t * type_info_ptr)
{
    hbool_t done = FALSE;
    hbool_t on_fl = FALSE;
    hbool_t result;
    hbool_t try_to_free_an_entry = FALSE;
    hbool_t reallocable_entry_available = FALSE;
    uint64_t fl_len;
    uint64_t max_fl_len;
    uint64_t test_val;
    H5I_mt_type_info_sptr_t snext = {NULL, 0ULL};
    H5I_mt_type_info_sptr_t new_snext;
    H5I_mt_type_info_sptr_t fl_shead;
    H5I_mt_type_info_sptr_t fl_stail;
    H5I_mt_type_info_sptr_t fl_snext;
    H5I_mt_type_info_sptr_t new_fl_snext;
    H5I_mt_type_info_sptr_t new_fl_shead;
    H5I_mt_type_info_sptr_t new_fl_stail;
    H5I_mt_type_info_sptr_t test_fl_shead;
    H5I_mt_type_info_sptr_t test_fl_stail;
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    atomic_fetch_add(&(H5I_mt_g.H5I__discard_mt_type_info__num_calls), 1ULL);

    assert(type_info_ptr);
    assert(H5I__TYPE_INFO == type_info_ptr->tag);

    assert(0 == atomic_load(&(type_info_ptr->init_count)));
    assert(0 == atomic_load(&(type_info_ptr->id_count)));

    assert(atomic_load(&(type_info_ptr->lfht_cleared)));

    assert(!atomic_load(&(type_info_ptr->on_fl)));

    snext = atomic_load(&(type_info_ptr->fl_snext));

    new_snext.ptr = NULL;
    new_snext.sn = snext.sn + 1;

    atomic_store(&(type_info_ptr->fl_snext), new_snext);

    result = atomic_compare_exchange_strong(&(type_info_ptr->on_fl), &on_fl, TRUE);
    assert(result);


    while ( ! done ) {

        fl_stail = atomic_load(&(H5I_mt_g.type_info_fl_stail));

        assert(fl_stail.ptr);

        /* it is possible that *fl_tail.ptr has passed through the free list
         * and been re-allocated between the time we loaded it, and now.
         * If so, fl_stail_ptr->on_fl will no longer be TRUE.
         * This isn't a problem, but if so, the following if statement will fail.
         */
        // assert(atomic_load(&(fl_stail.ptr->on_fl)));

        fl_snext = atomic_load(&(fl_stail.ptr->fl_snext));

        test_fl_stail = atomic_load(&(H5I_mt_g.type_info_fl_stail));

        if ( ( test_fl_stail.ptr == fl_stail.ptr ) && ( test_fl_stail.sn == fl_stail.sn ) ) {

            if ( NULL == fl_snext.ptr ) {

                /* attempt to append type_info_ptr by setting fl_tail->fl_snext.ptr to type_info_ptr.
                 * If this succeeds, update stats and attempt to set H5I_mt_g.type_info_fl_stail.ptr
                 * to type_info_ptr as well.  This may or may not succeed, but in either
                 * case we are done.
                 */
                new_fl_snext.ptr = type_info_ptr;
                new_fl_snext.sn  = fl_snext.sn + 1;
                if ( atomic_compare_exchange_strong(&(fl_stail.ptr->fl_snext), &fl_snext, new_fl_snext) ) {

                    atomic_fetch_add(&(H5I_mt_g.type_info_fl_len), 1);
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_structs_added_to_fl), 1);

                    new_fl_stail.ptr = type_info_ptr;
                    new_fl_stail.sn  = fl_stail.sn + 1;
                    if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_stail), 
                                                          &fl_stail, new_fl_stail) ) {

                        atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_head_update_cols), 1);
                    }

                    /* if appropriate, attempt to update H5I_mt_g.max_type_info_fl_len.  In the
                     * event of a collision, just ignore it and go on, as I don't see any
                     * reasonable way to recover.
                     */
                    if ( (fl_len = atomic_load(&(H5I_mt_g.type_info_fl_len))) >
                         (max_fl_len = atomic_load(&(H5I_mt_g.max_type_info_fl_len))) ) {

                        atomic_compare_exchange_strong(&(H5I_mt_g.max_type_info_fl_len), &max_fl_len, fl_len);
                    }

                    done = true;

                } else {

                    /* append failed -- update stats and try again */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_append_cols), 1);

                }
            } else {

                // assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_next->tag)));

                /* attempt to set lfht_ptr->fl_stail to fl_next.  It doesn't
                 * matter whether we succeed or fail, as if we fail, it
                 * just means that some other thread beat us to it.
                 *
                 * that satype, it doesn't hurt to collect stats
                 */
                new_fl_stail.ptr = fl_snext.ptr;
                new_fl_stail.sn  = fl_stail.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_stail), &fl_stail, new_fl_stail) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_tail_update_cols), 1);
                }
            }
        }
    }


    /* test to see if both H5I_mt_g.type_info_fl_len and H5I_mt_g.num_type_info_fl_entries_reallocable 
     * are greater than H5I_mt_g.max_desired_type_info_fl_len.  Check both, as while we want to 
     * keep the free list length around H5I_mt_g.max_desired_type_info_fl_len, we also want to 
     * have an ample supply of reallocable free list entries on hand.
     */
#if 1
    assert(atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable)) <= 
           atomic_load(&(H5I_mt_g.type_info_fl_len)));
#else
    assert(atomic_load(&(H5I_mt_g.snum_type_info_fl_entries_reallocable)).val <= 
           atomic_load(&(H5I_mt_g.type_info_fl_len)));
#endif

#if 1
    if ( ( atomic_load(&(H5I_mt_g.type_info_fl_len)) > atomic_load(&(H5I_mt_g.max_desired_type_info_fl_len)) ) &&
         ( atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable)) > 
           atomic_load(&(H5I_mt_g.max_desired_type_info_fl_len)) ) ) {
#else
    if ( ( atomic_load(&(H5I_mt_g.type_info_fl_len)) > atomic_load(&(H5I_mt_g.max_desired_type_info_fl_len)) ) &&
         ( atomic_load(&(H5I_mt_g.snum_type_info_fl_entries_reallocable)).val > 
           atomic_load(&(H5I_mt_g.max_desired_type_info_fl_len)) ) ) {
#endif

        try_to_free_an_entry = TRUE;

    } else {

        atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_fl_too_small), 1ULL);
    }

    if ( try_to_free_an_entry ) {

#if 1
        uint64_t num_fl_entries_reallocable;
#else
        H5I_suint64_t snum_fl_entries_reallocable;
        H5I_suint64_t new_snum_fl_entries_reallocable;
#endif

        /* While a reallocable entry is almost certainly available, there
         * is the possibility that other threads have snapped up all 
         * the reallocable entries in the time since we determined that 
         * we should try to free an entry.  Hence the following:
         */
#if 1
        while ( ( ! reallocable_entry_available ) &&
                ( 0 < (num_fl_entries_reallocable = 
                       atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable))) ) ) {

            assert( 0 < num_fl_entries_reallocable );

            if ( atomic_compare_exchange_strong(&(H5I_mt_g.num_type_info_fl_entries_reallocable),
                                                &num_fl_entries_reallocable, (num_fl_entries_reallocable - 1)) ) {

                reallocable_entry_available = TRUE;

            } else {

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions), 1ULL);
            }
        } /* end while */
#else
        while ( ( ! reallocable_entry_available ) &&
                ( 0 < (snum_fl_entries_reallocable = 
                       atomic_load(&(H5I_mt_g.snum_type_info_fl_entries_reallocable))).val ) ) {

            assert( 0 < snum_fl_entries_reallocable.val );

            new_snum_fl_entries_reallocable.val = snum_fl_entries_reallocable.val - 1;
            new_snum_fl_entries_reallocable.sn  = snum_fl_entries_reallocable.sn + 1;

            if ( atomic_compare_exchange_strong(&(H5I_mt_g.snum_type_info_fl_entries_reallocable),
                                                &snum_fl_entries_reallocable, new_snum_fl_entries_reallocable) ) {

                reallocable_entry_available = TRUE;

                fprintf(stderr, 
                        "\ndisc: old/new snum_type_info_fl_entries_reallocable = {%lld, %lld} / {%lld, %lld}\n",
                        (long long)snum_fl_entries_reallocable.val, 
                        (long long)snum_fl_entries_reallocable.sn,
                        (long long)new_snum_fl_entries_reallocable.val, 
                        (long long)new_snum_fl_entries_reallocable.sn);

            } else {

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions), 1ULL);
            }
        } /* end while */
#endif

        if ( ! reallocable_entry_available ) {

            /* No reallocable entries available -- just update stats and quit */

            atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_no_reallocable_entries), 1ULL);

        } else {

            done = FALSE;

            while ( ! done ) {

                fl_shead = atomic_load(&(H5I_mt_g.type_info_fl_shead));
                fl_stail = atomic_load(&(H5I_mt_g.type_info_fl_stail));

                assert(fl_shead.ptr);
                assert(fl_stail.ptr);

                fl_snext = atomic_load(&(fl_shead.ptr->fl_snext));

                test_fl_shead = atomic_load(&(H5I_mt_g.type_info_fl_shead));

                if ( ( test_fl_shead.ptr == fl_shead.ptr ) && ( test_fl_shead.sn == fl_shead.sn ) ) {

                    if ( fl_shead.ptr == fl_stail.ptr ) {

                        if ( NULL == fl_snext.ptr ) {

                            /* the free list is empty */
                            atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_frees_skipped_due_to_empty), 1);
                            done = TRUE;
                            break;
                        }

                        /* attempt to set H5I_mt_g.type_info_fl_stail to fl_snext.  It doesn't
                         * matter whether we succeed or fail, as if we fail, it
                         * just means that some other thread beat us to it.
                         *
                         * that said, it doesn't hurt to collect stats
                         */
                        new_fl_stail.ptr = fl_snext.ptr;
                        new_fl_stail.sn  = fl_stail.sn + 1;
                        if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_stail), &fl_stail, 
                                                              new_fl_stail) ) {

                            atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_tail_update_cols), 1ULL);
                        }
                    } else {

                        /* set up new_fl_shead */
                        assert(fl_snext.ptr);
                        new_fl_shead.ptr = fl_snext.ptr;
                        new_fl_shead.sn  = fl_shead.sn + 1;

                        if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_shead), 
                                                              &fl_shead, new_fl_shead) ) {

                            /* the attempt to remove the first item from the free list
                             * failed.  Update stats and try again.
                             */
                            atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_head_update_cols), 1ULL);

                        } else {

                            H5I_mt_type_info_sptr_t null_snext = {NULL, 0ULL};

                            /* first has been removed from the free list.  Set type_info_ptr to fl_shead.ptr,
                             * discard *type_info_ptr, update stats, and exit the loop by setting done to true.
                             */
                            type_info_ptr = fl_shead.ptr;

                            assert(H5I__TYPE_INFO == type_info_ptr->tag);
                            assert(type_info_ptr->on_fl);

                            /* prepare *type_info_ptr for discard */
                            type_info_ptr->tag = H5I__TYPE_INFO_INVALID;
                            atomic_store(&(type_info_ptr->fl_snext), null_snext);

                            free(type_info_ptr);

                            /* update stats */
                            atomic_fetch_add(&(H5I_mt_g.num_type_info_structs_freed), 1ULL);
                            test_val = atomic_fetch_sub(&(H5I_mt_g.type_info_fl_len), 1ULL);
                            assert( test_val > 0ULL);

                            done = true;
                        }
                    }
                }
            } /* while ( ! done ) */
        }
    } /* if ( try_to_free_entry ) */

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__discard_mt_type_info() */


#endif /* new version */

#if 0 /* old version */

/************************************************************************
 *
 * H5I__new_mt_type_info
 *
 *     Test to see if an instance of H5I_mt_type_info_t is available on the
 *     type info free list.  If there is, remove it from the free list, 
 *     re-initialize it, and return a pointer to it.
 *
 *     Otherwise, allocate and initialize an instance of struct
 *     lfht_fl_node_t and return a pointer to the included instance of
 *     lfht_node_t to the caller.
 *
 *     Return a pointer to the new instance on success, and NULL on
 *     failure.
 *
 *                                          JRM -- 8/30/23
 *
 ************************************************************************/

static H5I_mt_type_info_t * 
H5I__new_mt_type_info(const H5I_class_t *cls, unsigned reserved)
{
    hbool_t fl_search_done = FALSE;
    hbool_t result;
    H5I_mt_type_info_t * type_info_ptr = NULL;
    H5I_mt_type_info_sptr_t sfirst;
    H5I_mt_type_info_sptr_t new_sfirst;
    H5I_mt_type_info_sptr_t test_sfirst;
    H5I_mt_type_info_sptr_t slast;
    H5I_mt_type_info_sptr_t new_slast;
    H5I_mt_type_info_sptr_t snext;
    H5I_mt_type_info_sptr_t new_snext;
    H5I_mt_type_info_t * ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    sfirst = atomic_load(&(H5I_mt_g.type_info_fl_shead));

    if ( NULL == sfirst.ptr ) {

        /* free list is not yet initialized */
        fl_search_done = TRUE;
    }

    while ( ! fl_search_done ) {

        sfirst = atomic_load(&(H5I_mt_g.type_info_fl_shead));
        slast = atomic_load(&(H5I_mt_g.type_info_fl_stail));

        assert(sfirst.ptr);
        assert(slast.ptr);

        snext = atomic_load(&(sfirst.ptr->fl_snext));

        test_sfirst = atomic_load(&(H5I_mt_g.type_info_fl_shead));

        if ( ( test_sfirst.ptr == sfirst.ptr ) && ( test_sfirst.sn == sfirst.sn ) ) {

            if ( sfirst.ptr == slast.ptr ) {

                if ( NULL == snext.ptr ) {

                    /* the free list is empty */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty), 1);
                    fl_search_done = TRUE;
                    break;
                }

                /* attempt to set H5I_mt_g.type_info_fl_stail to snext.  It doesn't
                 * matter whether we succeed or fail, as if we fail, it
                 * just means that some other thread beat us to it.
                 *
                 * that satype, it doesn't hurt to collect stats
                 */
                new_slast.ptr = snext.ptr;
                new_slast.sn  = slast.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_stail), &slast, new_slast) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_tail_update_cols), 1ULL);
                }
            } else {

                /* set up new_sfirst now in case we need it later.  */
                assert(snext.ptr);
                new_sfirst.ptr = snext.ptr;
                new_sfirst.sn  = sfirst.sn + 1;

                if ( ! atomic_load(&(sfirst.ptr->re_allocable)) ) {

                    /* The entry at the head of the free list is not re allocable,
                     * which means that there may be a pointer to it somewhere.  
                     * Rather than take the risk, let it sit on the free list until 
                     * is is marked as re allocable.
                     */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_head_not_reallocable), 1ULL);
                    fl_search_done = TRUE;

                } else if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_shead), &sfirst, new_sfirst) ) {

                    /* the attempt to remove the first item from the free list
                     * failed.  Update stats and try again.
                     */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_head_update_cols), 1ULL);

                } else {

                    /* first has been removed from the free list.  Set fl_node_ptr to first,
                     * update stats, and exit the loop by setting fl_search_done to true.
                     */
                    type_info_ptr = sfirst.ptr;

                    assert(H5I__TYPE_INFO == type_info_ptr->tag);

                    type_info_ptr->cls = cls;

                    atomic_store(&(type_info_ptr->init_count), 0);
                    atomic_store(&(type_info_ptr->id_count), reserved);
                    atomic_store(&(type_info_ptr->last_id_info), NULL);
                    atomic_store(&(type_info_ptr->lfht_cleared), FALSE);

                    lfht_init(&(type_info_ptr->lfht));

                    assert(atomic_load(&(type_info_ptr->on_fl)));
                    atomic_store(&(type_info_ptr->on_fl), FALSE);

                    assert(atomic_load(&(type_info_ptr->re_allocable)));
                    atomic_store(&(type_info_ptr->on_fl), FALSE);

                    assert(atomic_load(&(type_info_ptr->re_allocable)));
                    atomic_store(&(type_info_ptr->re_allocable), FALSE);

                    new_snext.ptr = NULL;
                    new_snext.sn  = snext.sn + 1;

                    result = atomic_compare_exchange_strong(&(type_info_ptr->fl_snext), &snext, new_snext);
                    assert(result);

                    atomic_fetch_sub(&(H5I_mt_g.type_info_fl_len), 1ULL);
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_structs_alloced_from_fl), 1ULL);

                    fl_search_done = true;
                }
            }
        }
    } /* while ( ! fl_search_done ) */

    if ( NULL == type_info_ptr ) {

        type_info_ptr = (H5I_mt_type_info_t *)malloc(sizeof(H5I_mt_type_info_t));

        if ( NULL == type_info_ptr )
            HGOTO_ERROR(H5E_ID, H5E_CANTALLOC, NULL, "ID info allocation failed");

        atomic_fetch_add(&(H5I_mt_g.num_type_info_structs_alloced_from_heap), 1ULL);

        type_info_ptr->tag = H5I__TYPE_INFO;
        type_info_ptr->cls = cls;
        atomic_init(&(type_info_ptr->init_count), 0);
        atomic_init(&(type_info_ptr->id_count), 0ULL);
        atomic_init(&(type_info_ptr->nextid), reserved);
        atomic_init(&(type_info_ptr->last_id_info), NULL);
        atomic_init(&(type_info_ptr->lfht_cleared), FALSE);
        lfht_init(&(type_info_ptr->lfht));
        atomic_init(&(type_info_ptr->on_fl), FALSE);
        atomic_init(&(type_info_ptr->re_allocable), FALSE);
        snext.ptr = NULL;
        snext.sn = 0ULL;
        atomic_init(&(type_info_ptr->fl_snext), snext);
    }

    assert(type_info_ptr);

    /* Set return value */
    ret_value = type_info_ptr;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__new_mt_type_info() */

#else /* new version */

/************************************************************************
 *
 * H5I__new_mt_type_info
 *
 *     Test to see if an instance of H5I_mt_type_info_t is available on 
 *     the type info free list.  If there is, remove it from the free list, 
 *     re-initialize it, and return a pointer to it.
 *
 *     Otherwise, allocate and initialize an instance of struct
 *     H5I_mt_type_info_t, initialize it, and return a pointer to it.
 *
 *     Return a pointer to the new instance on success, and NULL on
 *     failure.
 *
 *                                          JRM -- 8/30/23
 *
 ************************************************************************/

static H5I_mt_type_info_t * 
H5I__new_mt_type_info(const H5I_class_t *cls, unsigned reserved)
{
    hbool_t fl_search_done = FALSE;;
    hbool_t result;
    hbool_t reallocable_entry_available = FALSE;
#if 1
    uint64_t num_fl_entries_reallocable;
#else
    H5I_suint64_t snum_fl_entries_reallocable;
    H5I_suint64_t new_snum_fl_entries_reallocable;
#endif
    H5I_mt_type_info_t * type_info_ptr = NULL;
    H5I_mt_type_info_sptr_t sfirst;
    H5I_mt_type_info_sptr_t new_sfirst;
    H5I_mt_type_info_sptr_t test_sfirst;
    H5I_mt_type_info_sptr_t slast;
    H5I_mt_type_info_sptr_t new_slast;
    H5I_mt_type_info_sptr_t snext;
    H5I_mt_type_info_sptr_t new_snext;
    H5I_mt_type_info_t * ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    atomic_fetch_add(&(H5I_mt_g.H5I__new_mt_type_info__num_calls), 1ULL);

    sfirst = atomic_load(&(H5I_mt_g.type_info_fl_shead));

    /* test to see if the free list has been initialized */

    if ( NULL == sfirst.ptr ) {

        /* free list is not yet initialized */
        fl_search_done = TRUE;
    }

    /* Test to see if there is a re-allocable entry on the free list.  Conceptually, we test to see
     * if H5I_mt_g.num_type_info_fl_entries_reallocable is positive, we decrement it and set entry_reallocable
     * to TRUE.  Practically, it is a bit more complicated, as it is possible that 
     * H5I_mt_g.num_type_info_fl_entries_reallocable between the time that we read it to see if it is 
     * positive, and we decrement it.  To deal with this, we use an atomic compare exchange to set 
     * the new value, and retry if there is a collision.
     */
    if ( ! fl_search_done ) {
#if 1
        while ( ( ! reallocable_entry_available ) && 
                ( 0 < (num_fl_entries_reallocable = 
                       atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable))) ) ) {

            assert( 0 < num_fl_entries_reallocable );

            if ( atomic_compare_exchange_strong(&(H5I_mt_g.num_type_info_fl_entries_reallocable),
                                                &num_fl_entries_reallocable, (num_fl_entries_reallocable - 1)) ) {

                reallocable_entry_available = TRUE;
            
            } else { 

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions), 1ULL);
            }
        } /* end while */
#else
        while ( ( ! reallocable_entry_available ) && 
                ( 0 < (snum_fl_entries_reallocable = 
                       atomic_load(&(H5I_mt_g.snum_type_info_fl_entries_reallocable))).val ) ) {

            assert( 0 < snum_fl_entries_reallocable.val);

            new_snum_fl_entries_reallocable.val = snum_fl_entries_reallocable.val - 1;
            new_snum_fl_entries_reallocable.sn  = snum_fl_entries_reallocable.sn + 1;

            if ( atomic_compare_exchange_strong(&(H5I_mt_g.snum_type_info_fl_entries_reallocable),
                                                &snum_fl_entries_reallocable, new_snum_fl_entries_reallocable) ) {

                reallocable_entry_available = TRUE;

                fprintf(stderr, 
                        "\nalloc: old/new snum_type_info_fl_entries_reallocable = {%lld, %lld} / {%lld, %lld}\n",
                        (long long)snum_fl_entries_reallocable.val, 
                        (long long)snum_fl_entries_reallocable.sn,
                        (long long)new_snum_fl_entries_reallocable.val, 
                        (long long)new_snum_fl_entries_reallocable.sn);
            
            } else { 

                /* update stats */
                atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions), 1ULL);
            }
        } /* end while */
#endif
        if ( ! reallocable_entry_available ) {

            /* No reallocable entries available, so set fl_search_done = TRUE */

            fl_search_done = TRUE;

            /* update stats */
            atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries), 1ULL);

        }
    } /* end if ( ! fl_search_done ) */


    while ( ! fl_search_done ) {

        sfirst = atomic_load(&(H5I_mt_g.type_info_fl_shead));
        slast = atomic_load(&(H5I_mt_g.type_info_fl_stail));

        assert(sfirst.ptr);
        assert(slast.ptr);

        snext = atomic_load(&(sfirst.ptr->fl_snext));

        test_sfirst = atomic_load(&(H5I_mt_g.type_info_fl_shead));

        if ( ( test_sfirst.ptr == sfirst.ptr ) && ( test_sfirst.sn == sfirst.sn ) ) {

            if ( sfirst.ptr == slast.ptr ) {

                if ( NULL == snext.ptr ) {

                    /* the free list is empty */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_empty), 1);
                    fl_search_done = TRUE;
                    break;
                }

                /* attempt to set H5I_mt_g.type_info_fl_stail to snext.  It doesn't
                 * matter whether we succeed or fail, as if we fail, it
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                new_slast.ptr = snext.ptr;
                new_slast.sn  = slast.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_stail), &slast, new_slast) ) {

                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_tail_update_cols), 1ULL);
                }
            } else {

                /* set up new_sfirst now in case we need it later.  */
                assert(snext.ptr);
                new_sfirst.ptr = snext.ptr;
                new_sfirst.sn  = sfirst.sn + 1;

                if ( ! atomic_compare_exchange_strong(&(H5I_mt_g.type_info_fl_shead), &sfirst, new_sfirst) ) {

                    /* the attempt to remove the first item from the free list
                     * failed.  Update stats and try again.
                     */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_head_update_cols), 1ULL);

                } else {

                    /* first has been removed from the free list.  Set fl_node_ptr to first,
                     * update stats, and exit the loop by setting fl_search_done to true.
                     */
                    type_info_ptr = sfirst.ptr;

                    assert(H5I__TYPE_INFO == type_info_ptr->tag);

                    type_info_ptr->cls = cls;

                    atomic_store(&(type_info_ptr->init_count), 0);
                    atomic_store(&(type_info_ptr->id_count), reserved);
                    atomic_store(&(type_info_ptr->last_id_info), NULL);
                    atomic_store(&(type_info_ptr->lfht_cleared), FALSE);

                    lfht_init(&(type_info_ptr->lfht));

                    assert(atomic_load(&(type_info_ptr->on_fl)));
                    atomic_store(&(type_info_ptr->on_fl), FALSE);

                    new_snext.ptr = NULL;
                    new_snext.sn  = snext.sn + 1;

                    result = atomic_compare_exchange_strong(&(type_info_ptr->fl_snext), &snext, new_snext);
                    assert(result);

                    atomic_fetch_sub(&(H5I_mt_g.type_info_fl_len), 1ULL);
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_structs_alloced_from_fl), 1ULL);

                    fl_search_done = true;
                }
            }
        }
    } /* while ( ! fl_search_done ) */

    if ( NULL == type_info_ptr ) {

        type_info_ptr = (H5I_mt_type_info_t *)malloc(sizeof(H5I_mt_type_info_t));

        if ( NULL == type_info_ptr )
            HGOTO_ERROR(H5E_ID, H5E_CANTALLOC, NULL, "ID info allocation failed");

        atomic_fetch_add(&(H5I_mt_g.num_type_info_structs_alloced_from_heap), 1ULL);

        type_info_ptr->tag = H5I__TYPE_INFO;
        type_info_ptr->cls = cls;
        atomic_init(&(type_info_ptr->init_count), 0);
        atomic_init(&(type_info_ptr->id_count), 0ULL);
        atomic_init(&(type_info_ptr->nextid), reserved);
        atomic_init(&(type_info_ptr->last_id_info), NULL);
        atomic_init(&(type_info_ptr->lfht_cleared), FALSE);
        lfht_init(&(type_info_ptr->lfht));
        atomic_init(&(type_info_ptr->on_fl), FALSE);
        snext.ptr = NULL;
        snext.sn = 0ULL;
        atomic_init(&(type_info_ptr->fl_snext), snext);
    }

    assert(type_info_ptr);

    /* Set return value */
    ret_value = type_info_ptr;

done:

    FUNC_LEAVE_NOAPI(ret_value)

} /* H5I__new_mt_id_info() */

#endif /* new version */

/************************************************************************
 *
 * H5I__enter()
 *
 *     Perform required book keeping on entry to the H5I package.  At 
 *     present this consists of incrementing H5I_mt_g.threads_active, 
 *     updating statistics, and updating free lists if appropriate.
 *
 *     Note that this function should eventually be converted to a 
 *     macro to reduce overhead.
 *
 *                                            JRM -- 12/14/23
 *
 * Changes: None.
 *
 ************************************************************************/

void
H5I__enter(hbool_t public_api)
{
    if ( public_api ) {

        atomic_fetch_add(&(H5I_mt_g.num_H5I_entries_via_public_API), 1ULL);

    } else {

        atomic_fetch_add(&(H5I_mt_g.num_H5I_entries_via_internal_API), 1ULL);
    }

    if ( atomic_fetch_add(&(H5I_mt_g.active_threads), 1ULL) >
         atomic_load(&(H5I_mt_g.max_active_threads)) ) {
 
        atomic_fetch_add(&(H5I_mt_g.max_active_threads), 1ULL);
    }

    return;

} /* H5I__enter() */


/************************************************************************
 *
 * H5I__exit()
 *
 *     Perform required book keeping on exit from the H5I package.  At 
 *     present this consists of updating statistics, and updating free 
 *     lists if appropriate.
 *
 *                                            JRM -- 12/14/23
 *
 * Changes: None.
 *
 ************************************************************************/

void
H5I__exit(void)
{

    uint64_t active_threads;
    uint64_t pre_api_entries;
    uint64_t post_api_entries;
    uint64_t pre_internal_entries;
    uint64_t post_internal_entries;
    uint64_t num_id_info_fl_entries_reallocable;
    uint64_t id_info_fl_len;
#if 1
    uint64_t num_type_info_fl_entries_reallocable;
#else
    H5I_suint64_t snum_type_info_fl_entries_reallocable;
#endif
    uint64_t type_info_fl_len;

    if ( 1ULL == atomic_fetch_sub(&(H5I_mt_g.active_threads), 1ULL) ) {

        atomic_fetch_add(&(H5I_mt_g.times_active_threads_is_zero), 1ULL);

        /* This is the only entry in H5I -- since we are about to exit, the 
         * the entire id free list must be reallocable.  Attempt to update 
         * H5I_mt_g.num_id_info_fl_entries_reallocable accordingly.  Note 
         * that we must verify that no thread becomes active during this 
         * process, and abort if one does. 
         */
        pre_api_entries = atomic_load(&(H5I_mt_g.num_H5I_entries_via_public_API));
        pre_internal_entries = atomic_load(&(H5I_mt_g.num_H5I_entries_via_internal_API));

        active_threads = atomic_load(&(H5I_mt_g.active_threads));

        num_id_info_fl_entries_reallocable = atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable));
        id_info_fl_len = atomic_load(&(H5I_mt_g.id_info_fl_len));

#if 1
        num_type_info_fl_entries_reallocable = atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable));
#else
        snum_type_info_fl_entries_reallocable = atomic_load(&(H5I_mt_g.snum_type_info_fl_entries_reallocable));
#endif
        type_info_fl_len = atomic_load(&(H5I_mt_g.type_info_fl_len));

        post_api_entries = atomic_load(&(H5I_mt_g.num_H5I_entries_via_public_API));
        post_internal_entries = atomic_load(&(H5I_mt_g.num_H5I_entries_via_internal_API));

        /* test to see if any threads have entered while we were collecting the 
         * data on the free lists.  If any have, abort, as our data on the free lists
         * may be inconsistent.
         */
        if ( ( active_threads != 0 ) || ( pre_api_entries != post_api_entries ) || 
             ( pre_internal_entries != post_internal_entries ) ) {

            /* one or more threads entered while we were collecting data.  Update
             * stats and do nothing.
             */
            atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_aborts), 1ULL);

        } else {

            assert( 0 == active_threads );

            if ( id_info_fl_len == num_id_info_fl_entries_reallocable ) {

                /* All entries in the id info free list are already maked as reallocable --
                 * Nothing to do here -- just update stats
                 */
                atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_noops), 1ULL);

            } else {

                uint64_t delta;
                uint64_t new_num_id_info_fl_entries_reallocable;

                assert( num_id_info_fl_entries_reallocable < id_info_fl_len );

                delta = id_info_fl_len - num_id_info_fl_entries_reallocable;

                /* It is possible that another thread is also trying to update 
                 * H5I_mt_g.num_id_info_fl_entries_reallocable.  To avoid duplicate
                 * increments, update H5I_mt_g.num_id_info_fl_entries_reallocable via
                 * a call to atomic_compare_exchange_strong().  If this fails, just 
                 * update stats and don't attempt a re-try.
                 */
                new_num_id_info_fl_entries_reallocable = num_id_info_fl_entries_reallocable + delta;

                if ( atomic_compare_exchange_strong(&(H5I_mt_g.num_id_info_fl_entries_reallocable),
                                                    &num_id_info_fl_entries_reallocable,
                                                    new_num_id_info_fl_entries_reallocable) ) {

                    /* success -- update stats accordingly */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_num_reallocable_updates), 1ULL);
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_num_reallocable_total), delta);

                } else {

                    /* failure -- update stats accordingly */
                    atomic_fetch_add(&(H5I_mt_g.num_id_info_fl_num_reallocable_update_collisions), 1ULL);

                }
            }

#if 1
            if ( type_info_fl_len == num_type_info_fl_entries_reallocable ) {
#else
            if ( type_info_fl_len == snum_type_info_fl_entries_reallocable.val ) {
#endif

                /* All entries in the type info free list are already maked as reallocable --
                 * Nothing to do here -- just update stats
                 */
                atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_noops), 1ULL);

            } else {

                uint64_t delta;
#if 1
                uint64_t new_num_type_info_fl_entries_reallocable;
#else
                H5I_suint64_t new_snum_type_info_fl_entries_reallocable;
#endif

#if 1
                assert( num_type_info_fl_entries_reallocable < type_info_fl_len );
#else
                assert( snum_type_info_fl_entries_reallocable.val < type_info_fl_len );
#endif

#if 1
                delta = type_info_fl_len - num_type_info_fl_entries_reallocable;
#else
                delta = type_info_fl_len - snum_type_info_fl_entries_reallocable.val;
#endif

                /* It is possible that another thread is also trying to update 
                 * H5I_mt_g.num_type_info_fl_entries_reallocable.  To avoid duplicate
                 * increments, update H5I_mt_g.num_type_info_fl_entries_reallocable via
                 * a call to atomic_compare_exchange_strong().  If this fails, just 
                 * update stats and don't attempt a re-try.
                 */
#if 1
                new_num_type_info_fl_entries_reallocable = num_type_info_fl_entries_reallocable + delta;
#else
                new_snum_type_info_fl_entries_reallocable.val = snum_type_info_fl_entries_reallocable.val + delta;
                new_snum_type_info_fl_entries_reallocable.sn  = snum_type_info_fl_entries_reallocable.sn + 1;
#endif

#if 1
                if ( atomic_compare_exchange_strong(&(H5I_mt_g.num_type_info_fl_entries_reallocable),
                                                    &num_type_info_fl_entries_reallocable,
                                                    new_num_type_info_fl_entries_reallocable) ) {
#else
                if ( atomic_compare_exchange_strong(&(H5I_mt_g.snum_type_info_fl_entries_reallocable),
                                                    &snum_type_info_fl_entries_reallocable,
                                                    new_snum_type_info_fl_entries_reallocable) ) {

                    fprintf(stderr, 
                            "\nexit: old/new snum_type_info_fl_entries_reallocable = {%lld, %lld} / {%lld, %lld}\n",
                            (long long)snum_type_info_fl_entries_reallocable.val, 
                            (long long)snum_type_info_fl_entries_reallocable.sn,
                            (long long)new_snum_type_info_fl_entries_reallocable.val, 
                            (long long)new_snum_type_info_fl_entries_reallocable.sn);
#endif

                    /* success -- update stats accordingly */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_num_reallocable_updates), 1ULL);
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_num_reallocable_total), delta);

                } else {

                    /* failure -- update stats accordingly */
                    atomic_fetch_add(&(H5I_mt_g.num_type_info_fl_num_reallocable_update_collisions), 1ULL);

                }
            }
        }
    }

    return;

} /* H5I__exit() */

#endif /* H5_HAVE_MULTITHREAD */

