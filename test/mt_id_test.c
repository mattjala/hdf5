#include "h5test.h"
#include "H5Iprivate.h"

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
 * k.type_id:   Initialized to 0, and set to the ID assigned to the type 
 *              when it is created.
 *
 * mt_safe:     Boolean flag indicating whether the free function for all IDs in 
 *              the type, and the realize and discard callback for future IDs in
 *              the type are multi-thread safe.
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
#define ID_TYPE_T_K__INITIALIZER {FALSE, FALSE, FALSE, FALSE, 0, 0}

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
    hbool_t                  mt_safe;
    _Atomic long long int    successful_clears;
    _Atomic long long int    failed_clears;
    _Atomic long long int    successful_destroys;
    _Atomic long long int    failed_destroys;
    H5I_free_t               free_func;
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
 * k.buffer_int: Padding to bring the size of id_type_t up to 16 bytes.
 *
 * k.id:        Initialized to 0, and set to the ID number assigned when the 
 *              ID is created.
 *
 * accesses:    Number of times the ID is accessed.
 *
 *********************************************************************************/

#define ID_OBJECT_T__TAG              0x2020
#define ID_OBJECT_K_T__INITIALIZER    {FALSE, FALSE, FALSE, FALSE, 0, 0};

typedef struct id_object_kernel_t {
    hbool_t    in_progress;
    hbool_t    allocated;
    hbool_t    discarded;
    hbool_t    future;
    int        buffer_int;
    hid_t      id;
} id_object_kernel_t;

typedef struct id_object_t{
    unsigned                   tag;
    int                        index;
    _Atomic id_object_kernel_t k;
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
 * k.bool_buf_1: Padding to bring the size of id_type_t up to 16 bytes.
 *
 * k.bool_buf_2: Padding to bring the size of id_type_t up to 16 bytes.
 *
 * k.bool_buf_3: Padding to bring the size of id_type_t up to 16 bytes.
 *
 * k.id:        Initialized to 0, and set to the ID number assigned when the 
 *              ID is created.
 *
 * accesses:    Number of times the ID is accessed.
 *
 *********************************************************************************/

#define ID_INSTANCE_T__TAG 0x3030
#define ID_INSTANCE_K_T__INITIALIZER {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, 0}

typedef struct id_instance_kernel_t {
    hbool_t  in_progress;
    hbool_t  created;
    hbool_t  discarded;
    hbool_t  future;
    hbool_t  realized;
    hbool_t  bool_buf_1;
    hbool_t  bool_buf_2;
    hbool_t  bool_buf_3;
    hid_t id;
} id_instance_kernel_t;

typedef struct id_instance_t {
    unsigned                     tag;
    int                          index;
    _Atomic id_instance_kernel_t k;
    _Atomic long long int        accesses;
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
 * cs:  Boolean flag indicating that the receiving function should clear the 
 *      index statistics on entry.
 *
 * ds:  Boolean flag indicating that the receiving function should dump the 
 *      index statistics on exit.
 *
 * rpt_failures: Boolean flag that error messages should be issued when errors 
 *      are detected,
 *
 * err_cnt: Integer field used to maintain a count of the number of errors 
 *      detected.
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

} mt_test_params_t;

id_type_t     *types_array;
id_object_t   *objects_array;
id_instance_t *id_instance_array;

void    init_globals(void);
void    reset_globals(void);
herr_t  free_func(void * obj, void ** request);
int     register_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     clear_type(id_type_t * id_type_ptr, hbool_t force, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     destroy_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     register_id(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                    hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     object_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                      hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     get_type(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, 
                 hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     remove_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                      hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     dec_ref(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                hbool_t cs, hbool_t ds, hbool_t rpt_failure, int tid);
int     inc_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     get_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     nmembers(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
htri_t  type_exists(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     inc_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);
int     dec_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid);


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


void serial_test_1(void);
void serial_test_2(int types_start, int types_count, int ids_start, int ids_count);
void serial_test_3(void);

void * mt_test_fcn_1(void * params);

void mt_test_fcn_1_serial_test(void);
void mt_test_1(int num_threads);

void init_globals(void)
{
    int i;
    struct id_type_kernel_t     type_k  = ID_TYPE_T_K__INITIALIZER;
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
        types_array[i].mt_safe = FALSE;
        atomic_init(&(types_array[i].successful_clears), 0ULL);
        atomic_init(&(types_array[i].failed_clears), 0ULL);
        atomic_init(&(types_array[i].successful_destroys), 0ULL);
        atomic_init(&(types_array[i].failed_destroys), 0ULL);
        types_array[i].free_func = free_func;
    }

    for ( i = 0; i < NUM_ID_OBJECTS; i++ )
    {
        objects_array[i].tag = ID_OBJECT_T__TAG;
        objects_array[i].index = i;
        atomic_init(&(objects_array[i].k), obj_k);
        atomic_init(&(objects_array[i].accesses), 0ULL);
    }

    for ( i = 0; i < NUM_ID_INSTANCES; i++ )
    {
        id_instance_array[i].tag = ID_INSTANCE_T__TAG;
        id_instance_array[i].index = i;
        atomic_init(&(id_instance_array[i].k), inst_k);
        atomic_init(&(id_instance_array[i].accesses), 0ULL);
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
        types_array[i].mt_safe = FALSE;
        atomic_store(&(types_array[i].successful_clears), 0ULL);
        atomic_store(&(types_array[i].failed_clears), 0ULL);
        atomic_store(&(types_array[i].successful_destroys), 0ULL);
        atomic_store(&(types_array[i].failed_destroys), 0ULL);
        types_array[i].free_func = free_func;
    }

    for ( i = 0; i < NUM_ID_OBJECTS; i++ )
    {
        objects_array[i].tag = ID_OBJECT_T__TAG;
        objects_array[i].index = i;
        atomic_store(&(objects_array[i].k), obj_k);
        atomic_store(&(objects_array[i].accesses), 0ULL);
    }

    for ( i = 0; i < NUM_ID_INSTANCES; i++ )
    {
        id_instance_array[i].tag = ID_INSTANCE_T__TAG;
        id_instance_array[i].index = i;
        atomic_store(&(id_instance_array[i].k), inst_k);
        atomic_store(&(id_instance_array[i].accesses), 0ULL);
    }

    return;

} /* reset_globals() */

herr_t free_func(void * obj, void H5_ATTR_UNUSED ** request)
{
    id_object_t * object_ptr = (id_object_t *)obj;
    id_object_kernel_t obj_k;
    id_object_kernel_t mod_obj_k = ID_OBJECT_K_T__INITIALIZER;

    assert(object_ptr);

    assert(ID_OBJECT_T__TAG == object_ptr->tag);

    obj_k = atomic_load(&(object_ptr->k));
    
    assert(obj_k.allocated);
    assert(!obj_k.discarded);
    assert(!obj_k.future);

    mod_obj_k.allocated = obj_k.allocated;
    mod_obj_k.discarded = TRUE;
    mod_obj_k.future    = obj_k.future;
    mod_obj_k.id        = obj_k.id;

    
    if ( atomic_compare_exchange_strong(&(object_ptr->k), &obj_k, mod_obj_k ) ) {

        return(SUCCEED);

    } else {

        return(FAIL);
    }
} /* free_func() */

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

        mod_id_inst_k.in_progress = TRUE;
        mod_id_inst_k.created     = id_inst_k.created;
        mod_id_inst_k.discarded   = id_inst_k.discarded;
        mod_id_inst_k.future      = id_inst_k.future;
        mod_id_inst_k.realized    = id_inst_k.realized;
        mod_id_inst_k.id          = id_inst_k.id;

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

        mod_id_obj_k.in_progress = TRUE;
        mod_id_obj_k.allocated   = id_obj_k.allocated;
        mod_id_obj_k.discarded   = id_obj_k.discarded;
        mod_id_obj_k.future      = id_obj_k.future;
        mod_id_obj_k.id          = id_obj_k.id;
        
        if ( ! atomic_compare_exchange_strong(&(id_obj_ptr->k), &id_obj_k, mod_id_obj_k) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                fprintf(stderr, "register_id():%d: can't mark target id obj in progress.\n", tid);
            }

            /* in progress flag is set in id_inst_ptr->k.  Must reset it */
            mod_id_inst_k.in_progress = FALSE;
            mod_id_inst_k.created     = id_inst_k.created;
            mod_id_inst_k.discarded   = id_inst_k.discarded;
            mod_id_inst_k.future      = id_inst_k.future;
            mod_id_inst_k.realized    = id_inst_k.realized;
            mod_id_inst_k.id          = id_inst_k.id;

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

        mod_id_inst_k.in_progress = FALSE;
        mod_id_inst_k.created     = id_inst_k.created;
        mod_id_inst_k.discarded   = id_inst_k.discarded;
        mod_id_inst_k.future      = id_inst_k.future;
        mod_id_inst_k.realized    = id_inst_k.realized;
        mod_id_inst_k.id          = id_inst_k.id;

        mod_id_obj_k.in_progress = FALSE;
        mod_id_obj_k.allocated   = id_obj_k.allocated;
        mod_id_obj_k.discarded   = id_obj_k.discarded;
        mod_id_obj_k.future      = id_obj_k.future;
        mod_id_obj_k.id          = id_obj_k.id;

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
 * object_verify()
 *
 *    Verify that the target ID has been created and inserted into the target index, and that 
 *    the associated instances id_instance_t and id_object_t (supplied via the id_ist_ptr and 
 *    id_obj_ptr) have been updated accordingly.
 *
 *    If the cs flag is set, the function clears statistics on entry.
 *
 *    If the ds flag is set, the function displays statistics just before exit.
 *
 *    If the rpt_failures flag is set, the function will write an error message to stderr 
 *    if an error is detected.  If such an error message is generated, the triggering thread 
 *    (given in the tid field) is reported.
 *
 *    The function returns 0 on success (i.e. if the target ID exists), and 1 if any error 
 *    (including non-existance) is detected.
 *
 ***********************************************************************************************/

int object_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                  hbool_t cs, hbool_t ds, hbool_t rpt_failures, int tid)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_object_kernel_t   id_obj_k;

    if ( ( NULL == id_type_ptr ) || ( ID_TYPE_T__TAG != id_type_ptr->tag ) ||
         ( NULL == id_inst_ptr ) || ( ID_INSTANCE_T__TAG != id_inst_ptr->tag ) ||
         ( NULL == id_obj_ptr )  || ( ID_OBJECT_T__TAG != id_obj_ptr->tag ) ) {

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
    id_obj_k  = atomic_load(&(id_obj_ptr->k));

    if ( success ) {

        if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

            assert(FALSE);

            success = FALSE;

            if ( rpt_failures ) {

                 fprintf(stderr, 
                   "object_verify():%d: target id type either in progress, not created, or discarded on entry.\n",
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
                   "object_verify():%d: target id inst either in progress, not created, or discarded on entry.\n",
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
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iobject_verify");
    }

    return(success ? 0 : 1);

} /* object_verify() */

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

            mod_id_inst_k.in_progress = id_inst_k.in_progress;
            mod_id_inst_k.created     = id_inst_k.created;
            mod_id_inst_k.discarded   = TRUE;
            mod_id_inst_k.future      = id_inst_k.future;
            mod_id_inst_k.realized    = id_inst_k.realized;
            mod_id_inst_k.id          = id_inst_k.id;

            atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k);

            id_inst_k = atomic_load(&(id_inst_ptr->k));

            inst_retry_cnt++;
        }

        /* for whatever reason, H5Iremove_verify() doesn't call the free routine 
         * for objects in the index.  Thus we should set the discarded flag on 
         * the object so we can detect other calls to the free function.
         */
        while ( ! id_obj_k.discarded ) {

            mod_id_obj_k.in_progress = id_obj_k.in_progress;
            mod_id_obj_k.allocated   = id_obj_k.allocated;
            mod_id_obj_k.discarded   = TRUE;
            mod_id_obj_k.future      = id_obj_k.future;
            mod_id_obj_k.id          = id_obj_k.id;

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

                mod_id_inst_k.in_progress = id_inst_k.in_progress;
                mod_id_inst_k.created     = id_inst_k.created;
                mod_id_inst_k.discarded   = TRUE;
                mod_id_inst_k.future      = id_inst_k.future;
                mod_id_inst_k.realized    = id_inst_k.realized;
                mod_id_inst_k.id          = id_inst_k.id;

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
    int                  ref_count = 0;
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

    TESTING("MT ID serial test #2");
    fflush(stdout);

    if ( H5open() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_2():%d: H5open() failed.\n", 0);
        }
    }

    H5I_clear_stats();

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

                fprintf(stderr, "nmembers(type_array[%d], ...) returns %d, %d expected,\n", i, num_mem, expected);
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

    // H5I_dump_stats(stdout);

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

void serial_test_3(void)
{
    hbool_t display_op_stats = FALSE;
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t rpt_failures = TRUE;
    int err_cnt = 0;
    int tid = 0;

    TESTING("MT ID serial test #3");
    fflush(stdout);

    if ( H5open() < 0 ) {

        err_cnt++;

        if ( rpt_failures ) {

            fprintf(stderr, "serial_test_3():%d: H5open() failed.\n", 0);
        }
    }

    H5I_clear_stats();

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

    //H5I_dump_stats(stdout);

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
                                /* err_cnt        = */     0
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

    // H5I_dump_stats(stdout);

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

    // H5I_dump_stats(stdout);

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

    mt_test_fcn_1_serial_test();

    reset_globals();

    for ( num_threads = 2; num_threads <= 32; num_threads++) {

        mt_test_1(num_threads);

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
    fprintf(stderr, "Multithread isn't enabled in the configure.\n");
    return (0);
}
#endif /* H5_HAVE_MULTITHREAD */
