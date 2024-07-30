#include "h5test.h"
#include "H5Iprivate.h"
#define H5I_FRIEND    /*suppress error about including H5Ipkg      */
#include "H5Ipkg.h"

#ifdef H5_HAVE_MULTITHREAD
#include <stdatomic.h>

/*********************************************************************************
 * struct id_type_t
 *
 * An array of instance of id_type_t is used to track the expected state of 
 * types in H5I in multi-thread tests.  The fields of this structure are 
 * discussed individually below.
 *
 * Note that id_type_kernel_t is included in this section, as it gathers together
 * all fields of id_type_t that must be handled as a single atomic unit.
 *
 * tag:         Integer field that must be set to ID_TYPE_T__TAG.  This field
 *              is used to sanity check pointers.
 *
 * index:       integer field containing the index of this instance of 
 *              id_type_t in its containing array.
 *
 * k            Atomic instance of id_type_kernel_t -- a structure that contains
 *              all fields in id_type_t that must be treated as a single atomic
 *              value.  These fields are discussed individually below:
 *
 * k.in_progress:  Creation of an id type is not atomic.  This field is set 
 *              when a thread is about to create an ID.  If successful, it 
 *              may proceed with the creation, and update kernel again when
 *              done.
 *
 * k.created:   Boolean flag that is initialized to FALSE, and set to TRUE
 *              when the type is created.  Due to k being an atomic structure
 *              and the above k.inprogress, this initialization must be done
 *              by a single process.
 *
 * k.discarded: Boolean flag that is initialized to FALSE, and set to TRUE
 *              when the type is discarded.
 *
 * k.buffer_bool: Padding to bring the size of id_type_t up to 16 bytes.
 *              
 *
 * k.buffer_int: Padding to bring the size of id_type_t up to 16 bytes.
 *
 * k.type_id:   Initialized to H5I_BADID, and set to the ID assigned to the type 
 *              when it is created.
 *
 * mt_safe:     Boolean flag indicating whether the free function for all IDs in 
 *              the type, and the realize and discard callback for future IDs in
 *              the type are multi-thread safe.
 *
 * free_func:   Pointer to the free function that is applied to all IDs
 *              in the type when they are discarded.
 *
 *
 * Stats:
 *
 * successful_registers: Number of times the type has been registered successfully.
 *
 * failed_registers: Number of failed attempts to register the type.
 *
 * successful_clears: Number of times the type has been cleared successfully.
 *
 * failed_clears: Number of times that a clear of the type has failed.
 *
 * successful_destroys: Number of times that the type has been destroyed 
 *              successfully.
 *
 * failed_destroys: Number of times that an attempt to destroy the type has 
 *              failed.
 *
 * free_func:   Pointer to the free function that is applied to all IDs
 *              in the type when they are discarded.
 *
 *********************************************************************************/

#define ID_TYPE_T__TAG           0x1010
#define ID_TYPE_T_K__INITIALIZER {FALSE, FALSE, FALSE, FALSE, 0, H5I_BADID}

typedef struct id_type_kernel_t {
    hbool_t    in_progress;
    hbool_t    created;
    hbool_t    discarded;
    hbool_t    buffer_bool;
    int        buffer_int;
    hid_t      type_id;
} id_type_kernel_t;

typedef struct id_type_t {
    unsigned                 tag;
    int                      index;
    _Atomic id_type_kernel_t k;
    H5I_free_t               free_func;
    hbool_t                  mt_safe;

    /* stats */
    _Atomic long long int    successful_registers;
    _Atomic long long int    failed_registers;
    _Atomic long long int    successful_clears;
    _Atomic long long int    failed_clears;
    _Atomic long long int    successful_destroys;
    _Atomic long long int    failed_destroys;
} id_type_t;


/*********************************************************************************
 * struct id_object_t
 *
 * An array of instance of id_object_t is used to supply the objects associated
 * with newly allocated IDs.  These objects are acted upon by the free_func 
 * on discard, and by the per ID realize and discard callback if the ID is a 
 * future ID.
 * 
 * The fields of this structure are discussed individually below.
 *
 * Note that id_object_kernel_t is included in this header comment, as it gathers 
 * together all fields of id_object_t that must be handled as a single atomic unit.
 *
 * tag:         Integer field that must be set to ID_OBJECT_T__TAG.  This field
 *              is used to sanity check pointers.
 *
 * index:       integer field containing the index of this instance of 
 *              id_object_t in its containing array.
 *
 * id_index     Atomic integer field containing the index of the associated 
 *              instance of id_instance_t in its containing array, or -1 if 
 *              this field is undefined.  
 *
 * old_id_index: Atomic integer field that is undefined (i.e. set to -1) in 
 *              the typical case.  This field is only used when an instance of 
 *              id_object_t is transfered from one instance of id_instance_t
 *              to another.  In this case, it is used to store the index of 
 *              the previous instance of id_instance_t.
 *
 *              At present, this only happens when a future ID is realized.
 *
 *              Note that while this process is not  atomic in the test code
 *              the H5I code should enforce atomicity -- thus if there is any
 *              inconsistency in the fields that are modified during the realization
 *              of the future ID, and the ID type is not marked as thread safe,
 *              this is an error in the HDF5 code.
 *
 * k            Atomic instance of id_object_kernel_t -- a structure that contains
 *              all fields in id_object_t that must be treated as a single atomic
 *              value.  These fields are discussed individually below:
 *
 * k.in_progress:  Creation of an id is not atomic.  This field is set 
 *              when a thread is about to create an ID.  If successful, it 
 *              may proceed with the creation, and update kernel again when
 *              done.
 *
 * k.allocated: Boolean flag that is initialized to FALSE, and set to TRUE
 *              when the ID is created.  Due to k being an atomic structure
 *              and the above k.in_progress, this initialization must be done
 *              by a single process.
 *
 * k.discarded: Boolean flag that is initialized to FALSE, and set to TRUE
 *              when the ID is discarded.
 *
 * k.future:    Boolean flag that is set to TRUE if the ID was created as 
 *              a future ID, and has not yet been converted to a regular ID.
 *
 * real_id_def_in_prog: Boolean flag that is initialized to FALSE, and set to 
 *              TRUE while a real ID is being associated with the future ID.
 *              It is set back to FALSE when real_id_defined is set to TRUE.
 *
 * k.real_id_defined: Boolean flag that is initialized to FALSE.  IF the object
 *              is associated with a future ID, and the associated real ID
 *              is defined, this field is set to TRUE
 *
 * k.future_id_realized: Boolean flag that is initialized to FALSE, and set to 
 *              TRUE when realize_cb returns success.  
 *
 * k.future_id_discarded:  Boolean flag that is initialized to FALSE, and set
 *              to TRUE if the discard_cb is successful.  Note that this 
 *              may happen when future_id_realize is FALSE (i.e. the future
 *              id has not been realized -- if so, all subsequent calls to 
 *              the realize_cb() must fail
 *
 * k.id:        Initialized to 0, and set to the ID number assigned when the 
 *              ID is created.
 *
 * real_id:     If k.real_id_defined is TRUE, this field contains the ID of the 
 *              real ID. Undefined otherwise.
 *
 * real_id_obj_index: If k.real_id_defined is TRUE, this field contains the index
 *              of the instance of id_object_kernel_t in its containing array.
 *              Otherwise the field is undefined.
 *
 * accesses:    Number of times the ID is accessed.
 *
 *********************************************************************************/

#define ID_OBJECT_T__TAG              0x2020
#define ID_OBJECT_K_T__INITIALIZER    {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, H5I_INVALID_HID}

typedef struct id_object_kernel_t {
    hbool_t    in_progress;
    hbool_t    allocated;
    hbool_t    discarded;
    hbool_t    future;
    hbool_t    real_id_def_in_prog;
    hbool_t    real_id_defined;
    hbool_t    future_id_realized;
    hbool_t    future_id_discarded;
    hid_t      id;
} id_object_kernel_t;

typedef struct id_object_t{
    unsigned                   tag;
    int                        index;
    _Atomic int                id_index;
    _Atomic int                old_id_index;
    _Atomic id_object_kernel_t k;
    _Atomic hid_t              real_id;
    _Atomic int                real_id_obj_index;
    _Atomic long long int      accesses;
} id_object_t; 


/*********************************************************************************
 * struct id_instance_t
 *
 * An array of instance of id_instance_t is used to track the expected state of
 * IDs.
 * 
 * The fields of this structure are discussed individually below.
 *
 * Note that id_instance_kernel_t is included in this header comment, as it gathers 
 * together all fields of id_instance_t that must be handled as a single atomic unit.
 *
 * tag:         Integer field that must be set to ID_INSTANCE_T__TAG.  This field
 *              is used to sanity check pointers.
 *
 * index:       integer field containing the index of this instance of 
 *              id_instance_t in its containing array.
 *
 * k            Atomic instance of id_instance_kernel_t -- a structure that contains
 *              all fields in id_instance_t that must be treated as a single atomic
 *              value.  These fields are discussed individually below:
 *
 * k.in_progress:  Creation of an id is not atomic.  This field is set 
 *              when a thread is about to create an ID.  If successful, it 
 *              may proceed with the creation, and update kernel again when
 *              done.
 *
 * k.created:   Boolean flag that is initialized to FALSE, and set to TRUE
 *              when the ID is created.  Due to k being an atomic structure
 *              and the above k.in_progress, this initialization must be done
 *              by a single process.
 *
 * k.discarded: Boolean flag that is initialized to FALSE, and set to TRUE
 *              when the ID is discarded.
 *
 * k.future:    Boolean flag that is set to TRUE if the ID was created as 
 *              a future ID, and to FALSE othereise.
 *              
 * k.realized:  Boolean flag that is initialized to FALSE, and set to TRUE
 *              if the ID was created as a future ID, and has been converted
 *              to a regular ID.
 *
 * k.future_id_src:  Boolean flag that is initialize to FALSE, and set to TRUE
 *              when a real ID is linked to a future ID in preparation for the
 *              realization of the future ID.  
 *
 *              When the linked future ID is realized, it discards its own object,
 *              and adopts the object from the source real ID.  When this is done,
 *              the obj_index field is invalidated by setting it to -1.
 *
 * k.bool_buf_1: Padding to bring the size of id_type_t up to 16 bytes.
 *
 * k.bool_buf_2: Padding to bring the size of id_type_t up to 16 bytes.
 *
 * k.id:        Initialized to 0, and set to the ID number assigned when the 
 *              ID is created.
 *
 * obj_index:   Atomic integer field containing the index of the associated 
 *              instance of id_object_t -- if any.  The field is set to -1
 *              when undefined.
 *
 *              This field is initialized to -1, and set to the index of the 
 *              associatedinstance of id_object_t on id registration.  Once
 *              set, it is not changed unless the instance of id_instance_t
 *              is involved in the realization of a future ID.
 *
 *              if the instance of id_instance_t is marked as a future ID
 *              (k.future == TRUE), the value of obj_index is modified to that 
 *              of the same field in the instance of id_instance_t of the linked 
 *              real ID on future ID realization.
 *
 *              If the instance of id_instance_t is marked as a future ID
 *              source (k.future_id_src == TRUE), the obj_index field is 
 *              set to -1 on future ID realization.
 *
 * type_index:  In some multi-thread tests, this field is used to associated a 
 *              (possibly not yet registered) instance of id_instance_t with a 
 *              (possibly not yet registered) instance of id_type_t. 
 *
 *              In this context, the ID must belong to the type associated with 
 *              the indicated instance of id_type_t.  
 *
 *              When un-used, this field is set to -1.
 *
 * type_id:     Atomic instance of H5I_type_t.  This field is a convenience field for
 *              use when type_index is non-negative.  In this context, it is 
 *              initialized to H5I_BADID, and set to the id of the type
 *              associated with the instance of id_type_t indicated by type_index
 *              if it is defined.  Note that that this field being set does not 
 *              imply that the indicated type ID is still valid.
 *
 * remove_verify_starts:  Atomic unsigned long long.  This field is incremented
 *              whenever a thread is just about to call H5Iremove_verify(), and 
 *              decremented if the call to H5Iremove_verify() fails.
 *
 *              This field is necessary because H5Iremove_verify() doesn't call 
 *              the free_func(), and thus the test framework is not updated 
 *              atomically when a call to H5Iremove_verify() completes successfully.
 *              As a result it is possible for other calls to fail when according 
 *              to the test bed, the target ID exists and the other call should 
 *              have succeeded.  
 *
 *              By incrementing the remove_verify_starts counter when a call to 
 *              H5Iremove_verify() is initiated, and decrementing it if the call
 *              fails, we give the test frame enough information to detect cases
 *              in which a successful H5Iremove_verify() could be the cause of 
 *              an otherwise un-explained failure.
 *
 * 
 * Statistics:
 *
 * The following fields are used to collect statistics on the ID with which this 
 * instance of id_instance_t is associated.  All statistics fields are initialized
 * to zero, and incremented to reflect the target events.
 *
 * Note that statistics are not maintained in all tests.
 *
 * successful_registrations:  Atomic long long integer used to track the number 
 *              of times the ID associated with this instance of id_instance_t
 *              has been successfully registered.  As currently conceived, this
 *              value should never be greater than 1.
 *
 * failed_registrations: Atomic long long integer used to track the number
 *              of failed attempts to register the object asscciated with this 
 *              instance of id_instance_t.  
 *
 * successful_verifies: Atomic long long integer used to track the number of 
 *              successful lookups of the object associated with the id.
 *
 * failed_verifies: Atomic long long integer used to track the number of
 *              failed lookups of the object associated with the id.
 *
 * successful_inc_refs: Atomic long long integer used to track the number of
 *              successful reference count increments of the id.
 *
 * failed_inc_refs: Atomic long long integer used to track the number of
 *              failed reference count increments of the id.
 *
 * successful_dec_refs: Atomic long long integer used to track the number of
 *              successful reference count decrements of the id.
 *
 * failed_dec_refs: Atomic long long integer used to track the number of
 *              failed reference count decrements of the id.
 *
 * successful_get_ref_cnts: Atomic long long integer used to track the number of
 *              successful attempts to get the reference count for an id.
 *
 * failed_get_ref_cnts: Atomic long long integer used to track the number of
 *              failed attempts to get the reference count for an id.
 *
 * successful_remove_verifies: Atomic long long integer used to track the number of
 *              successful attempts to remove the ID and return a pointer to its
 *              associated object.
 *
 * failed_remove_verifies: Atomic long long integer used to track the number of
 *              failed attempts to remove the ID and return a pointer to its
 *              associated object.
 *
 *********************************************************************************/

#define ID_INSTANCE_T__TAG 0x3030
#define ID_INSTANCE_K_T__INITIALIZER {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, H5I_INVALID_HID}

typedef struct id_instance_kernel_t {
    hbool_t  in_progress;
    hbool_t  created;
    hbool_t  discarded;
    hbool_t  future;
    hbool_t  realized;
    hbool_t  future_id_src;
    hbool_t  bool_buf_1;
    hbool_t  bool_buf_2;
    hid_t id;
} id_instance_kernel_t;

typedef struct id_instance_t {
    unsigned                     tag;
    int                          index;
    _Atomic id_instance_kernel_t k;
    _Atomic int                  obj_index;
    int                          type_index;
    _Atomic H5I_type_t           type_id;
    _Atomic unsigned long long   remove_verify_starts;

    /* statistics */
    _Atomic long long int        successful_registrations;
    _Atomic long long int        failed_registrations;
    _Atomic long long int        successful_verifies;
    _Atomic long long int        failed_verifies;
    _Atomic long long int        successful_inc_refs;
    _Atomic long long int        failed_inc_refs;
    _Atomic long long int        successful_dec_refs;
    _Atomic long long int        failed_dec_refs;
    _Atomic long long int        dec_ref_deletes;
    _Atomic long long int        successful_get_ref_cnts;
    _Atomic long long int        failed_get_ref_cnts;
    _Atomic long long int        successful_remove_verifies;
    _Atomic long long int        failed_remove_verifies;

} id_instance_t;


#define NUM_ID_TYPES            256
#define NUM_ID_OBJECTS          (1024 * 1024)
#define NUM_ID_INSTANCES        (1024 * 1024)

#define MAX_NUM_THREADS         32

/***********************************************************************************
 *
 * struct mt_test_params_t
 *
 * Structure used to pass control information into and results out of H5I
 * multi-thread test functions.  The individual fields in this structure are
 * discussed below.
 *
 * thread_num: Unique ID assigned to this thread.  Used for debugging.
 *
 * types_start: Index of the initial entry in the types_array.
 *
 * types_count: Number of instance in the types_array to use.
 *
 * types_stride: Increment between entries used in the types_arry.
 *
 * ids_start:  Index of the initial entry in the id_instance_array.
 *
 * ids_count: Number of instance in the id_instance_array to use.
 *
 * ids_stride: Increment between entries used in the id_instance_array.
 *
 * objects_start: Index of the initial entry in the objects_array.
 *
 * objects_count: Number of instances in the objects_array to use.
 *
 * objects_stride: Increment between entries used in the objects_array.
 *
 * cs:	Boolean flag instructing the receiving function to clear stats on entry.
 *
 * ds: 	Boolean flag instructing the receiving function to display stats on exit.
 *
 * rpt_failures: Boolean flag instructing any function that receives it to print
 *	an error message to stderr if any error is detected.
 *
 * err_cnt: Integer field used to collect the total number of errors detected
 *
 * ambig_cnt: Integer field used to collec the total number of ambiguous test 
 *      results.
 *
 ***********************************************************************************/

typedef struct mt_test_params_t {

    int thread_id;

    int types_start;
    int types_count;
    int types_stride;

    int ids_start;
    int ids_count;
    int ids_stride;

    int objects_start;
    int objects_count;
    int objects_stride;

    bool cs;
    bool ds;
    bool rpt_failures;

    int err_cnt;
    int ambig_cnt;

} mt_test_params_t;

id_type_t     *types_array;
id_object_t   *objects_array;
id_instance_t *id_instance_array;


void    init_globals(void);
void    reset_globals(void);


herr_t  free_func(void * obj, void ** request);
herr_t  realize_cb_0(void * future_object, hid_t * actual_object_id);
herr_t  discard_cb_0(void * future_object);


int     register_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
void    try_register_type(int type_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     clear_type(id_type_t * id_type_ptr, hbool_t force, hbool_t cs, hbool_t ds, hbool_t rpt_failures, 
                   int tid);
void    try_clear_type(int type_index, hbool_t force, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     destroy_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     try_destroy_type(int type_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);


int     register_id(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                    hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
void    try_register_id(int id_index, int obj_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     register_future_id(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                           H5I_future_realize_func_t realize_cb, H5I_future_discard_func_t discard_cb,
                           hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     link_real_and_future_ids(id_object_t * future_id_obj_ptr, id_object_t * real_id_obj_ptr,
                                 hbool_t rpt_failures);
int     object_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                      hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     try_object_verify(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     get_type(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, 
                 hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     remove_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                      hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     try_remove_verify(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     dec_ref(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                hbool_t cs, hbool_t ds, hbool_t rpt_failure, int tid);
int     try_dec_ref(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     inc_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     try_inc_ref(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     get_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     try_get_ref(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     nmembers(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
htri_t  type_exists(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     inc_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     dec_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     try_dec_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);


int  create_types(int types_start, int types_count, int types_stride, hbool_t cs, hbool_t ds, 
                  hbool_t rpt_failures, int tid);
int  dec_type_refs(int types_start, int types_count, int types_stride, 
                   hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int  inc_type_refs(int types_start, int types_count, int types_stride, 
                   hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int  destroy_types(int types_start, int types_count, int types_stride, 
                   hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);

int register_ids(int types_start, int types_count, int types_stride, int ids_start, int ids_count, int ids_stride,
                 hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int  dec_refs(int types_start, int types_count, int types_stride, int ids_start, int ids_count, int ids_stride,
                 hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int  inc_refs(int ids_start, int ids_count, int ids_stride, 
              hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int verify_objects(int types_start, int types_count, int types_stride, 
                   int ids_start, int ids_count, int ids_stride,
                   hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);



#define SERIAL_TEST_1__DISPLAY_FINAL_STATS              FALSE
#define SERIAL_TEST_2__DISPLAY_FINAL_STATS              FALSE
#define SERIAL_TEST_3__DISPLAY_FINAL_STATS              FALSE
#define SERIAL_TEST_4__DISPLAY_FINAL_STATS              FALSE
#define MT_TEST_FCN_1_SERIAL_TEST__DISPLAY_FINAL_STATS  FALSE

#define MT_TEST_1__DISPLAY_FINAL_STATS                  FALSE
#define MT_TEST_2__DISPLAY_FINAL_STATS                  FALSE


void serial_test_1(void);
void serial_test_2(int types_start, int types_count, int ids_start, int ids_count);
void serial_test_3(void);
void serial_test_4(void);

void * mt_test_fcn_1(void * params);
void * mt_test_fcn_2(void * params);

void mt_test_fcn_1_serial_test(void);
void mt_test_1(int num_threads);
void mt_test_2(int num_threads);

void init_globals(void)
{
    int                         i;
    id_type_kernel_t            type_k  = ID_TYPE_T_K__INITIALIZER;
    struct id_object_kernel_t   obj_k   = ID_OBJECT_K_T__INITIALIZER;
    struct id_instance_kernel_t inst_k  = ID_INSTANCE_K_T__INITIALIZER;

    types_array = malloc(NUM_ID_TYPES * sizeof(id_type_t));
    objects_array = malloc(NUM_ID_OBJECTS * sizeof(id_object_t));
    id_instance_array = malloc(NUM_ID_INSTANCES * sizeof(id_instance_t));

    if ( ( NULL == types_array ) || ( NULL == objects_array ) || ( NULL == id_instance_array ) ) {

        fprintf(stderr, "init_globals(): One or more array allocations failed -- exiting.\n");
        exit(1);
    }

    for ( i = 0; i < NUM_ID_TYPES; i++ )
    {
        types_array[i].tag = ID_TYPE_T__TAG;
        types_array[i].index = i;
        atomic_init(&(types_array[i].k), type_k);
        types_array[i].free_func = free_func;
        types_array[i].mt_safe = FALSE;

        /* stats */
        atomic_init(&(types_array[i].successful_registers), 0ULL);
        atomic_init(&(types_array[i].failed_registers), 0ULL);
        atomic_init(&(types_array[i].successful_clears), 0ULL);
        atomic_init(&(types_array[i].failed_clears), 0ULL);
        atomic_init(&(types_array[i].successful_destroys), 0ULL);
        atomic_init(&(types_array[i].failed_destroys), 0ULL);
    }

    for ( i = 0; i < NUM_ID_OBJECTS; i++ )
    {
        objects_array[i].tag = ID_OBJECT_T__TAG;
        objects_array[i].index = i;
        atomic_init(&(objects_array[i].id_index), -1);
        atomic_init(&(objects_array[i].old_id_index), -1);
        atomic_init(&(objects_array[i].k), obj_k);
        atomic_init(&(objects_array[i].real_id), 0ULL);
        atomic_init(&(objects_array[i].real_id_obj_index), -1);
        atomic_init(&(objects_array[i].accesses), 0ULL);
    }

    for ( i = 0; i < NUM_ID_INSTANCES; i++ )
    {
        id_instance_array[i].tag = ID_INSTANCE_T__TAG;
        id_instance_array[i].index = i;
        atomic_init(&(id_instance_array[i].k), inst_k);
        atomic_init(&(id_instance_array[i].obj_index), -1);
        id_instance_array[i].type_index = -1;
        atomic_init(&(id_instance_array[i].type_id), H5I_BADID);
        atomic_init(&(id_instance_array[i].remove_verify_starts), 0ULL);

        /* stats */
        atomic_init(&(id_instance_array[i].successful_registrations), 0ULL);
        atomic_init(&(id_instance_array[i].failed_registrations), 0ULL);
        atomic_init(&(id_instance_array[i].successful_verifies), 0ULL);
        atomic_init(&(id_instance_array[i].failed_verifies), 0ULL);
        atomic_init(&(id_instance_array[i].successful_inc_refs), 0ULL);
        atomic_init(&(id_instance_array[i].failed_inc_refs), 0ULL);
        atomic_init(&(id_instance_array[i].successful_dec_refs), 0ULL);
        atomic_init(&(id_instance_array[i].failed_dec_refs), 0ULL);
        atomic_init(&(id_instance_array[i].dec_ref_deletes), 0ULL);
        atomic_init(&(id_instance_array[i].successful_get_ref_cnts), 0ULL);
        atomic_init(&(id_instance_array[i].failed_get_ref_cnts), 0ULL);
        atomic_init(&(id_instance_array[i].successful_remove_verifies), 0ULL);
        atomic_init(&(id_instance_array[i].failed_remove_verifies), 0ULL);
    }

    return;

} /* init_globals() */

void reset_globals(void)
{
    int i;
    struct id_type_kernel_t     type_k  = ID_TYPE_T_K__INITIALIZER;
    struct id_object_kernel_t   obj_k   = ID_OBJECT_K_T__INITIALIZER;
    struct id_instance_kernel_t inst_k  = ID_INSTANCE_K_T__INITIALIZER;

    for ( i = 0; i < NUM_ID_TYPES; i++ )
    {
        types_array[i].tag = ID_TYPE_T__TAG;
        types_array[i].index = i;
        atomic_store(&(types_array[i].k), type_k);
        types_array[i].free_func = free_func;
        types_array[i].mt_safe = FALSE;

        /* stats */
        atomic_store(&(types_array[i].successful_registers), 0ULL);
        atomic_store(&(types_array[i].failed_registers), 0ULL);
        atomic_store(&(types_array[i].successful_clears), 0ULL);
        atomic_store(&(types_array[i].failed_clears), 0ULL);
        atomic_store(&(types_array[i].successful_destroys), 0ULL);
        atomic_store(&(types_array[i].failed_destroys), 0ULL);
    }

    for ( i = 0; i < NUM_ID_OBJECTS; i++ )
    {
        objects_array[i].tag = ID_OBJECT_T__TAG;
        objects_array[i].index = i;
        atomic_store(&(objects_array[i].id_index), -1);
        atomic_store(&(objects_array[i].old_id_index), -1);
        atomic_store(&(objects_array[i].k), obj_k);
        atomic_store(&(objects_array[i].real_id), 0ULL);
        atomic_store(&(objects_array[i].real_id_obj_index), -1);
        atomic_store(&(objects_array[i].accesses), 0ULL);
    }

    for ( i = 0; i < NUM_ID_INSTANCES; i++ )
    {
        id_instance_array[i].tag = ID_INSTANCE_T__TAG;
        id_instance_array[i].index = i;
        atomic_store(&(id_instance_array[i].k), inst_k);
        atomic_store(&(id_instance_array[i].obj_index), -1);
        id_instance_array[i].type_index = -1;
        atomic_store(&(id_instance_array[i].type_id), H5I_BADID);
        atomic_store(&(id_instance_array[i].remove_verify_starts), 0ULL);

        /* stats */
        atomic_store(&(id_instance_array[i].successful_registrations), 0ULL);
        atomic_store(&(id_instance_array[i].failed_registrations), 0ULL);
        atomic_store(&(id_instance_array[i].successful_verifies), 0ULL);
        atomic_store(&(id_instance_array[i].failed_verifies), 0ULL);
        atomic_store(&(id_instance_array[i].successful_inc_refs), 0ULL);
        atomic_store(&(id_instance_array[i].failed_inc_refs), 0ULL);
        atomic_store(&(id_instance_array[i].successful_dec_refs), 0ULL);
        atomic_store(&(id_instance_array[i].failed_dec_refs), 0ULL);
        atomic_store(&(id_instance_array[i].dec_ref_deletes), 0ULL);
        atomic_store(&(id_instance_array[i].successful_get_ref_cnts), 0ULL);
        atomic_store(&(id_instance_array[i].failed_get_ref_cnts), 0ULL);
        atomic_store(&(id_instance_array[i].successful_remove_verifies), 0ULL);
        atomic_store(&(id_instance_array[i].failed_remove_verifies), 0ULL);
    }

    return;

} /* reset_globals() */


/***********************************************************************************************
 * free_func()
 *
 *      Discard the object associated with an ID in the index.
 *
 *      In the context of the H5I test code, this means that both the instance of id_object_t
 *      pointed to by the obj parameter, and the instance of id_instance_t associated with 
 *      it must be marked as deleted.
 *
 *      Note that this implementation of the free function is not multi-thread safe -- and
 *      must therefore not be used with ID types that are marked as multi-thread safe.
 *
 *                                                        JRM -- 4/16/24
 *      
 ***********************************************************************************************/

herr_t free_func(void * obj, void H5_ATTR_UNUSED ** request)
{
    int                           id_index;
    volatile id_object_t        * object_ptr = (id_object_t *)obj;
    volatile id_instance_kernel_t id_inst_k;
    id_instance_kernel_t          mod_id_inst_k = ID_INSTANCE_K_T__INITIALIZER;
    volatile id_object_kernel_t   obj_k;
    id_object_kernel_t            mod_obj_k = ID_OBJECT_K_T__INITIALIZER;

    assert(object_ptr);

    assert(ID_OBJECT_T__TAG == object_ptr->tag);

    id_index = atomic_load(&(object_ptr->id_index));

    assert( ( 0 <= id_index ) && ( id_index < NUM_ID_INSTANCES ) );
    assert( ID_INSTANCE_T__TAG == id_instance_array[id_index].tag );

    id_inst_k = atomic_load(&(id_instance_array[id_index].k));

    obj_k = atomic_load(&(object_ptr->k));

    assert( id_inst_k.created );
    assert( ! id_inst_k.discarded );
#if 0 
    assert( ! id_inst_k.future );
#else 
    assert( ( ! id_inst_k.future ) || ( id_inst_k.realized ) );
#endif

    assert( obj_k.allocated );
    assert( ! obj_k.discarded );
    assert( ! obj_k.future );

    mod_id_inst_k.in_progress     = id_inst_k.in_progress;
    mod_id_inst_k.created         = id_inst_k.created;
    mod_id_inst_k.discarded       = TRUE;
    mod_id_inst_k.future          = id_inst_k.future;
    mod_id_inst_k.realized        = id_inst_k.realized;
    mod_id_inst_k.future_id_src   = id_inst_k.future_id_src;
    mod_id_inst_k.in_progress     = id_inst_k.in_progress;
    mod_id_inst_k.id              = id_inst_k.id;

    mod_obj_k.allocated           = obj_k.allocated;
    mod_obj_k.discarded           = TRUE;
    mod_obj_k.future              = obj_k.future;
    mod_obj_k.real_id_def_in_prog = obj_k.real_id_def_in_prog;
    mod_obj_k.real_id_defined     = obj_k.real_id_defined;
    mod_obj_k.future_id_realized  = obj_k.future_id_realized;
    mod_obj_k.future_id_discarded = obj_k.future_id_discarded;
    mod_obj_k.id                  = obj_k.id;

    if ( ( atomic_compare_exchange_strong(&(id_instance_array[id_index].k), &id_inst_k, mod_id_inst_k) ) &&
         ( atomic_compare_exchange_strong(&(object_ptr->k), &obj_k, mod_obj_k) ) ) {

        return(SUCCEED);

    } else {

        fprintf(stderr, "\nfree_func() failed for id_index = %d\n\n", id_index);

        return(FAIL);
    }
} /* free_func() */


/***********************************************************************************************
 * realize_cb_0
 *
 *      Test framework implementation of the realize_cb used to test future IDs.
 *
 *      After initial sanity checks:
 *
 *      1) Verify that the supplied future object is still valid,
 *
 *      2) Verify that the supplied future object is marked as being a future object,
 *
 *      3) Verify that a real object for the supplied future object has been created
 *         (i.e. that the real_id_defined flag is set), and
 *
 *      4) Verify that the supplied future object has not yet been realized (i.e.
 *         that the future_id_realized flag has not been set).
 *
 *      5) Verify that the supplied future object has not yet been discarded (i.e.
 *         that the future_id_discarded_flag has not beem set.
 *
 *      If any of these conditions FAIL, return FAIL
 *
 *      If all of these conditions are met, attempt to update the test test framework 
 *      to reflect the immenant relization of the future ID as follows:
 *
 *      1) Invalidate the obj_index field of the id_instance_t associated with the 
 *         real id.  Do this with a compare and set, so as to detect the case in which
 *         the real ID has been deleted.  If this is the case, return failure.
 *
 *         Note that we are running into a fundamental problem with the future ID 
 *         mechanism -- since H5I doesn't know anything about the real ID until it 
 *         gets a successful return from the realize callback, it can't protect 
 *         the real ID from deletion prior to its consumption as part of fhe future
 *         ID realization.
 *
 *         Needless to say, a well behaved host application can do this, but we don't 
 *         want to depend on this.  Thus for now, I am papering over the problem in my
 *         test bed, as the effect of using a compare and set here is to cause the 
 *         realize callback to fail on subsequent if the first realize call succeeds.
 *
 *      2) Set the old_id_index field of the id_object_t associated with the real_id
 *         equal to its id_index field, and then set the id_index field equal to the 
 *         same field in the future object.
 *
 *      3) Set the obj_index field in the future instance of id_instance_t to the value
 *         of the index field of the real_id instance of id_object_t.
 *
 *      4) Invalidate the id_index field of the future id instance of id_object_t.
 *
 *      5) set set the furure_id_realized flag via a call to 
 *          atomic_compare_exchange_strong() on &(future_id->k).  
 *
 *      If all the above succeed, set *actual_object_id to future_object->real_id, 
 *      and return SUCCEED.
 *
 *      Otherwise, return FAIL.
 *
 *      Observe that the operations of the realize callback are anything but atomic.  As 
 *      long as the host type is not listed as mulit-thread safe, this function should 
 *      be run within the global mutex -- making this point moot.  
 *
 *                                                              JRM -- 3/8/24
 *
 * Returs:  
 *
 *      SUCCEED on success, and FAIL otherwise.
 *
 * Changes;
 *
 *      None.
 *
 ***********************************************************************************************/

herr_t realize_cb_0(void * future_object, hid_t * actual_object_id)
{
    hbool_t                     success = TRUE;
    hbool_t                     rpt_failures = FALSE;
    int                         real_id_inst_index   = -1;
    int                         real_id_obj_index    = -1;
    int                         future_id_inst_index = -1;
    volatile id_instance_t    * real_id_inst_ptr     = NULL;
    volatile id_instance_t    * future_id_inst_ptr   = NULL;
    volatile id_object_t      * real_id_obj_ptr      = NULL;
    volatile id_object_t      * future_id_obj_ptr = (id_object_t *)future_object;
    id_object_kernel_t          future_id_obj_k;
    id_object_kernel_t          mod_future_id_obj_k = ID_OBJECT_K_T__INITIALIZER;

    if ( ( NULL == future_id_obj_ptr )  || ( ID_OBJECT_T__TAG != future_id_obj_ptr->tag ) || 
         ( NULL == actual_object_id ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "realize_cb_0(): Invalid params on entry.\n");
        }
    }

    /* Verify that the supplied future object is valid. */
    if ( success ) {

        future_id_obj_k  = atomic_load(&(future_id_obj_ptr->k));

        if ( ( future_id_obj_k.in_progress ) || ( ! future_id_obj_k.allocated ) || 
             ( future_id_obj_k.discarded ) ) {

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                       "realize_cb_0(): future id obj in progress, not created, or discarded on entry.\n");
            }
        }
    }

    /* Verify that the supplied future object is marked as being a future object. */
    if ( ( success ) && ( ! future_id_obj_k.future ) ) {

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "realize_cb_0(): future id obj not marked as future.\n");
        }
    }

    /* Verify that a real object for the supplied future object has been created
     * (i.e. that the real_id_defined flag is set).
     */
    if ( ( success ) && ( ! future_id_obj_k.real_id_defined ) ) {

        /* real ID isn't defined yet -- function reports failure, but no error message */

        success = FALSE;
    }

    /* Verify that the supplied future object has not yet been realized (i.e.
     * that the future_id_realized flag has not been set).
     *
     * If so, just return failure since this condition will occur if the source real ID is deleted
     * before the future ID is realized.  If so, the realize_cb will succeed the first time around,
     * but the realization will fail when H5I tries to remove the 
     */
    if ( ( success ) && ( future_id_obj_k.future_id_realized ) ) {

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "realize_cb_0(): future id already realized.\n");
        }
    }

    if ( ( success ) && ( future_id_obj_k.future_id_discarded ) ) {

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "realize_cb_0(): future id has been discarded.\n"); 
        }
    }
        
    if ( success ) {

        int expected_obj_index;

        /* Initial sanity have passed -- attempt to realize the ID.  Assuming that H5I is maintaining
         * the global lock during the realize_cb call, and there is no attempt to re-use instances
         * of id_instance_t or id_object_t, this operation should succeed.
         */

        /* get indicies and pointers to the real id instance and object, and the future id_instance.  
         *
         * Do basic sanity checks, but don't verify that the real ID is still valid.  If it has been 
         * deleted, this should be caught by the H5I future ID realization code.
         */
        real_id_obj_index = atomic_load(&(future_id_obj_ptr->real_id_obj_index));

        assert(real_id_obj_index >= 0);
        assert(real_id_obj_index < NUM_ID_OBJECTS);

        real_id_obj_ptr = &(objects_array[real_id_obj_index]);

        assert(real_id_obj_ptr);
        assert(ID_OBJECT_T__TAG == real_id_obj_ptr->tag);

        real_id_inst_index = atomic_load(&(real_id_obj_ptr->id_index));

        assert(real_id_inst_index >= 0);
        assert(real_id_inst_index < NUM_ID_INSTANCES);

        real_id_inst_ptr = &(id_instance_array[real_id_inst_index]);

        assert(real_id_inst_ptr);
        assert(ID_INSTANCE_T__TAG == real_id_inst_ptr->tag);

        future_id_inst_index = atomic_load(&(future_id_obj_ptr->id_index));

        assert(future_id_inst_index >= 0);
        assert(future_id_inst_index < NUM_ID_INSTANCES);

        future_id_inst_ptr = &(id_instance_array[future_id_inst_index]);

        assert(future_id_inst_ptr);
        assert(ID_INSTANCE_T__TAG == future_id_inst_ptr->tag);
 
        assert(atomic_load(&(real_id_inst_ptr->obj_index)) == real_id_obj_ptr->index);
        assert(atomic_load(&(real_id_obj_ptr->id_index)) == real_id_inst_ptr->index);
 
        assert(atomic_load(&(future_id_inst_ptr->obj_index)) == future_id_obj_ptr->index);
        assert(atomic_load(&(future_id_obj_ptr->id_index)) == future_id_inst_ptr->index);


        /* Invalidate the obj_index field of the id_instance_t associated with the 
         * real id.  Do this with a compare and set, so as to detect the case in which
         * the real ID has been deleted.  If this is the case, return failure.
         *
         * Note that we are running into a fundamental problem with the future ID 
         * mechanism -- since H5I doesn't know anything about the real ID until it 
         * gets a successful return from the realize callback, it can't protect 
         * the real ID from deletion prior to its consumption as part of fhe future
         * ID realization.
         *
         * Needless to say, a well behaved host application can do this, but we don't 
         * want to depend on this.  Thus for now, I am papering over the problem in my
         * test bed, as the effect of using a compare and set here is to cause the 
         * realize callback to fail on subsequent if the first realize call succeeds.
         */
        expected_obj_index = real_id_obj_index;

        if ( ! atomic_compare_exchange_strong(&(real_id_inst_ptr->obj_index), &expected_obj_index, -1) ) {

            /* unexpected real_id_inst_ptr->obj_index -- exit reporting failure. */
            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "realize_cb_0(): expected real_id_inst_ptr->obj_index = %d, actual = %d.\n",
                       real_id_obj_index, expected_obj_index); 
            }
        }
    }

    if ( success ) {

        /* Set the old_id_index field of the id_object_t associated with the real_id
         * equal to its id_index field, and then set the id_index field equal to the 
         * value of the same field in the future object.
         */
        assert(atomic_load(&(real_id_obj_ptr->old_id_index)) == -1);
        atomic_store(&(real_id_obj_ptr->old_id_index), atomic_load(&(real_id_obj_ptr->id_index)));
        atomic_store(&(real_id_obj_ptr->id_index), future_id_inst_index);
 

        /* Set the obj_index field in the future instance of id_instance_t to the value
         * of the index field of the real_id instance of id_object_t.
         */
        assert(future_id_obj_ptr->index == atomic_load(&(future_id_inst_ptr->obj_index)));
        atomic_store(&(future_id_inst_ptr->obj_index), real_id_obj_ptr->index);
 

        /* Invalidate the id_index field of the future id instance of id_object_t, but save
         * the value in the old_id_index field first. 
         */
        assert(future_id_inst_ptr->index == atomic_load(&(future_id_obj_ptr->id_index)));
        assert(-1 == atomic_load(&(future_id_obj_ptr->old_id_index)));
        atomic_store(&(future_id_obj_ptr->old_id_index), atomic_load(&(future_id_obj_ptr->id_index)));
        atomic_store(&(future_id_obj_ptr->id_index), -1);
 

        /* Set set the furure_id_realized flag via a call to 
         * atomic_compare_exchange_strong() on &(future_id->k).  
         */
        mod_future_id_obj_k.in_progress         = future_id_obj_k.in_progress;
        mod_future_id_obj_k.allocated           = future_id_obj_k.allocated;
        mod_future_id_obj_k.discarded           = future_id_obj_k.discarded;
        mod_future_id_obj_k.future              = future_id_obj_k.future;
        mod_future_id_obj_k.real_id_defined     = future_id_obj_k.real_id_defined;
        mod_future_id_obj_k.future_id_realized  = TRUE;
        mod_future_id_obj_k.future_id_discarded = future_id_obj_k.future_id_discarded;
        mod_future_id_obj_k.id                  = future_id_obj_k.id;

        if ( atomic_compare_exchange_strong(&(future_id_obj_ptr->k), &future_id_obj_k, mod_future_id_obj_k) ) {

            *actual_object_id = atomic_load(&(future_id_obj_ptr->real_id));

        } else {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "realize_cb_0(): attempt to set ruture_id_realized failed.\n"); 
            }
        }
    } 

    return( success ? SUCCEED : FAIL );

} /* realize_cb_0() */


/***********************************************************************************************
 * discard_cb_0
 *
 *      Test framework implementation of the discard_cb used to test future IDs.  Note that 
 *      this implementation of the discard call back is not multi-thread safe, and depends
 *      upon calling function to ensure mutual exclusion on the target future object during 
 *      execution.
 *
 *      The discard callback in invoked under two circumstances.
 *
 *      The first case is the discard of the object associated with a future ID after it has 
 *      been replaced with the object associated the source real ID.  In this case, it is 
 *      necessary to set the discarded and future_id_discarded flags on the target instance 
 *      of id_object_t, and the realized flag in the associated instance of 1d_instance_t.
 *
 *      The second case is the discard of the object associated with a future ID when that 
 *      ID is discarded before it can be realized.  Here, it is only necessary to set 
 *      the discarded flag in the target instance of id_object_t.
 *
 *      The two cases can be distinguished by the value of the future_id_realized flag.
 *
 *      The function proceeds as follows:
 *
 *      First perform basic sanity checks, and them
 *
 *      1) Verify that the supplied future object is still valid,
 *
 *      2) Verify that the supplied future object is marked as being a future object,
 *
 *      if either of these tests fail, return failure.
 *
 *      Then test to see if the supplied future object is maked as being realized (i.e.
 *      that the future_id_realized flag is set).  
 *
 *      if it isn't, we are dealing with case 2 above.  set the discarded flag via a 
 *      call to via a call to atomic_compare_exchange_strong() on &(future_id_obj_ptr->k), 
 *      and return SUCCESS if this operation succeeds, and FAIL otherwise.
 *
 *      If it is, we are dealing with case 1 above.  Proceeds as follows:
 *
 *      3) Verify that the supplied future object has not yet been discarded (i.e.
 *         that the future_id_discarded flag has not been set).
 *
 *      4) Verify that the id_index field of the future object is valid.
 *
 *      5) Verify that the instance of id_instance_t referenced by the id_index field
 *         is valid, is marked as a future ID, but not yet marked as being realized.
 *
 *      If all of these conditions are met, attempt to set the discarded and future_id_
 *      discarded flags via a call to atomic_compare_exchange_strong() on 
 *      &(future_id_obj_ptr->k), and attempt to set the realized flag on the associated
 *      instance of id_instance_t to TRUE.  
 *
 *      If any of these tests fail, or if any of the flag updates fail, return FALSE.
 *
 *      Otherwise, return TRUE.
 *
 *                                                              JRM -- 3/8/24
 *
 * Returs:  
 *
 *      SUCCEED on success, and FAIL otherwise.
 *
 * Changes;
 *
 *      None.
 *
 ***********************************************************************************************/

herr_t discard_cb_0(void * future_object)
{
    hbool_t                       success = TRUE;
    hbool_t                       rpt_failures = TRUE;
    int                           future_id_inst_index = -1;
    id_object_t                 * future_id_obj_ptr = (id_object_t *)future_object;
    id_object_kernel_t            id_obj_k;
    id_object_kernel_t            mod_id_obj_k = ID_OBJECT_K_T__INITIALIZER;
    volatile id_instance_t      * future_id_inst_ptr = NULL;
    volatile id_instance_kernel_t id_inst_k;
    id_instance_kernel_t          mod_id_inst_k = ID_INSTANCE_K_T__INITIALIZER;
    

    if ( ( NULL == future_id_obj_ptr )  || ( ID_OBJECT_T__TAG != future_id_obj_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "discard_cb_0(): Invalid params on entry.\n");
        }
    }

    if ( success ) {

        id_obj_k  = atomic_load(&(future_id_obj_ptr->k));

        if ( ( id_obj_k.in_progress ) || ( ! id_obj_k.allocated ) || ( id_obj_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                       "discard_cb_0(): future id obj in progress, not created, or discarded on entry.\n");
            }
        }
    }

    if ( success ) {

        if ( ! id_obj_k.future ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "discard_cb_0(): future id obj not marked as future.\n");
            }
        }
    }

    if ( ( success ) && ( ! id_obj_k.future_id_realized ) ) {

        /* this is case 2 -- just set the discarded flag and return success.  Note 
         * that whether this set succeeds or not, we are done.
         */

        mod_id_obj_k.in_progress         = id_obj_k.in_progress;
        mod_id_obj_k.allocated           = id_obj_k.allocated;
        mod_id_obj_k.discarded           = TRUE;
        mod_id_obj_k.future              = id_obj_k.future;
        mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
        mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
        mod_id_obj_k.future_id_discarded = id_obj_k.future_id_discarded;
        mod_id_obj_k.id                  = id_obj_k.id;

        if ( ! atomic_compare_exchange_strong(&(future_id_obj_ptr->k), &id_obj_k, mod_id_obj_k) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                     "discard_cb_0(): case 2: attempt to set discarded flag on future id objected failed.\n");
            }
        }

    }

    if ( ( success ) && ( id_obj_k.future_id_realized ) ) {

        if ( id_obj_k.future_id_discarded ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "discard_cb_0(): future id already discarded.\n"); 
            }
        }
    }

    if ( ( success ) && ( id_obj_k.future_id_realized ) ) {

        assert(-1 == atomic_load(&(future_id_obj_ptr->id_index)));

        future_id_inst_index = atomic_load(&(future_id_obj_ptr->old_id_index));

        if ( ( future_id_inst_index < 0 ) || ( future_id_inst_index >= NUM_ID_INSTANCES ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "discard_cb_0(): invalid future_id_inst_index (%d).\n", future_id_inst_index); 
            }
        } else {

            future_id_inst_ptr = &(id_instance_array[future_id_inst_index]);
        } 
    }
    
    if ( ( success ) && ( id_obj_k.future_id_realized ) ) {

        if ( ( NULL == future_id_inst_ptr ) || ( ID_INSTANCE_T__TAG != future_id_inst_ptr->tag ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "discard_cb_0(): invalid future_id_inst_ptr.\n"); 
            }
        }
    }

    if ( ( success ) && ( id_obj_k.future_id_realized ) ) {

        id_inst_k  = atomic_load(&(future_id_inst_ptr->k));

        if ( ( id_inst_k.in_progress ) || ( ! id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "discard_cb_0(): future id instance in progress, not created, or discarded.\n");
            }
        } else if ( ! id_inst_k.future ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "discard_cb_0(): future id instance not marked as future id.\n");
            }
        } else if ( id_inst_k.realized ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "discard_cb_0(): future id instance already marked as realized.\n");
            }
        }
    }
        
    if ( ( success ) && ( id_obj_k.future_id_realized ) ) {

        mod_id_obj_k.in_progress         = id_obj_k.in_progress;
        mod_id_obj_k.allocated           = id_obj_k.allocated;
        mod_id_obj_k.discarded           = TRUE;
        mod_id_obj_k.future              = id_obj_k.future;
        mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
        mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
        mod_id_obj_k.future_id_discarded = TRUE;
        mod_id_obj_k.id                  = id_obj_k.id;

        if ( ! atomic_compare_exchange_strong(&(future_id_obj_ptr->k), &id_obj_k, mod_id_obj_k) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "discard_cb_0(): attempt to set flags on future id objected failed..\n");
            }
        }
    }

    if ( ( success ) && ( id_obj_k.future_id_realized ) ) {

        mod_id_inst_k.in_progress   = id_inst_k.in_progress;
        mod_id_inst_k.created       = id_inst_k.created;
        mod_id_inst_k.discarded     = id_inst_k.discarded;
        mod_id_inst_k.future        = id_inst_k.future;
        mod_id_inst_k.realized      = TRUE;
        mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
        mod_id_inst_k.in_progress   = id_inst_k.in_progress;
        mod_id_inst_k.id            = id_inst_k.id;

        if ( ! atomic_compare_exchange_strong(&(future_id_inst_ptr->k), &id_inst_k, mod_id_inst_k) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "discard_cb_0(): attempt to set flags on future id instance failed..\n");
            }
        }

    }

    return( success ? SUCCEED : FAIL );

} /* discard_cb_0() */


/***********************************************************************************************
 * register_type()
 *
 *    Register a new type, associate it with the supplied instance of id_type_t, and update that
 *    structure accordingly.  
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and 1 if any error is detected.
 *
 ***********************************************************************************************/

int register_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t                   success = TRUE; /* will set to FALSE on failure */
    volatile id_type_kernel_t id_k;
    volatile id_type_kernel_t mod_id_k = ID_TYPE_T_K__INITIALIZER;

    if ( cs ) {

        H5I_clear_stats();
    }

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ) {

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "register_type():%d: initial sanity checks failed.\n", tid);
        }
    }

    if ( success ) {

        id_k = atomic_load(&(id_type_ptr->k));

        if ( ( id_k.in_progress ) || ( id_k.created ) ) {

            success = FALSE; /* another thread beat us to it */

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "register_type():%d: type at index %d registraion already done or in progress (1).\n",
                        tid, id_type_ptr->index);
            }

        } else {

            mod_id_k.in_progress = TRUE;
            mod_id_k.created     = id_k.created;
            mod_id_k.discarded   = id_k.discarded;
            mod_id_k.type_id     = id_k.type_id;

            if ( ! atomic_compare_exchange_strong(&(id_type_ptr->k), &id_k, mod_id_k) ) {

                success = FALSE; /* another thread beat us to it */

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "register_type():%d: type at index %d registraion already done or in progress (2).\n",
                            tid, id_type_ptr->index);
                }
            }
        }
    }

    if ( success ) {

        hid_t type_id;

        id_k = atomic_load(&(id_type_ptr->k));

        type_id = H5Iregister_type((size_t)0, 0, id_type_ptr->free_func);

        if ( H5I_BADID == type_id ) {

            mod_id_k.in_progress = FALSE;
            mod_id_k.created     = id_k.created;
            mod_id_k.discarded   = id_k.discarded;
            mod_id_k.type_id     = id_k.type_id;

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "register_type():%d: call to H5Iregister_type() failed of type at index %d.\n",
                        tid, id_type_ptr->index);
            }

            assert(success);

        } else {

            mod_id_k.in_progress = FALSE;
            mod_id_k.created     = TRUE;
            mod_id_k.discarded   = id_k.discarded;
            mod_id_k.type_id     = type_id;
        }

        if ( ! atomic_compare_exchange_strong(&(id_type_ptr->k), &id_k, mod_id_k) ) {

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "register_type():%d: update of id_type_ptr->k failed.  index = %d, id = 0x%llx.\n",
                        tid, id_type_ptr->index, (unsigned long long)type_id);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iregister_type");
    }

    return(success?0:1);

} /* register_type() */


/***********************************************************************************************
 * try_register_type()
 *
 *    Attempt to register a new type, associate it with types_array[type_index] and update that
 *    instance of id_type_t accordingly.  
 *
 *    If successful, increment types_array[type_index].successful_registers.  On failure, 
 *    increment increment types_array[type_index].failed_registers
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *                                                           JRM -- 4/2/24
 *
 * Returns: void
 *
 ***********************************************************************************************/

void try_register_type(int type_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t                   success = TRUE; /* will set to FALSE on failure */
    hbool_t                   index_ok = TRUE; /* will set to FALSE if not */
    volatile id_type_kernel_t id_k;
    volatile id_type_kernel_t mod_id_k = ID_TYPE_T_K__INITIALIZER;

    if ( cs ) {

        H5I_clear_stats();
    }

    if ( ( 0 > type_index ) || ( type_index >= NUM_ID_TYPES ) ) {

        success = FALSE;
        index_ok = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "try_register_type():%d: type_index = %d out of reange.\n", tid, type_index);
        }
    }

    if ( success ) {

        assert(ID_TYPE_T__TAG == types_array[type_index].tag);

        id_k = atomic_load(&(types_array[type_index].k));

        if ( ( id_k.in_progress ) || ( id_k.created ) ) {

            success = FALSE; /* another thread beat us to it */

            if ( rpt_failures ) {

                fprintf(stderr, 
                     "try_register_type():%d: type at index %d registraion already done or in progress (1).\n",
                     tid, type_index);
            }

        } else {

            mod_id_k.in_progress = TRUE;
            mod_id_k.created     = id_k.created;
            mod_id_k.discarded   = id_k.discarded;
            mod_id_k.type_id     = id_k.type_id;

            if ( ! atomic_compare_exchange_strong(&(types_array[type_index].k), &id_k, mod_id_k) ) {

                success = FALSE; /* another thread beat us to it */

                if ( rpt_failures ) {

                    fprintf(stderr, 
                     "try_register_type():%d: type at index %d registraion already done or in progress (2).\n",
                     tid, type_index);
                }
            }
        }
    }

    if ( success ) {

        hid_t type_id;

        id_k = atomic_load(&(types_array[type_index].k));

        type_id = H5Iregister_type((size_t)0, 0, types_array[type_index].free_func);

        if ( H5I_BADID == type_id ) {

            mod_id_k.in_progress = FALSE;
            mod_id_k.created     = id_k.created;
            mod_id_k.discarded   = id_k.discarded;
            mod_id_k.type_id     = id_k.type_id;

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "try_register_type():%d: call to H5Iregister_type() failed for type at index %d.\n",
                        tid, type_index);
            }
        } else {

            mod_id_k.in_progress = FALSE;
            mod_id_k.created     = TRUE;
            mod_id_k.discarded   = id_k.discarded;
            mod_id_k.type_id     = type_id;
        }

        if ( ! atomic_compare_exchange_strong(&(types_array[type_index].k), &id_k, mod_id_k) ) {

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "try_register_type():%d: update of id_type_ptr->k failed.  index = %d, id = 0x%llx.\n",
                        tid, type_index, (unsigned long long)type_id);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iregister_type");
    }

    if ( index_ok ) {

        if ( success ) {

            atomic_fetch_add(&(types_array[type_index].successful_registers), 1ULL);

        } else {

            atomic_fetch_add(&(types_array[type_index].failed_registers), 1ULL);
        }
    }

    return;

} /* try_register_type() */


/***********************************************************************************************
 * clear_type()
 *
 *    If it is marked as existing, call H5Iclear_type() on the type ID associated with the 
 *    instance of id_type_t pointed to by id_type_ptr, with the supplied force flag, and 
 *    update *id_type_ptr accordingly.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and 1 if any error (including non-existance of the 
 *    target type) is detected.
 *
 ***********************************************************************************************/

int clear_type(id_type_t * id_type_ptr, hbool_t force, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t          success = TRUE; /* will set to FALSE on failure */
    id_type_kernel_t id_type_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ) {

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "clear_type():%d: initial sanity checks failed.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( success ) {

        if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr,
                        "clear_id():%d: target id type either in progress, not created, or discarded on entry.\n",
                        tid);
            }
        }
    }

    if ( success ) {

        if ( H5Iclear_type((H5I_type_t)(id_type_k.type_id), force) != SUCCEED ) {

            success = FALSE;
            atomic_fetch_add(&(id_type_ptr->failed_clears), 1ULL);

            if ( rpt_failures ) {

                fprintf(stderr, "clear_type():%d: H5Iclear_type(0x%llx, %d) reports failure.\n", 
                        tid, (unsigned long long)(id_type_k.type_id), (int)force);
            }
        } else {

            atomic_fetch_add(&(id_type_ptr->successful_clears), 1ULL);
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iclear_type");
    }

    return( success ? 0 : 1 );

} /* clear_type() */


/***********************************************************************************************
 * try_clear_type()
 *
 *    Attempt to clear the type associated with it with types_array[type_index] and update that
 *    instance of id_type_t accordingly.  
 *
 *    Call H5Iclear_type() on types_array[type_index].k.type_id with the supplied force flag, and
 *    update types_array[type_index] accordingly.  
 *
 *    Note that types_array[type_index].k.type_id may not be defined, or the target type may 
 *    have been deteted when the call is made.  However, the call to H5Iclear_type() is made
 *    regardless, and the results recorded.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *                                                         JRM -- 4/2/24
 *
 *    Returns: void
 *
 ***********************************************************************************************/

void try_clear_type(int type_index, hbool_t force, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t                   index_ok = TRUE;
    hbool_t                   success = TRUE; /* will set to FALSE on failure */
    volatile id_type_kernel_t id_type_k;

    if ( ( 0 > type_index ) || ( type_index >= NUM_ID_TYPES ) ) {

        success = FALSE;
        index_ok = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "try_clear_type():%d: type_index = %d out of reange.\n", tid, type_index);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    if ( success ) {

        assert( ID_TYPE_T__TAG == types_array[type_index].tag );

        id_type_k = atomic_load(&(types_array[type_index].k));

        if ( H5Iclear_type((H5I_type_t)(id_type_k.type_id), force) != SUCCEED ) {

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "try_clear_type():%d: H5Iclear_type(0x%llx, %d) reports failure.\n", 
                        tid, (unsigned long long)(id_type_k.type_id), (int)force);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iclear_type");
    }

    if ( index_ok ) {

        if ( success ) {

            atomic_fetch_add(&(types_array[type_index].successful_clears), 1ULL);

        } else {

            atomic_fetch_add(&(types_array[type_index].failed_clears), 1ULL);
        }
    }

    return;

} /* try_clear_type() */


/***********************************************************************************************
 * destroy_type()()
 *
 *    If it is marked as existing, call H5Idestroy_type() on the type ID associated with the 
 *    instance of id_type_t pointed to by id_type_ptr, and update *id_type_ptr accordingly.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and 1 if any error (including non-existance of the 
 *    target type) is detected.
 *
 ***********************************************************************************************/

int destroy_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t          success = TRUE; /* will set to FALSE on failure */
    hbool_t          destroy_succeeded;
    int              retries = -1;
    id_type_kernel_t id_type_k;
    id_type_kernel_t mod_id_type_k = ID_TYPE_T_K__INITIALIZER;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ) {

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "destroy_type():%d: initial sanity checks failed.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( success ) {

        if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr,
                 "destroy_type():%d: target id type either in progress, not created, or discarded on entry.\n",
                 tid);
            }
        }
    }

    if ( success ) {

        if ( H5Idestroy_type((H5I_type_t)(id_type_k.type_id)) != SUCCEED ) {

            success = FALSE;
            destroy_succeeded = FALSE;
            atomic_fetch_add(&(id_type_ptr->failed_destroys), 1ULL);

        } else {

            destroy_succeeded = TRUE;
            atomic_fetch_add(&(id_type_ptr->successful_destroys), 1ULL);
        }

        if ( destroy_succeeded ) { /* set the discarded flag */

            id_type_k = atomic_load(&(id_type_ptr->k));

            while ( ! id_type_k.discarded ) {

                mod_id_type_k.in_progress = id_type_k.in_progress;
                mod_id_type_k.created     = id_type_k.created;
                mod_id_type_k.discarded   = TRUE;
                mod_id_type_k.type_id     = 0;

                atomic_compare_exchange_strong(&(id_type_ptr->k), &id_type_k, mod_id_type_k);

                id_type_k = atomic_load(&(id_type_ptr->k));

                retries++;
            }

            if ( retries > 0 ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                           "dec_ref():%d: %d retries needed  to mark type as discarded after H5Idestroy_type(0x%llx) reports success.\n", 
                           tid, retries, (unsigned long long)(id_type_k.type_id));
                }
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idestroy_type");
    }

    return(success ? 0 : 1);

} /* destroy_type() */


/***********************************************************************************************
 * try_destroy_type()
 *
 *    Attempt to destroy the type associated with it with types_array[type_index] and update that
 *    instance of id_type_t accordingly.
 *
 *    Call H5Idestroy_type() on types_array[type_index].k.type_id, and update 
 *    types_array[type_index] accordingly.
 *
 *    Examine the kernel of types_array[type_index] before and after to determine 
 *    the expected result, and flag an assert if the result is contrary to the expected one.
 *    Note that the expected result may not be known -- in which case the test is ambiguous.
 *
 *    Note that types_array[type_index].k.type_id may not be defined, or the target type may
 *    have been deteted when the call is made.  However, the call to H5Iclear_type() is made
 *    regardless, and the results recorded.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr
 *    if an error is detected.  If such an error message is generated, the triggering thread
 *    (given in the tid field) is reported.
 *                                                         JRM -- 4/2/24
 *
 *    Returns: 1 if the result is ambiguous, and 0 otherwise.
 *
 ***********************************************************************************************/

int try_destroy_type(int type_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t                   success = TRUE; /* will set to FALSE on failure */
    int                       retries = -1;
    int                       ambiguous_result = 0;
    herr_t                    result;
    volatile id_type_kernel_t pre_id_type_k;
    volatile id_type_kernel_t post_id_type_k;
    volatile id_type_kernel_t id_type_k;
    id_type_kernel_t          mod_id_type_k = ID_TYPE_T_K__INITIALIZER;

    assert( ( 0 <= type_index ) && ( type_index < NUM_ID_TYPES ));
    assert( ID_TYPE_T__TAG == types_array[type_index].tag );

    if ( cs ) {

        H5I_clear_stats();
    }


    if ( success ) {

        pre_id_type_k = atomic_load(&(types_array[type_index].k));

        result = H5Idestroy_type((H5I_type_t)(pre_id_type_k.type_id));

        post_id_type_k = atomic_load(&(types_array[type_index].k));
 

        if ( result != SUCCEED ) {

            success = FALSE;

        } else {

            success = TRUE;

            id_type_k = atomic_load(&(types_array[type_index].k));

            while ( ! id_type_k.discarded ) {

                mod_id_type_k.in_progress = id_type_k.in_progress;
                mod_id_type_k.created     = id_type_k.created;
                mod_id_type_k.discarded   = TRUE;
                mod_id_type_k.type_id     = 0;

                atomic_compare_exchange_strong(&(types_array[type_index].k), &id_type_k, mod_id_type_k);

                id_type_k = atomic_load(&(types_array[type_index].k));

                retries++;
            }

            if ( retries > 0 ) {

                if ( rpt_failures ) {

                    fprintf(stderr, 
                           "try_destroy_type():%d: %d retries needed to mark type (0x%llx) as discarded.\n", 
                           tid, retries, (unsigned long long)(id_type_k.type_id));
                }
            }
        }


        if ( ( ( ! pre_id_type_k.created ) && ( ! post_id_type_k.created ) ) ||
             ( ( pre_id_type_k.discarded ) && ( post_id_type_k.discarded ) ) ) {

            /* Type did not exists at the time that H5Idestroy_type() was called -- thus
             * the call should have failed.
             */
            assert( ! success );

        } else if ( ( ( pre_id_type_k.created ) && ( post_id_type_k.created ) ) ||
                    ( ( ! pre_id_type_k.discarded ) && ( ! post_id_type_k.discarded ) ) ) {

            /* the type existed when H5Idestroy_type() case called -- thus the call should
             * have succeeded.
             */
            assert( success );

        } else {

            /* the type may or may not have existed at the time that H5Idestroy_type() was 
             * called -- thus the test result is ambiguous.
             */
            ambiguous_result++;
        }
    }

    if ( success ) {

        atomic_fetch_add(&(types_array[type_index].successful_destroys), 1ULL);

    } else {

        atomic_fetch_add(&(types_array[type_index].failed_destroys), 1ULL);
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idestroy_type");
    }

    return(ambiguous_result);

} /* try_destroy_type() */


/***********************************************************************************************
 * register_id()
 *
 *    Register a new id, associate it with the supplied instances of id_instance_t and 
 *    id_object_t, and update those structures accordingly.  
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and 1 if any error is detected.
 *
 ***********************************************************************************************/

int register_id(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_instance_kernel_t mod_id_inst_k = ID_INSTANCE_K_T__INITIALIZER;
    id_object_kernel_t   id_obj_k;
    id_object_kernel_t   mod_id_obj_k = ID_OBJECT_K_T__INITIALIZER;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ||
         ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ||
         ( NULL == id_obj_ptr )  || ( ID_OBJECT_T__TAG != id_obj_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "register_id():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));
    id_obj_k  = atomic_load(&(id_obj_ptr->k));

    if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, 
                    "register_id():%d: target id type either in progress, not created, or discarded on entry.\n",
                    tid);
        }
    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success )
    {
        if ( ( id_inst_k.in_progress ) || ( id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "register_id():%d: target id inst in progress, not created, or discarded on entry.\n",
                        tid);
            }
        }
    }

    if ( success )
    {
        if ( ( id_obj_k.in_progress ) || ( id_obj_k.allocated ) || ( id_obj_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "register_id():%d: target id obj in progress, not created, or discarded on entry.\n",
                        tid);
            }
        }
    }

    if ( success ) {

        mod_id_inst_k.in_progress   = TRUE;
        mod_id_inst_k.created       = id_inst_k.created;
        mod_id_inst_k.discarded     = id_inst_k.discarded;
        mod_id_inst_k.future        = id_inst_k.future;
        mod_id_inst_k.realized      = id_inst_k.realized;
        mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
        mod_id_inst_k.id            = id_inst_k.id;

        if ( ! atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "register_id():%d: can't mark target id inst in progress.\n", tid);
            }
        } else {

            id_inst_k = atomic_load(&(id_inst_ptr->k)); /* get fresh copy */
        }
    }

    if ( success ) { 

        mod_id_obj_k.in_progress         = TRUE;
        mod_id_obj_k.allocated           = id_obj_k.allocated;
        mod_id_obj_k.discarded           = id_obj_k.discarded;
        mod_id_obj_k.future              = id_obj_k.future;
        mod_id_obj_k.real_id_def_in_prog = id_obj_k.real_id_def_in_prog;
        mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
        mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
        mod_id_obj_k.future_id_discarded = id_obj_k.future_id_discarded;
        mod_id_obj_k.id                  = id_obj_k.id;
        
        if ( ! atomic_compare_exchange_strong(&(id_obj_ptr->k), &id_obj_k, mod_id_obj_k) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "register_id():%d: can't mark target id obj in progress.\n", tid);
            }

            /* in progress flag is set in id_inst_ptr->k.  Must reset it */
            mod_id_inst_k.in_progress   = FALSE;
            mod_id_inst_k.created       = id_inst_k.created;
            mod_id_inst_k.discarded     = id_inst_k.discarded;
            mod_id_inst_k.future        = id_inst_k.future;
            mod_id_inst_k.realized      = id_inst_k.realized;
            mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
            mod_id_inst_k.id            = id_inst_k.id;

            if ( ! atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k) ) {

                if ( rpt_failures ) {

                    fprintf(stderr, "register_id():%d: can't reset in progress on mark target id inst.\n", tid);
                }
            }
        } else {

            id_obj_k  = atomic_load(&(id_obj_ptr->k));
        }
    }

    if ( success ) {

        assert(-1 == atomic_load(&(id_obj_ptr->id_index)));
        assert(-1 == atomic_load(&(id_obj_ptr->old_id_index)));
        assert(-1 == atomic_load(&(id_inst_ptr->obj_index)));

        atomic_store(&(id_obj_ptr->id_index), id_inst_ptr->index);
        atomic_store(&(id_inst_ptr->obj_index), id_obj_ptr->index);

        mod_id_inst_k.in_progress   = FALSE;
        mod_id_inst_k.created       = id_inst_k.created;
        mod_id_inst_k.discarded     = id_inst_k.discarded;
        mod_id_inst_k.future        = id_inst_k.future;
        mod_id_inst_k.realized      = id_inst_k.realized;
        mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
        mod_id_inst_k.id            = id_inst_k.id;

        mod_id_obj_k.in_progress         = FALSE;
        mod_id_obj_k.allocated           = id_obj_k.allocated;
        mod_id_obj_k.discarded           = id_obj_k.discarded;
        mod_id_obj_k.future              = id_obj_k.future;
        mod_id_obj_k.real_id_def_in_prog = id_obj_k.real_id_def_in_prog;
        mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
        mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
        mod_id_obj_k.future_id_discarded = id_obj_k.future_id_discarded;
        mod_id_obj_k.id                  = id_obj_k.id;

        id = H5Iregister(type, (void *)id_obj_ptr);

        if ( id != H5I_INVALID_HID ) { 

            mod_id_inst_k.created = TRUE;
            mod_id_inst_k.id      = id;

            mod_id_obj_k.allocated   = TRUE;
            mod_id_obj_k.id          = id;

        } else {

            success = FALSE;

            assert(FALSE);

            if ( rpt_failures ) {

                fprintf(stderr, "register_id():%d: Call to H5Iregister() failed.\n", tid);
            }
        } 

        if ( ! atomic_compare_exchange_strong(&(id_obj_ptr->k), &id_obj_k, mod_id_obj_k) ) {

            success = FALSE;

            assert(FALSE);

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "register_id():%d: Can't update id object for id registration success or failure.\n",
                        tid);
            }
        }

        if ( ! atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k) ) {

            success = FALSE;

            assert(FALSE);

            if ( rpt_failures ) {

                fprintf(stderr, 
                        "register_id():%d: Can't update id instande for id registration success or failure.\n",
                        tid);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iregister");
    }

    return(success ? 0 : 1);

} /* register_id() */ 


/***********************************************************************************************
 * try_register_id()
 *
 *    Attempt to register a new id of type id_instance_array[id_index].type_index and 
 *    associated object objects_array[obj_index], and update id_instance_array[id_index] and
 *    objects_array[obj_index] accordingly.
 *
 *    Note that there is no guarnatee that the type exists.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *                                                        JRM -- 4/2/24
 *
 ***********************************************************************************************/

void try_register_id(int id_index, int obj_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    int                  type_index;
    H5I_type_t           type_id = H5I_BADID;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_instance_kernel_t mod_id_inst_k = ID_INSTANCE_K_T__INITIALIZER;
    id_object_kernel_t   id_obj_k;
    id_object_kernel_t   mod_id_obj_k = ID_OBJECT_K_T__INITIALIZER;

    if ( cs ) {

        H5I_clear_stats();
    }

    assert( ( 0 <= id_index ) && ( id_index < NUM_ID_INSTANCES ) );
    assert( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) );

    if ( success ) {

        assert( ID_INSTANCE_T__TAG == id_instance_array[id_index].tag );
        assert( ID_OBJECT_T__TAG   == objects_array[obj_index].tag );

        type_index = id_instance_array[id_index].type_index;
        type_id    = atomic_load(&(id_instance_array[id_index].type_id));

        if ( ( type_index < 0 ) || ( type_index >= NUM_ID_TYPES ) ) {

            if ( rpt_failures ) {
            
                fprintf(stderr, "try_register_id():%d: type_index = %d out of reange.\n", tid, type_index);
            }
            assert( ( 0 <= type_index ) && ( type_index < NUM_ID_TYPES ) );
        }
    }

    if ( success ) {

        id_inst_k = atomic_load(&(id_instance_array[id_index].k));
        id_obj_k  = atomic_load(&(objects_array[obj_index].k));

        assert( ID_TYPE_T__TAG == types_array[type_index].tag );

        if ( H5I_BADID == type_id ) {

            id_type_k = atomic_load(&(types_array[type_index].k));
            type_id = id_type_k.type_id;

            if ( H5I_BADID != type_id ) {

                assert( ( H5I_BADID < type_id ) && ( type_id < H5I_MAX_NUM_TYPES ) );

                atomic_store(&(id_instance_array[id_index].type_id), type_id);
            }
        }
    }


    if ( success )
    {
        if ( ( id_inst_k.in_progress ) || ( id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            if ( rpt_failures ) {

                fprintf(stderr, 
                     "try_register_id():%d: target id inst in progress, not created, or discarded on entry.\n",
                     tid);
            }

            assert(FALSE);
        }
    }

    if ( success )
    {
        if ( ( id_obj_k.in_progress ) || ( id_obj_k.allocated ) || ( id_obj_k.discarded ) ) {

            if ( rpt_failures ) {

                fprintf(stderr, 
                     "try_register_id():%d: target id obj in progress, not created, or discarded on entry.\n",
                     tid);
            }

            assert(FALSE);
        }
    }

    if ( success ) {

        mod_id_inst_k.in_progress   = TRUE;
        mod_id_inst_k.created       = id_inst_k.created;
        mod_id_inst_k.discarded     = id_inst_k.discarded;
        mod_id_inst_k.future        = id_inst_k.future;
        mod_id_inst_k.realized      = id_inst_k.realized;
        mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
        mod_id_inst_k.id            = id_inst_k.id;

        if ( ! atomic_compare_exchange_strong(&(id_instance_array[id_index].k), &id_inst_k, mod_id_inst_k) ) {

            if ( rpt_failures ) {

                fprintf(stderr, "try_register_id():%d: can't mark target id inst in progress.\n", tid);
            }

            assert(FALSE);

        } else {

            id_inst_k = atomic_load(&(id_instance_array[id_index].k)); /* get fresh copy */

            assert( id_inst_k.in_progress );
        }
    }

    if ( success ) { 

        mod_id_obj_k.in_progress         = TRUE;
        mod_id_obj_k.allocated           = id_obj_k.allocated;
        mod_id_obj_k.discarded           = id_obj_k.discarded;
        mod_id_obj_k.future              = id_obj_k.future;
        mod_id_obj_k.real_id_def_in_prog = id_obj_k.real_id_def_in_prog;
        mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
        mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
        mod_id_obj_k.future_id_discarded = id_obj_k.future_id_discarded;
        mod_id_obj_k.id                  = id_obj_k.id;
        
        if ( ! atomic_compare_exchange_strong(&(objects_array[obj_index].k), &id_obj_k, mod_id_obj_k) ) {

            if ( rpt_failures ) {

                fprintf(stderr, "try_register_id():%d: can't mark target id obj in progress.\n", tid);
            }

            /* in progress flag is set in id_instance_array[id_index].k.  Must reset it */
            mod_id_inst_k.in_progress   = FALSE;
            mod_id_inst_k.created       = id_inst_k.created;
            mod_id_inst_k.discarded     = id_inst_k.discarded;
            mod_id_inst_k.future        = id_inst_k.future;
            mod_id_inst_k.realized      = id_inst_k.realized;
            mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
            mod_id_inst_k.id            = id_inst_k.id;

            if ( ! atomic_compare_exchange_strong(&(id_instance_array[id_index].k), &id_inst_k, mod_id_inst_k) ) {

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "try_register_id():%d: can't reset in progress on target id inst.\n", tid);
                }
            }

            assert( FALSE ) ;

        } else {

            id_obj_k  = atomic_load(&(objects_array[obj_index].k)); /* get fresh copy */

            assert( id_obj_k.in_progress );
        }
    }

    if ( success ) {

        assert(-1 == atomic_load(&(objects_array[obj_index].id_index)));
        assert(-1 == atomic_load(&(objects_array[obj_index].old_id_index)));
        assert(-1 == atomic_load(&(id_instance_array[id_index].obj_index)));

        atomic_store(&(objects_array[obj_index].id_index), id_instance_array[id_index].index);
        atomic_store(&(id_instance_array[id_index].obj_index), objects_array[obj_index].index);

        mod_id_inst_k.in_progress   = FALSE;
        mod_id_inst_k.created       = id_inst_k.created;
        mod_id_inst_k.discarded     = id_inst_k.discarded;
        mod_id_inst_k.future        = id_inst_k.future;
        mod_id_inst_k.realized      = id_inst_k.realized;
        mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
        mod_id_inst_k.id            = id_inst_k.id;

        mod_id_obj_k.in_progress         = FALSE;
        mod_id_obj_k.allocated           = id_obj_k.allocated;
        mod_id_obj_k.discarded           = id_obj_k.discarded;
        mod_id_obj_k.future              = id_obj_k.future;
        mod_id_obj_k.real_id_def_in_prog = id_obj_k.real_id_def_in_prog;
        mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
        mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
        mod_id_obj_k.future_id_discarded = id_obj_k.future_id_discarded;
        mod_id_obj_k.id                  = id_obj_k.id;

        id = H5Iregister(type_id, (void *)(&(objects_array[obj_index])));

        if ( id != H5I_INVALID_HID ) { 

            mod_id_inst_k.created = TRUE;
            mod_id_inst_k.id      = id;

            mod_id_obj_k.allocated   = TRUE;
            mod_id_obj_k.id          = id;

        } else {

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "try_register_id():%d: Call to H5Iregister() failed.\n", tid);
            }

            assert(FALSE);
        } 

        if ( ! atomic_compare_exchange_strong(&(objects_array[obj_index].k), &id_obj_k, mod_id_obj_k) ) {

            if ( rpt_failures ) {

                fprintf(stderr, 
                      "try_register_id():%d: Can't update id object for id registration success on failure.\n",
                      tid);
            }

            assert(FALSE);
        }

        if ( ! atomic_compare_exchange_strong(&(id_instance_array[id_index].k), &id_inst_k, mod_id_inst_k) ) {

            if ( rpt_failures ) {

                fprintf(stderr, 
                    "try_register_id():%d: Can't update id instance for id registration success or failure.\n",
                    tid);
            }

            assert(FALSE);
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iregister");
    }

    if ( success ) {

        atomic_fetch_add(&(id_instance_array[id_index].successful_registrations), 1ULL);

    } else {

        atomic_fetch_add(&(id_instance_array[id_index].failed_registrations), 1ULL);
    }

    return;

} /* try_register_id() */ 


/***********************************************************************************************
 * register_future_id()
 *
 *    Register a future ID.  Note that the creation of the associated real ID, and the update
 *    of the object associated with the future ID to point to the real ID are seprate operations.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and 1 if any error is detected.
 *
 ***********************************************************************************************/

int register_future_id(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                       H5I_future_realize_func_t realize_cb, H5I_future_discard_func_t discard_cb,
                       hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_instance_kernel_t mod_id_inst_k = ID_INSTANCE_K_T__INITIALIZER;
    id_object_kernel_t   id_obj_k;
    id_object_kernel_t   mod_id_obj_k = ID_OBJECT_K_T__INITIALIZER;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ||
         ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ||
         ( NULL == id_obj_ptr )  || ( ID_OBJECT_T__TAG != id_obj_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "register_future_id():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));
    id_obj_k  = atomic_load(&(id_obj_ptr->k));

    if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, 
           "register_future_id():%d: target id type either in progress, not created, or discarded on entry.\n",
                    tid);
        }
    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success )
    {
        if ( ( id_inst_k.in_progress ) || ( id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                  "register_future_id():%d: target id inst in progress, not created, or discarded on entry.\n",
                  tid);
            }
        }
    }

    if ( success )
    {
        if ( ( id_obj_k.in_progress ) || ( id_obj_k.allocated ) || ( id_obj_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, 
                   "register_future_id():%d: target id obj in progress, not created, or discarded on entry.\n",
                   tid);
            }
        }
    }

    if ( success ) {

        mod_id_inst_k.in_progress   = TRUE;
        mod_id_inst_k.created       = id_inst_k.created;
        mod_id_inst_k.discarded     = id_inst_k.discarded;
        mod_id_inst_k.future        = id_inst_k.future;
        mod_id_inst_k.realized      = id_inst_k.realized;
        mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
        mod_id_inst_k.id            = id_inst_k.id;

        if ( ! atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "register_future_id():%d: can't mark target id inst in progress.\n", tid);
            }
        } else {

            id_inst_k = atomic_load(&(id_inst_ptr->k)); /* get fresh copy */
        }
    }

    if ( success ) { 

        mod_id_obj_k.in_progress         = TRUE;
        mod_id_obj_k.allocated           = id_obj_k.allocated;
        mod_id_obj_k.discarded           = id_obj_k.discarded;
        mod_id_obj_k.future              = id_obj_k.future;
        mod_id_obj_k.real_id_def_in_prog = id_obj_k.real_id_def_in_prog;
        mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
        mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
        mod_id_obj_k.future_id_discarded = id_obj_k.future_id_discarded;
        mod_id_obj_k.id                  = id_obj_k.id;
        
        if ( ! atomic_compare_exchange_strong(&(id_obj_ptr->k), &id_obj_k, mod_id_obj_k) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "register_future_id():%d: can't mark target id obj in progress.\n", tid);
            }

            /* in progress flag is set in id_inst_ptr->k.  Must reset it */
            mod_id_inst_k.in_progress   = FALSE;
            mod_id_inst_k.created       = id_inst_k.created;
            mod_id_inst_k.discarded     = id_inst_k.discarded;
            mod_id_inst_k.future        = id_inst_k.future;
            mod_id_inst_k.realized      = id_inst_k.realized;
            mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
            mod_id_inst_k.id            = id_inst_k.id;

            if ( ! atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k) ) {

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "register_future_id():%d: can't reset in progress on mark target id inst.\n", tid);
                }
            }
        } else {

            id_obj_k  = atomic_load(&(id_obj_ptr->k));
        }
    }

    if ( success ) {

        assert(-1 == atomic_load(&(id_obj_ptr->id_index)));
        assert(-1 == atomic_load(&(id_obj_ptr->old_id_index)));
        assert(-1 == atomic_load(&(id_inst_ptr->obj_index)));

        atomic_store(&(id_obj_ptr->id_index), id_inst_ptr->index);
        atomic_store(&(id_inst_ptr->obj_index), id_obj_ptr->index);

        mod_id_inst_k.in_progress   = FALSE;
        mod_id_inst_k.created       = id_inst_k.created;
        mod_id_inst_k.discarded     = id_inst_k.discarded;
        mod_id_inst_k.future        = id_inst_k.future;
        mod_id_inst_k.realized      = id_inst_k.realized;
        mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
        mod_id_inst_k.id            = id_inst_k.id;

        mod_id_obj_k.in_progress         = FALSE;
        mod_id_obj_k.allocated           = id_obj_k.allocated;
        mod_id_obj_k.discarded           = id_obj_k.discarded;
        mod_id_obj_k.future              = id_obj_k.future;
        mod_id_obj_k.real_id_def_in_prog = id_obj_k.real_id_def_in_prog;
        mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
        mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
        mod_id_obj_k.future_id_discarded = id_obj_k.future_id_discarded;
        mod_id_obj_k.id                  = id_obj_k.id;

        id = H5Iregister_future(type, (void *)id_obj_ptr, realize_cb, discard_cb);

        if ( id != H5I_INVALID_HID ) { 

            mod_id_inst_k.created = TRUE;
            mod_id_inst_k.future  = TRUE;
            mod_id_inst_k.id      = id;

            mod_id_obj_k.allocated   = TRUE;
            mod_id_obj_k.future      = TRUE;
            mod_id_obj_k.id          = id;

        } else {

            success = FALSE;

            assert(FALSE);

            if ( rpt_failures ) {

                fprintf(stderr, "register_future__id():%d: Call to H5Iregister_future() failed.\n", tid);
            }
        } 

        if ( ! atomic_compare_exchange_strong(&(id_obj_ptr->k), &id_obj_k, mod_id_obj_k) ) {

            success = FALSE;

            assert(FALSE);

            if ( rpt_failures ) {

                fprintf(stderr, 
                   "register_future_id():%d: Can't update id object for id registration success or failure.\n",
                   tid);
            }
        }

        if ( ! atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k) ) {

            success = FALSE;

            assert(FALSE);

            if ( rpt_failures ) {

                fprintf(stderr, 
                 "register_future_id():%d: Can't update id instande for id registration success or failure.\n",
                 tid);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iregister");
    }

    return(success ? 0 : 1);

} /* register_future_id() */ 


/***********************************************************************************************
 * link_real_and_future_ids
 *
 *    Link the supplied real id object with the supplied future id object.  Once this is 
 *    done, the next look up of the future ID will cause the future ID object to be replaced
 *    with the real ID object.
 *
 *    Note that both the real and future IDS must be registered before this function is callec.
 *
 *                                                              JRM -- 3/8/24
 *
 * Returs:  
 *
 *      zero on success, and one otherwise.
 *
 * Changes;
 *
 *      None.
 *
 ***********************************************************************************************/

int link_real_and_future_ids(id_object_t * future_id_obj_ptr, id_object_t * real_id_obj_ptr, 
                             hbool_t rpt_failures)
{
    hbool_t                       done = FALSE;
    hbool_t                       success = TRUE;
    int                           real_id_inst_index;
    volatile id_instance_t      * real_id_inst_ptr;
    volatile id_instance_kernel_t real_id_inst_k;
    id_instance_kernel_t          mod_real_id_inst_k = ID_INSTANCE_K_T__INITIALIZER;
    volatile id_object_kernel_t   real_id_obj_k;
    volatile id_object_kernel_t   future_id_obj_k;
    id_object_kernel_t            mod_future_id_obj_k = ID_OBJECT_K_T__INITIALIZER;

    if ( ( NULL == future_id_obj_ptr )  || ( ID_OBJECT_T__TAG != future_id_obj_ptr->tag ) ||
         ( NULL == real_id_obj_ptr )    || ( ID_OBJECT_T__TAG != real_id_obj_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "link_real_and_future_ids(): Invalid params on entry.\n");
        }
    }

    /* First mark the instance of id_instance_t associated with *real_id_obj_ptr as being 
     * the source real ID for a future ID.  
     *
     * Note that there isn't much of a problem if we succed in marking the real id instance
     * as being the source real ID for a future ID and then fail to set up the future id 
     * instance.
     */
    while ( ( success ) && ( ! done ) ) {

        real_id_obj_k    = atomic_load(&(real_id_obj_ptr->k));

        if ( success ) {

            if ( ( real_id_obj_k.in_progress ) || ( ! real_id_obj_k.allocated ) || 
                 ( real_id_obj_k.discarded ) ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "link_real_and_future_ids(): real id obj in progress, not created, or discarded on entry(1).\n");
                }
            }
        }

        if ( success ) {

            real_id_inst_index = atomic_load(&(real_id_obj_ptr->id_index));

            if ( ( real_id_inst_index < 0) || ( real_id_inst_index >= NUM_ID_INSTANCES ) ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, "link_real_and_future_ids(): real id instance index out of range).\n");
                }
            }
        }

        if ( success ) {

            real_id_inst_ptr = &(id_instance_array[real_id_inst_index]);

            if ( ( NULL == real_id_inst_ptr ) || ( ID_INSTANCE_T__TAG != real_id_inst_ptr->tag ) ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, "link_real_and_future_ids(): Invalid real_id_inst_ptr.\n");
                }
            }
        }

        if ( success ) {

            real_id_inst_k = atomic_load(&(real_id_inst_ptr->k));

            if ( real_id_inst_k.future_id_src ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "link_real_and_future_ids(): future_id_src already set in real ID inst.\n");
                }
            }
        }

        if ( success ) {

            mod_real_id_inst_k.in_progress   = real_id_inst_k.in_progress;
            mod_real_id_inst_k.created       = real_id_inst_k.created;
            mod_real_id_inst_k.discarded     = real_id_inst_k.discarded;
            mod_real_id_inst_k.future        = real_id_inst_k.future;
            mod_real_id_inst_k.realized      = real_id_inst_k.realized;
            mod_real_id_inst_k.future_id_src = TRUE;
            mod_real_id_inst_k.id            = real_id_inst_k.id;

            if ( atomic_compare_exchange_strong(&(real_id_inst_ptr->k), 
                                                &real_id_inst_k, mod_real_id_inst_k) ) {
                done = TRUE;

            } else {

                continue;
            }
        }
    }

    if ( success ) {

        done = FALSE;
    }


    while ( ( success ) && ( ! done ) ) {

        future_id_obj_k  = atomic_load(&(future_id_obj_ptr->k));
        real_id_obj_k    = atomic_load(&(real_id_obj_ptr->k));

        if ( success ) {

            if ( ( future_id_obj_k.in_progress ) || ( ! future_id_obj_k.allocated ) || 
                 ( future_id_obj_k.discarded ) ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "link_real_and_future_ids(): future id obj in progress, not created, or discarded on entry.\n");
                }
            } 
        }

        if ( success ) {

            if ( ( real_id_obj_k.in_progress ) || ( ! real_id_obj_k.allocated ) || 
                 ( real_id_obj_k.discarded ) ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "link_real_and_future_ids(): real id obj in progress, not created, or discarded on entry(2).\n");
                }
            }
        }

        if ( success ) {

            if ( ! future_id_obj_k.future ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, "link_real_and_future_ids(): future id obj not marked as future.\n");
                }
            }
        }

        if ( success ) {

            if ( real_id_obj_k.future ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, "link_real_and_future_ids(): real id obj is marked as future.\n");
                }
            }
        }

        if ( success ) {

            if ( future_id_obj_k.future_id_discarded ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, "link_real_and_future_ids(): future id already discarded.\n"); 
                }
            }
        }

        if ( success ) {

            if ( future_id_obj_k.real_id_defined ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, "link_real_and_future_ids(): real id already defined.\n"); 
                }
            }
        }
              
        if ( success ) {

            if ( future_id_obj_k.real_id_def_in_prog ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "link_real_and_future_ids(): link of real and future ID alread in progress.\n"); 
                }
            }
        }
        
        if ( success ) {

            /* attempt to set the real_id_def_in_progress flag.  IF this fails, re-start the 
             * loop and try again.  The objective here is to serialize attempts at setting the 
             * the real ID -- thus forcing exactly one attempt to succeed.
             */

            mod_future_id_obj_k.in_progress         = future_id_obj_k.in_progress;
            mod_future_id_obj_k.allocated           = future_id_obj_k.allocated;
            mod_future_id_obj_k.discarded           = future_id_obj_k.discarded;
            mod_future_id_obj_k.future              = future_id_obj_k.future;
            mod_future_id_obj_k.real_id_def_in_prog = TRUE;
            mod_future_id_obj_k.real_id_defined     = future_id_obj_k.real_id_defined;
            mod_future_id_obj_k.future_id_realized  = future_id_obj_k.future_id_realized;
            mod_future_id_obj_k.future_id_discarded = future_id_obj_k.future_id_discarded;
            mod_future_id_obj_k.id                  = future_id_obj_k.id;

            if ( atomic_compare_exchange_strong(&(future_id_obj_ptr->k), 
                                                &future_id_obj_k, mod_future_id_obj_k) ) {
                done = TRUE;

            } else {

                continue;
            }
        }
    } /* while ( ( success ) && ( ! done ) ) */

    done = FALSE;

    while ( ( success ) && ( ! done ) ) {

        /* we have successfully set future_id_obj_ptr->k.real_id_def_in_prog to TRUE.  Now 
         * set future_id_obj_ptr->real_id and future_id_obj_ptr->real_id_obj_index, and then 
         * reset future_id_obj_ptr->k.real_id_def_in_prog and set 
         * future_id_obj_ptr->k.real_id_defined.
         */

        int   real_id_obj_index;
        hid_t real_id;

        real_id_obj_index = real_id_obj_ptr->index;
        real_id = real_id_obj_k.id;

        /* set the real_id and real_id_obj_index fields. */
        atomic_store(&(future_id_obj_ptr->real_id), real_id);
        atomic_store(&(future_id_obj_ptr->real_id_obj_index), real_id_obj_index);

        /* get a fresh copy of future_id_obj_k */
        future_id_obj_k  = atomic_load(&(future_id_obj_ptr->k));

        assert( future_id_obj_k.real_id_def_in_prog );
        assert( ! future_id_obj_k.real_id_defined );
            
        /* note that the real_id_def_in_prog flag only blocks additional attempts to 
         * associate a real ID with the future ID.  Thus is is possible for other 
         * operations to occur while this association of a real ID with a future ID 
         * is in progress.  
         *
         * As a result, it is possible that the following atomic_compare_exchange_strong()
         * will fail.  If so, we will just retry.  Note that the above setting of the real_id
         * and the real_id_obj_index fields is not an issue, since these fields are not read unless
         * the real_id_defined flag is true.
         */

        mod_future_id_obj_k.in_progress         = future_id_obj_k.in_progress;
        mod_future_id_obj_k.allocated           = future_id_obj_k.allocated;
        mod_future_id_obj_k.discarded           = future_id_obj_k.discarded;
        mod_future_id_obj_k.future              = future_id_obj_k.future;
        mod_future_id_obj_k.real_id_def_in_prog = FALSE;
        mod_future_id_obj_k.real_id_defined     = TRUE;
        mod_future_id_obj_k.future_id_realized  = future_id_obj_k.future_id_realized;
        mod_future_id_obj_k.future_id_discarded = future_id_obj_k.future_id_discarded;
        mod_future_id_obj_k.id                  = future_id_obj_k.id;

        if ( atomic_compare_exchange_strong(&(future_id_obj_ptr->k), 
                                            &future_id_obj_k, mod_future_id_obj_k) ) {

            done = TRUE;
        }
    } /* while ( ( success ) && ( ! done ) ) */

    return( success ? 0 : 1 );

} /* link_real_and_future_ids() */


/***********************************************************************************************
 * object_verify()
 *
 *      Call H5Iobject_verify() and verify that it returns the expected pointer.  In general, 
 *      this will be a pointer to and instance of id_object_t as supplied by in the id_obj_ptr
 *      parameter.  However, if the call to H5Iobject_verify is expected to fail, id_obj_ptr
 *      should be NULL.
 *
 *      If the cs flag is set, the function clears statistics on entry.
 *
 *      If the ds flag is set, the function displays statistics just before exit.
 *
 *      If the rpt_failures flag is set, the function will write an error message to stderr 
 *      if an error is detected.  If such an error message is generated, the triggering thread 
 *      (given in the tid field) is reported.
 *
 *      The function returns 0 on success -- that is either:
 *
 *              H5Iobject_verify() returns a non-NULL value, and this value is equal to 
 *              id_obj_ptr, or
 *
 *              H5Iobject_verify() returns NULL and id_obj_ptr is NULL.
 *
 *      In all other cases, the function returns 1.
 *
 *                                                        JRM -- 3/14/24
 *
 * Changes:
 *
 *      None.
 *
 ***********************************************************************************************/

int object_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                  hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_object_kernel_t   id_obj_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ||
         ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ||
         ( ( NULL != id_obj_ptr ) && ( ID_OBJECT_T__TAG != id_obj_ptr->tag ) ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "object_verify():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));

    if ( id_obj_ptr ) {

        id_obj_k  = atomic_load(&(id_obj_ptr->k));
    }

    /* If the target ID is neither in progress, not created, discarded, a future ID that 
     * hasn't been realized, nor a real ID that has been used to realize a future ID, 
     * id_object_ptr must not be NULL.  
     *
     * If so, id_obj_ptr->index must equal id_inst_ptr->obj_index, and id_obj_ptr->id_index must 
     * equal id_inst_ptr->index.
     *
     * Verify that this condition holds on entry.
     */
    if ( ! ( ( id_inst_k.in_progress ) || ( ! id_inst_k.created ) || ( id_inst_k.discarded ) ||
             ( ( id_inst_k.future ) && ( ! id_inst_k.realized ) ) || 
             ( ( id_inst_k.future_id_src ) && ( -1 == atomic_load(&(id_inst_ptr->obj_index)) ) ) ) ) {

        if ( NULL == id_obj_ptr ) {

            assert(FALSE);

            success = FALSE;

            fprintf(stderr, "object_verify():%d: NULL expected object ptr for non future id.\n", tid);

        } else if ( id_inst_ptr->index != id_obj_ptr->id_index ) {

            assert(FALSE);

            success = FALSE;

            fprintf(stderr, "object_verify():%d:i id_inst_ptr->index = %d != %d = id_obj_ptr->id_index.\n", 
                    tid, id_inst_ptr->index, atomic_load(&(id_obj_ptr->id_index)));
        
        } else if ( id_obj_ptr->index != id_inst_ptr->obj_index  ) {

            assert(FALSE);

            success = FALSE;

            fprintf(stderr, "object_verify():%d:i id_obj_ptr->index = %d != %d = id_inst_ptr->obj_index.\n", 
                    tid, id_obj_ptr->index, atomic_load(&(id_inst_ptr->obj_index)));
        }        
    }

    if ( success ) {

        if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "object_verify():%d: id_obj_ptr->index = %d != %d = id_obj_ptr->id_index.\n", 
                        tid, id_obj_ptr->index, atomic_load(&(id_inst_ptr->obj_index)));
            }
        } else {

            type = (H5I_type_t)id_type_k.type_id;
        } 
    }


    /* If id_obj_ptr is not NULL, verify that the id instance has been created and not discarded.
     * Since a NULL id_obj_ptr indicates that the lookup is expected to FAIL, skip this test if 
     * id_obj_ptr is NULL.
     */
    if ( success )
    {
        if ( ( NULL != id_obj_ptr ) && 
             ( ( id_inst_k.in_progress) || ( ! id_inst_k.created ) || ( id_inst_k.discarded ) ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                   "object_verify():%d: target id inst either in progress, not created, or discarded on entry.\n",
                   tid);
            }
        } else {

            id = id_inst_k.id;
        }
    }

    if ( ( success ) && ( id_obj_ptr ) ) 
    {
        if ( ( id_obj_k.in_progress) || ( ! id_obj_k.allocated ) || ( id_obj_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                    "object_verify():%d: target id obj either in progress, not created, or discarded on entry.\n",
                    tid);
            }
        }
    }

    if ( success ) {

        if ( (void *)id_obj_ptr != H5Iobject_verify(id, type) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "object_verify():%d: H5Iobject_verify(id = 0x%llx, type = 0x%llx).\n",
                         tid, (unsigned long long)id, (unsigned long long)type);
            }
        } else if ( id_obj_ptr ) {

            /* the call to H5Iobject_verify() has returned id_obj_ptr, and id_obj_ptr is not NULL.
             * verify that id_obj_ptr->index equals id_inst_ptr->obj_index, and id_obj_ptr->id_index 
             * equals id_inst_ptr->index.
             */

            if ( id_inst_ptr->index != atomic_load(&(id_obj_ptr->id_index)) ) {

                assert(FALSE);

                success = FALSE;

                fprintf(stderr, "object_verify():%d:f id_inst_ptr->index = %d != %d = id_obj_ptr->id_index.\n",
                        tid, id_inst_ptr->index, atomic_load(&(id_obj_ptr->id_index)));
        
            } else if ( id_obj_ptr->index != atomic_load(&(id_inst_ptr->obj_index)) ) {

                assert(FALSE);

                success = FALSE;

                fprintf(stderr, 
                        "object_verify():%d:f id_obj_ptr->index = %d != %d = id_inst_ptr->obj_index.\n", 
                        tid, id_obj_ptr->index, atomic_load(&(id_inst_ptr->obj_index)));
            }        
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iobject_verify");
    }

    return(success ? 0 : 1);

} /* object_verify() */


/***********************************************************************************************
 * try_object_verify()
 *
 *      Call H5Iobject_verify() on id_instance_array[id_index].k.id, and verify that the pointer
 *      returned equals &(objects_array[obj_index]).  Update stats in id_instance_array[id_index]
 *      and objects_array[obj_index] as appropriate to refect success or failure.
 *
 *      Note that id_instance_array[id_index].k.id may or may not have been initialized on 
 *      entry.
 *
 *      If the cs flag is set, the function clears statistics on entry.
 *
 *      If the ds flag is set, the function displays statistics just before exit.
 *
 *      If the rpt_failures flag is set, the function will write an error message to stderr 
 *      if an error is detected.  If such an error message is generated, the triggering thread 
 *      (given in the tid field) is reported.
 *                                                     JRM -- 4/4/24
 *
 * returns: void
 *
 * Changes:
 *
 *      None.
 *
 ***********************************************************************************************/

int try_object_verify(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t                       success = TRUE; /* will set to FALSE on failure */
    int                           ambiguous_results = 0;
    int                           obj_index;
    int                           type_index;
    unsigned long long            pre_remove_verify_starts;
    unsigned long long            post_remove_verify_starts;
    id_object_t                 * expected_id_obj_ptr = NULL;
    id_object_t                 * reported_id_obj_ptr = NULL;
    volatile id_type_kernel_t     id_type_k;
    volatile id_instance_kernel_t pre_id_inst_k;
    volatile id_instance_kernel_t post_id_inst_k;

    if ( cs ) {

        H5I_clear_stats();
    }

    assert( ( 0 <= id_index ) && ( id_index < NUM_ID_INSTANCES ) );
    assert( ID_INSTANCE_T__TAG == id_instance_array[id_index].tag );

    type_index = id_instance_array[id_index].type_index;

    assert( ( 0 <= type_index ) && ( type_index < NUM_ID_TYPES ) );
    assert( ID_TYPE_T__TAG == types_array[type_index].tag );

    id_type_k = atomic_load(&(types_array[type_index].k));

    pre_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));

    pre_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

    if ( success ) {

        reported_id_obj_ptr = (id_object_t *)H5Iobject_verify(pre_id_inst_k.id, id_type_k.type_id);

        obj_index = atomic_load(&(id_instance_array[id_index].obj_index));

        post_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

        post_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));

        assert( ( -1 == obj_index ) || ( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) ) );
        assert( ( -1 == obj_index ) || ( ID_OBJECT_T__TAG == objects_array[obj_index].tag ) );

        if ( NULL == reported_id_obj_ptr ) {

            success = FALSE;

        } else {

            success = TRUE;

            assert( H5I_BADID != id_type_k.type_id );
            assert( H5I_INVALID_HID != pre_id_inst_k.id );

            assert( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) );
            assert( ID_OBJECT_T__TAG == objects_array[obj_index].tag );

            expected_id_obj_ptr = &(objects_array[obj_index]);

            assert( expected_id_obj_ptr == reported_id_obj_ptr );
        }

        if ( ( ! pre_id_inst_k.created ) && ( ! post_id_inst_k.created ) ) {

            /* the type did not exist when H5Iobject_verify() was called -- thus the 
             * call must fail.
             */
            assert( ! success );

            /* However, it is possible that the id is in the process of being registerd.  
             * If so, the object index is set before the created flag is set.  Thus it
             * is possible that object verify will fail, but 
             * id_instance_array[id_index].obj_index will be set.  
             *
             * At present, the only possible values for id_instance_array[id_index].obj_index
             * are -1 and id_index.  
             */
            assert( ( -1 == obj_index ) || ( id_index == obj_index ) );

        } else if ( ( pre_id_inst_k.discarded ) && ( post_id_inst_k.discarded ) ) {

            /* the type did not exist when H5Iobject_verify() was called -- thus the 
             * call must fail.
             */
            if ( success ) {

                if ( rpt_failures ) {

                    fprintf(stderr, "try_object_verify():%d: pre / post discarded = %d / %d\n",
                            tid, (int)pre_id_inst_k.discarded, (int)post_id_inst_k.discarded);
                }
                H5I_dump_stats(stdout);
            }
            assert( ! success );
            assert( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) );

        } else if ( ( pre_id_inst_k.created ) && ( post_id_inst_k.created ) &&
                    ( ! pre_id_inst_k.discarded ) && ( ! post_id_inst_k.discarded ) ) {

            if ( ( ( pre_remove_verify_starts > 0 ) || ( post_remove_verify_starts > 0 ) ) && ( ! success ) ) {

                /* A call to H5Iremove verify() may have been active about the time the call to 
                 * H5Iobject_verify() was made.  Since this would explain the failure, log an
                 * ambiguous test.
                 */
                ambiguous_results++;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "try_object_verify(%d):%d failure possibly explained by a remove verify in progress.\n",
                            id_index, tid);
                }
            } else {

                /* the id existed when H5Iobject_verify() was called -- thus the
                 * call must succeed.
                 */
                assert( success );
                assert( -1 != obj_index );
            }
        } else {

            /* the id may or may not have existed when H5Iverify_object() was called -- thus 
             * the outcome of the test is ambiguous.
             */
            ambiguous_results++;
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iobject_verify");
    }

    if ( success ) {

        atomic_fetch_add(&(id_instance_array[id_index].successful_verifies), 1ULL);
        atomic_fetch_add(&(objects_array[obj_index].accesses), 1ULL);

    } else {

        atomic_fetch_add(&(id_instance_array[id_index].failed_verifies), 1ULL);
    }

    return(ambiguous_results);

} /* try_object_verify() */


/***********************************************************************************************
 * get_type()
 *
 *    Verify that the target ID associated with the instance of id_instance_t pointed to by 
 *    id_inst_ptr exists, and has the type associaed with the instance of id_type_t pointed 
 *    to by id_type_ptr.  
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success (i.e. if the target ID exists and is of the indicated
 *    type), and 1 if not, or if any error (including non-existance) is detected.
 *
 ***********************************************************************************************/

int get_type(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, 
             hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id = 0;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ||
         ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "get_type():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));

    if ( success ) {

        if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                        "get_type():%d: target id type either in progress, not created, or discarded on entry.\n",
                        tid);
            }
        } else {

            type = (H5I_type_t)id_type_k.type_id;
        }
    } 

    if ( success )
    {
        if ( ( id_inst_k.in_progress) || ( ! id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                        "get_type():%d: target id inst either in progress, not created, or discarded on entry.\n",
                        tid);
            }
        } else {

            id = id_inst_k.id;
        }
    }

    if ( success ) {

        if ( type != H5Iget_type(id) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "get_type():%d: type == 0x%llx != H5Iget_type(id = 0x%llx).\n",
                        tid, (unsigned long long)type, (unsigned long long)id);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iget_type");
    }

    return(success ? 0 : 1);

} /* get_type() */


/***********************************************************************************************
 * remove_verify()
 *
 *    Call H5Iremove_verify() to delete the target ID.
 *
 *    Before doing so, verify that the supplied instances of id_instance_t and id_object_t
 *    indicate that the target ID exists.  Similarly, verify that supplied instance of 
 *    id_type_t exists.  In passing, also look up the target type and ID needed to call
 *    H5Iremove_verify().
 *
 *    Assuming that H5Iremove_verify() is successful, update the instance of id_object_t 
 *    associated with the ID to mark it as removed.  This is necessary as H5Iremove_verify()
 *    doesn't call the free function associaed with the type (and ID).
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success (i.e. if the target ID exists and is successfully 
 *    removed, and 1 if not, or if any error (including non-existance) is detected.
 *
 ***********************************************************************************************/

int remove_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                  hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_instance_kernel_t mod_id_inst_k = ID_INSTANCE_K_T__INITIALIZER;
    id_object_kernel_t   id_obj_k;
    id_object_kernel_t   mod_id_obj_k = ID_OBJECT_K_T__INITIALIZER;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ||
         ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ||
         ( NULL == id_obj_ptr )  || ( ID_OBJECT_T__TAG != id_obj_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "remove_verify():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));
    id_obj_k = atomic_load(&(id_obj_ptr->k));

    if ( success ) {

        if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                   "remove_verify():%d: target id type either in progress, not created, or discarded on entry.\n",
                   tid);
            }
        } else {

            type = (H5I_type_t)id_type_k.type_id;
        }
    } 

    if ( success )
    {
        if ( ( id_inst_k.in_progress) || ( ! id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                   "remove_verify():%d: target id inst either in progress, not created, or discarded on entry.\n",
                   tid);
            }
        } else {

            id = id_inst_k.id;
        }
    }

    if ( success )
    {
        if ( ( id_obj_k.in_progress) || ( ! id_obj_k.allocated ) || ( id_obj_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                    "remove_verify():%d: target id obj either in progress, not created, or discarded on entry.\n",
                    tid);
            }
        }
    }

    if ( success ) {

        if ( (void *)id_obj_ptr != H5Iremove_verify(id, type) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "remove_verify():%d: H5Iremove_verify(id = 0x%llx, type = 0x%llx) failed.\n",
                         tid, (unsigned long long)id, (unsigned long long)type);
            }
        } else {

            id_obj_k  = atomic_load(&(id_obj_ptr->k));

            if ( id_obj_k.id != id ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                     fprintf(stderr, "remove_verify():%d: ID missmatch -- id = 0x%llx != 0x%llx = id_obj_k.id.\n",
                             tid, (unsigned long long)id, (unsigned long long)(id_obj_k.id));
                }
            }
        }
    }

    if ( success ) { 

        if ( ( id_inst_k.discarded ) || ( id_obj_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "remove_verify():%d: ((id_inst_k.discarded) || (id_obj_k.discarded))\n", tid);
            }
        }
    }

    if ( success ) { /* must mark *id_inst_ptr as discarded */

        int inst_retry_cnt = -1;
        int obj_retry_cnt = -1;

        /* mark *id_inst_k as discarded */
        while ( ! id_inst_k.discarded ) {

            mod_id_inst_k.in_progress   = id_inst_k.in_progress;
            mod_id_inst_k.created       = id_inst_k.created;
            mod_id_inst_k.discarded     = TRUE;
            mod_id_inst_k.future        = id_inst_k.future;
            mod_id_inst_k.realized      = id_inst_k.realized;
            mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
            mod_id_inst_k.id            = id_inst_k.id;

            atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k);

            id_inst_k = atomic_load(&(id_inst_ptr->k));

            inst_retry_cnt++;
        }

        /* for whatever reason, H5Iremove_verify() doesn't call the free routine 
         * for objects in the index.  Thus we should set the discarded flag on 
         * the object so we can detect other calls to the free function.
         */
        while ( ! id_obj_k.discarded ) {

            mod_id_obj_k.in_progress         = id_obj_k.in_progress;
            mod_id_obj_k.allocated           = id_obj_k.allocated;
            mod_id_obj_k.discarded           = TRUE;
            mod_id_obj_k.future              = id_obj_k.future;
            mod_id_obj_k.real_id_def_in_prog = id_obj_k.real_id_def_in_prog;
            mod_id_obj_k.real_id_defined     = id_obj_k.real_id_defined;
            mod_id_obj_k.future_id_realized  = id_obj_k.future_id_realized;
            mod_id_obj_k.future_id_discarded = id_obj_k.future_id_discarded;
            mod_id_obj_k.id                  = id_obj_k.id;

            atomic_compare_exchange_strong(&(id_obj_ptr->k), &id_obj_k, mod_id_obj_k);

            id_obj_k = atomic_load(&(id_obj_ptr->k));

            obj_retry_cnt++;
        }

        if ( ( inst_retry_cnt > 0 ) || ( obj_retry_cnt > 0 ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "remove_verify():%d: inst_retry_cnt = %d, obj_retry_cnt = %d\n", 
                         tid, inst_retry_cnt, obj_retry_cnt);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iremove_verify");
    }

    return( success ? 0 : 1 );

} /* remove_verify() */


/***********************************************************************************************
 * try_remove_verify()
 *
 *      Call H5Iremove_verify() on id_instance_array[id_index].k.id, and test to see if the 
 *      pointer returned equals &(objects_array[obj_index]).  Update stats in 
 *      id_instance_array[id_index] and objects_array[obj_index] as appropriate to refect 
 *      success or failure.
 *
 *      Note that id_instance_array[id_index].k.id may or may not have been initialized on
 *      entry.
 *
 *      Thus the call may either succeed or fail.  In either case, examine 
 *      id_instance_array[id_index] to see whether success or failure is expected, and assert
 *      if an unexpected is returned by H5Iremove_verify().  In ambiguous cases, increment the 
 *      ambiguous results counter, and generate an error message if rpt_failures is TRUE.
 *
 *      If the cs flag is set, the function clears statistics on entry.
 *
 *      If the ds flag is set, the function displays statistics just before exit.
 *
 *      If the rpt_failures flag is set, the function will write an error message to stderr
 *      if an error is detected.  If such an error message is generated, the triggering thread
 *      (given in the tid field) is reported.
 *                                                     JRM -- 4/4/24
 *
 * returns: the number of ambiguous results detected.
 *
 * Changes:
 *
 *      None.
 *
 ***********************************************************************************************/

int try_remove_verify(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    int                  type_index;
    int                  obj_index;
    int                  ambiguous_results = 0;
    unsigned long long   pre_remove_verify_starts;
    unsigned long long   post_remove_verify_starts;
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t pre_id_inst_k;
    id_instance_kernel_t post_id_inst_k;
    id_object_t        * reported_id_obj_ptr = NULL;


    if ( cs ) {

        H5I_clear_stats();
    }

    assert( ( 0 <= id_index ) && ( id_index < NUM_ID_INSTANCES ) );
    assert( ID_INSTANCE_T__TAG == id_instance_array[id_index].tag );

    pre_id_inst_k = atomic_load(&(id_instance_array[id_index].k));
    id = pre_id_inst_k.id;

    type_index = id_instance_array[id_index].type_index;

    assert( ( 0 <= type_index ) && ( type_index < NUM_ID_TYPES ) );
    assert( ID_TYPE_T__TAG == types_array[type_index].tag );

    id_type_k = atomic_load(&(types_array[type_index].k));
    type = (H5I_type_t)id_type_k.type_id;

    obj_index = atomic_load(&(id_instance_array[id_index].obj_index));

    assert( ( -1 == obj_index ) || ( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) ) );
    assert( ( -1 == obj_index ) || ( ID_OBJECT_T__TAG == objects_array[obj_index].tag ) );

    if ( success ) {

        pre_remove_verify_starts = atomic_fetch_add(&(id_instance_array[id_index].remove_verify_starts), 1ULL);

        reported_id_obj_ptr =  H5Iremove_verify(id, type);

        post_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

        if ( NULL == reported_id_obj_ptr ) {

            post_remove_verify_starts = atomic_fetch_sub(&(id_instance_array[id_index].remove_verify_starts), 1ULL);

            success = FALSE;

        } else {

            post_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));
        }

        if ( ( ! pre_id_inst_k.created ) && ( ! post_id_inst_k.created ) ) {

            /* The id didn't exist when H5Iremove_verify() was called -- thus 
             * the call must have failed.  
             *
             * While the object index will usually be -1, the registration of an ID in the 
             * test bed is not an atomic process.  In particular, since the obj_index is 
             * set before the id is marked as created, it is possible that it will be 
             * set even though the id instance is not listed as being created.  
             *
             * Deal wiht this case by allowing the obj_index to be either -1 or equal to
             * to the id_index.  While this is good for now, note that it may change.
             */
            assert( ! success );
            assert( ( -1 == obj_index ) || ( id_index == obj_index ) );;

        } else if ( ( pre_id_inst_k.created ) && ( post_id_inst_k.created ) &&
                    ( pre_id_inst_k.discarded ) && ( post_id_inst_k.discarded ) ) {

            /* The id was created and discarded before H5Iremove_verify() was called -- 
             * thus the call must have failed.  However, the object index must 
             * be defined (i.e. not -1).  Further the indicated object must be 
             * marked as discarded, and must be linked to the id via its id_index
             * field.
             */
            assert( ! success );
            assert( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) );
            assert( id_index == atomic_load(&(objects_array[obj_index].id_index)) );

        } else if ( ( pre_id_inst_k.created ) && ( post_id_inst_k.created ) &&
                    ( ! pre_id_inst_k.discarded ) && ( ! post_id_inst_k.discarded ) ) {

             if ( ( ( 0 < pre_remove_verify_starts ) || ( 0 < post_remove_verify_starts ) )  && ( ! success ) ) {

                /* another call to H5Iremove_verify() has been active sometime around the 
                 * same time as this call.  Thus the failure of this call to H5Iremove_verify()
                 * is probably OK.  Since we don't know this sure, log this as an ambiguous
                 * result.
                 */

                ambiguous_results++;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                            "try_remove_verify(%d):%d: failure possibly caused by a concurrent revmoce verify.\n",
                            id_index, tid);
                }
            } else {

                /* the ID was created and not discarded at the time that H5Iremove_verify() 
                 * was called -- thus the call must have succeeded.  
                 *
                 * Note that while H5Iremove_verify() will delete the ID from the 
                 * index, it doesn't call the free function.  Thus we call it here 
                 * to update the test code for the deletion.
                 */
                assert( success );
                assert( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) );
                assert( id_index == atomic_load(&(objects_array[obj_index].id_index)) );

                if ( SUCCEED != free_func((void *)(&(objects_array[obj_index])), NULL) ) {

                    if ( rpt_failures ) {

                        fprintf(stderr, "try_remove_verify():%d: free_func() failed -- id/obj indexes = %d / %d.\n",
                                tid, id_index, obj_index);
                    }
                    assert(FALSE);
                }
            }
        } else if ( ( pre_id_inst_k.created ) && ( post_id_inst_k.created ) &&
                    ( ! pre_id_inst_k.discarded ) && ( post_id_inst_k.discarded ) ) {

            /* The ID was created before the call to H5Iremove_verify() and deleted 
             * afterwards.  Since H5Iremove_verify() does not call the free_func(), this 
             * implies that some other call deleted the target ID from the index, and 
             * thus that the call to H5Iremove_verify() failed.
             *
             * However, since the ID pre-existed the call, object index should be 
             * valid, and should be linked with the ID.
             */
            assert( ! success );
            assert( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) );
            assert( id_index == atomic_load(&(objects_array[obj_index].id_index)) );

        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iremove_verify");
    }

    if ( success ) {

        atomic_fetch_add(&(id_instance_array[id_index].successful_remove_verifies), 1ULL);

    } else {

        atomic_fetch_add(&(id_instance_array[id_index].failed_remove_verifies), 1ULL);
    }

    return(ambiguous_results);

} /* try_remove_verify() */


/***********************************************************************************************
 * dec_ref()
 *
 *    If the target id instance is listed as existing in the supplied instance of id_instance_t,
 *    call H5Idec_ref() to decrement the current ref count for the id.  If the reference count 
 *    is decremented to zero, mark the supplied id instance as discarded, and verify that 
 *    the associated id_object_t is marked as discarded.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success (i.e. if the target ID exists and H5Idec_ref() returned
 *    a non-negative integer, and 1 otherwise, or if any error (including non-existance) is 
 *    detected.
 *
 ***********************************************************************************************/

int dec_ref(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
            hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    hid_t                id;
    int                  ref_count;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_instance_kernel_t mod_id_inst_k = ID_INSTANCE_K_T__INITIALIZER;
    id_object_kernel_t   id_obj_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ||
         ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ||
         ( NULL == id_obj_ptr )  || ( ID_OBJECT_T__TAG != id_obj_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "dec_ref():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));
    id_obj_k = atomic_load(&(id_obj_ptr->k));

    if ( success ) {

        if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                         "dec_ref():%d: target id type either in progress, not created, or discarded on entry.\n",
                         tid);
            }
        }
    } 

    if ( success )
    {

        if ( ( id_inst_k.in_progress) || ( ! id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                   "dec_ref():%d: target id inst either in progress, not created, or discarded on entry.\n",
                   tid);
            }
        } else {

            id = id_inst_k.id;
        }
    }

    if ( success )
    {
        if ( ( id_obj_k.in_progress) || ( ! id_obj_k.allocated ) || ( id_obj_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                         "dec_ref():%d: target id obj either in progress, not created, or discarded on entry.\n",
                         tid);
            }
        }
    }

    if ( success ) {

        ref_count = H5Idec_ref(id);
#if 0
        fprintf(stderr, "H5Idec_ref(0x%llx) returns %d.\n", (unsigned long long)id, ref_count);
#endif
        if ( ref_count < 0 ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "dec_ref():%d: H5Idec_ref(0x%llx) returns %d.\n", 
                        tid, (unsigned long long)id, ref_count);
            }
        } else if ( 0 == ref_count ) {

            int retries = -1;

            /* the ID has been deleted.  Verify that the free function has been called
             * on the ID object, and mark the instance as deleted.
             */

            id_obj_k = atomic_load(&(id_obj_ptr->k));

            if ( ! id_obj_k.discarded ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                           "dec_ref():%d: H5Idec_ref(0x%llx) returns 0, but id object not marked as discared.\n", 
                           tid, (unsigned long long)id);
                }
            }

            /* mark *id_inst_k as discarded */
            while ( ! id_inst_k.discarded ) {

                mod_id_inst_k.in_progress   = id_inst_k.in_progress;
                mod_id_inst_k.created       = id_inst_k.created;
                mod_id_inst_k.discarded     = TRUE;
                mod_id_inst_k.future        = id_inst_k.future;
                mod_id_inst_k.realized      = id_inst_k.realized;
                mod_id_inst_k.future_id_src = id_inst_k.future_id_src;
                mod_id_inst_k.id            = id_inst_k.id;

                atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k);

                id_inst_k = atomic_load(&(id_inst_ptr->k));

                retries++;
            }

            if ( retries > 0 ) {

                assert(FALSE);

                success = FALSE;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                           "dec_ref():%d: %d retries needed  to mark instance as discarded after H5Idec_ref(0x%llx) returns 0.\n", 
                           tid, retries, (unsigned long long)id);
                }
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idec_ref");
    }

    return(success ? 0 : 1);

} /* dec_ref() */


/***********************************************************************************************
 * try_dec_ref()
 *
 *      Call H5Ided_ref() on id_instance_array[id_index].k.id, and update stats in
 *      id_instance_array[id_index] to reflect success or failure.
 *
 *      Record the relevant elements of the state of id_instance_array[id_index] both 
 *      before and after the call to H5Idec_ref().  Test to see if this data is consistent
 *      with the result of the call to H5Idec_ref(), and trigger an assert if it is not.
 *
 *      Note that it is not always possible to determine the expected result from this 
 *      data -- if so, test is ambiguous.
 *
 *      Note that id_instance_array[id_index].k.id may or may not have been registered on
 *      entry.
 *
 *      If the cs flag is set, the function clears statistics on entry.
 *
 *      If the ds flag is set, the function displays statistics just before exit.
 *
 *      If the rpt_failures flag is set, the function will write an error message to stderr
 *      if an error is detected.  If such an error message is generated, the triggering thread
 *      (given in the tid field) is reported.
 *                                                     JRM -- 4/4/24
 *
 * returns: 1 if the result is ambiguous, and 0 otherwise.
 *
 ***********************************************************************************************/

int try_dec_ref(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    int                  ref_count;
    int                  ambiguous_results = 0;
    unsigned long long   pre_remove_verify_starts;
    unsigned long long   post_remove_verify_starts;
    id_instance_kernel_t pre_id_inst_k;
    id_instance_kernel_t post_id_inst_k;

    if ( cs ) {

        H5I_clear_stats();
    }

    if ( success ) {

        assert( ( 0 <= id_index ) && ( id_index < NUM_ID_INSTANCES ) );

        assert( ID_INSTANCE_T__TAG == id_instance_array[id_index].tag );

        pre_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));

        pre_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

        ref_count = H5Idec_ref(pre_id_inst_k.id);

#if 0
        fprintf(stderr, "H5Idec_ref(0x%llx) returns %d.\n", (unsigned long long)id, ref_count);
#endif

        post_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

        post_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));

        if ( ref_count < 0 ) {

            success = FALSE;

        } else if ( 0 == ref_count ) {

            int                  obj_index;
            id_object_kernel_t   id_obj_k;

            success = TRUE;

            /* 0 == ref_count implies that the call to H5Idec_ref() deleted the id from the 
             * the index.  
             */

            /* Verify that the associated object is marked as being deleted. */

            obj_index = atomic_load(&(id_instance_array[id_index].obj_index));

            assert( ( 0 <= obj_index ) && ( obj_index < NUM_ID_INSTANCES ) );

            assert( ID_OBJECT_T__TAG == objects_array[obj_index].tag );

            /* verify that the object is marked as being discarded */
            id_obj_k = atomic_load(&(objects_array[obj_index].k));

            assert( id_obj_k.discarded );


            /* verify that id_instance_array[id_index] is marked as being deleted */

            assert( post_id_inst_k.discarded );

        } else {

            assert( ref_count > 0 );

            success = TRUE;
        }

        if ( ( ( ! pre_id_inst_k.created ) && ( ! post_id_inst_k.created ) ) ||
             ( ( pre_id_inst_k.discarded ) && ( post_id_inst_k.discarded ) ) ) {

            /* the id was not registered during at the time H5Idec_ref() was called -- thus
             * the call cannot have succeeded.
             */
            assert( ! success );
            assert( -1 == ref_count );

        } else if ( ( pre_id_inst_k.created ) && ( ! pre_id_inst_k.discarded ) &&
                    ( post_id_inst_k.created ) && ( ! post_id_inst_k.discarded ) ) {

            if ( ( ( 0 < pre_remove_verify_starts ) || ( 0 < post_remove_verify_starts ) ) && ( ref_count <= 0 ) ) {

                /* A call to H5Iremove verify() may have been active about the time the call to
                 * H5Idec_ref() was made.  Since this could explain the failure, log an
                 * ambiguous test.
                 */
                ambiguous_results++;

                if ( rpt_failures ) {

                    fprintf(stderr,
                            "try_dec_ref(%d):%d failure possibly explained by a remove verify in progress.\n",
                            id_index, tid);
                }
            } else { 

                /* ID was registerd before and after the call to H5Idec_ref().  Thus the 
                 * call must have succeeded, and the returned ref count must have been 
                 * positive.
                 */
                assert( ref_count > 0 );
                assert( success );
            }
        } else if ( ref_count == 0 ) {

            /* H5Idec_ref() reports that the target ID existed, had a ref count of 1,
             * and was therefore deleted.
             */
            assert( ( pre_id_inst_k.created ) && ( ! pre_id_inst_k.discarded ) );
            assert( ( post_id_inst_k.created ) && ( post_id_inst_k.discarded ) );
            assert( success );

            atomic_fetch_add(&(id_instance_array[id_index].dec_ref_deletes), 1ULL);

        } else {

            /* The id may or may not have been registered when H5Idec_ref() was called.
             * Thus the test result is ambiguous.
             */
            ambiguous_results++;
        } 
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idec_ref");
    }

    if ( success ) {

        atomic_fetch_add(&(id_instance_array[id_index].successful_dec_refs), 1ULL);

    } else {

        atomic_fetch_add(&(id_instance_array[id_index].failed_dec_refs), 1ULL);
    }

    return(ambiguous_results);

} /* try_dec_ref() */


/***********************************************************************************************
 * inc_ref()
 *
 *    If the target id instance is listed as existing in the supplied instance of id_instance_t,
 *    call H5Iinc_ref() to increment the current ref count for the id.  
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success (i.e. if the target ID exists and H5Iinc_ref() returned
 *    a positive integer, and 1 otherwise, or if any error (including non-existance) is detected.
 *
 ***********************************************************************************************/

int inc_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    hid_t                id;
    int                  ref_count;
    id_instance_kernel_t id_inst_k;

    if ( ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        ref_count = -2;

        if ( rpt_failures ) {

            fprintf(stderr, "inc_ref():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_inst_k = atomic_load(&(id_inst_ptr->k));

    if ( success ) {

        id_inst_k = atomic_load(&(id_inst_ptr->k));

        if ( ( id_inst_k.in_progress) || ( ! id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                   "inc_ref():%d: target id inst either in progress, not created, or discarded on entry.\n",
                   tid);
            }
        } else {

            id = id_inst_k.id;
        }
    }

    if ( success ) {

        ref_count = H5Iinc_ref(id);

        if ( ref_count <= 0 ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "inc_ref():%d: H5Iinc_ref(0x%llx) returned %d -- positive value expected.\n",
                         tid, (unsigned long long)id, ref_count);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iinc_ref");
    }

    return(success ? 0 : 1);

} /* inc_ref() */


/***********************************************************************************************
 * try_inc_ref()
 *
 *      Call H5Iinc_ref() on id_instance_array[id_index].k.id, and update stats in 
 *      id_instance_array[id_index] to reflect success or failure.  
 *
 *      If the target id is defined both before and after the call to H5Iinc_ref(), the 
 *      function asserts that the call to H5Iinc_ref() is successful.
 *
 *      If the target id is undefined both before and after the call to H5Iinc_ref(), the
 *      function asserts that the call to H5Iinc_ref() is unsuccessful.
 *
 *      If the target id is undefined on one side of the call to H5Iinc_ref(), and defined
 *      on the other side, the expected result of the call to H5Iinc_ref() is unknown, and 
 *      thus the result of the test is ambiguous.  
 *
 *      Note that id_instance_array[id_index].k.id may or may not have been registered on
 *      entry.
 *
 *      If the cs flag is set, the function clears statistics on entry.
 *
 *      If the ds flag is set, the function displays statistics just before exit.
 *
 *      If the rpt_failures flag is set, the function will write an error message to stderr
 *      if an error is detected.  If such an error message is generated, the triggering thread
 *      (given in the tid field) is reported.
 *                                                     JRM -- 4/4/24
 *
 * returns: 1 if the result is ambiguous, and 0 otherwise.
 *
 ***********************************************************************************************/

int try_inc_ref(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE if H5Iinc_ref() fails */
    hid_t                id;
    int                  ref_count;
    int                  obj_index;
    int                  ambiguous_results = 0;
    unsigned long long   pre_remove_verify_starts;
    unsigned long long   post_remove_verify_starts;
    id_instance_kernel_t pre_id_inst_k;
    id_instance_kernel_t post_id_inst_k;

    assert( ( id_index >= 0 ) && ( id_index < NUM_ID_INSTANCES ) );

    if ( cs ) {

        H5I_clear_stats();
    }

    if ( success ) {

        assert( ID_INSTANCE_T__TAG == id_instance_array[id_index].tag );

        pre_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));

        pre_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

        id = pre_id_inst_k.id;

        obj_index = atomic_load(&(id_instance_array[id_index].obj_index));

        assert( ( -1 == obj_index ) || ( ( 0 <= obj_index ) && ( obj_index < NUM_ID_OBJECTS ) ) );

        ref_count = H5Iinc_ref(id);

        post_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

        post_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));
#if 0
        fprintf(stderr, "H5Iinc_ref(0x%llx) returns %d.\n", (unsigned long long)id, ref_count);
#endif
        if ( ref_count <= 0 ) {
            
            success = FALSE;
        }

        if ( ( pre_id_inst_k.created ) && ( post_id_inst_k.created ) &&
             ( ! pre_id_inst_k.discarded ) && ( ! post_id_inst_k.discarded ) ) {

            if ( ( ( 0 < pre_remove_verify_starts ) || ( 0 < post_remove_verify_starts ) )  && ( ! success ) ) {

                ambiguous_results++;

                if ( rpt_failures ) {

                    fprintf(stderr, "try_inc_ref(%d):%d: failure possibly caused by a concurrent revoce verify.\n",
                            id_index, tid);
                }
            } else {

                assert(success);
            }
        } else if ( ( ( ! pre_id_inst_k.created ) && ( ! post_id_inst_k.created ) ) ||
                    ( ( pre_id_inst_k.discarded ) && ( post_id_inst_k.discarded ) ) ) {

            assert(!success);

        } else {

            ambiguous_results++;
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idec_ref");
    }

    if ( success ) {

        atomic_fetch_add(&(id_instance_array[id_index].successful_inc_refs), 1ULL);

    } else {

        atomic_fetch_add(&(id_instance_array[id_index].failed_inc_refs), 1ULL);
    }

    return(ambiguous_results);

} /* try_inc_ref() */


/***********************************************************************************************
 * get_ref()
 *
 *    If the target id instance is listed as existing in the supplied instance of id_instance_t,
 *    call H5Iget_ref() to obtain the current ref count for the id, and return it.
 *
 *    If the target id instance is not listed as existing, return -1.
 *
 *    If an error is detected, return -2.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success (i.e. if the target ID exists and is successfully 
 *    removed, and 1 if not, or if any error (including non-existance) is detected.
 *
 ***********************************************************************************************/

int get_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    hid_t                id;
    int                  ref_count;
    id_instance_kernel_t id_inst_k;

    if ( ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        ref_count = -2;

        if ( rpt_failures ) {

            fprintf(stderr, "get_ref():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    if ( success ) {

        id_inst_k = atomic_load(&(id_inst_ptr->k));

        if ( ( id_inst_k.in_progress) || ( ! id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;
            ref_count = -2;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                   "get_ref():%d: target id inst either in progress, not created, or discarded on entry.\n",
                   tid);
            }
        } else {

            id = id_inst_k.id;
        }
    }

    if ( success ) {

        ref_count = H5Iget_ref(id);
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iget_ref");
    }

    return(ref_count);

} /* get_ref()() */

/***********************************************************************************************
 * try_get_ref()
 *
 *      Call H5Iget_ref() on id_instance_array[id_index].k.id, and update stats in 
 *      id_instance_array[id_index] to reflect success or failure.  
 *
 *      If the target id is defined both before and after the call to H5Iget_ref(), the 
 *      function asserts that the call to H5Iget_ref() is successful and returns a 
 *      non-negative value (since we are using an external API call, it is possible that 
 *      the application ref coutn is zero, while the internal ref count is positive).
 *
 *      If the target id is undefined both before and after the call to H5Iget_ref(), the
 *      function asserts that the call to H5Iget_ref() returns -1, which indicates that 
 *      that the target id doesn't exist.
 *
 *      If the target id is undefined on one side of the call to H5Iinc_ref(), and defined
 *      on the other side, the expected result of the call to H5Iinc_ref() is unknown, and 
 *      thus the result of the test is ambiguous.  
 *
 *      Note that id_instance_array[id_index].k.id may or may not have been registered on
 *      entry.
 *
 *      If the cs flag is set, the function clears statistics on entry.
 *
 *      If the ds flag is set, the function displays statistics just before exit.
 *
 *      If the rpt_failures flag is set, the function will write an error message to stderr
 *      if an error is detected.  If such an error message is generated, the triggering thread
 *      (given in the tid field) is reported.
 *                                                     JRM -- 5/2/24
 *
 * returns: 1 if the result is ambiguous, and 0 otherwise.
 *
 ***********************************************************************************************/

int try_get_ref(int id_index, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    hid_t                id;
    int                  ambiguous_results = 0;
    int                  ref_count;
    unsigned long long   pre_remove_verify_starts;
    unsigned long long   post_remove_verify_starts;
    id_instance_kernel_t pre_id_inst_k;
    id_instance_kernel_t post_id_inst_k;

    assert( ( id_index >= 0 ) && ( id_index < NUM_ID_INSTANCES ) );

    if ( cs ) {

        H5I_clear_stats();
    }

    if ( success ) {

        assert( ID_INSTANCE_T__TAG == id_instance_array[id_index].tag );

        pre_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));

        pre_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

        id = pre_id_inst_k.id;

        ref_count = H5Iget_ref(id);

        post_id_inst_k = atomic_load(&(id_instance_array[id_index].k));

        post_remove_verify_starts = atomic_load(&(id_instance_array[id_index].remove_verify_starts));
#if 0
        fprintf(stderr, "H5Iget_ref(0x%llx) returns %d.\n", (unsigned long long)id, ref_count);
#endif
        if ( ref_count < 0 ) {

            success = FALSE;
        }

        if ( ( pre_id_inst_k.created ) && ( post_id_inst_k.created ) &&
             ( ! pre_id_inst_k.discarded ) && ( ! post_id_inst_k.discarded ) ) {

            if ( ( ( pre_remove_verify_starts > 0 ) || ( post_remove_verify_starts > 0 ) ) && ( ! success ) ) {

                ambiguous_results++;

                if ( rpt_failures ) {

                    fprintf(stderr,
                            "try_object_verify(%d):%d failure possibly explained by a remove verify in progress.\n",
                            id_index, tid);
                }
            } else {

                assert(success);
            }
        } else if ( ( ( ! pre_id_inst_k.created ) && ( ! post_id_inst_k.created ) ) ||
                    ( ( pre_id_inst_k.discarded ) && ( post_id_inst_k.discarded ) ) ) {

            assert(!success);

        } else {

            ambiguous_results++;
        }
    }

    if ( success ) {

        atomic_fetch_add(&(id_instance_array[id_index].successful_get_ref_cnts), 1ULL);

    } else {

        atomic_fetch_add(&(id_instance_array[id_index].failed_get_ref_cnts), 1ULL);
    }


    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iget_ref");
    }

    return(ambiguous_results);

} /* try_get_ref()() */

/***********************************************************************************************
 * nmembers()
 *
 *    If the target type id is listed as existing in the supplied instance of id_type_t,
 *    call H5Inmembers() to obtain the current number of IDs of the supplied type, and then
 *    return this value.
 *
 *    If the target id instance is not listed as existing, H5Inmembers() failes, or any other 
 *    error is detected, return -1.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 ***********************************************************************************************/

int nmembers(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    hsize_t              num_members;    /* H5Inmembers() will overwrite this if it is called */
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "nmembers():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( success ) {

        if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                        "nmembers():%d: target id type either in progress, not created, or discarded on entry.\n",
                        tid);
            }
        } else {

            type = (H5I_type_t)id_type_k.type_id;
        }
    } 


    if ( success ) {

        if ( H5Inmembers(type, &num_members) != SUCCEED ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                        "nmembers():%d: H5Inmembers(type = 0x%llx, &num_members) failed.\n",
                        tid, (unsigned long long)type);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "nmembers");
    }

    if ( success ) {

        return((int)num_members);

    } else {

        return(-1);
    }

} /* nmembers() */

/***********************************************************************************************
 * type_exists()
 *
 *    If the target type id is listed as having been created in the supplied instance of id_type_t,
 *    call H5Itype_exists(), and return its return value.  Otherwise, return FAIL.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 ***********************************************************************************************/

htri_t type_exists(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    htri_t               result = TRUE; /* will set to FAIL on failure */
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ) {

        assert(FALSE);

        result = FAIL;

        if ( rpt_failures ) {

            fprintf(stderr, "type_exists():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( ! id_type_k.created ) {

        result = FAIL;

    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( result == TRUE ) {

        result = H5Itype_exists(type);
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Itype_exists");
    }

    return(result);

} /* type_exists() */

/***********************************************************************************************
 * inc_type_ref()
 *
 *    If the target type id is listed as having been created in the supplied instance of id_type_t,
 *    call H5Iinc_type_ref().  If this call returns success, return 0.  If it fails, or if any 
 *    other error is detected (including target type id not created) return 1.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 ***********************************************************************************************/

int inc_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FAIL on failure */
    int                  ref_count;
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "inc_type_ref():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( success ) {

        if ( ! id_type_k.created ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "inc_type_ref():%d: target id type not created.\n", tid);
            }
        } else {

            type = (H5I_type_t)id_type_k.type_id;
        }
    } 

    if ( success ) {

        ref_count = H5Iinc_type_ref(type);

        if ( ref_count == -1 ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "inc_type_ref():%d: H5Iinc_type_ref(0x%llx) reports failure.\n", 
                         tid, (unsigned long long)type);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iinc_type_ref");
    }

    return(success ? 0 : 1);

} /* inc_type_ref() */


/***********************************************************************************************
 * dec_type_ref()
 *
 *    If the target type id is listed as having been created in the supplied instance of id_type_t,
 *    call H5Idec_type_ref().  If this call returns success, return 0.  If it fails, or if any 
 *    other error is detected (including target type id not created) return 1.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 ***********************************************************************************************/

int dec_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FAIL on failure */
    herr_t               ref_count;
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "dec_type_ref():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( success ) {

        if ( ! id_type_k.created ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "dec_type_ref():%d: target id type not created.\n", tid);
            }
        } else {

            type = (H5I_type_t)id_type_k.type_id;
        }
    } 

    if ( success ) {

        ref_count = H5Idec_type_ref(type);

        if ( ref_count == -1 ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "dec_type_ref():%d: H5Idec_type_ref(0x%llx) reports failure.\n", 
                         tid, (unsigned long long)type);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idec_type_ref");
    }

    return( success ? 0 : 1 );

} /* dec_type_ref() */


/***********************************************************************************************
 * try_dec_type_ref()
 *
 *    If the target type id is listed as having been created in the supplied instance of id_type_t,
 *    call H5Idec_type_ref().  If this call returns success, return 0.  If it fails, or if any 
 *    other error is detected (including target type id not created) return 1.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 ***********************************************************************************************/

int try_dec_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t              success = TRUE; /* will set to FAIL on failure */
    herr_t               ref_count;
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ) {

        assert(FALSE);

        success = FALSE;

        if ( rpt_failures ) {

            fprintf(stderr, "dec_type_ref():%d: Invalid params on entry.\n", tid);
        }
    }

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( success ) {

        if ( ! id_type_k.created ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "dec_type_ref():%d: target id type not created.\n", tid);
            }
        } else {

            type = (H5I_type_t)id_type_k.type_id;
        }
    } 

    if ( success ) {

        ref_count = H5Idec_type_ref(type);

        if ( ref_count == -1 ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, "dec_type_ref():%d: H5Idec_type_ref(0x%llx) reports failure.\n", 
                         tid, (unsigned long long)type);
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idec_type_ref");
    }

    return( success ? 0 : 1 );

} /* try_dec_type_ref() */


/***********************************************************************************************
 * create_types()
 *
 *    Register types_count new types, associate them with entries in types_array[] starting with
 *    types_start, and every types_stride entries thereafter until types_count types have been 
 *    created.  For each effected instance of id_type_t in types_array[], update that
 *    structure accordingly.  
 *
 *    These type creations are implemented via calls to register_type().
 *
 *    The cs and ds flags are simply passed to register_type().
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if one or more errors are detected.  If such an error message is generated, the 
 *    triggering thread (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and the number of errors encountered otherwise.
 * 
 ***********************************************************************************************/

int create_types(int types_start, int types_count, int types_stride, hbool_t cs, hbool_t ds, 
                 hbool_t rpt_failures, int tid)
{
    int i;
    int err_cnt = 0;

    for ( i = types_start; i < types_start + (types_count * types_stride); i += types_stride ) {

        assert(i >= 0);
        assert(i < NUM_ID_TYPES);

        err_cnt += register_type(&(types_array[i]), cs, ds, rpt_failures, tid);
    }

    if ( ( err_cnt > 0 ) && ( rpt_failures ) ) {

        fprintf(stderr, "create_types():%d: %d errors reported (types start/count/stride = %d/%d/%d)\n",
                tid, err_cnt, types_start, types_count, types_stride);
    }

    return(err_cnt);

} /* create_types() */

/***********************************************************************************************
 * dec_type_refs()
 *
 *    Decrement the reference counts on types_count types, starting with types_start in 
 *    types_array[], and then every types_stride entries thereafter until the reference counts
 *    of types_count types have been decremented.  Do this via calls to dec_type_ref().
 *    Return the number of errors encountered.
 *
 *    The cs and ds flags are simply passed to dec_type_ref().
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if one or more errors are detected.  If such an error message is generated, the 
 *    triggering thread (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and the number of errors encountered otherwise.
 * 
 ***********************************************************************************************/

int dec_type_refs(int types_start, int types_count, int types_stride, hbool_t cs, hbool_t ds, 
                  hbool_t rpt_failures, int tid)
{
    int err_cnt = 0;
    int i;

    for ( i = types_start; i < types_start + (types_count * types_stride); i += types_stride ) {

        assert(i >= 0);
        assert(i < NUM_ID_TYPES);

        err_cnt += dec_type_ref(&(types_array[i]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    if ( ( err_cnt > 0 ) && ( rpt_failures ) ) {

        fprintf(stderr, "dec_type_refs():%d: %d errors reported (types start/count/stride = %d/%d/%d)\n",
                tid, err_cnt, types_start, types_count, types_stride);
    }

    return(err_cnt);

} /* dec_type_refs() */

/***********************************************************************************************
 * inc_type_refs()
 *
 *    Increment the reference counts on types_count types, starting with types_start in 
 *    types_array[], and then every types_stride entry thereafter until the reference counts
 *    of types_count types have been incremented.  Do this via calls to inc_type_ref().
 *    Return the number of errors encountered.
 *
 *    The cs and ds flags are simply passed to inc_type_ref().
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if one or more errors are detected.  If such an error message is generated, the 
 *    triggering thread (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and the number of errors encountered otherwise.
 * 
 ***********************************************************************************************/

int inc_type_refs(int types_start, int types_count, int types_stride, 
                  hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    int err_cnt = 0;
    int i;

    for ( i = types_start; i < types_start + (types_count * types_stride); i += types_stride ) {

        assert(i >= 0);
        assert(i < NUM_ID_TYPES);

        err_cnt += inc_type_ref(&(types_array[i]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    if ( ( err_cnt > 0 ) && ( rpt_failures ) ) {

        fprintf(stderr, "inc_type_refs():%d: %d errors reported (types start/count/stride = %d/%d/%d)\n",
                tid, err_cnt, types_start, types_count, types_stride);
    }

    return(err_cnt);

} /* inc_type_refs() */

int destroy_types(int types_start, int types_count, int types_stride, 
                  hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    int i;
    int err_cnt = 0;

    for ( i = types_start; i < types_start + (types_count * types_stride); i += types_stride ) {

        assert(i >= 0);
        assert(i < NUM_ID_TYPES);

        err_cnt += destroy_type(&(types_array[i]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    if ( ( err_cnt > 0 ) && ( rpt_failures ) ) {

        fprintf(stderr, "destroy_types():%d: %d errors reported (types start/count/stride = %d/%d/%d)\n",
                tid, err_cnt, types_start, types_count, types_stride);
    }

    return(err_cnt);

} /* destroy_types() */

/*******************************************************************************************
 * register_ids()
 *
 *    Register ids_count new ids, associate them with entries in id_instance_array[] starting 
 *    with ids_start, and every ids_stride entries thereafter until ids_count ids have been 
 *    created.  For each effected instance of id_instance_t in id_inst_array[] and effected
 *    instance of id_object_t in id_objs_array[], update those structures accordingly.  
 *
 *    These id creations are implemented via calls to register_type().
 *
 *    The cs and ds flags are simply passed to register_id().
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if one or more errors are detected.  If such an error message is generated, the 
 *    triggering thread (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and the number of errors encountered otherwise.
 * 
 *
 *******************************************************************************************/

int register_ids(int types_start, int types_count, int types_stride, 
                 int ids_start, int ids_count, int ids_stride,
                 hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    int i;
    int j;
    int k;
    int err_cnt = 0;

    for ( j = ids_start, k = 0; j < ids_start + (ids_count * ids_stride); j++, k = (k + 1) % types_count ) {

        assert( k >= 0);
        assert( k < types_stride);
        i = types_start + (k * types_stride);
        assert( i >= types_start );
        assert( i < types_start + (types_count * types_stride) );

        err_cnt += register_id(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                             cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    if ( ( err_cnt > 0 ) && ( rpt_failures ) ) {

        fprintf(stderr, 
               "register_ids():%d: %d errors reported (types st/cnt/str = %d/%d/%d, ids st/cnt/str = %d/%d/%d)\n",
               tid, err_cnt, types_start, types_count, types_stride, ids_start, ids_count, ids_stride);
    }

    return(err_cnt);

} /* register_ids() */

/*******************************************************************************************
 * dec_refs()
 *
 *    Decrement the ref counts of ids_count existing IDs.  If the ref count drops to zero, 
 *    mark the associated instances of id_object_t as deleted, and verify that the associated 
 *    instances of id_object_t are marked as deleted as well.
 *    
 *    Decrement the ref counts of IDs associated with entries in id_instance_array[] starting 
 *    with ids_start, and every ids_stride entries thereafter until ids_count ids have been 
 *    created.  
 *
 *    These id creations are implemented via calls to register_type().
 *
 *    The cs and ds flags are simply passed to register_id().
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if one or more errors are detected.  If such an error message is generated, the 
 *    triggering thread (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and the number of errors encountered otherwise.
 * 
 *
 *******************************************************************************************/

int dec_refs(int types_start, int types_count, int types_stride, int ids_start, int ids_count, int ids_stride,
             hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    int i;
    int j;
    int k;
    int err_cnt = 0;

#if 0
    fprintf(stderr, "\ndec_refs(): types st/cnt/str = %d/%d/%d, ids st/cnt/str = %d/%d/%d\n\n",
            types_start, types_count, types_stride, ids_start, ids_count, ids_stride);
#endif

    for ( j = ids_start, k = 0; 
          j < ids_start + (ids_count * ids_stride); 
          j += ids_stride, k = (k + 1) % types_count ) {

        assert( k >= 0);
        assert( k < types_stride);
        i = types_start + (k * types_stride);
        assert( i >= types_start );
        assert( i < types_start + (types_count * types_stride) );

        err_cnt += dec_ref(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                           cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    if ( ( err_cnt > 0 ) && ( rpt_failures ) ) {

        fprintf(stderr, 
               "dec_refs():%d: %d errors reported (types st/cnt/str = %d/%d/%d, ids st/cnt/str = %d/%d/%d)\n",
               tid, err_cnt, types_start, types_count, types_stride, ids_start, ids_count, ids_stride);
    }

    return(err_cnt);

} /* dec_refs() */

/*******************************************************************************************
 * inc_refs()
 *
 *    Increment the ref counts of ids_count existing IDs. 
 *    
 *    Increment the ref counts of IDs associated with entries in id_instance_array[] starting 
 *    with ids_start, and every ids_stride entries thereafter until ids_count ids have been 
 *    created.  
 *
 *    These id creations are implemented via calls to register_type().
 *
 *    The cs and ds flags are simply passed to register_id().
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if one or more errors are detected.  If such an error message is generated, the 
 *    triggering thread (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and the number of errors encountered otherwise.
 *
 *******************************************************************************************/

int inc_refs(int ids_start, int ids_count, int ids_stride, 
             hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    int err_cnt = 0;
    int j;

    for ( j = ids_start; j < ids_start + (ids_count * ids_stride); j += ids_stride ) {

        err_cnt += inc_ref(&(id_instance_array[j]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    if ( ( err_cnt > 0 ) && ( rpt_failures ) ) {

        fprintf(stderr, "inc_refs():%d: %d errors reported (ids st/cnt/str = %d/%d/%d)\n",
                tid, err_cnt, ids_start, ids_count, ids_stride);
    }

    return(err_cnt);

} /* inc_refs() */

/*******************************************************************************************
 * verify_objects()
 *
 *    Verify that ids_count ids exist, and that their associated entries in 
 *    id_instance_array[] and id_objs_array[] (starting with ids_start, and every ids_stride 
 *    entries thereafter until ids_count ids have been verified) have been updated accordingly.
 *
 *    These verifications are implemented via calls to verify_object().
 *
 *    The cs and ds flags are simply passed to verify_object().
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if one or more errors are detected.  If such an error message is generated, the 
 *    triggering thread (given in the tid field) is reported.
 *
 *    The function returns 0 on success, and the number of errors / failed verifications
 *    otherwise.
 *
 *******************************************************************************************/

int verify_objects(int types_start, int types_count, int types_stride, 
                   int ids_start, int ids_count, int ids_stride,
                   hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    int i;
    int j;
    int k;
    int err_cnt = 0;

    for ( j = ids_start, k = 0; 
          j < ids_start + (ids_count * ids_stride); 
          j += ids_stride, k = (k + 1) % types_count ) {

        assert( k >= 0);
        assert( k < types_stride);
        i = types_start + (k * types_stride);
        assert( i >= types_start );
        assert( i < types_start + (types_count * types_stride) );

        err_cnt += object_verify(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                                 cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    if ( ( err_cnt > 0 ) && ( rpt_failures ) ) {

        fprintf(stderr, 
            "verify_objects():%d: %d errors reported (types st/cnt/str = %d/%d/%d, ids st/cnt/str = %d/%d/%d)\n",
            tid, err_cnt, types_start, types_count, types_stride, ids_start, ids_count, ids_stride);
    }

    return(err_cnt);

} /* verify_objects() */


/*******************************************************************************************
'*
 * serial_test_1
 *
 * Quick smoke check for the test wrappers on most of the H5I public API calls.
 *
 * The function performs the following operations;
 *
 * 1) Open the HDF5 library.
 *
 * 2) For ( i = 0; i++; i < NUM_ID_TYPES )
 *
 *    a) register the indicated type.
 *
 *    b) register an ID in the new type
 *
 *    c) verify the ID
 *
 *    d) get the type of the ID, and verify that it matches
 *
 *    e) remove and verify the ID
 *
 *    f) register two more IDs in the new type
 *
 *    g) verify that both of the new IDs have ref count 1
 *
 *    h) increment the ref count on both IDs
 *
 *    i) verify that both of the new IDs have ref count 2
 *
 *    j) decrement the ref count on one of the IDs, and verify that its ref count is 
 *       is now 1.
 *
 *    k) decrement its ref count again, and verify that the number of IDs in the 
 *       type is now 1.
 *
 *    l) register two more IDs and verify that their ref counts are 1
 *
 *    m) increment the ref counts of the new IDs from l) above and verify that their
 *       ref counts are both 2
 *
 *    n) verify that there are now three IDs in the type
 *
 *    o) clear the type, and verify that the number of IDs is still three.
 *
 *    p) verify that the type exists.
 *
 *    q) Increment the type ref count, decrement it, and then verify that the type 
 *       still exits.
 *
 *    r) If i is even, decrement the ref count the ref count of the type.  If i is odd
 *       destroy the type.  In either case, verifty that the type no longer exists.
 *
 * 3) Run sanity checks on the statistics maintained for free lists.
 *
 * 4) shutdown HDF5.
 *
 *******************************************************************************************/

void serial_test_1(void)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t rpt_failures = TRUE;
    int i;
    int j;
    int k;
    int l;
    int m;
    int err_cnt = 0;
    int num_mem;
    int ref_count;
    int tid = 0;
#if 0
    fprintf(stderr, "sizeof(hbool_t)              = %d\n", (int)sizeof(hbool_t));
    fprintf(stderr, "sizeof(bool)                 = %d\n", (int)sizeof(bool));
    fprintf(stderr, "sizeof(unsigned)             = %d\n", (int)sizeof(unsigned));
    fprintf(stderr, "sizeof(int)                  = %d\n", (int)sizeof(int));
    fprintf(stderr, "sizeof(hid_t)                = %d\n", (int)sizeof(hid_t));
    fprintf(stderr, "sizeof(id_type_kernel_t)     = %d\n", (int)sizeof(id_type_kernel_t));
    fprintf(stderr, "sizeof(id_instance_kernel_t) = %d\n", (int)sizeof(id_instance_kernel_t));
    fprintf(stderr, "sizeof(id_object_kernel_t)   = %d\n", (int)sizeof(id_object_kernel_t));
#endif
    TESTING("MT ID serial test #1");


    if ( H5open() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_1():%d: H5open() failed.\n", 0);
        }
    }
    for ( i = 0; i < NUM_ID_TYPES; i++ ) {

#if 0
        if ( 1 == i || 2 == i ) {

            cs = TRUE;
            ds = TRUE;

        } else {

            cs = FALSE;
            ds = FALSE;
        }
#endif
        j = i + NUM_ID_TYPES;
        k = j + NUM_ID_TYPES;
        l = k + NUM_ID_TYPES;
        m = l + NUM_ID_TYPES;

        if ( ds ) {

            fprintf(stdout, "\ni/j/k/l/m = %d/%d/%d/%d/%d\n\n", i, j, k, l, m);
        }

        err_cnt += register_type(&(types_array[i]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        err_cnt += register_id(&(types_array[i]), &(id_instance_array[i]), &(objects_array[i]), 
                               cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        err_cnt += object_verify(&(types_array[i]), &(id_instance_array[i]), &(objects_array[i]), 
                                 cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        err_cnt += get_type(&(types_array[i]), &(id_instance_array[i]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        err_cnt += remove_verify(&(types_array[i]), &(id_instance_array[i]), &(objects_array[i]), 
                                 cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        err_cnt += register_id(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]),
                               cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
      
        err_cnt += register_id(&(types_array[i]), &(id_instance_array[k]), &(objects_array[k]),
                               cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
      
        ref_count = get_ref(&(id_instance_array[j]), cs, ds, rpt_failures, tid);
        if ( ref_count != 1 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 1 expected.\n", tid, ref_count);
            }
        }

        ref_count = get_ref(&(id_instance_array[k]), cs, ds, rpt_failures, tid);
        if ( ref_count != 1 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 1 expected.\n", tid, ref_count);
            }
        }

        err_cnt += inc_ref(&(id_instance_array[j]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        err_cnt += inc_ref(&(id_instance_array[k]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
      
        ref_count = get_ref(&(id_instance_array[j]), cs, ds, rpt_failures, tid);
        if ( ref_count != 2 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 2 expected.\n", tid, ref_count);
            }
        }

        ref_count = get_ref(&(id_instance_array[k]), cs, ds, rpt_failures, tid);
        if ( ref_count != 2 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 2 expected.\n", tid, ref_count);
            }
        }

        err_cnt += dec_ref(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                           cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
      
        ref_count = get_ref(&(id_instance_array[j]), cs, ds, rpt_failures, tid);
        if ( ref_count != 1 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 1 expected.\n", tid, ref_count);
            }
        }

        err_cnt += dec_ref(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                           cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        if ( 1 != (num_mem = nmembers(&(types_array[i]), cs, ds, rpt_failures, tid)) ) {

            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: nmembers returns %d, 1 expected.\n", tid, num_mem);
            }
        }

        err_cnt += register_id(&(types_array[i]), &(id_instance_array[l]), &(objects_array[l]),
                               cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        err_cnt += register_id(&(types_array[i]), &(id_instance_array[m]), &(objects_array[m]),
                               cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
      
        ref_count = get_ref(&(id_instance_array[l]), cs, ds, rpt_failures, tid);
        if ( ref_count != 1 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 1 expected.\n", tid, ref_count);
            }
        }

        ref_count = get_ref(&(id_instance_array[m]), cs, ds, rpt_failures, tid);
        if ( ref_count != 1 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 1 expected.\n", tid, ref_count);
            }
        }

        err_cnt += inc_ref(&(id_instance_array[l]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);

        err_cnt += inc_ref(&(id_instance_array[m]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
      
        ref_count = get_ref(&(id_instance_array[l]), cs, ds, rpt_failures, tid);
        if ( ref_count != 2 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 2 expected.\n", tid, ref_count);
            }
        }

        ref_count = get_ref(&(id_instance_array[m]), cs, ds, rpt_failures, tid);
        if ( ref_count != 2 ) {
            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: ref_count = %d -- 2 expected.\n", tid, ref_count);
            }
        }


        if ( 3 != (num_mem = nmembers(&(types_array[i]), cs, ds, rpt_failures, tid)) ) {

            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: nmembers returns %d, 3 expected.\n", tid, num_mem);
            }
        }


        err_cnt += clear_type(&(types_array[i]), FALSE, cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);


        if ( 3 != (num_mem = nmembers(&(types_array[i]), cs, ds, rpt_failures, tid)) ) {

            err_cnt++;
            assert(FALSE);
            if ( rpt_failures ) {

                fprintf(stderr, "serial_test_1():%d: nmembers returns %d, 3 expected.\n", tid, num_mem);
            }
        }


        switch ( type_exists(&(types_array[i]), cs, ds, rpt_failures, tid) ) {
            case TRUE:
                /* nothing to do -- this is the expected value */
                break;
            case FALSE:
                err_cnt++;
                assert(FALSE);
                if ( rpt_failures ) {

                    fprintf(stderr, 
                    "serial_test_1():%d: type_exists(&(types_array[%d]), ...) returned FALSE, TRUE expected.\n",
                    tid, i);
                }
                break;
            case FAIL:
                err_cnt++;
                assert(FALSE);
                break;
            default:
                err_cnt++;
                assert(FALSE);
                if ( rpt_failures ) {

                    fprintf(stderr, 
                    "serial_test_1():%d: type_exists(&(types_array[%d]), ...) returned invalid value.\n",
                    tid, i);
                }
                break;
        }


        err_cnt += inc_type_ref(&(types_array[i]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);


        err_cnt += dec_type_ref(&(types_array[i]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);


        switch ( type_exists(&(types_array[i]), cs, ds, rpt_failures, tid) ) {
            case TRUE:
                /* nothing to do -- this is the expected value */
                break;
            case FALSE:
                err_cnt++;
                assert(FALSE);
                if ( rpt_failures ) {

                    fprintf(stderr, 
                    "serial_test_1():%d: type_exists(&(types_array[%d]), ...) returned FALSE, TRUE expected.\n",
                    tid, i);
                }
                break;
            case FAIL:
                err_cnt++;
                assert(FALSE);
                break;
            default:
                err_cnt++;
                assert(FALSE);
                if ( rpt_failures ) {

                    fprintf(stderr, 
                    "serial_test_1():%d: type_exists(&(types_array[%d]), ...) returned invalid value.\n",
                    tid, i);
                }
                break;
        }


        if ( (i % 2) > 0 ) {

            err_cnt += dec_type_ref(&(types_array[i]), cs, ds, rpt_failures, tid);
            assert(0 == err_cnt);

        } else {

            err_cnt += destroy_type(&(types_array[i]), cs, ds, rpt_failures, tid);
            assert(0 == err_cnt);
        }

        
        switch ( type_exists(&(types_array[i]), cs, ds, rpt_failures, tid) ) {
            case TRUE:
                err_cnt++;
                assert(FALSE);
                if ( rpt_failures ) {

                    fprintf(stderr, 
                    "serial_test_1():%d: type_exists(&(types_array[%d]), ...) returned TRUE, FALSE expected.\n",
                    tid, i);
                }
                break;
            case FALSE:
                /* nothing to do -- this is the expected value */
                break;
            case FAIL:
                err_cnt++;
                assert(FALSE);
                break;
            default:
                err_cnt++;
                assert(FALSE);
                if ( rpt_failures ) {

                    fprintf(stderr, 
                    "serial_test_1():%d: type_exists(&(types_array[%d]), ...) returned invalid value.\n",
                    tid, i);
                }
                break;
        }
    }

    if ( SERIAL_TEST_1__DISPLAY_FINAL_STATS ) {

        H5I_dump_stats(stdout);
    }

    /* Sanity checks on the id info and type info free lists. */

    /* verify that the id info and type info free lists balance. 
     * This assertion can ignore the initial free list length, since whatever it is
     * on entry, the number of ids created is such that it will be empty by the
     * time any entries are returned to the free list.  The +1 accounts for the 
     * minimum length of the free list
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.id_info_fl_len)) + 1) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.type_info_fl_len)) + 1) );

    /* Verify that the increments to the id info and type info reallocatable counts balance.
     * Note that it is possible that this test will fail if increments to the free list entries
     * reallocable fields collide just so.  This seems very improbable, but if it happens with 
     * any regularity, the test will have to be modified to account for this.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap)) +
                  atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap)) +
                  atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );


    if ( H5close() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_1():%d: H5close() failed.\n", 0);
        }
    }

    if ( 0 == err_cnt ) {

         PASSED();

    } else {

         H5_FAILED();
    }

    return;

} /* serial_test_1() */


/*******************************************************************************************
 *
 * serial_test_2
 *
 *      Second serial test of the wrappers of the H5I public API calls.
 *
 *      Test proceeds as follows:
 *
 *      1) Register types as directed by the types_start and types_count parameters
 *
 *      2) Register IDs as directed by the parameters.
 *
 *      3) Working in reverse order, verify the ID registrations in 2).
 *
 *      4) Verify that the types (or indexes) have the expected number of members.
 *
 *      5) Increment the ref count on every other ID, decrementing the ref count on 
 *         the remaining IDs.
 *
 *      6) Clear all the types indicated by types_start and types_count.
 *
 *      7) Verify that every other ID still exists.
 *
 *      8) Destroy every other type, and decrement the ref count on the other 
 *         half -- should have the same effect.
 *
 *******************************************************************************************/

void serial_test_2(int types_start, int types_count, int ids_start, int ids_count)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t rpt_failures = TRUE;
    int err_cnt = 0;
    int i;
    int j;
    int expected;
    int num_mem;
    int tid = 0;
    uint64_t init_id_info_fl_len;
    uint64_t init_type_info_fl_len;
    uint64_t init_num_id_info_fl_entries_reallocable;
    uint64_t init_num_type_info_fl_entries_reallocable;;

    TESTING("MT ID serial test #2");
    fflush(stdout);

    if ( H5open() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_2():%d: H5open() failed.\n", 0);
        }
    }

    H5I_clear_stats();

    init_id_info_fl_len   = atomic_load(&(H5I_mt_g.id_info_fl_len));
    init_type_info_fl_len = atomic_load(&(H5I_mt_g.type_info_fl_len));

    init_num_id_info_fl_entries_reallocable   = atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable));
    init_num_type_info_fl_entries_reallocable = atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable));

    for ( i = types_start; i < types_start + types_count; i++ ) {

        err_cnt += register_type(&(types_array[i]), cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    for ( j = ids_start; j < ids_start + ids_count; j++ ) {

        i = types_start + (j % types_count);
        assert( i >= types_start );
        assert( i < types_start + types_count );

        err_cnt += register_id(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                               cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    for ( j = ids_start + ids_count - 1; j >= ids_start; j-- ) {

        i = types_start + (j % types_count);
        assert( i >= types_start );
        assert( i < types_start + types_count );

        err_cnt += object_verify(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                                 cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    for ( i = types_start; i < types_start + types_count; i++ ) {

        num_mem = nmembers(&(types_array[i]), cs, ds, rpt_failures, tid);
        expected = (ids_count / types_count) + (((ids_count % types_count) > i) ? 1 : 0);

        if ( num_mem != expected ) {
            assert(FALSE);
            err_cnt++;
            if ( rpt_failures ) {

                fprintf(stderr, "nmembers(type_array[%d], ...) returns %d, %d expected,\n", 
                        i, num_mem, expected);
            }
        }
    }

    for ( j = ids_start; j < ids_start + ids_count; j++ ) {

        i = types_start + (j % types_count);
        assert( i >= types_start );
        assert( i < types_start + types_count );

        if ( j % 2 > 0 ) {

            err_cnt += inc_ref(&(id_instance_array[j]), cs, ds, rpt_failures, tid);
            assert(0 == err_cnt);

        } else {

            err_cnt += dec_ref(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                               cs, ds, rpt_failures, tid);
            assert(0 == err_cnt);
        }
    }

    for ( i = types_start; i < types_start + types_count; i++ ) {

        err_cnt += clear_type(&(types_array[i]), FALSE, cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    for ( j = ids_start + 1; j < ids_start + ids_count; j += 2 ) {

        i = types_start + (j % types_count);
        assert( i >= types_start );
        assert( i < types_start + types_count );

        err_cnt += object_verify(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), 
                                 cs, ds, rpt_failures, tid);
        assert(0 == err_cnt);
    }

    for ( i = types_start; i < types_start + types_count; i++ ) {

        if ( (i % 2) > 0 ) {

            err_cnt += dec_type_ref(&(types_array[i]), cs, ds, rpt_failures, tid);
            assert(0 == err_cnt);

        } else {

            err_cnt += destroy_type(&(types_array[i]), cs, ds, rpt_failures, tid);
            assert(0 == err_cnt);
        }
    }

    if ( SERIAL_TEST_2__DISPLAY_FINAL_STATS ) {

        H5I_dump_stats(stdout);

        fprintf(stderr, "init_id_info_fl_len = %lld, init_type_info_fl_len = %lld\n",
                (unsigned long long)init_id_info_fl_len, (unsigned long long)init_type_info_fl_len);
        fprintf(stderr, 
                "init_num_id_info_fl_entries_reallocable = %lld, init_num_type_info_fl_entries_reallocable = %lld\n",
                (unsigned long long)init_num_id_info_fl_entries_reallocable,
                (unsigned long long)init_num_type_info_fl_entries_reallocable);
    }

    /* Sanity checks on the id info and type info free lists. */

    /* verify that the id info and type info free lists balance.
     * Note that in this case, the allocs and frees balance, so we must take 
     * account of the initial free list length in out tests.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.id_info_fl_len)) + init_id_info_fl_len) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.type_info_fl_len)) + init_type_info_fl_len) );

    /* Verify that the increments to the id info and type info reallocatable counts balance.
     *
     * As above, we must include the initial free list lengths since the allocations and 
     * deallocations balence in this test.
     *
     * Note that it is possible that this test will fail if increments to the free list entries
     * reallocable fields collide just so.  This seems very improbable, but if it happens with 
     * any regularity, the test will have to be modified to account for this.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap)) -
                  init_id_info_fl_len + init_num_id_info_fl_entries_reallocable +
                  atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap)) -
                  init_type_info_fl_len + init_num_type_info_fl_entries_reallocable +
                  atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    if ( H5close() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_2():%d: H5close() failed.\n", 0);
        }
    }

    if ( 0 == err_cnt ) {

         PASSED();

    } else {

         H5_FAILED();
    }

    return;

} /* serial_test_2() */


/*******************************************************************************************
 *
 * serial_test_3
 *
 * Third serial test of the wrappers of the H5I public API calls.
 *
 * The test proceed as follows:
 *
 *  1) Open HDF5.
 *
 *  2) Create three types.
 *
 *  3) Register 10,000 IDs distributed across the three types.
 *
 *  4) Increment the ref count on the first two types.
 *
 *  5) decrement the ref count on the first thousand IDs in each type.
 *
 *  6) increment the ref count on the second three thousand IDs
 *
 *  7) Starting with the 3001th ID, verify the next 4000 IDs.
 *
 *  8) Decrement the ref count on all three types.
 *
 *  9) Destroy the first two types
 *
 * 10) Run sanity checks on the free list stats.
 *
 * 11) Close HDF5
 *
 * Report failure if any errors were detected, and success otherwise.
 *
 *******************************************************************************************/

void serial_test_3(void)
{
    hbool_t display_op_stats = FALSE;
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t rpt_failures = TRUE;
    int err_cnt = 0;
    int tid = 0;
    uint64_t init_id_info_fl_len;
    uint64_t init_type_info_fl_len;
    uint64_t init_num_id_info_fl_entries_reallocable;
    uint64_t init_num_type_info_fl_entries_reallocable;;

    TESTING("MT ID serial test #3");
    fflush(stdout);

    if ( H5open() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_3():%d: H5open() failed.\n", 0);
        }
    }

    H5I_clear_stats();

    init_id_info_fl_len   = atomic_load(&(H5I_mt_g.id_info_fl_len));
    init_type_info_fl_len = atomic_load(&(H5I_mt_g.type_info_fl_len));

    init_num_id_info_fl_entries_reallocable   = atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable));
    init_num_type_info_fl_entries_reallocable = atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable));

    err_cnt += create_types(0, 3, 3, cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "create_types()");
        H5I_clear_stats();
    }

    err_cnt += register_ids(0, 3, 3, 0, 10000, 1, cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "register_ids");
        H5I_clear_stats();
    }

    err_cnt += inc_type_refs(0, 2, 3, cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "inc_type_refs()");
        H5I_clear_stats();
    }

    err_cnt += dec_refs(0, 1, 1, 0, 1000, 3, cs, ds, rpt_failures, tid);
    err_cnt += dec_refs(3, 1, 1, 1, 1000, 3, cs, ds, rpt_failures, tid);
    err_cnt += dec_refs(6, 1, 1, 2, 1000, 3, cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "dec_refs()");
        H5I_clear_stats();
    }    

    err_cnt += inc_refs(3001, 3000, 1, cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "inc_refs()");
        H5I_clear_stats();
    }

    err_cnt += verify_objects(0, 3, 3,  3000, 7000, 1, cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "verify_objects()");
        H5I_clear_stats();
    }

    err_cnt += dec_type_refs(0, 3, 3, cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "dec_type_refs()");
        H5I_clear_stats();
    }

    err_cnt += destroy_types(0, 2, 3, cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "destroy_types()");
        H5I_clear_stats();
    }

    if ( SERIAL_TEST_3__DISPLAY_FINAL_STATS ) {

        H5I_dump_stats(stdout);

        fprintf(stderr, "init_id_info_fl_len = %lld, init_type_info_fl_len = %lld\n",
                (unsigned long long)init_id_info_fl_len, (unsigned long long)init_type_info_fl_len);
        fprintf(stderr, 
                "init_num_id_info_fl_entries_reallocable = %lld, init_num_type_info_fl_entries_reallocable = %lld\n",
                (unsigned long long)init_num_id_info_fl_entries_reallocable,
                (unsigned long long)init_num_type_info_fl_entries_reallocable);
    }

    /* Sanity checks on the id info and type info free lists. */

    /* verify that the id info and type info free lists balance.
     * Note that in this case, the allocs and frees balance, so we must take 
     * account of the initial free list length in out tests.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.id_info_fl_len)) + init_id_info_fl_len) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.type_info_fl_len)) + init_type_info_fl_len) );

    /* Verify that the increments to the id info and type info reallocatable counts balance.
     *
     * As above, we must include the initial free list lengths since the allocations and 
     * deallocations balence in this test.
     *
     * Note that it is possible that this test will fail if increments to the free list entries
     * reallocable fields collide just so.  This seems very improbable, but if it happens with 
     * any regularity, the test will have to be modified to account for this.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap)) -
                  init_id_info_fl_len + init_num_id_info_fl_entries_reallocable +
                  atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap)) -
                  init_type_info_fl_len + init_num_type_info_fl_entries_reallocable +
                  atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    if ( H5close() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_3():%d: H5close() failed.\n", 0);
        }
    }

    if ( 0 == err_cnt ) {

         PASSED();

    } else {

         H5_FAILED();
    }

    return;

} /* serial_test_3() */


/*******************************************************************************************
 *
 * serial_test_4
 *
 *      Smoke check of the future ID facility.
 *
 *       1) Create 64 future IDs and 64 real IDs that will evenutally be associated with 
 *          the future IDs, and allow them to be realized.  Do not set up the associations
 *          just yet.
 *
 *
 *      Verify that verification of future IDs works in the base case:
 *
 *       2) Lookup all the future and real IDs.  Verify that the real ID lookups succeed, 
 *          and that the future ID lookups all fail.
 *
 *       3) For every eighth future ID (i = 0, 16, 32, 48, ... ), link the associated real 
 *          ID (j = 1, 17, 33, 47, ...) to the future ID.
 *
 *       4) Verify that lookups of all the real IDs still succeed.
 *
 *       5) Lookup all the future and real IDs.  Verify that every eighth future ID lookup 
 *          (i = 0, 16, 32, 48, ... ) succeeds, while the rest fail, and that every eighth
 *          real ID lookup (j = 1, 17, 33, 47, ...) fails while the rest succeed.
 *
 *
 *      Link some real IDs to future IDs, and then delete the real IDs before the linked 
 *      future IDs are realized.  This appears to expose a fundamental problem with the current
 *      future ID mechanism.  At present, this is papered over via the user provided realize and
 *      discard callbacks -- however, in the absence of this fix, data structure corruption 
 *      is possible.  NEED A FIX FOR THIS.
 *
 *       6) For every eighth + 1 future ID (i = 2, 18, 34, 50, ... ), link the associated real 
 *          ID (j = 3, 19, 35, 51, ...) to the future ID.
 *
 *       7) Verify that lookups of the newly linked real IDs still succeed.
 *
 *       8) Decrement the ref count on eacy of the newly linked reals IDs.
 *
 *       9) Verify that lookups of the newly linked real IDs all fail.
 *
 *      10) Lookup all the ruture and real IDs.  
 *
 *          Verify that every eighth future ID lookup (i = 0, 16, 32, 48, ... ) succeeds, 
 *          while the rest fail
 *
 *          Verify that every eighth (j = 1, 17, 33, 47, ... ) and every eighth + 1
 *          (j = 3, 19, 35, 51, ...) real ID fails, while the rest succeed. 
 *
 *
 *      Verify that source real IDs are deleted properly even if their reference counts are 
 *      are greater than 1.
 *
 *      11) For every eighth + 1 future ID (i = 2, 18, 34, 50, ... ), link the associated real 
 *          ID (j = 3, 19, 35, 51, ...) to the future ID.
 *
 *      12) Verify that lookups of the newly linked real IDs still succeed.
 *
 *      13) Incecrement the ref counts on each of the newly linked reals IDs.
 *
 *      14) Verify that lookups of the newly linked real IDs all succeed.
 *
 *      15) Lookup all the ruture and real IDs.  
 *
 *          Verify that every eighth and every eight +2 future ID lookup (i = 0, 16, 32, 48, ... ) 
 *          succeeds, while the rest fail
 *
 *          Verify that every eighth (j = 1, 17, 33, 47, ... ), every eighth + 1
 *          (j = 3, 19, 35, 51, ...) and every eight + 2 (j = 5, 21, 37, 53, ...) real ID 
 *          fails, while the rest succeed. 
 *      
 *
 *
 *******************************************************************************************/

void serial_test_4(void)
{
    hbool_t display_op_stats = FALSE;
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t rpt_failures = TRUE;
    int err_cnt = 0;
    int i;
    int j;
    int tid = 0;
    uint64_t init_id_info_fl_len;
    uint64_t init_type_info_fl_len;
    uint64_t init_num_id_info_fl_entries_reallocable;
    uint64_t init_num_type_info_fl_entries_reallocable;;

    TESTING("MT ID serial test #4");
    fflush(stdout);

    if ( H5open() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_4():%d: H5open() failed.\n", 0);
        }
    }

    H5I_clear_stats();

    init_id_info_fl_len   = atomic_load(&(H5I_mt_g.id_info_fl_len));
    init_type_info_fl_len = atomic_load(&(H5I_mt_g.type_info_fl_len));

    init_num_id_info_fl_entries_reallocable   = atomic_load(&(H5I_mt_g.num_id_info_fl_entries_reallocable));
    init_num_type_info_fl_entries_reallocable = atomic_load(&(H5I_mt_g.num_type_info_fl_entries_reallocable));

    err_cnt += register_type(&(types_array[0]), cs, ds, rpt_failures, tid);

    /* create the future IDs and their associated real IDs.  Note that they are not 
     * associated yet, so the future IDs can't be realized on search.
     */
    for ( i = 0; i < 128; i += 2 )
    {
        j = i + 1;

        err_cnt += register_future_id(&(types_array[0]), &(id_instance_array[i]), &(objects_array[i]),
                                      realize_cb_0, discard_cb_0, cs, ds, rpt_failures, tid);

        err_cnt += register_id(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]), 
                               cs, ds, rpt_failures, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "Register future IDs 1");

        H5I_clear_stats();
    }

    /* verify that lookups of the future IDs fail and lookups of the real IDs succeed */
    for ( i = 0; i < 128; i += 2 ) {
    
        j = i + 1;

        err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), NULL,
                                 cs, ds, rpt_failures, tid);

        err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]), 
                                 cs, ds, rpt_failures, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "ID lookups 1 -- test a");

        H5I_clear_stats();
    }

    /* For every eighth future ID (i = 0, 16, 32, 48, ... ), link the associated real 
     * ID (j = 1, 17, 33, 47, ...) to the future ID.
     */
    for ( i = 0; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += link_real_and_future_ids(&(objects_array[i]),  &(objects_array[j]), rpt_failures);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "Link real and future IDs 1 -- test a");

        H5I_clear_stats();
    }
    
    /* Verify that lookups of all the real IDs still succeed. */
    for ( j = 1; j <= 128; j += 2 ) {

        err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]), 
                                 cs, ds, rpt_failures, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "ID lookups 2 -- verify real IDs -- test a");

        H5I_clear_stats();
    }


    /* Lookup all the future and real IDs.  Verify that every eighth future ID lookup 
     * (i = 0, 16, 32, 48, ... ) succeeds, while the rest fail, and that every eighth
     * real ID lookup (j = 1, 17, 33, 47, ...) fails while the rest succeed.
     */
    for ( i = 0; i < 128; i += 2 ) {
    
        j = i + 1;

        if ( 0 == i % 16 ) {

            id_object_kernel_t id_obj_k;

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), &(objects_array[j]),
                                     cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), NULL,
                                    cs, ds, rpt_failures, tid);

            id_obj_k = atomic_load(&(objects_array[i].k));

            if ( ! id_obj_k.discarded ) {

                err_cnt++;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                           "objects_array[%d], not marked as discarded after future id realized.\n", i);
                }
            }
        } else {

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), NULL,
                                   cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]),
                                     cs, ds, TRUE, tid);
        }
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "Realize future IDs 1 -- test a");

        H5I_clear_stats();
    }


    /* For every eighth + 1 future ID (i = 2, 18, 34, 50, ... ), link the associated real 
     * ID (j = 3, 19, 35, 51, ...) to the future ID.
     */
    for ( i = 2; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += link_real_and_future_ids(&(objects_array[i]),  &(objects_array[j]), rpt_failures);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "Link real and future IDs 2 -- test b");

        H5I_clear_stats();
    }
 

    /* Verify that lookups of the newly linked real IDs still succeed. */
    for ( i = 2; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]),
                                 cs, ds, TRUE, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "ID lookups 3 -- verify real IDs -- test b");

        H5I_clear_stats();
    }


    /* Decrement the ref count on eacy of the newly linked reals IDs. */
    for ( i = 2; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += dec_ref(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]), cs, ds, TRUE, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "decrement (really delete) real ID ref counts 1 -- test b");

        H5I_clear_stats();
    }
 

    /* Verify that lookups of the newly linked real IDs all fail. */
    for ( i = 2; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), NULL, cs, ds, TRUE, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "ID lookups 4 -- verify real IDs -- should fail -- test b");

        H5I_clear_stats();
    }


    /* Lookup all the ruture and real IDs.  
     *
     * Verify that every eighth future ID lookup (i = 0, 16, 32, 48, ... ) succeeds, 
     * while the rest fail
     *
     * Verify that every eighth (j = 1, 17, 33, 47, ... ) and every eighth + 1
     * (j = 3, 19, 35, 51, ...) real ID fails, while the rest succeed. 
     */
    for ( i = 0; i < 128; i += 2 ) {
    
        j = i + 1;

        if ( 0 == i % 16 ) {

            id_object_kernel_t id_obj_k;

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), &(objects_array[j]),
                                     cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), NULL,
                                    cs, ds, rpt_failures, tid);

            id_obj_k = atomic_load(&(objects_array[i].k));

            if ( ! id_obj_k.discarded ) {

                err_cnt++;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                           "objects_array[%d], not marked as discarded after future id realized.\n", i);
                }
            }
        } else if ( 2 == i % 16 ) {

            /* Attempt to verify the future ID twise -- should fail both times.  Need to do this 
             * because the fact that the real ID has been deleted will not become evident until after
             * the realize callback has been called on the first invocation.  Thus a second attempt 
             * is necessary to verify that we handle the repeat call to the realize callback gracefully.
             */
            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), NULL, cs, ds, FALSE, tid);
            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), NULL, cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), NULL, cs, ds, TRUE, tid);

        } else {

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), NULL,
                                   cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]),
                                     cs, ds, TRUE, tid);
        }
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "Realize future IDs 2 -- should fail -- test b");

        H5I_clear_stats();
    }



    /* For every eighth + 1 future ID (i = 2, 18, 34, 50, ... ), link the associated real 
     * ID (j = 3, 19, 35, 51, ...) to the future ID.
     */
    for ( i = 4; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += link_real_and_future_ids(&(objects_array[i]),  &(objects_array[j]), rpt_failures);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "Link real and future IDs 3 -- test c");

        H5I_clear_stats();
    }
 

    /* Verify that lookups of the newly linked real IDs still succeed. */
    for ( i = 4; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]),
                                 cs, ds, TRUE, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "ID lookups 5 -- verify real IDs -- test c");

        H5I_clear_stats();
    }


    /* Incecrement the ref counts on each of the newly linked reals IDs. */
    for ( i = 4; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += inc_ref(&(id_instance_array[j]), cs, ds, rpt_failures, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "ncrement real ID ref counts 1 -- test c");

        H5I_clear_stats();
    }
 

    /* Verify that lookups of the newly linked real IDs all succeed. */
    for ( i = 4; i < 128; i += 2 * 8 ) {

        j = i + 1;

        err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]),
                                 cs, ds, TRUE, tid);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "ID lookups 6 -- verify real IDs -- test c");

        H5I_clear_stats();
    }


    /* Lookup all the future and real IDs.  
     *
     *      Verify that every eighth and every eight +2 future ID lookup (i = 0, 16, 32, 48, ... ) 
     *      succeeds, while the rest fail
     *
     *      Verify that every eighth (j = 1, 17, 33, 47, ... ), every eighth + 1
     *      (j = 3, 19, 35, 51, ...) and every eight + 2 (j = 5, 21, 37, 53, ...) real ID 
     *      fails, while the rest succeed. 
     */
    for ( i = 0; i < 128; i += 2 ) {
    
        j = i + 1;

        if ( 0 == i % 16 ) {

            id_object_kernel_t id_obj_k;

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), &(objects_array[j]),
                                     cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), NULL,
                                    cs, ds, rpt_failures, tid);

            id_obj_k = atomic_load(&(objects_array[i].k));

            if ( ! id_obj_k.discarded ) {

                err_cnt++;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                           "objects_array[%d], not marked as discarded after future id realized.\n", i);
                }
            }
        } else if ( 2 == i % 16 ) {

            /* Attempt to verify the future ID twise -- should fail both times.  Need to do this 
             * because the fact that the real ID has been deleted will not become evident until after
             * the realize callback has been called on the first invocation.  Thus a second attempt 
             * is necessary to verify that we handle the repeat call to the realize callback gracefully.
             */
            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), NULL, cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), NULL, cs, ds, TRUE, tid);

        } else if ( 4 == i % 16 ) {

            id_object_kernel_t id_obj_k;

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), &(objects_array[j]),
                                     cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), NULL,
                                    cs, ds, rpt_failures, tid);

            id_obj_k = atomic_load(&(objects_array[i].k));

            if ( ! id_obj_k.discarded ) {

                err_cnt++;

                if ( rpt_failures ) {

                    fprintf(stderr, 
                           "objects_array[%d], not marked as discarded after future id realized.\n", i);
                }
            }
        } else {

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[i]), NULL,
                                   cs, ds, FALSE, tid);

            err_cnt += object_verify(&(types_array[0]), &(id_instance_array[j]), &(objects_array[j]),
                                     cs, ds, TRUE, tid);
        }
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "Realize future IDs 3 -- should succeed -- test c");

        H5I_clear_stats();
    }
 

    /* cleanup after test */

    err_cnt += destroy_type(&(types_array[0]), cs, ds, rpt_failures, tid);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "destroy_type()");
        H5I_clear_stats();
    }

    if ( SERIAL_TEST_4__DISPLAY_FINAL_STATS ) {

        H5I_dump_stats(stdout);

        fprintf(stderr, "init_id_info_fl_len = %lld, init_type_info_fl_len = %lld\n",
                (unsigned long long)init_id_info_fl_len, (unsigned long long)init_type_info_fl_len);
        fprintf(stderr, 
                "init_num_id_info_fl_entries_reallocable = %lld, init_num_type_info_fl_entries_reallocable = %lld\n",
                (unsigned long long)init_num_id_info_fl_entries_reallocable,
                (unsigned long long)init_num_type_info_fl_entries_reallocable);
    }

    /* Sanity checks on the id info and type info free lists. */

    /* verify that the id info and type info free lists balance.
     * Note that in this case, the allocs and frees balance, so we must take 
     * account of the initial free list length in out tests.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.id_info_fl_len)) + init_id_info_fl_len) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.type_info_fl_len)) + init_type_info_fl_len) );

    /* Verify that the increments to the id info and type info reallocatable counts balance.
     *
     * As above, we must include the initial free list lengths since the allocations and 
     * deallocations balence in this test.
     *
     * Note that it is possible that this test will fail if increments to the free list entries
     * reallocable fields collide just so.  This seems very improbable, but if it happens with 
     * any regularity, the test will have to be modified to account for this.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap)) -
                  init_id_info_fl_len + init_num_id_info_fl_entries_reallocable +
                  atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap)) -
                  init_type_info_fl_len + init_num_type_info_fl_entries_reallocable +
                  atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    if ( H5close() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_4():%d: H5close() failed.\n", 0);
        }
    }

    if ( 0 == err_cnt ) {

         PASSED();

    } else {

         H5_FAILED();
    }

    return;

} /* serial_test_4() */


/*******************************************************************************************
 *
 * mt_test_fcn_1()
 *
 *      First multi-thread test function.  
 *
 *      This test function is intended to implement an initial smoke check in which 
 *      multiple IDs of multiple types are created, accessed, ref count incremented and/or 
 *      decremented, and finally discarded -- all without any expected failures.
 *
 *      Note that all types must be defined prior to the invocation of this function.
 *
 *      The test proceeds as follows:
 *
 *      1) register params->ids_count IDs with the specified start and stride in the 
 *         id_instance_array[], and with types specified by the params->type_start,
 *         parame->types-count, params->types_stride,
 *
 *      2) Increment the ref count on all the IDs just created.
 *
 *      3) verify the IDs just created.
 *
 *      4) Increment the ref count on every other type.
 *
 *      5) Decrement the ref count on every other ID
 *
 *      6) Verify all IDs
 *
 *      7) Decrement the ref count on all IDs -- in effect, this deletes every other ID
 *         registered in 1).
 *
 *******************************************************************************************/

void * mt_test_fcn_1(void * _params)
{
    hbool_t display_op_stats = FALSE;
    hbool_t show_progress = FALSE;
    int                i;
    int                j;
    mt_test_params_t * params = (mt_test_params_t *)_params;

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 0 -- entering \n", params->thread_id);
        fprintf(stderr, 
         "mt_test_fcn_1:%d: params st/cnt/st = %d/%d/%d, id st/cnt/st = %d/%d/%d, obj st/cnt/st = %d/%d/%d\n",
         params->thread_id, 
         params->types_start, params->types_count, params->types_stride,
         params->ids_start, params->ids_count, params->ids_stride,
         params->objects_start, params->objects_count, params->objects_stride);
    }

    if ( display_op_stats ) {

        H5I_clear_stats();
    }

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 1\n", params->thread_id);
    }

    params->err_cnt += register_ids(params->types_start, params->types_count, params->types_stride,
                                    params->ids_start, params->ids_count, params->ids_stride,
                                    params->cs, params->ds, params->rpt_failures, params->thread_id);

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 2\n", params->thread_id);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "register_ids()");
        H5I_clear_stats();
    }

    params->err_cnt += inc_refs(params->ids_start, params->ids_count, params->ids_stride, 
                                params->cs, params->ds, params->rpt_failures, params->thread_id);

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 3\n", params->thread_id);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "inc_refs()");
        H5I_clear_stats();
    }

    params->err_cnt += verify_objects(params->types_start, params->types_count, params->types_stride,
                                      params->ids_start, params->ids_count, params->ids_stride,
                                      params->cs, params->ds, params->rpt_failures, params->thread_id);

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 4\n", params->thread_id);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "verify_objects()");
        H5I_clear_stats();
    }

    params->err_cnt += inc_type_refs(params->types_start, params->types_count / 2, params->types_stride,
                                     params->cs, params->ds, params->rpt_failures, params->thread_id);

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 5\n", params->thread_id);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "inc_type_refs()");
        H5I_clear_stats();
    }

    for ( i = 0; i < params->types_count; i++ ) {

        j = params->types_start + (i * params->types_stride);

        params->err_cnt += dec_refs(j, 1, 1, params->ids_start + i, params->ids_count / (params->types_count * 2),
                                    params->ids_stride * params->types_count, params->cs, params->ds, 
                                    params->rpt_failures, params->thread_id);
    }

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 6\n", params->thread_id);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "dec_refs()");
        H5I_clear_stats();
    }

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 7\n", params->thread_id);
    }

    params->err_cnt += verify_objects(params->types_start, params->types_count, params->types_stride,
                                      params->ids_start, params->ids_count, params->ids_stride, 
                                      params->cs, params->ds, params->rpt_failures, params->thread_id);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "verify_objects()");
        H5I_clear_stats();
    }

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 8\n", params->thread_id);
    }
    
    params->err_cnt += dec_refs(params->types_start, params->types_count, params->types_stride,
                                params->ids_start, params->ids_count, params->ids_stride,
                                params->cs, params->ds, params->rpt_failures, params->thread_id);

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 9\n", params->thread_id);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "dec_refs()");
        H5I_clear_stats();
    }

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 10 -- exiting\n", params->thread_id);
    }

    return(NULL);

} /* mt_test_fcn_1() */


/*******************************************************************************************
 *
 * mt_test_fcn_2()
 *
 *      Second multi-thread test function.  
 *
 *      This test function is intended to implement a test consisting of a largely random 
 *      interleaving of various H5I API calls in which types are created, flushed, and 
 *      destroyed, and IDs are registered, accessed, reference counts incremented and 
 *      decremented, and eventually deleted.  Since this process is largely random, this 
 *      means that types and IDs may be referenced after they are deleted.  
 *
 *******************************************************************************************/

void * mt_test_fcn_2(void * _params)
{
    hbool_t            show_progress = FALSE;
    hbool_t            proceed = TRUE;
    hbool_t            types_assigned[NUM_ID_TYPES];
    int                type_index;
    int                target_id_type_index;
    int                id_incr_ambig = 0;
    int                id_decr_ambig = 0;
    int                id_get_ref_ambig = 0;
    int                id_obj_ver_ambig = 0;
    int                id_obj_remove_ver_ambig = 0;
    int                i;
    int                j;
    int                target_id_index;
    int                operation;
    int                ops_per_id = 10;
    int                ids_completed = 0;
    int              * id_op_counts = NULL;
    mt_test_params_t * params = (mt_test_params_t *)_params;

    if ( show_progress ) {

        fprintf(stderr, "mt_test_fcn_1:%d: -- point 0 -- entering \n", params->thread_id);
        fprintf(stderr, 
         "mt_test_fcn_2:%d: type st/cnt/st = %d/%d/%d, id st/cnt/st = %d/%d/%d, obj st/cnt/st = %d/%d/%d\n",
         params->thread_id, 
         params->types_start, params->types_count, params->types_stride,
         params->ids_start, params->ids_count, params->ids_stride,
         params->objects_start, params->objects_count, params->objects_stride);
    }

    // H5E_BEGIN_TRY {

    /* setup the types assigned array, and register the assigned types in passing.
     *
     * Cells in this array are set to TRUE if this thread is is responsible for 
     * registering and de-registring the type managed by the instance of 
     * id_type_t at the same index in types_array[].
     */
    j = 0;
    type_index = params->types_start;
    for ( i = 0; i < NUM_ID_TYPES; i++ ) {

        if ( i < type_index ) {

            types_assigned[i] = FALSE;

        } else {

            assert( i == type_index );

            assert(0 == atomic_load(&(types_array[type_index].successful_registers)));
            assert(0 == atomic_load(&(types_array[type_index].failed_registers)));

            try_register_type(type_index, params->cs, params->ds, params->rpt_failures, 
                              params->thread_id);

            assert(1 == atomic_load(&(types_array[type_index].successful_registers)));
            assert(0 == atomic_load(&(types_array[type_index].failed_registers)));

            types_assigned[i] = TRUE;

            j++;

            if ( j < params->types_count ) {

                type_index += params->types_stride;

            } else {  /* no more types assigned to this thread */

                type_index = NUM_ID_TYPES; 
            }
        }
    }

    H5E_BEGIN_TRY {

    assert( j == params->types_count );

    /* setup the id progress array, and initialize it to zero */
    if ( NULL == (id_op_counts = (int *)malloc(((size_t)(NUM_ID_INSTANCES)) * sizeof(int))) ) {

        proceed = FALSE;

    } else {

        for ( i = 0; i < NUM_ID_INSTANCES; i++ ) {
 
            id_op_counts[i] = 0;
        }
    }

    while ( ( proceed ) && ( ids_completed < params->ids_count ) ) {

        /* pick an ID */
        
        target_id_index = params->ids_start + ((rand() % params->ids_count) * params->ids_stride);
        assert(target_id_index >= 0);
        assert(target_id_index < NUM_ID_INSTANCES);

        /* load the target ID's assigned type index.  Note that this type is not guaranteed to 
         * have benn registered yet unless it is one of the types assigned to this thread.
         */
        target_id_type_index = atomic_load(&(id_instance_array[target_id_index].type_index));
        assert( 0 <= target_id_type_index );
        assert( target_id_type_index < NUM_ID_TYPES );

        /* if id_op_counts[target_id_index] == 0, check to see if the target_id_type_index is one 
         * of the type indexes assigned to this thread.  If so, register the target id.  Otherwise, 
         * increment its reference count.
         */
        if ( 0 == id_op_counts[target_id_index] ) {

            if ( types_assigned[target_id_type_index] ) {

                /* the type assigned to target_id_type_index has been registered -- now register the id */

                assert(0 == atomic_load(&(id_instance_array[target_id_index].successful_registrations)));
                assert(0 == atomic_load(&(id_instance_array[target_id_index].failed_registrations)));

                /* use the same index for the object as the instance */
                try_register_id(target_id_index, target_id_index, params->cs, params->ds, 
                                params->rpt_failures, params->thread_id);

                assert(1 == atomic_load(&(id_instance_array[target_id_index].successful_registrations)));
                assert(0 == atomic_load(&(id_instance_array[target_id_index].failed_registrations)));
                
            } else {

                /* the target id may or may not be registerd -- just try to increment its ref count */
                i = try_inc_ref(target_id_index, params->cs, params->ds, 
                                                 params->rpt_failures, params->thread_id);
                params->ambig_cnt += i;
                id_incr_ambig += i;

            }

            id_op_counts[target_id_index]++;

        } else if ( id_op_counts[target_id_index] >= ops_per_id ) {

            ids_completed++;

        } else {

            operation = rand() % 100;

            id_op_counts[target_id_index]++;

            switch ( operation ) {

                case  0:
                case  1:
                case  2:
                case  3: 
                case  4:
                case  5:
                case  6:
                case  7:
                case  8:
                case  9:
#if 1
                    i = try_inc_ref(target_id_index, params->cs, params->ds, 
                                    params->rpt_failures, params->thread_id);
                    params->ambig_cnt += i;
                    id_incr_ambig += i;
                    break;
#endif
                case 10:
                case 11:
                case 12:
                case 13: 
                case 14:
                case 15:
                case 16:
                case 17:
                case 18:
#if 1
                    i = try_dec_ref(target_id_index, params->cs, params->ds, 
                                    params->rpt_failures, params->thread_id);
                    params->ambig_cnt += i;
                    id_decr_ambig += i;
                    break;
#endif
                case 19:
#if 1
                    i = try_remove_verify(target_id_index, params->cs, params->ds,
                                          params->rpt_failures, params->thread_id);
                    params->ambig_cnt += i;
                    id_obj_remove_ver_ambig += i;
                    break;
#endif
                case 20:
                case 21:
                case 22:
                case 23: 
                case 24:
                case 25:
                case 26:
                case 27:
                case 28:
                case 29:
                    i = try_get_ref(target_id_index, params->cs, params->ds,
                                    params->rpt_failures, params->thread_id);
                    params->ambig_cnt += i;
                    id_get_ref_ambig += i;
                    break;

                case 30:
                case 31:
                case 32:
                case 33: 
                case 34:
                case 35:
                case 36:
                case 37:
                case 38:
                case 39:
                case 40:
                case 41:
                case 42:
                case 43: 
                case 44:
                case 45:
                case 46:
                case 47:
                case 48:
                case 49:
                case 50:
                case 51:
                case 52:
                case 53: 
                case 54:
                case 55:
                case 56:
                case 57:
                case 58:
                case 59:
                case 60:
                case 61:
                case 62:
                case 63: 
                case 64:
                case 65:
                case 66:
                case 67:
                case 68:
                case 69:
                case 70:
                case 71:
                case 72:
                case 73: 
                case 74:
                case 75:
                case 76:
                case 77:
                case 78:
                case 79:
                case 80:
                case 81:
                case 82:
                case 83: 
                case 84:
                case 85:
                case 86:
                case 87:
                case 88:
                case 89:
                case 90:
                case 91:
                case 92:
                case 93: 
                case 94:
                case 95:
                case 96:
                case 97:
                case 98:
                case 99:
                    i = try_object_verify(target_id_index, params->cs, params->ds, 
                                                           params->rpt_failures, params->thread_id);
                    params->ambig_cnt += i;
                    id_obj_ver_ambig += i;
                    break;

                default: 
                    assert(FALSE);
            }
        }
    } /* while */

    if ( MT_TEST_1__DISPLAY_FINAL_STATS ) {

        fprintf(stderr, 
          "\nref cnt incr / decr / get ambig = %d / %d / %d, obj ver / rmv ver ambig = %d / %d, ambig_cnt = %d\n\n",
            id_incr_ambig, id_decr_ambig, id_get_ref_ambig, id_obj_ver_ambig, id_obj_remove_ver_ambig, 
            params->ambig_cnt);
    }

    for ( i = 0; i < NUM_ID_TYPES; i++ ) {

        if ( types_assigned[i] ) {

            params->ambig_cnt += try_destroy_type(i, params->cs, params->ds, params->rpt_failures, 
                                                  params->thread_id);
        }
    }

    } H5E_END_TRY

    return(NULL);

} /* mt_test_fcn_2() */


/*******************************************************************************************
 * mt_test_fcn_1_serial_test()
 *
 *      Serial test designed to test mt_test_fcn_t.
 *
 *      1) set up the instance of mt_test_params_t necessary for the call to mt_test_fcn_1.
 *
 *      2) Create the types needed in the call to mt_test_fcn_1().
 *
 *      3) call mt_test_fcn_1().
 *
 *      4) destroy the types created in 2).
 *
 *      
 *******************************************************************************************/

void mt_test_fcn_1_serial_test(void)
{
    int err_cnt = 0;
    mt_test_params_t params = { /* thread_id      = */     0,
                                /* types_start    = */     0,
                                /* types_count    = */     3,
                                /* types_stride   = */     3,
                                /* ids_start      = */     0,
                                /* ids_count      = */ 10000,
                                /* ids_stride     = */     1,
                                /* objects_start  = */     0,
                                /* objects_count  = */ 10000,
                                /* objects_stride = */     1,
                                /* cs             = */ FALSE,
                                /* ds             = */ FALSE,
                                /* rpt_failures   = */ FALSE,
                                /* err_cnt        = */     0,
                                /* ambig_cnt      = */     0
                              };

    TESTING("mt_test_fcn_1 serial test");
    fflush(stdout);

    if ( H5open() < 0 ) {

        err_cnt++;

        if ( params.rpt_failures ) {

            fprintf(stderr, "mt_test_fcn_1_serial_test():%d: H5open() failed.\n", params.thread_id);
        }
    }

    err_cnt += create_types(params.types_start, params.types_count, params.types_stride, 
                            params.cs, params.ds, params.rpt_failures, params.thread_id);

    mt_test_fcn_1((void *)(&params));
    err_cnt += params.err_cnt;

    err_cnt += destroy_types(params.types_start, params.types_count, params.types_stride,
                             params.cs, params.ds, params.rpt_failures, params.thread_id);

    if ( MT_TEST_FCN_1_SERIAL_TEST__DISPLAY_FINAL_STATS ) {

        H5I_dump_stats(stdout);
    }

    /* Sanity checks on the id info and type info free lists. */

    /* verify that the id info and type info free lists balance. 
     * This assertion can ignore the initial free list length, since whatever it is
     * on entry, the number of ids created is such that it will be empty by the
     * time any entries are returned to the free list.  The +1 accounts for the 
     * minimum length of the free list
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.id_info_fl_len)) + 1) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.type_info_fl_len)) + 1) );

    /* Verify that the increments to the id info and type info reallocatable counts balance.
     * Note that it is possible that this test will fail if increments to the free list entries
     * reallocable fields collide just so.  This seems very improbable, but if it happens with 
     * any regularity, the test will have to be modified to account for this.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap)) +
                  atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap)) +
                  atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    if ( H5close() < 0 ) {

        err_cnt++;

        if ( params.rpt_failures ) {

            fprintf(stderr, "mt_test_fcn_1_serial_test():%d: H5close() failed.\n", params.thread_id);
        }
    }

    if ( 0 == err_cnt ) {

         PASSED();

    } else {

         H5_FAILED();
    }

    return;

} /* mt_test_fcn_1_serial_test() */


/*******************************************************************************************
 *
 * mt_test_1()
 *
 *      Initial multi-thread test of H5I.  The objective is to perform an initial smoke 
 *      check with no failed calls.
 *
 *      1) set up the instances of mt_test_params_t for the specified number of threads.
 *
 *      2) Create the types needed in the call to mt_test_fcn_1().
 *
 *      3) Create the specified number of threads and invoke mt_test_fcn_1 with the 
 *         appropriate instance of mt_test_params_t.
 *
 *      4) Wait for all threads to complete.
 *
 *      5) destroy the types created in 2).
 *
 *      
 *******************************************************************************************/

void mt_test_1(int num_threads) 
{
    char             banner[80];
    hbool_t          cs = FALSE;
    hbool_t          ds = FALSE;
    hbool_t          rpt_failures = TRUE;
    int              i;
    int              err_cnt = 0;
    pthread_t        threads[MAX_NUM_THREADS];
    mt_test_params_t params[MAX_NUM_THREADS];

    assert( 1 <= num_threads );
    assert( num_threads <= MAX_NUM_THREADS );

    sprintf(banner, "multi-thread test 1 -- %d threads", num_threads);

    TESTING(banner);
    fflush(stdout);

    if ( H5open() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "mt_test_1():%d: H5open() failed.\n", 0);
        }
    }

    for ( i = 0; i < num_threads; i++ ) {

        params[i].thread_id      = i;

        params[i].types_start    = 0;
        params[i].types_count    = 3;
        params[i].types_stride   = 3;

        params[i].ids_start      = i * 20000;
        params[i].ids_count      = 20000;
        params[i].ids_stride     = 1;

        params[i].objects_start  = i * 20000;
        params[i].objects_count  = 20000;
        params[i].objects_stride = 1;

        params[i].cs             = cs;
        params[i].ds             = ds;
        params[i].rpt_failures   = rpt_failures;

        params[i].err_cnt        = 0;
        params[i].ambig_cnt      = 0;
#if 0
        fprintf(stderr, 
                "params[%d] types st/cnt/str = %d/%d/%d, ids st/cnt/str = %d/%d/%d, objs st/cnt/str = %d/%d/%d\n",
                i, params[i].types_start, params[i].types_count, params[i].types_stride,
                params[i].ids_start, params[i].ids_count, params[i].ids_stride,
                params[i].objects_start, params[i].objects_count, params[i].objects_stride);
#endif
    }

    err_cnt += create_types(params[0].types_start, params[0].types_count, params[0].types_stride, 
                            cs, ds, rpt_failures, 0);

    for ( i = 0;  i < num_threads; i++ ) {

        if ( 0 != pthread_create(&(threads[i]), NULL, &mt_test_fcn_1, (void *)(&(params[i])))) {

            assert(FALSE);

            err_cnt++;

            if ( rpt_failures ) {

                fprintf(stderr, "mt_test_1(): create of thread %d failed.\n", i);
            }
        }
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < num_threads; i++) {

        if ( 0 != pthread_join(threads[i], NULL) ) {

            assert(FALSE);

            err_cnt++;

            if ( rpt_failures ) {

                fprintf(stderr, "mt_test_1(): join of thread %d failed.\n", i);
            }
        } else {

            /* collect error count from joined thread */
            err_cnt += params[i].err_cnt;
        }
    }

    err_cnt += destroy_types(params[0].types_start, params[0].types_count, params[0].types_stride, 
                             params[0].cs, params[0].ds, params[0].rpt_failures, params[0].thread_id);

    if ( MT_TEST_1__DISPLAY_FINAL_STATS ) {

        H5I_dump_stats(stdout);
    }

    /* Sanity checks on the id info and type info free lists. */

    /* verify that the id info and type info free lists balance. 
     * This assertion can ignore the initial free list length, since whatever it is
     * on entry, the number of ids created is such that it will be empty by the
     * time any entries are returned to the free list.  The +1 accounts for the 
     * minimum length of the free list
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.id_info_fl_len)) + 1) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.type_info_fl_len)) + 1) );

    /* Verify that the increments to the id info and type info reallocatable counts balance.
     * Note that it is possible that this test will fail if increments to the free list entries
     * reallocable fields collide just so.  This seems very improbable, but if it happens with 
     * any regularity, the test will have to be modified to account for this.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap)) +
                  atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap)) +
                  atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    if ( H5close() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "mt_test_1():%d: H5close() failed.\n", 0);
        }
    }

    if ( 0 == err_cnt ) {

         PASSED();

    } else {

         H5_FAILED();
    }

    return;

} /* mt_test_1() */


/*******************************************************************************************
 *
 * mt_test_2()
 *
 *      Initial multi-thread test of H5I.  The objective is to perform an initial smoke 
 *      check with no failed calls.
 *
 *      1) set up the instances of mt_test_params_t for the specified number of threads.
 *
 *      2) Create the types needed in the call to mt_test_fcn_1().
 *
 *      3) Create the specified number of threads and invoke mt_test_fcn_1 with the 
 *         appropriate instance of mt_test_params_t.
 *
 *      4) Wait for all threads to complete.
 *
 *      5) destroy the types created in 2).
 *
 *      
 *******************************************************************************************/

void mt_test_2(int num_threads) 
{
    char             banner[80];
    hbool_t          cs = FALSE;
    hbool_t          ds = FALSE;
    hbool_t          rpt_failures = FALSE;
    int              i;
    int              err_cnt = 0;
    int              ambig_cnt = 0;
    int              types_per_thread;
    long long int    type_successful_registers = 0;
    long long int    type_failed_registers = 0;
    long long int    type_successful_clears = 0;
    long long int    type_failed_clears = 0;
    long long int    type_successful_destroys = 0;
    long long int    type_failed_destroys = 0;
    long long int    id_successful_registrations = 0;
    long long int    id_failed_registrations = 0;
    long long int    id_successful_verifies = 0;
    long long int    id_failed_verifies = 0;
    long long int    id_successful_inc_refs = 0;
    long long int    id_failed_inc_refs = 0;
    long long int    id_successful_dec_refs = 0;
    long long int    id_failed_dec_refs = 0;
    long long int    id_dec_ref_deletes = 0;
    long long int    id_successful_get_ref_cnts = 0;
    long long int    id_failed_get_ref_cnts = 0;
    long long int    id_successful_remove_verifies = 0;
    long long int    id_failed_remove_verifies = 0;
    long long int    obj_accesses = 0;
    pthread_t        threads[MAX_NUM_THREADS];
    mt_test_params_t params[MAX_NUM_THREADS];

    assert( 1 <= num_threads );
    assert( num_threads <= MAX_NUM_THREADS );

    sprintf(banner, "multi-thread test 2 -- %d threads", num_threads);

    TESTING(banner);
    fflush(stdout);

    if ( H5open() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "mt_test_1():%d: H5open() failed.\n", 0);
        }
    }

    types_per_thread = ( (int)H5I_MAX_NUM_TYPES - (int)H5I_NTYPES) / num_threads;

    if ( types_per_thread > 4 ) {

        types_per_thread = 4; 
    }

    for ( i = 0; i < num_threads; i++ ) {

        params[i].thread_id      = i;

        params[i].types_start    = i;
        params[i].types_count    = types_per_thread;
        params[i].types_stride   = num_threads;

        params[i].ids_start      = 0;
        params[i].ids_count      = NUM_ID_INSTANCES;
        params[i].ids_stride     = 1;

        params[i].objects_start  = 0; /* these fields */
        params[i].objects_count  = 0; /* not used in  */
        params[i].objects_stride = 0; /* this test    */

        params[i].cs             = cs;
        params[i].ds             = ds;
        params[i].rpt_failures   = rpt_failures;

        params[i].err_cnt        = 0;
        params[i].ambig_cnt      = 0;
#if 0
        fprintf(stderr, 
                "params[%d] types st/cnt/str = %d/%d/%d, ids st/cnt/str = %d/%d/%d, objs st/cnt/str = %d/%d/%d\n",
                i, params[i].types_start, params[i].types_count, params[i].types_stride,
                params[i].ids_start, params[i].ids_count, params[i].ids_stride,
                params[i].objects_start, params[i].objects_count, params[i].objects_stride);
#endif
    }

    for ( i = 0; i < NUM_ID_INSTANCES; i++ ) {

        id_instance_array[i].type_index = i % (types_per_thread * num_threads);
    }

    for ( i = 0;  i < num_threads; i++ ) {

        if ( 0 != pthread_create(&(threads[i]), NULL, &mt_test_fcn_2, (void *)(&(params[i])))) {

            assert(FALSE);

            err_cnt++;

            if ( rpt_failures ) {

                fprintf(stderr, "mt_test_1(): create of thread %d failed.\n", i);
            }
        }
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < num_threads; i++) {

        if ( 0 != pthread_join(threads[i], NULL) ) {

            assert(FALSE);

            err_cnt++;

            if ( rpt_failures ) {

                fprintf(stderr, "mt_test_1(): join of thread %d failed.\n", i);
            }
        } else {

            /* collect error count from joined thread */
            err_cnt += params[i].err_cnt;
            ambig_cnt += params[i].ambig_cnt;
        }
    }

    for ( i = 0; i < NUM_ID_TYPES; i++ ) {

        type_successful_registers += atomic_load(&(types_array[i].successful_registers));
        type_failed_registers     += atomic_load(&(types_array[i].failed_registers));
        type_successful_clears    += atomic_load(&(types_array[i].successful_clears));
        type_failed_clears        += atomic_load(&(types_array[i].failed_clears));
        type_successful_destroys  += atomic_load(&(types_array[i].successful_destroys));
        type_failed_destroys      += atomic_load(&(types_array[i].failed_destroys));
    }

    for ( i = 0; i < NUM_ID_INSTANCES; i++ ) {

        id_successful_registrations   += atomic_load(&(id_instance_array[i].successful_registrations));
        id_failed_registrations       += atomic_load(&(id_instance_array[i].failed_registrations));
        id_successful_verifies        += atomic_load(&(id_instance_array[i].successful_verifies));
        id_failed_verifies            += atomic_load(&(id_instance_array[i].failed_verifies));
        id_successful_inc_refs        += atomic_load(&(id_instance_array[i].successful_inc_refs));
        id_failed_inc_refs            += atomic_load(&(id_instance_array[i].failed_inc_refs));
        id_successful_dec_refs        += atomic_load(&(id_instance_array[i].successful_dec_refs));
        id_failed_dec_refs            += atomic_load(&(id_instance_array[i].failed_dec_refs));
        id_dec_ref_deletes            += atomic_load(&(id_instance_array[i].dec_ref_deletes));
        id_successful_get_ref_cnts    += atomic_load(&(id_instance_array[i].successful_get_ref_cnts));
        id_failed_get_ref_cnts        += atomic_load(&(id_instance_array[i].failed_get_ref_cnts));
        id_successful_remove_verifies += atomic_load(&(id_instance_array[i].successful_remove_verifies));
        id_failed_remove_verifies     += atomic_load(&(id_instance_array[i].failed_remove_verifies));
    }

    for ( i = 0; i < NUM_ID_OBJECTS; i++ ) {

        obj_accesses += atomic_load(&(objects_array[i].accesses));
    }

    if ( MT_TEST_1__DISPLAY_FINAL_STATS ) {

        fprintf(stderr, "\nerror count = %d, ambiguous count = %d\n\n", err_cnt, ambig_cnt);

        fprintf(stderr, "type successful / failed registers     = %lld / %lld\n", 
                type_successful_registers, type_failed_registers);
        fprintf(stderr, "type successful / failed clears        = %lld / %lld\n",
                type_successful_clears, type_failed_clears);
        fprintf(stderr, "type successful / failed destroys      = %lld / %lld\n\n",
                type_successful_destroys, type_failed_destroys);

        fprintf(stderr, "id successful / failed registrations   = %lld / %lld\n",
                id_successful_registrations, id_failed_registrations);
        fprintf(stderr, "id successful / failed verifies        = %lld / %lld\n", 
                id_successful_verifies, id_failed_verifies);
        fprintf(stderr, "id successful / failed inc refs        = %lld / %lld\n",
                id_successful_inc_refs, id_failed_inc_refs);
        fprintf(stderr, "id successful / failed dec refs        = %lld / %lld\n", 
                id_successful_dec_refs, id_failed_dec_refs);
        fprintf(stderr, "id dec ref deletes                     = %lld\n", 
                id_dec_ref_deletes);
        fprintf(stderr, "id successful / failed get refs        = %lld / %lld\n",
                id_successful_get_ref_cnts, id_failed_get_ref_cnts);
        fprintf(stderr, "id successful / failed remove verifies = %lld / %lld\n\n",
                id_successful_remove_verifies, id_failed_remove_verifies);

        fprintf(stderr, "object accesses                        = %lld\n\n", obj_accesses);

        H5I_dump_stats(stdout);
    }

    /* Sanity checks on the id info and type info free lists. */

    /* verify that the id info and type info free lists balance. 
     * This assertion can ignore the initial free list length, since whatever it is
     * on entry, the number of ids created is such that it will be empty by the
     * time any entries are returned to the free list.  The +1 accounts for the 
     * minimum length of the free list
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.id_info_fl_len)) + 1) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_freed)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_fl)) -
                  atomic_load(&(H5I_mt_g.type_info_fl_len)) + 1) );

    /* Verify that the increments to the id info and type info reallocatable counts balance.
     * Note that it is possible that this test will fail if increments to the free list entries
     * reallocable fields collide just so.  This seems very improbable, but if it happens with 
     * any regularity, the test will have to be modified to account for this.
     */
    assert( 0 == (atomic_load(&(H5I_mt_g.num_id_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_id_info_structs_alloced_from_heap)) +
                  atomic_load(&(H5I_mt_g.num_id_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    assert( 0 == (atomic_load(&(H5I_mt_g.num_type_info_fl_num_reallocable_total)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_added_to_fl)) -
                  atomic_load(&(H5I_mt_g.num_type_info_structs_alloced_from_heap)) +
                  atomic_load(&(H5I_mt_g.num_type_info_fl_alloc_req_denied_due_to_no_reallocable_entries))) );

    if ( H5close() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "mt_test_1():%d: H5close() failed.\n", 0);
        }
    }

    if ( 0 == err_cnt ) {

        PASSED();
        fprintf(stdout, "        %3d ambiguous test results.\n", ambig_cnt);

    } else {

         H5_FAILED();
    }

    return;

} /* mt_test_2() */


/*******************************************************************************************
 *
 * main()
 *
 *      Main function for tests of the multi-thread version of H5I.
 *
 *******************************************************************************************/

int main(void) 
{
    int num_threads;

    init_globals();

    serial_test_1();

    reset_globals();

    serial_test_2(0, 32, 0, NUM_ID_INSTANCES);

    reset_globals();

    serial_test_3();

    reset_globals();

    serial_test_4();

    reset_globals();

    mt_test_fcn_1_serial_test();

    reset_globals();

    for ( num_threads = 2; num_threads <= 32; num_threads++) {

        mt_test_1(num_threads);

        reset_globals();
    }

    for ( num_threads = 1; num_threads <= 32; num_threads++) {

        mt_test_2(num_threads);

        reset_globals();
    }

    // H5I_dump_stats(stdout);
    // H5I_clear_stats();

    return(0);

} /* main() */
#else /* H5_HAVE_MULTITHREAD */
int
main(void)
{
    TESTING("multithread");
    SKIPPED();
    fprintf(stderr, "Multithread isn't enabled in configure.\n");
    return (0);
}
#endif /* H5_HAVE_MULTITHREAD */
