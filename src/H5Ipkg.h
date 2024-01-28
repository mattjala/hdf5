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
 * Purpose: This file contains declarations which are visible only within
 *          the H5I package.  Source files outside the H5I package should
 *          include H5Iprivate.h instead.
 */
#if !(defined H5I_FRIEND || defined H5I_MODULE)
#error "Do not include this file outside the H5I package!"
#endif

#ifndef H5Ipkg_H
#define H5Ipkg_H

/* Get package's private header */
#include "H5Iprivate.h"

/**************************/
/* Package Private Macros */
/**************************/

/*
 * Number of bits to use for ID Type in each ID. Increase if more types
 * are needed (though this will decrease the number of available IDs per
 * type). This is the only number that must be changed since all other bit
 * field sizes and masks are calculated from TYPE_BITS.
 */
#define TYPE_BITS 7
#define TYPE_MASK (((hid_t)1 << TYPE_BITS) - 1)

#define H5I_MAX_NUM_TYPES TYPE_MASK

/*
 * Number of bits to use for the ID index in each ID (assumes 8-bit
 * bytes). We don't use the sign bit.
 */
#define ID_BITS ((sizeof(hid_t) * 8) - (TYPE_BITS + 1))
#define ID_MASK (((hid_t)1 << ID_BITS) - 1)

/* Map an ID to an ID type number */
#define H5I_TYPE(a) ((H5I_type_t)(((hid_t)(a) >> ID_BITS) & TYPE_MASK))

#if H5_HAVE_MULTITHREAD
#include "lfht.h"
#endif /* H5_HAVE_MULTITHREAD */

/****************************/
/* Package Private Typedefs */
/****************************/
#if H5_HAVE_MULTITHREAD /****************************************************************************************/

/****************************************************************************************
 *
 * struct H5I_mt_id_info_sptr_t
 *
 * H5I_mt_id_info_sptr_t combines a pointer to H5I_mt_id_info_t with a serial number
 * that must be incremented each time the value of the pointer is changed.
 *
 * When it appears in either H5I_mt_id_info_t or H5I_mt_t, it should do so
 * as an atomic object.  Its purpose is to avoid ABA bugs.
 *
 * ptr: pointer to an instance of H5I_mt_id_info_t, or NULL if undefined.
 *
 * sn:  uint64_t that is initialized to 0, and must be incremented by 1 each time the
 *      ptr field is modified.
 *
 ****************************************************************************************/

typedef struct H5I_mt_id_info_t H5I_mt_id_info_t; /* forward declaration */

typedef struct H5I_mt_id_info_sptr_t {

    H5I_mt_id_info_t * ptr;
    uint64_t           sn;

} H5I_mt_id_info_sptr_t;


/****************************************************************************************
 *
 * struct H5I_mt_type_info_sptr_t
 *
 * H5I_mt_vol_info_sptr_t combines a pointer to H5I_mt_vol_info_t with a serial number
 * that must be incremented each time the value of the pointer is changed.
 *
 * When it appears in either H5I_mt_type_info_t or H5I_mt_t, it should do so
 * as an atomic object.  Its purpose is to avoid ABA bugs.
 *
 * ptr: pointer to an instance of H5I_mt_type_info_t, or NULL if undefined.
 *
 * sn:  uint64_t that is initialized to 0, and must be incremented by 1 each time the
 *      ptr field is modified.
 *
 ****************************************************************************************/

typedef struct H5I_mt_type_info_t H5I_mt_type_info_t; /* forward declaration */

typedef struct H5I_mt_type_info_sptr_t {

    H5I_mt_type_info_t * ptr;
    uint64_t             sn;

} H5I_mt_type_info_sptr_t;


/****************************************************************************************
 *
 * struct H5I_mt_t
 *
 * A single, global instance of H5I_mt_t is used to collect all global variables that
 *
 * 1) are existing globals that must be made into atomic variables in the MT context, or
 *
 * 2) are new globals needed for the MT case, or 
 *
 * 3) are needed to maintain statistics on the multi-thread version of H5I.
 *
 * The single instance is declared as a global, and initialized in H5I_init().
 *
 * Fields are discussed individually below.
 *
 *
 * Pre-existing Globals:
 *
 * type_info_array: Array of atomic pointers to H5I_mt_type_info_t of length
 *      H5I_MAX_NUM_TYPES used to map type IDs to the associated instances of 
 *      H5I_mt_type_info_t,
 *
 *      All elements of the type_info_array are initialized to NULL, and are 
 *      set back to NULL when an instance of H5I_mt_type_info_t is discarded.
 *
 * type_info_allocation_table:  Array of atomic booleans used to track which 
 *      entries in the type_info_array are allocated.
 *
 *      In the single thread version of H5I, type IDs can be re-used.  I had 
 *      originally planed to simplify the H5I code by dis-allowing this in the 
 *      multi-thread H5I code, however this breaks the serial tests.  While it 
 *      is doubtful that this has any practical significance, this is not 
 *      something to be changed lightly -- particularly in the initial prototype.
 *
 *      To make things more complicated, type IDs are allocated in two different
 *      ways.  Index types used by the HDF5 library proper are allocated statically
 *      in the header files, while index types created by users are allocated 
 *      dynamically in H5Iregister_type().  In the single thread code, this is 
 *      done via one of two methods:
 *
 *      First, type IDs are allocated sequentially using the H5I_next_type_g
 *      to maintain the next ID to be allocated.  This method is used until 
 *      the value of H5I_next_type_g is equal to H5I_MAX_NUM_TYPES.  When this
 *      method is exhausted, the sequential code does a linear scan on the 
 *      H5I_type_info_array_g, and uses the index of the first NULL entry 
 *      encountered as the next type ID to allocate.
 *
 *      This proceedure of scanning the type_info_array is not directly applicable
 *      to the multi-thread case as the ID is allocated well before the associated
 *      entry in the type_info_array is set to point to the new instance of 
 *      H5I_mt_type_info_t.  While this could be finessed with a special value 
 *      indicating that the entry in the type_info_array was allocated but invalid,
 *      an allocation table seemed to offer a cleaner solution.
 *
 *      Thus the booleans in the type_info_allocation_table are set to TRUE when
 *      a type ID is allocated, and back to FALSE when it is de-allocated.  In 
 *      contrast, the pointers in the type_info_array are NULL if the associated
 *      ID is undefined, and point to the associated instance of H5I_mt_type_info_t
 *      if the ID is defined.  This lets us treat type ID allocation and definition
 *      as two, separate atomic operations.
 *
 *      Note that for now at least, I am retaining the two methods of allocating 
 *      type IDs.  While this is redundant, I hesitate to change the behaviour of 
 *      the H5I code any more than is necessary -- at least for the initial 
 *      multi-thread implementation.  
 *
 * next_type: Integer variable to keep track of the number of types allocated.  
 *      Its value is the next type ID to be handed out, so it is always one greater 
 *      than the number of types.
 *
 *      Starts at 1 instead of 0 because it makes trace output look nicer.  If more
 *      types (or IDs within a type) are needed, adjust TYPE_BITS in H5Ipkg.h
 *      and/or increase size of hid_t
 *
 *      Note that to simplify the MT implementation, type IDs are not recycled,
 *      unlike the current single thread implementation.
 *
 * marking: Functionally, the marking field replaces the H5I_marking_g boolean 
 *      in the single thread version of H5I.  It is an integer instead of a 
 *      boolean to allow for multiple threads setting and re-setting it.
 *
 *      In the serial version, when H5I_marking_g is set, when entries are deleted,
 *      they are first marked (i.e. info_ptr->k.marked is set to TRUE), an then
 *      deleted from the index at a later point.
 *
 *      The argument for this is that callbacks may delete entries on the index
 *      and thus cause data structure corruption.  
 *
 *      However, in the multi-thread case, entries can be deleted whenever, so 
 *      the point seems moot -- we simply need to adjust to that fact.
 *
 *      However, for now I am keeping the marking field, and will use as per the 
 *      single thread version.  That said, it will probably be removed later.
 *
 *
 * New Globals:
 *
 * active_threads: Atomic integer used to track the number of threads currently
 *      active in H5I.
 *
 *
 * id_info_fl_shead: Atomic instance of struct H5I_mt_id_info_sptr_t, which contains 
 *      a pointer (ptr) to the head of the id info free list, and a serial number (sn) 
 *      which must be incremented each time a new value is assigned to id_info_fl_shead.
 *
 *      The objective here is to prevent ABA bugs, which would otherwise occasionally 
 *      allow re-allocation of instances of H5I_mt_id_info_t before all threads that
 *      might have pointers to them have left H5I.
 *
 *      Note that once initialized, the id info free list will always contain at least
 *      one entry, and is logically empty if id_info_fl_shead.ptr == id_info_fl_stail.ptr 
 *      != NULL.
 *
 * id_info_fl_stail: Atomic instance of struct H5I_mt_id_info_sptr_t, which contains
 *      a pointer (ptr) to the tail of the id info free list, and a serial number (sn)
 *      which must be incremented each time a new value is assigned to id_info_fl_stail.
 *
 *      The objective here is to prevent ABA bugs, which would otherwise occasionally
 *      allow re-allocation of instances of H5I_mt_id_info_t before all threads that
 *      might have pointers to them have left H5I.
 *
 * id_info_fl_len: Atomic unsigned integer used to maintain a count of the number of 
 *      nodes on the id info free list.  Note that due to the delay between free list
 *      insertions and deletins, and the update of this field, this count may be off 
 *      for brief periods of time.
 *
 *      Recall that the free list must always contain at least one entry.  Thus, when 
 *      correct, fl_len will be one greater than the number of entries on the free list.
 *
 * max_desired_id_info_fl_len: Unsigned integer field containing the desired maximum 
 *      id info free list length.  This is of necessity a soft limit as entries cannot
 *      be removed from the head of the free list unless their re-allocable fields are 
 *      TRUE.  
 *
 *
 * type_info_fl_shead: Atomic instance of struct H5I_mt_type_info_sptr_t, which contains 
 *      a pointer (ptr) to the head of the type info free list, and a serial number (sn) 
 *      which must be incremented each time a new value is assigned to type_info_fl_shead.
 *
 *      The objective here is to prevent ABA bugs, which would otherwise occasionally 
 *      allow re-allocation of instances of H5I_mt_type_info_t before all threads that
 *      might have pointers to them have left H5I.
 *
 *      Note that once initialized, the type info free list will always contain at least
 *      one entry, and is logically empty if type_info_fl_shead.ptr == type_info_fl_stail.ptr 
 *      != NULL.
 *
 * type_info_fl_stail: Atomic instance of struct H5I_mt_type_info_sptr_t, which contains
 *      a pointer (ptr) to the tail of the type info free list, and a serial number (sn)
 *      which must be incremented each time a new value is assigned to type_info_fl_stail.
 *
 *      The objective here is to prevent ABA bugs, which would otherwise occasionally
 *      allow re-allocation of instances of H5I_mt_type_info_t before all threads that
 *      might have pointers to them have left H5I.
 *
 * type_info_fl_len: Atomic unsigned integer used to maintain a count of the number of 
 *      nodes on the type info free list.  Note that due to the delay between free list
 *      insertions and deletins, and the update of this field, this count may be off 
 *      for brief periods of time.
 *
 *      Recall that the free list must always contain at least one entry.  Thus, when 
 *      correct, type_info_fl_len will be one greater than the number of entries on the 
 *      free list.
 *
 * max_desired_type_info_fl_len: Unsigned integer field containing the desired maximum 
 *      type info free list length.  This is of necessity a soft limit as entries cannot
 *      be removed from the head of the free list unless their re-allocable fields are 
 *      TRUE.  
 *
 * Statistics:
 *
 * dump_stats_on_shutdown: Boolean flag that controls display of statistics in 
 *      H5I_term_package().  When set to TRUE, stats are displayed when shutdown is
 *      complete, and just before stats are reset.
 *
 * init_type_registrations: Number of initial type registrations.
 *
 * duplicate_type_registrations: Number of duplicate type registrations.
 *
 * type_registration_collisions: Number of type registration collisions.  Collisions 
 *      occur when two or more threads simultaneously create new instances of 
 *      H5I_type_info_t, but only one can be inserted into H5I_type_info_array_g[] for 
 *      the target type.
 *
 *
 * ID Info Free List Statistics:
 *
 * max_id_info_fl_len: Maximum number of entries that have resided on the id info free 
 *      list at any point during the current run.  In the multi-thread case, this number 
 *      should be viewed as aproximate.
 *
 * num_id_info_structs_alloced_from_heap: Number of instances of H5I_mt_id_info_t 
 *      allocated from the heap.
 *
 * num_id_info_structs_alloced_from_fl:  Number of times an instance of H5I_mt_id_info_t
 *      has been allocated from the id info free list.
 *
 * num_id_info_structs_freed: Number of instances of H5I_mt_id_info_t that have been 
 *      freed -- that is returned to the heap.
 *
 * num_id_info_structs_added_to_fl: Number of times an instance of H5I_mt_id_info_t
 *      has been added to the id info free list.
 *
 * num_id_info_fl_head_update_cols: Number of id info free list head update collisions.
 *
 * num_id_info_fl_tail_update_cols: Number of id info free list tail update collisions.
 *
 * num_id_info_fl_append_cols: Number of collisions when appending an instance of 
 *     H5I_mt_id_info_t to the id info free list.
 *
 * num_id_info_structs_marked_reallocatable: Each instance of H5I_mt_id_info_t has its 
 *      reallocatable field set to FALSE when it is allocated, or it is inserted
 *      on the id info free list. This field is set to TRUE when it is known that all
 *      threads that might have a pointer to the instance of H5I_mt_id_info_t have left
 *      H5I.  num_id_info_structs_marked_reallocatable is incremented each time the 
 *      reallocatable field of an instance of H5I_mt_id_info_t is set to TRUE.  Note
 *      that this should only happen when an instance is on the id info free list.
 *
 * num_id_info_fl_alloc_req_denied_due_to_empty:  Number of times an alloc request
 *      for an instance of H5I_mt_id_info_t has had to be satisfied from the heap
 *      instead of the free list because the id info free list is empty.  Recall 
 *      that the id info free list is logically empty when it contains only a single
 *      entry.
 *
 * num_id_info_fl_alloc_req_denied_due_to_head_not_reallocable:  Number of times an 
 *      alloc request for an instance of H5I_mt_id_info_t has had to be satisfied 
 *      from the heap instead of the free list because the reallocatable field of the
 *      instance of H5I_mt_id_info_t at the head of the id info free list is set to
 *      FALSE.
 *
 * num_id_info_fl_frees_skipped_due_to_empty: Number of times that an instance of 
 *      H5I_mt_id_info_t on the id info free list is not released to the heap because
 *      the free list is empty (or more correctly, has fewer than 
 *      max_desired_id_info_fl_len entries).
 *
 * num_id_info_fl_frees_skipped_due_to_head_not_reallocable: Number of times that an 
 *      instance of H5I_mt_id_info_t on the id info free list is not released to the heap 
 *      because the reallocatable field of the instance of H5I_mt_id_info_t at the head 
 *      of the id info free list is set to FALSE.
 *
 *
 * Type Info Free List Statistics:
 *
 * max_type_info_fl_len: Maximum number of entries that have resided on the type info free 
 *      list at any point during the current run.  In the multi-thread case, this number 
 *      should be viewed as aproximate.
 *
 * num_type_info_structs_alloced_from_heap: Number of instances of H5I_mt_type_info_t 
 *      allocated from the heap.
 *
 * num_type_info_structs_alloced_from_fl:  Number of times an instance of 
 *      H5I_mt_type_info_t has been allocated from the type info free list.
 *
 * num_type_info_structs_freed: Number of instances of H5I_mt_type_info_t that have been 
 *      freed -- that is returned to the heap.
 *
 * num_type_info_structs_added_to_fl: Number of times an instance of H5I_mt_type_info_t
 *      has been added to the type info free list.
 *
 * num_type_info_fl_head_update_cols: Number of type info free list head update collisions.
 *
 * num_type_info_fl_tail_update_cols: Number of type info free list tail update collisions.
 *
 * num_type_info_fl_append_cols: Number of collisions when appending an instance of 
 *     H5I_mt_type_info_t to the type info free list.
 *
 * num_type_info_structs_marked_reallocatable: Each instance of H5I_mt_type_info_t has its 
 *      reallocatable field set to FALSE when it is allocated, or it is inserted
 *      on the type info free list. This field is set to TRUE when it is known that all
 *      threads that might have a pointer to the instance of H5I_mt_type_info_t have left
 *      H5I.  num_type_info_structs_marked_reallocatable is incremented each time the 
 *      reallocatable field of an instance of H5I_mt_type_info_t is set to TRUE.  Note
 *      that this should only happen when an instance is on the type info free list.
 *
 * num_type_info_fl_alloc_req_denied_due_to_empty:  Number of times an alloc request
 *      for an instance of H5I_mt_type_info_t has had to be satisfied from the heap
 *      instead of the free list because the type info free list is empty.  Recall 
 *      that the type info free list is logically empty when it contains only a single
 *      entry.
 *
 * num_type_info_fl_alloc_req_denied_due_to_head_not_reallocable:  Number of times an 
 *      alloc request for an instance of H5I_mt_type_info_t has had to be satisfied 
 *      from the heap instead of the free list because the reallocatable field of the
 *      instance of H5I_mt_type_info_t at the head of the type info free list is set to
 *      FALSE.
 * 
 *
 * Statistics on the behaviour of the H5I__mark_node() function:
 *
 * H5I__mark_node__num_calls: Number of times that H5I__mark_node() is called.
 *
 * H5I__mark_node__num_calls_with_global_mutex: Number of times that H5I__mark_node() 
 *      is called by a thread that has the global mutex.
 *
 * H5I__mark_node__num_calls_without_global_mutex: Number of times that H5I__mark_node() 
 *      is called by a thread that doesn't have the global mutex.
 *

 *
 * H5I__mark_node__already_marked: Number of times that H5I__mark_node() is called on an 
 *      instance of H5I_mt_id_info_t that is alread marked for deletion.
 *
 * H5I__mark_node__marked: Number of times that H5I__mark_node() marks the supplied 
 *      instance of H5I_mt_id_info_t for deletion.
 *
 * H5I__mark_node__marked_by_another_thread: Number of times that H5I__mark_node() is 
 *      called on an un-marked instance of H5I_mt_id_info_t which is marked for 
 *      deletion by another thread before H5I__mark_node() can do so.
 *
 * H5I__mark_node__no_ops: Number of times that H5I__mark_node() examines the supplied 
 *      instance of H5I_mt_id_info_t and does nothing.  Note that this count does not 
 *      include either instances of H5I_mt_id_info_t that are already marked, or cases
 *      in which the discard_cb or free_func fails and force is not set.
 *
 * H5I__mark_node__global_mutex_locks_for_discard_cb: Number of times that H5I__mark_node()
 *      obtains the global mutex prior to a call to the discard_cb.
 *
 * H5I__mark_node__global_mutex_unlocks_for_discard_cb; Number of times that 
 *      H5I__mark_node() drops the global mutex immediately after a call to the 
 *      discard_cb
 *
 * H5I__mark_node__discard_cb_failures_marked: Number of times that H5I__mark_node() 
 *      calls the discard_cb associated with the supplied instance of H5I_mt_id_info_t,
 *      that call fails, and marks the supplied instance of H5I_mt_id_info_t
 *      reguardless.
 *
 * H5I__mark_node__discard_cb_failures_unmarked: Number of times that H5I__mark_node()
 *      calls the discard_cb associated with the supplied instance of H5I_mt_id_info_t,
 *      that call fails, and does not mark the supplied instance of H5I_mt_id_info_t.
 *
 * H5I__mark_node__discard_cb_successes: Number of times that H5I__mark_node()
 *      successfully calls the discard_cb associated with the supplied instance of 
 *      H5I_mt_id_info_t.
 *
 * H5I__mark_node__global_mutex_locks_for_free_func: Number of times that 
 *      H5I__mark_node() obtains the global mutex prior to a call to the free_func.
 *
 * H5I__mark_node__global_mutex_unlocks_for_free_func: Number of times that 
 *      H5I__mark_node() drops the global mutex just after a call to the free_func.
 *
 * H5I__mark_node__free_func_failures_marked: Number of times that H5I__mark_node()
 *      calls the free_func associated with the supplied instance of H5I_mt_id_info_t,
 *      that call fails, and H5I__mark_node() marks the supplied instance of 
 *      H5I_mt_id_info_t reguardless.
 *
 * H5I__mark_node__free_func_failures_unmarked: Number of times that H5I__mark_node()
 *      calls the free_func associated with the supplied instance of H5I_mt_id_info_t,
 *      that call fails, and H5I__mark_node() does not mark the supplied instance of 
 *      H5I_mt_id_info_t.
 *
 * H5I__mark_node__free_func_successes: Number of times that H5I__mark_node()
 *      successfully calls the free_func associated with the supplied instance of 
 *      H5I_mt_id_info_t.
 *
 * H5I__mark_node__retries: Number of times that H5I__mark_node() has to retry the 
 *      the operation.  This happens when an attempt to overwrite the kernel of the 
 *      supplied instance of H5I_mt_id_info_t fails -- presumably due to some other 
 *      thread modifying the kernel between the time that H5I__mark_node() reads it
 *      and attempts to overwrite it with a modified version.
 *
 * 
 * Statistics on the behaviour of the H5I__remove_common() function.
 *
 * H5I__remove_common__num_calls: Number of times that H5I__remove_common() is called.
 *
 * H5I__remove_common__already_marked: Number of times that H5I__remove_common() is 
 *      called on an ID whose instance of H5I_mt_id_info_t that is alread marked for 
 *      deletion.
 *
 * H5I__remove_common__marked_by_another_thread:  Number of times that 
 *      H5I__remove_common() is called on an ID whose associaetd instance of 
 *      H5I_mt_id_info_t is un-marked for deletion on entry, but is marked for
 *      deletion by another thread before H5I__remove_common() can do so.
 *
 * H5I__remove_common__marked: Number of times that H5I__remove_common() is called
 *      on an ID and marks the associated instance of H5I_mt_id_info_t for deletion.
 *
 * H5I__remove_common__target_not_in_lfht:  Number of times that H5I__remove_common() is called
 *      on an ID that doesn't appear in the lock free hash table.
 *
 * H5I__remove_common__retries; Number of time that H5I__remove_common() has to retry
 *      the operation.  This happens when an attempt to overwrite the kernel of the
 *      instance of H5I_mt_id_info_t associated with the supplied ID fails -- presumably 
 *      due to some other thread modifying the kernel between the time that 
 *      H5I__remove_common() reads it and attempts to overwrite it with a modified version.
 *
 *
 * Statistics on the behaviour of the H5I__find_id() function.
 *
 * H5I__find_id__num_calls: Number of times that H5I__find_id() is called.
 *
 * H5I__find_id__num_calls_with_global_mutex: Number of times that H5I__find_id() is called
 *      by a thread that holds the global mutex.
 *
 * H5I__find_id__num_calls_without_global_mutex: Number of times that H5I__find_id() is 
 *      called by a thread that doesn't hold the global mutex.
 *
 * H5I__find_id__ids_found: Number of times that H6I__find_id() succeeds and returns
 *      a pointer to the object associated with the supplied ID.
 *
 * H5I__find_id__num_calls_to_realize_cb: Number of times that H5I__find_id() calls 
 *      the realize_cb.
 *
 * H5I__find_id__global_mutex_locks_for_realize_cb: Number of times that H5I__find_id()
 *      obtains the global mutex before calling the realize_cb.
 *
 * H5I__find_id__global_mutex_unlocks_for_realize_cb: Number of times that H5I__find_id()
 *      drops the global mutex immediately after calling the realize_cb.
 *
 * H5I__find_id__num_calls_to_discard_cb: Number of times that H5I__find_id() calls
 *      the discard_cb.
 *
 * H5I__find_id__global_mutex_locks_for_discard_cb: Number of times that H5I__find_id()
 *      obtains the global mutex before calling the discard_cb.
 *
 * H5I__find_id__global_mutex_unlocks_for_discard_cb: Number of times that H5I__find_id()
 *      drops the global mutex immediately after calling the discard_cb.
 *
 * H5I__find_id__future_id_conversions_attempted: Number of times that H5I__find_id()
 *      is called on a future ID, and attempts to convert it to a real ID.
 *
 * H5I__find_id__future_id_conversions_completed: Number of times that H5I__find_id()
 *      successfully converts a future ID to a real ID.
 *
 * H5I__find_id__retries: Number of times that H5I__find_id() has to re-try the 
 *      operation.  This is caused by either another thread modifying the kernel of
 *      instance of H5I_mt_id_info_t associated with the ID in the period between 
 *      the time that H5I__find_id() reads it, and then tries to overwrite it with 
 *      a modified version, or the function encounters a set do_not_disturb flag.
 *
 *
 * Statistics on the behaviour of the H5I_register_using_existing_id() function.
 *
 * H5I_register_using_existing_id__num_calls: Number of times that 
 *      H5I_register_using_existing_id() is called.
 *
 * H5I_register_using_existing_id__num_marked_only: Number of times that the supplied
 *      existing ID refers to an ID that has been marked for deletion, but not yet 
 *      removed from the index.
 *
 * H5I_register_using_existing_id__num_id_already_in_use: Number of times that the supplied
 *      existing ID refers to an ID that is already back in use.
 *
 * H5I_register_using_existing_id__num_failures: Number of times that 
 *      H5I_register_using_existing_id() reports failure.
 *
 *
 * Statistics on the behaviour of the H5I_subst() function.
 *
 * H5I_subst__num_calls: Number of times that H5I_subst() is called.
 *
 * H5I_subst__marked_on_entry; Number of times that the supplied ID is marked for 
 *      deletion on entry.
 *
 *  H5I_subst__marked_during_call: Number of times that the supplied ID is marked for
 *      deletion by another thread after entry.
 *
 * H5I_subst__retries: Number of times that H5I_subst() has to retry its modifications
 *      to the kernel of the id info associated with the ID.
 *
 * H5I_subst__failures: Number of times that H5I_subst() fails to perform the 
 *      requested operation.
 *
 *
 * Statistics on the behaviour of the H5I__dec_ref() function.
 *
 * H5I__dec_ref__num_calls: Number of times that H5I__dec_ref() is called.
 *
 * H5I__dec_ref__num_app_calls: Number of times that H5I__dec_ref() is called with 
 *      the app flag set to TRUE.  
 *
 * H5I__dec_ref__num_calls_with_global_mutex: Number of times that H5I__dec_ref() is 
 *      called by a thread that holds the global mutex.
 *
 * H5I__dec_ref__num_calls_without_global_mutex: Number of times that H5I__dec_ref() is 
 *      called by a thread that does not hold the global mutex.
 *
 * H5I__dec_ref__marked_on_entry: Number of times that the supplied ID is marked for
 *      deletion on entry.  Note that this should be almost impossible, as the 
 *      ID will have to be marked for deletion between the call to H5I__find_id() 
 *      and the check for the deleted flag early in the do/while loop.
 *
 * H5I__dec_ref__marked_during_call: Number of times that the supplied ID is marked for
 *      deletion by another thread after entry.
 *
 * H5I__dec_ref__marked: Number of times that H5I__dec_ref() successfully marks an 
 *      ID for deletion after reducing its ref count to zero.
 *
 * H5I__dec_ref__decremented: Number of times that H5I__dec_ref() successfully 
 *      decrements the ref count of an ID, but the resulting ref count is still 
 *      greater that zero.
 *
 * H5I__dec_ref__app_decremented: Number of times that H5I__dec_ref() successfully 
 *      decrements the application ref count of an ID, while also decrementing 
 *      the regular reference count.
 *
 * H5I__dec_ref__calls_to_free_func: Number of times that H5I__dec_ref() 
 *      calls the free_func()
 *
 * H5I__dec_ref__global_mutex_locks_for_free_func: Number of times that H5I__dec_ref()
 *      obtains the global mutext just prior to calling the free_func.
 *
 * H5I__dec_ref__global_mutex_unlocks_for_free_func:  Number of times that 
 *      H5I__dec_ref() drops the global mutext just after calling the free_func.
 *
 * H5I__dec_ref__free_func_failed; Number of times that H5I__dec_ref() calls the 
 *      free function on an ID, and that free function fails.
 *
 * H5I__dec_ref__retries: Number of times that H5I__dec_ref() has to retry the 
 *      operation.  Retries are caused by changes to the kernel between read 
 *      and attempt to overwrite with a modified version, and by encountering 
 *      a set do_not_disturb flag.
 *
 *
 * Statistics on the behavious of the H5I__inc_ref() function.
 *
 * H5I__inc_ref__num_calls: Number of times that H5I__inc_ref() is called
 *
 * H5I__inc_ref__num_app_calls: Number of times that H5I__inc_ref() is called
 *      with the app_ref parameter set.
 *
 * H5I__inc_ref__marked_on_entry:  Number of times that the supplied ID is marked for
 *      deletion on entry.  Note that this should be almost impossible, as the
 *      ID will have to be marked for deletion between the call to H5I__find_id() and 
 *      the check for the marked flag early in the do/while loop.
 *
 * H5I__inc_ref__marked_during_call: Number of times that the supplied ID is marked for
 *      deletion by another thread after entry.
 *
 * H5I__inc_ref__incremented: Number of times that H5I__inc_ref() successfully 
 *      increments the ref count on an ID.
 *
 * H5I__inc_ref__app_incremented: Number of times that H5I__inc_ref() successfully 
 *      increments the application ref count on an ID.
 *
 * H5I__inc_ref__retries: Number of times that H5I__inc_ref() has to retry the
 *      operation.  Retries are caused by changes to the kernel between read
 *      and attempt to overwrite with a modified version, and by encountering
 *      a set do_not_disturb flag.
 *
 *
 * Statistics on the behaviour of the H5I__iterate_cb() function.
 *
 * H5I__iterate_cb__num_calls: Number of times that H5I__iterate_cb() is called.
 *
 * H5I__iterate_cb__num_calls__with_global_mutex: Number of times that 
 *      H5I__iterate_cb() is called by a thread that holds the global mutex.
 *
 * H5I__iterate_cb__num_calls__without_global_mutex: Number of times that 
 *      H5I__iterate_cb() is called by a thread that doesn't hold the global mutex.
 *
 * H5I__iterate_cb__marked_during_call: Number of times that the ID supplied to
 *      H5I__iterate_cb() is marked for deletion by another thread during the 
 *      execution of the function.
 *
 * H5I__iterate_cb__num_user_func_calls: Number of times that H5I__iterate_cb() 
 *      calls the user_func.
 *
 * H5I__iterate_cb__global_mutex_locks_for_user_func: Number of times that 
 *      H5I__iterate_cb() obtains the global mutex before calling the 
 *      user func.
 *
 * H5I__iterate_cb__global_mutex_unlocks_for_user_func: Number of times that 
 *      H5I__iterate_cb() drops the global mutex immedaitely after calling the
 *      user func.
 *
 * H5I__iterate_cb__num_user_func_successes: Number of times that the supplied 
 *      user function is called and returns success.
 *
 * H5I__iterate_cb__num_user_func_iter_stops: Number of times that the supplied
 *      user function is called and halts the itteration.
 *
 * H5I__iterate_cb__num_user_func_fails: Number of times that the supplied
 *      user function is called and returns failure -- also halting the 
 *      itteration.
 *
 * H5I__iterate_cb__num_user_func_skips: Number of times that H5I__iterate_cb()
 *      skips any attempt to call the user function.
 *
 * H5I__iterate_cb__num_retries: Number of times that H5I__iterate_cb() has to
 *      retry the operation.
 *
 *
 * Statistics on the behaviour of the H5I__unwrap() function.
 *
 * H5I__unwrap__num_calls: Number of times that H5I__unwrap() is called.
 *
 * H5I__unwrap__num_calls_with_global_mutex:  Number of times that H5I__unwrap() 
 *      is called by a thread that holds the global mutex.
 *
 * H5I__unwrap__num_calls_without_global_mutex:  Number of times that H5I__unwrap() 
 *      is called by a thread that does not hold the global mutex.
 *
 * H5I__unwrap__times_global_mutex_locked_for_H5VL:  Number of times that 
 *      H5I__unwrap() has locked the global mutex prior to a call to 
 *      H5VL_object_data().
 *
 * H5I__unwrap__times_global_mutex_unlocked_for_H5VL:  Number of times that 
 *      H5I__unwrap() has unlocked the global mutex after a call to 
 *      H5VL_object_data().
 *
 * H5I__unwrap__times_global_mutex_locked_for_H5T:  Number of times that 
 *      H5I__unwrap() has locked the global mutex prior to a call to 
 *      H5T_get_actual_type().
 *
 * H5I__unwrap__times_global_mutex_unlocked_for_H5T:  Number of times that 
 *      H5I__unwrap() has unlocked the global mutex after a call to 
 *      H5T_get_actual_type().
 *
 * 
 * Statistics on the behaviour of the H5I_is_file_object() function.
 *
 * H5I_is_file_object__num_calls: Number of times that H5I_is_file_object() is 
 *      called.
 *
 * H5I_is_file_object__num_calls_to_H5T_is_named: Number of times that 
 *      H5I_is_file_object() calls H5T_is_named().
 *
 * H5I_is_file_object__global_mutex_locks_for_H5T_is_named:  Number of times that
 *      H5I_is_file_object() obtains the global must just prior to a call to 
 *      H5T_is_named().
 *
 * H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named:i Number of times that
 *      H5I_is_file_object() drops the global must just after to a call to
 *      H5T_is_named().
 *
 *
 * Statistics on operations on the do not disturb flag.  See the discussion of 
 * the this fields and the kernel in the comments on H5I_mt_id_info_t for further 
 * details.
 *
 * num_do_not_disturb_yields: Number of times a thread encounters a H5I_mt_id_info_t
 *      kernel with the do_not_disturb flag set, and retries as a result.
 *
 * num_successful_do_not_disturb_sets: Number of times that a thread successfully
 *      attempts to set the do_not_disturb flag on the kernel of an instance of 
 *      H5I_mt_id_info_t.
 *
 * num_failed_do_not_disturb_sets: Number of times that a thread unsuccessfully
 *      attempts to set the do_not_disturb flag on the kernel of an instance of 
 *      H5I_mt_id_info_t.
 *
 * num_do_not_disturb_resets: Number of time that a thread resets the do_not_disturb
 *      flag on the kernel of an instance of H5I_mt_id_info_t.  Note that in theory,
 *      this operation will always succeed.  If it doesn't, it should trigger an 
 *      assertion failure.
 *
 *
 * Statistics on H5I entries and numbers of threads active in H5I.
 *
 * num_H5I_entries_via_public_API: Number of times that H5I has been entered via 
 *      public API calls.
 *
 * num_H5I_entries_via_internal_API: Number of times that H5I has been entered via
 *      private API calls.  Note that H5I public API functions call private H5I APIs,
 *      and that some private API calls are called from within H5I.  Thus this 
 *      number is typically overstated.
 *
 * max_active_threads: Maximum number of threads active in H5I at any point in time.
 *      Note that due to the number of internal and external API entry points, this 
 *      value will likely be overstated.
 *
 * times_active_threads_is_zero:  Number of times that the number of threads active 
 *      in H5I drops to zero.
 *
 ****************************************************************************************/

#define H5I__MAX_DESIRED_ID_INFO_FL_LEN    256ULL
#define H5I__MAX_DESIRED_TYPE_INFO_FL_LEN   16ULL

typedef struct H5I_mt_type_info_t H5I_mt_type_info_t;  /* forward declaration */

typedef struct H5I_mt_t {

    /* Pre-existing Globals: */
    _Atomic (H5I_mt_type_info_t *) type_info_array[H5I_MAX_NUM_TYPES];
    _Atomic hbool_t type_info_allocation_table[H5I_MAX_NUM_TYPES];
    _Atomic int next_type;
    _Atomic int marking;

    /* New Globals: */
    _Atomic uint32_t active_threads;

    _Atomic H5I_mt_id_info_sptr_t   id_info_fl_shead;
    _Atomic H5I_mt_id_info_sptr_t   id_info_fl_stail;
    _Atomic uint64_t                id_info_fl_len;
    _Atomic uint64_t                max_desired_id_info_fl_len; /* Ray changed it to atomic */

    _Atomic H5I_mt_type_info_sptr_t type_info_fl_shead;
    _Atomic H5I_mt_type_info_sptr_t type_info_fl_stail;
    _Atomic uint64_t                type_info_fl_len;
    _Atomic uint64_t                max_desired_type_info_fl_len; /* Ray changed it to atomic */

    /* Statistics: */

    _Atomic hbool_t dump_stats_on_shutdown;
    
    /* type registration stats */
    _Atomic uint64_t init_type_registrations;
    _Atomic uint64_t duplicate_type_registrations;
    _Atomic uint64_t type_registration_collisions;

    /* id info free list stats */
    _Atomic uint64_t max_id_info_fl_len;
    _Atomic uint64_t num_id_info_structs_alloced_from_heap;
    _Atomic uint64_t num_id_info_structs_alloced_from_fl;
    _Atomic uint64_t num_id_info_structs_freed;
    _Atomic uint64_t num_id_info_structs_added_to_fl;
    _Atomic uint64_t num_id_info_fl_head_update_cols;
    _Atomic uint64_t num_id_info_fl_tail_update_cols;
    _Atomic uint64_t num_id_info_fl_append_cols;
    _Atomic uint64_t num_id_info_structs_marked_reallocatable;
    _Atomic uint64_t num_id_info_fl_alloc_req_denied_due_to_empty;
    _Atomic uint64_t num_id_info_fl_alloc_req_denied_due_to_head_not_reallocable;
    _Atomic uint64_t num_id_info_fl_frees_skipped_due_to_empty;
    _Atomic uint64_t num_id_info_fl_frees_skipped_due_to_head_not_reallocable;

    /* type info free list stats */
    _Atomic uint64_t max_type_info_fl_len;
    _Atomic uint64_t num_type_info_structs_alloced_from_heap;
    _Atomic uint64_t num_type_info_structs_alloced_from_fl;
    _Atomic uint64_t num_type_info_structs_freed;
    _Atomic uint64_t num_type_info_structs_added_to_fl;
    _Atomic uint64_t num_type_info_fl_head_update_cols;
    _Atomic uint64_t num_type_info_fl_tail_update_cols;
    _Atomic uint64_t num_type_info_fl_append_cols;
    _Atomic uint64_t num_type_info_structs_marked_reallocatable;
    _Atomic uint64_t num_type_info_fl_alloc_req_denied_due_to_empty;
    _Atomic uint64_t num_type_info_fl_alloc_req_denied_due_to_head_not_reallocable;
    _Atomic uint64_t num_type_info_fl_frees_skipped_due_to_empty;
    _Atomic uint64_t num_type_info_fl_frees_skipped_due_to_head_not_reallocable;

    /* H5I__mark_node() stats */
    _Atomic uint64_t H5I__mark_node__num_calls;
    _Atomic uint64_t H5I__mark_node__num_calls_with_global_mutex;
    _Atomic uint64_t H5I__mark_node__num_calls_without_global_mutex;
    _Atomic uint64_t H5I__mark_node__already_marked;
    _Atomic uint64_t H5I__mark_node__marked;
    _Atomic uint64_t H5I__mark_node__marked_by_another_thread;
    _Atomic uint64_t H5I__mark_node__no_ops;
    _Atomic uint64_t H5I__mark_node__global_mutex_locks_for_discard_cb;
    _Atomic uint64_t H5I__mark_node__global_mutex_unlocks_for_discard_cb;
    _Atomic uint64_t H5I__mark_node__discard_cb_failures_marked;
    _Atomic uint64_t H5I__mark_node__discard_cb_failures_unmarked;
    _Atomic uint64_t H5I__mark_node__discard_cb_successes;
    _Atomic uint64_t H5I__mark_node__global_mutex_locks_for_free_func;
    _Atomic uint64_t H5I__mark_node__global_mutex_unlocks_for_free_func;
    _Atomic uint64_t H5I__mark_node__free_func_failures_marked;
    _Atomic uint64_t H5I__mark_node__free_func_failures_unmarked;
    _Atomic uint64_t H5I__mark_node__free_func_successes;
    _Atomic uint64_t H5I__mark_node__retries;

    /* H5I__remove_common() stats */
    _Atomic uint64_t H5I__remove_common__num_calls;
    _Atomic uint64_t H5I__remove_common__already_marked;
    _Atomic uint64_t H5I__remove_common__marked_by_another_thread;
    _Atomic uint64_t H5I__remove_common__marked;
    _Atomic uint64_t H5I__remove_common__target_not_in_lfht;
    _Atomic uint64_t H5I__remove_common__retries;

    /* H5I__find_id() stats */
    _Atomic uint64_t H5I__find_id__num_calls;

    _Atomic uint64_t H5I__find_id__num_calls_with_global_mutex;
    _Atomic uint64_t H5I__find_id__num_calls_without_global_mutex;

    _Atomic uint64_t H5I__find_id__ids_found;

    _Atomic uint64_t H5I__find_id__num_calls_to_realize_cb;
    _Atomic uint64_t H5I__find_id__global_mutex_locks_for_realize_cb;
    _Atomic uint64_t H5I__find_id__global_mutex_unlocks_for_realize_cb;
    _Atomic uint64_t H5I__find_id__num_calls_to_discard_cb;
    _Atomic uint64_t H5I__find_id__global_mutex_locks_for_discard_cb;
    _Atomic uint64_t H5I__find_id__global_mutex_unlocks_for_discard_cb;

    _Atomic uint64_t H5I__find_id__future_id_conversions_attempted;
    _Atomic uint64_t H5I__find_id__future_id_conversions_completed;
    _Atomic uint64_t H5I__find_id__retries;

    /* H5I_register_using_existing_id() stats */
    _Atomic uint64_t H5I_register_using_existing_id__num_calls;
    _Atomic uint64_t H5I_register_using_existing_id__num_marked_only;
    _Atomic uint64_t H5I_register_using_existing_id__num_id_already_in_use;
    _Atomic uint64_t H5I_register_using_existing_id__num_failures;

    /* H5I_subst() stats */
    _Atomic uint64_t H5I_subst__num_calls;
    _Atomic uint64_t H5I_subst__marked_on_entry;
    _Atomic uint64_t H5I_subst__marked_during_call;
    _Atomic uint64_t H5I_subst__retries;
    _Atomic uint64_t H5I_subst__failures;

    /* H5I__dec_ref() stats */
    _Atomic uint64_t H5I__dec_ref__num_calls;
    _Atomic uint64_t H5I__dec_ref__num_app_calls;
    _Atomic uint64_t H5I__dec_ref__num_calls_with_global_mutex;
    _Atomic uint64_t H5I__dec_ref__num_calls_without_global_mutex;
    _Atomic uint64_t H5I__dec_ref__marked_on_entry;
    _Atomic uint64_t H5I__dec_ref__marked_during_call;
    _Atomic uint64_t H5I__dec_ref__marked;
    _Atomic uint64_t H5I__dec_ref__decremented;
    _Atomic uint64_t H5I__dec_ref__app_decremented;
    _Atomic uint64_t H5I__dec_ref__calls_to_free_func;
    _Atomic uint64_t H5I__dec_ref__global_mutex_locks_for_free_func;
    _Atomic uint64_t H5I__dec_ref__global_mutex_unlocks_for_free_func;
    _Atomic uint64_t H5I__dec_ref__free_func_failed;
    _Atomic uint64_t H5I__dec_ref__retries;

    /* H5I__inc_ref() stats */
    _Atomic uint64_t H5I__inc_ref__num_calls;
    _Atomic uint64_t H5I__inc_ref__num_app_calls;
    _Atomic uint64_t H5I__inc_ref__marked_on_entry;
    _Atomic uint64_t H5I__inc_ref__marked_during_call;
    _Atomic uint64_t H5I__inc_ref__incremented;
    _Atomic uint64_t H5I__inc_ref__app_incremented;
    _Atomic uint64_t H5I__inc_ref__retries;

    /* H5I__iterate_cb() stats */
    _Atomic uint64_t H5I__iterate_cb__num_calls;
    _Atomic uint64_t H5I__iterate_cb__num_calls__with_global_mutex;
    _Atomic uint64_t H5I__iterate_cb__num_calls__without_global_mutex;
    _Atomic uint64_t H5I__iterate_cb__marked_during_call;
    _Atomic uint64_t H5I__iterate_cb__num_user_func_calls;
    _Atomic uint64_t H5I__iterate_cb__global_mutex_locks_for_user_func;
    _Atomic uint64_t H5I__iterate_cb__global_mutex_unlocks_for_user_func;
    _Atomic uint64_t H5I__iterate_cb__num_user_func_successes;
    _Atomic uint64_t H5I__iterate_cb__num_user_func_iter_stops;
    _Atomic uint64_t H5I__iterate_cb__num_user_func_fails;
    _Atomic uint64_t H5I__iterate_cb__num_user_func_skips;
    _Atomic uint64_t H5I__iterate_cb__num_retries;
 
    /* H5I__unwrap() stats */
    _Atomic uint64_t H5I__unwrap__num_calls;
    _Atomic uint64_t H5I__unwrap__num_calls_with_global_mutex;
    _Atomic uint64_t H5I__unwrap__num_calls_without_global_mutex;
    _Atomic uint64_t H5I__unwrap__times_global_mutex_locked_for_H5VL;
    _Atomic uint64_t H5I__unwrap__times_global_mutex_unlocked_for_H5VL;
    _Atomic uint64_t H5I__unwrap__times_global_mutex_locked_for_H5T;
    _Atomic uint64_t H5I__unwrap__times_global_mutex_unlocked_for_H5T;
 
    /* H5I_is_file_object() stats */
    _Atomic uint64_t H5I_is_file_object__num_calls;
    _Atomic uint64_t H5I_is_file_object__num_calls_to_H5T_is_named;
    _Atomic uint64_t H5I_is_file_object__global_mutex_locks_for_H5T_is_named;
    _Atomic uint64_t H5I_is_file_object__global_mutex_unlocks_for_H5T_is_named;

    /* do not disturb flag stats */
    _Atomic uint64_t num_do_not_disturb_yields;
    _Atomic uint64_t num_successful_do_not_disturb_sets;
    _Atomic uint64_t num_failed_do_not_disturb_sets;
    _Atomic uint64_t num_do_not_disturb_resets;

    /* active_threads stats */
    _Atomic uint64_t num_H5I_entries_via_public_API;
    _Atomic uint64_t num_H5I_entries_via_internal_API;
    _Atomic uint64_t max_active_threads;
    _Atomic uint64_t times_active_threads_is_zero;

} H5I_mt_t; 

/************************************************************************************ 
 * 
 * struct H5I_mt_id_info_t 
 * 
 * H5I_mt_id_info_t is a re-write of H5_id_info_t with modifications to facilitate 
 * the multi-thread version of H5I. 
 * 
 * As such, most of the fields will be familiar from H5_id_info_t. 
 * 
 * Note that most of these are gathered together into a single, atomic sub-structure, 
 * to allow atomic operations on the the id info. 
 * 
 * The remaining fields are either constant during the life of an instance of 
 * H5I_mt_id_info_t, or exist to support the free list that a deleted instance of 
 * H5I_mt_id_info_t must reside on until we are sure that no thread retains a pointer 
 * to it. 
 *
 * The fields of H5I_mt_id_info_t are discussed individually below.  
 * 
 * tag: unsigned int 32 set to H5I__ID_INFO when allocated, and to 
 *      H5I__id_INFO_INVALID just before the instance of H5I_mt_id_info_t 
 *      is deallocated. 
 * 
 * id:  ID associated with this instance of H5I_mt_id_info_t.  This is the id used to 
 *      locate the instance in the lock free hash table. 
 * 
 * 
 * k:   The non-MT version of H5I_mt_id_info_t has a number of variables that must be
 *      kept in synchronization.  The obvious way of doing this would be to protect
 *      them with a mutex.  However, it seems best to avoid locking to the extent
 *      possible so as to avoid lock ordering considerations. 
 * 
 *      This leads to the option of encapsulating the variables in a single atomic
 *      structure, the kernel for short.  In this case, the kernel must be read
 *      atomically, modified,  and written back atomically with a
 *      compare_exchange_strong().  In the event of failure in the
 *      compare_exchange_strong(), the procedure must be repeated
 *      until it is successful, or the point becomes moot – for example if the id info
 *      is marked as being deleted. 
 * 
 *      The hidden assumption here is that operations on the encapsulated variables
 *      can be rolled back if the compare_exchange_strong() fails.  This is clearly
 *      true with ref counts, and with overwrites of the pointer to the object
 *      associated with the id info.  If we further view an id as being functionally
 *      deleted once is it marked as deleted, in principle, this should be true of
 *      deletions as well, as we should be able to do the remainder of the cleanup
 *      at leisure. 
 *
 *      By similar logic, this should include the discard of un-realized future IDs, 
 *      as once the marked flag is set, no other will modify the kernel (see step 2 in
 *      the protocols below). 
 * 
 *      Unfortunately, the serial version of H5I doesn't work this way.  In 
 *      particular, in H5I__mark_node(), if either the discard_cb (in the case of a 
 *      future ID) or the free_func (in the case of a regular ID) fails, and the 
 *      force flag is not set, the target ID is not marked for deletion, and the 
 *      (possibly corrupt) buffer pointed to by the object field is left in the 
 *      index.  This seems a questionable design choice, but until we have a working
 *      prototype, going with it seems to be the best option.  Obviously, it should
 *      be revisited once that point is reached. 
 * 
 *      Conversions from future to real IDs present a similar problem, as the 
 *      conversion may fail, and cannot be rolled back.  Further, this operation may 
 *      be attempted repeatedly until it succeeds. 
 * 
 *      To square this circle, we need a mechanism for serializing callbacks, and for 
 *      ensuring that operations that can't be rolled back can't be interrupted.
 * 
 *      We do this by adding two flags to the kernel -- the already defined 
 *      marked flag, and the new do_not_disturb flag, and then proceeding by the 
 *      appropriate protocol as given below. 
 * 
 *      In the cases where roll backs are possible, proceed as follows: 
 * 
 *       1) Load the kernel. 
 * 
 *       2) Check to see if the marked flag is set.  If so, issue an ID doesn’t exist 
 *          error and return. 
 * 
 *       3) Check to see if the do_not_disturb flag is set.  If so, do a thread yield 
 *          or sleep, and return to 1) above. 
 *  
 *       4) Perform the desired operations (i.e ref count increment or decrement, or 
 *          object pointer overwite on the local copy of the kernel.  If the ref 
 *          count drops to zero, set the marked flag on the local copy of the kernel. 
 * 
 *       5) Attempt to overwrite the global copy of the kernel with the local copy 
 *          via a compare_exchange_strong().  If this succeeds, we are done.  
 *          Otherwise roll back the operation, and return to 1. 
 * 
 *          Note no thread yield or sleep in this case, as this will typically be 
 *          another thread that jumped in and modified the kernel.  If the 
 *          do_not_disturb flag is set, we will hit it on the next pass. 
 * 
 *      Note that this thread yield or sleep and then re-try approach is also 
 *      used in the lock free hash table to handle a very unlikely collisions without 
 *      the use of locks in the lock free hash table. 
 * 
 *      For operations that can't be rolled back (i.e. realization or discard of 
 *      future IDs, ID frees, etc), the above procedure is modified as follows:
 * 
 *       1) Load the kernel. 
 * 
 *       2) Check to see if the marked flag is set.  If so, issue an ID doesn’t exist 
 *          error and return. 
 * 
 *       3) Check to see if the do_not_disturb flag is set.  If so, do a thread yield 
 *          or sleep, and return to 1) above. 
 * 
 *       4) Check to see if the desired operation is still pending (i.e. if the 
 *          operation is converting a future ID to a real ID, is the is_future flag 
 *          still TRUE?). If it isn't, some other thread has already performed the 
 *          operation so we can exit with success. 
 * 
 *          Otherwise: 
 * 
 *       5) Set the do_not_disturb flag in the local copy of the kernel, and attempt 
 *          to overwrite the global copy of the kernel with the local copy via a 
 *          compare_exchange_strong(). 
 * 
 *          If this fails, do a thread yield or sleep, and return to 1. 
 * 
 *          If it succeeds, we know that we have exclusive access to the kernel until 
 *          we reset the do_not_disturb flag on the global copy, as no new thread 
 *          looking at the kernel will proceed beyond reading the flag, and the 
 *          compare_exchange_strong() of any existing thread attempting to modify the 
 *          kernel will fail -- sending it back to step 1. 
 * 
 *       6) Attempt to perform the desired operation. 
 * 
 *          If this fails, reset the do_not_disturb flag in the local copy of the 
 *          kernel, overwrite the global copy of the kernel with the local copy via a 
 *          compare_exchange_strong(), and report the failure of the operation if
 *          appropriate.
 * 
 *          Observe that the call to compare_exchange_strong() must succeed, per the 
 *          argument given in the final paragraph in 5 above.  Note that we use 
 *          compare_exchange_strong() in this case only as a sanity check.  Assuming  
 *          my analysis is correct, we could simply use atomic_store() on  
 *          architectures where compare_exchange_strong() is not available.   
 *          Unfortunately, the rest of the algorithm does depend on 
 *          compare_exchange_strong(), so it will have to be re-worked for those
 *          architectures. 
 * 
 *          If the operation succeeds, update the kernel accordingly, reset the 
 *          do_not_disturb flag in the local copy of the kernel, and overwrite the 
 *          global copy of the  kernel with the local copy via a 
 *          compare_exchange_strong().  As before this operation must succeed, and we
 *          are done. 
 * 
 *      An atomic instance of H5I_mt_id_info_kernel_t is used to instantiate the 
 *      kernel mentioned above.  It maintains its fields as a single atomic object. 
 *      As the size of this structure is too large for true atomic operations, C11 
 *      maintains atomicity via mutexes.  This hurts performance, but since the 
 *      objective is to avoid explicit locking (and thus lock ordering concerns) this 
 *      is fine -- for now at least. 
 *
 *      Note that if we combined all the booleans in a flags field, 
 *      and reduced the size of the count and app_count integers, we could fit the 
 *      H5I_mt_id_info_kernel_t into 128 bytes, allowing true atomic operation on 
 *      many (most) more modern CPUs.  However, that is an optimization for another 
 *      day, as is re-working the future ID feature into something more multi-thread
 *      friendly.
 * 
 *      Since H5I_mt_id_info_kernel_t is only used either in H5I_mt_id_info_t, or to 
 *      stage reads and writes of the kernal in that structure, its fields are 
 *      discussed here. 
 * 
 *      k.count: Reference count on this ID.  This is typically the number of 
 *           references to the ID elsewhere in the HDF5 library.  This ref count is 
 *           used to prevent deletion of the id (and the associated instance of 
 *           H5I_mt_id_info_t until all references have been dropped. 
 * 
 *      k.app_count: Application reference count on this ID.  This allows the 
 *           application to prevent deletion of this ID (under most circumstances) 
 *           until all its references to the ID have been dropped. 
 * 
 *      k.object: Pointer to void.  Points to the data (if any) associated with
 *           this ID. 
 * 
 *      k.marked: Boolean flag indicating whether this instance of H5I_mt_id_info_t 
 *           has been marked for deletion.  Once set, this flag is never re-set, and 
 *           any ID for which this flag is set must be viewed as logically deleted, 
 *           even though the actual removal from the lock free has table and deletion 
 *           may occur later. 
 * 
 *      k.do_not_disturb: Boolean flag.  When set, a thread that needs to perform 
 *           an operation on the ID that can't be rolled back is in progress.  All 
 *           other threads must wait until this operation completes (see discussion
 *           above). 
 * 
 *      k.is_future: Boolean flag indicating whether this ID represents a future ID. 
 * 
 *      k.have_global_mutex: Boolean flag that should be set to TRUE when 
 *           k.do_not_disturb is set to TRUE, and the setting thread has the HDF5
 *           global mutex at the time.
 * 
 *           This field is a temporary hack designed allow HDF5 callbacks to access 
 *           the index without deadlocking.  Thus, when the do_not_disturb flag is 
 *           detected, it can be ignored if the have_global_mutex flag is set and the 
 *           current thread has the global mutext.
 *
 *           It will almost certainly be replace with a thread ID stored in
 *           H5I_mt_id_info_t proper, and set whenever k.do_not_disturb is set.  While
 *           this will make k.do_not_disturb into a recursive lock, it will also 
 *           require additional logic to allow for the possibility that the kernel 
 *           has been modified while the k.do_not_disturb flag is set.
 * 
 *      If we followed the single thread version of H5I exactly, the realize_cb and 
 *      discard_cb would have to be atomic since they are set to NULL when is_future 
 *      is set to FALSE.  However, that doesn't seem necessary, so the are non-atomic 
 *      fields in H5I_mt_id_info_t.  This should be OK, as the only time they are
 *      modified is when the instance of H5I_mt_id_info_t is being initialized prior 
 *      to insertion into the index.  Since only one thread has access at that point, 
 *      leaving them as regular fields should work.  However, if compilers start 
 *      optimizing across function boundaries, this will have to be re-visited. 
 *
 *      More generally, note that the above is a bit of a kluge to accommodate the 
 *      current implementation of future IDs, and more generally, to accommodate call
 *      backs that can fail and/or can’t be rolled back.
 *
 *      While we are probably stuck with the current callbacks for the native VOL
 *      for the foreseeable future, new, more multi-thread friendly versions of the
 *      H5I callbacks should be developed.
 *
 *      Finally, note that while we have technically managed to avoid locks, the
 *      do_not_disturb flag is effectively a lock which will have to be made
 *      recursive.  Its main virtue is its near total lack of overhead in cases
 *      where locking is not required.  Hopefully, this will make up for its other
 *      sins.
 * 
 * realize_cb: 'realize' callback for future object.  
 * 
 * discard_cb: 'discard' callback for future object. 
 * 
 * 
 * Fields supporting the H5I_mt_id_info_t free list: 
 * 
 * on_fl: Atomic boolean flag that is set to TRUE when the instance of  
 *      H5I_mt_id_info_t is place on the id info free list, and to FALSE on initial  
 *      allocation from the heap, or when the instance is allocated from the free
 *      list. 
 * 
 * re_allocable:  Atomic boolean flag that is set to FALSE on allocation from the   
 *      heap or from the free list.  It is set to TRUE if the entry is on the free    
 *      list and it is known that it is no longer on the lock free hash table, and  
 *      no thread currently in H5I has a pointer to it. 
 * 
 * fl_snext: Atomic instance of H5I_mt_id_info_sptr_t used in the maintenance of the 
 *      id info free list.  The structure contains both a pointer and a serial   
 *      number, which facilitates the avoidance of ABA bugs when managing the free
 *      list. 
 * 
 ************************************************************************************/

#define H5I__ID_INFO            0x1010 /* 4112 */
#define H5I__ID_INFO_INVALID    0x2020 /* 8224 */

typedef struct H5I_mt_id_info_kernel_t {

    unsigned                  count;      /* Ref. count for this ID */
    unsigned                  app_count;  /* Ref. count of application visible IDs */
    const void              * object;     /* Pointer associated with the ID */

    hbool_t                   marked;     /* Marked for deletion */
    hbool_t                   do_not_disturb;  
    hbool_t                   is_future;  /* Whether this ID represents a future object */
    hbool_t                   have_global_mutex; 

} H5I_mt_id_info_kernel_t;

typedef struct H5I_mt_id_info_t {

    uint32_t tag;

    hid_t id;

    _Atomic H5I_mt_id_info_kernel_t k;

    /* Future ID callbacks */
    H5I_future_realize_func_t realize_cb; /* 'realize' callback for future object */
    H5I_future_discard_func_t discard_cb; /* 'discard' callback for future object */

    _Atomic hbool_t on_fl;

    _Atomic hbool_t re_allocable;

    _Atomic H5I_mt_id_info_sptr_t fl_snext;

} H5I_mt_id_info_t;


/****************************************************************************************
 *
 * struct H5I_mt_type_info_t
 *
 * H5I_mt_type_info_t is a re-write of H5_type_info_t with modifications to facilitate the  
 * multi-thread version of H5I. 
 *
 * As such, most of the fields will be familiar from H5_type_info_t.  
 *
 * tag: unsigned int 32 set to H5I__TYPE_INFO when allocated, and to H5I__TYPE_INFO_INVALID
 *      just before the instance of H5I_mt_id_info_t is deallocated.
 *
 * cls: Pointer to the ID class.
 *
 * init_count: Number of times this type has been initialized less the number of times
 *      its reference count has been decremented.
 *
 * id_count: Current number of IDs in the type.
 *
 * next_id: ID to be allocated to the next object inserted into the index.
 *
 * last_id_info: Pointer to the instance of H5I_mt_id_info_t associated with the last 
 *      ID accessed in the index.  Note that it is possible for this pointer to be NULL,
 *      or for it to point (briefly) to and instance of H5I_mt_id_info_t that has been 
 *      marked for deletion.
 *
 * lfht_cleared: Boolean flag that is set to TRUE when the lock free hash table associated
 *      with the index is cleared in preparation for deletion.
 *
 * lfht: The instance of lfht_t that forms the root of the lock free hash table in which 
 *      all objects in the index are stored, and which supports the look up of the 
 *      instance of H5I_mt_id_info_t associated with any given ID.
 *
 *      Note that the lock free hash table may contain pointers to instances of 
 *      H5I_mt_id_info_t that have been marked for deletion.  Such entries and their
 *      associated IDs have been logically deleted from the index, even their associated
 *      instances of H5I_mt_id_info_t remain in the lock free hash table.
 *
 *
 * Fields supporting the H5I_mt_type_info_t free list:
 *
 * on_fl: Atomic boolean flag that is set to TRUE when the instance of H5I_mt_type_info_t
 *      is place on the type info free list, and to FALSE on initial allocation from the 
 *      heap, or when the instance is allocated from the free list.
 *
 * re_allocable:  Atomic boolean flag that is set to FALSE on allocation from the heap
 *      or from the free list.  It is set to TRUE if the entry is on the free list and 
 *      it is known that it is no longer on the lock free hash table, and no thread
 *      currently in H5I has a pointer to it.
 *
 * fl_snext: Atomic instance of H5I_mt_type_info_sptr_t used in the maintenance of the 
 *      type info free list.  The structure contains both a pointer and a serial number,
 *      which facilitates the avoidance of ABA bugs when managing the free list.
 * 
 ****************************************************************************************/

#define H5I__TYPE_INFO            0x1011 /* 4113 */
#define H5I__TYPE_INFO_INVALID    0x2021 /* 8225 */

typedef struct H5I_mt_type_info_t {
    uint32_t                        tag;
    const H5I_class_t             * cls;          /* Pointer to ID class */
    _Atomic unsigned                init_count;   /* # of times this type has been initialized */
    _Atomic uint64_t                id_count;     /* Current number of IDs held */
    _Atomic uint64_t                nextid;       /* ID to use for the next object */
    H5I_mt_id_info_t * _Atomic      last_id_info; /* Info for most recent ID looked up */
    _Atomic hbool_t                 lfht_cleared; /* TRUE iff the lock free hash table has been cleared 
                                                   * in prep for deletion */
    lfht_t                          lfht;         /* lock free hash table for this ID type */
    _Atomic hbool_t                 on_fl;
    _Atomic hbool_t                 re_allocable;
    _Atomic H5I_mt_type_info_sptr_t fl_snext;
} H5I_type_info_t;

#else /* H5_HAVE_MULTITHREAD */ /********************************************************************************/

/* ID information structure used */
typedef struct H5I_id_info_t {
    hid_t       id;        /* ID for this info */
    unsigned    count;     /* Ref. count for this ID */
    unsigned    app_count; /* Ref. count of application visible IDs */
    const void *object;    /* Pointer associated with the ID */

    /* Future ID info */
    hbool_t                   is_future;  /* Whether this ID represents a future object */
    H5I_future_realize_func_t realize_cb; /* 'realize' callback for future object */
    H5I_future_discard_func_t discard_cb; /* 'discard' callback for future object */

    /* Hash table ID fields */
    hbool_t        marked; /* Marked for deletion */
    UT_hash_handle hh;     /* Hash table handle (must be LAST) */
} H5I_id_info_t;

/* Type information structure used */
typedef struct H5I_type_info_t {
    const H5I_class_t *cls;          /* Pointer to ID class */
    unsigned           init_count;   /* # of times this type has been initialized */
    uint64_t           id_count;     /* Current number of IDs held */
    uint64_t           nextid;       /* ID to use for the next object */
    H5I_id_info_t     *last_id_info; /* Info for most recent ID looked up */
    H5I_id_info_t     *hash_table;   /* Hash table pointer for this ID type */
} H5I_type_info_t;

#endif /* H5_HAVE_MULTITHREAD */ /*******************************************************************************/

/*****************************/
/* Package Private Variables */
/*****************************/

/* Array of pointers to ID types */
#if H5_HAVE_MULTITHREAD 

/* This structure contains cognates of H5I_type_info_array_g and H5I_next_type_g globals, 
 * additional global variables need for the multi-thread version of H5I, and statistics.
 */
H5_DLLVAR H5I_mt_t H5I_mt_g;

#else /* H5_HAVE_MULTITHREAD */

H5_DLLVAR H5I_type_info_t *H5I_type_info_array_g[H5I_MAX_NUM_TYPES];

/* Variable to keep track of the number of types allocated.  Its value is the
 * next type ID to be handed out, so it is always one greater than the number
 * of types.
 * Starts at 1 instead of 0 because it makes trace output look nicer.  If more
 * types (or IDs within a type) are needed, adjust TYPE_BITS in H5Ipkg.h
 * and/or increase size of hid_t
 */
H5_DLLVAR int H5I_next_type_g;
#endif /* H5_HAVE_MULTITHREAD */

/******************************/
/* Package Private Prototypes */
/******************************/

H5_DLL hid_t          H5I__register(H5I_type_t type, const void *object, hbool_t app_ref,
                                    H5I_future_realize_func_t realize_cb, H5I_future_discard_func_t discard_cb);
H5_DLL int            H5I__destroy_type(H5I_type_t type);
H5_DLL void          *H5I__remove_verify(hid_t id, H5I_type_t type);
H5_DLL int            H5I__inc_type_ref(H5I_type_t type);
H5_DLL int            H5I__get_type_ref(H5I_type_t type);
#if H5_HAVE_MULTITHREAD
H5_DLL H5I_mt_id_info_t *H5I__find_id(hid_t id);
H5_DLL void H5I__enter(hbool_t public_api);
H5_DLL void H5I__exit(void);
#else /* H5_HAVE_MULTITHREAD */
H5_DLL H5I_id_info_t *H5I__find_id(hid_t id);
#endif /* H5_HAVE_MULTITHREAD */

/* Testing functions */
#ifdef H5I_TESTING
H5_DLL ssize_t H5I__get_name_test(hid_t id, char *name /*out*/, size_t size, hbool_t *cached);
#endif /* H5I_TESTING */

#endif /*H5Ipkg_H*/
