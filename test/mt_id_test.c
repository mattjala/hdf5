#include "h5test.h"
#include "H5Iprivate.h"

#ifdef H5_HAVE_MULTITHREAD
#include <stdatomic.h>

#define ID_TYPE_T__TAG  0x1010

typedef struct id_type_kernel_t {
    hbool_t    in_progress;
    hbool_t    created;
    hbool_t    discarded;
    int        type_id;
} id_type_kernel_t;

typedef struct id_type_t {
    unsigned                 tag;
    _Atomic id_type_kernel_t k;
    hbool_t                  mt_safe;
    _Atomic long long int    successful_clears;
    _Atomic long long int    failed_clears;
    _Atomic long long int    successful_destroys;
    _Atomic long long int    failed_destroys;
    H5I_free_t               free_func;
} id_type_t;


#define ID_OBJECT_T__TAG 0x2020

typedef struct id_object_kernel_t {
    hbool_t    in_progress;
    hbool_t    allocated;
    hbool_t    discarded;
    hbool_t    future;
    hid_t      id;
} id_object_kernel_t;

typedef struct id_object_t{
    unsigned                   tag;
    _Atomic id_object_kernel_t k;
    _Atomic long long int      accesses;
} id_object_t; 


#define ID_INSTANCE_T__TAG 0x3030

typedef struct id_instance_kernel_t {
    hbool_t  in_progress;
    hbool_t  created;
    hbool_t  discarded;
    hbool_t  future;
    hbool_t  realized;
    hid_t id;
} id_instance_kernel_t;

typedef struct id_instance_t {
    unsigned                     tag;
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
 *
 ***********************************************************************************/

typedef struct mt_test_params_t {

    int types_start;
    int types_count;
    int types_stride;

    int ids_start;
    int ids_count;
    int ids_stride;

    int objects_start;
    int objects_count;
    int objects_stride;

} mt_test_params_t;

id_type_t     types_array[NUM_ID_TYPES];
id_object_t   objects_array[NUM_ID_OBJECTS];
id_instance_t id_instance_array[NUM_ID_INSTANCES];

void    init_globals(void);
void    reset_globals(void);
herr_t  free_func(void * obj, void ** request);
hbool_t register_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds);
hbool_t clear_type(id_type_t * id_type_ptr, hbool_t force, hbool_t cs, hbool_t ds);
hbool_t destroy_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds);
hbool_t register_id(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                    hbool_t cs, hbool_t ds);
hbool_t object_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                      hbool_t cs, hbool_t ds);
hbool_t get_type(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds);
hbool_t remove_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                      hbool_t cs, hbool_t ds);
hbool_t dec_ref(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                hbool_t cs, hbool_t ds);
hbool_t inc_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds);
int     get_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds);
int     nmembers(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds);
htri_t  type_exists(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds);
hbool_t inc_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds);
hbool_t dec_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds);


void create_types(int types_start, int types_count, int types_stride);
void dec_type_refs(int types_start, int types_count, int types_stride);
void inc_type_refs(int types_start, int types_count, int types_stride);
void destroy_types(int types_start, int types_count, int types_stride);

void register_ids(int types_start, int types_count, int types_stride, int ids_start, int ids_count, int ids_stride);
void dec_refs(int types_start, int types_count, int types_stride, int ids_start, int ids_count, int ids_stride);
void inc_refs(int ids_start, int ids_count, int ids_stride);
void verify_objects(int types_start, int types_count, int types_stride, int ids_start, int ids_count, 
                    int ids_stride);


void serial_test_1(void);
void serial_test_2(int types_start, int types_count, int ids_start, int ids_count);
void serial_test_3(void);

void * mt_test_fcn_1(void * params);

void mt_test_fcn_1_serial_test(void);
void mt_test_1(int num_threads);

void init_globals(void)
{
    int i;
    struct id_type_kernel_t     type_k  = {FALSE, FALSE, FALSE, 0};
    struct id_object_kernel_t   obj_k   = {FALSE, FALSE, FALSE, FALSE, 0};
    struct id_instance_kernel_t inst_k  = {FALSE, FALSE, FALSE, FALSE, FALSE, 0};

    for ( i = 0; i < NUM_ID_TYPES; i++ )
    {
        types_array[i].tag = ID_TYPE_T__TAG;
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
        atomic_init(&(objects_array[i].k), obj_k);
        atomic_init(&(objects_array[i].accesses), 0ULL);
    }

    for ( i = 0; i < NUM_ID_INSTANCES; i++ )
    {
        id_instance_array[i].tag = ID_INSTANCE_T__TAG;
        atomic_init(&(id_instance_array[i].k), inst_k);
        atomic_init(&(id_instance_array[i].accesses), 0ULL);
    }

    return;

} /* init_globals() */

void reset_globals(void)
{
    int i;
    struct id_type_kernel_t     type_k  = {FALSE, FALSE, FALSE, 0};
    struct id_object_kernel_t   obj_k   = {FALSE, FALSE, FALSE, FALSE, 0};
    struct id_instance_kernel_t inst_k  = {FALSE, FALSE, FALSE, FALSE, FALSE, 0};

    for ( i = 0; i < NUM_ID_TYPES; i++ )
    {
        types_array[i].tag = ID_TYPE_T__TAG;
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
        atomic_store(&(objects_array[i].k), obj_k);
        atomic_store(&(objects_array[i].accesses), 0ULL);
    }

    for ( i = 0; i < NUM_ID_INSTANCES; i++ )
    {
        id_instance_array[i].tag = ID_INSTANCE_T__TAG;
        atomic_store(&(id_instance_array[i].k), inst_k);
        atomic_store(&(id_instance_array[i].accesses), 0ULL);
    }

    return;

} /* reset_globals() */

herr_t free_func(void * obj, void ** request)
{
    id_object_t * object_ptr = (id_object_t *)obj;
    id_object_kernel_t obj_k;
    id_object_kernel_t mod_obj_k;

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


hbool_t register_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds)
{
    hbool_t          success = TRUE; /* will set to FALSE on failure */
    hbool_t          result;
    int              type_id;
    id_type_kernel_t id_k;
    id_type_kernel_t mod_id_k;

    assert(id_type_ptr);

    assert( ID_TYPE_T__TAG == id_type_ptr->tag );

    if ( cs ) {

        H5I_clear_stats();
    }

    id_k = atomic_load(&(id_type_ptr->k));

    if ( ( id_k.in_progress ) || ( id_k.created ) ) {

        success = FALSE; /* another thread beat us to it */

    } else {

        mod_id_k.in_progress = TRUE;
        mod_id_k.created     = id_k.created;
        mod_id_k.discarded   = id_k.discarded;
        mod_id_k.type_id     = id_k.type_id;
    }

    if ( success ) {

        if ( ! atomic_compare_exchange_strong(&(id_type_ptr->k), &id_k, mod_id_k) ) {

            success = FALSE; /* another thread beat us to it */

        } else {

            /* get fresh copy of the type kernel */
            id_k = atomic_load(&(id_type_ptr->k));
        }
    }

    if ( success ) {

        type_id = (int)H5Iregister_type((size_t)0, 0, id_type_ptr->free_func);

        if ( H5I_BADID == type_id ) {

            mod_id_k.in_progress = FALSE;
            mod_id_k.created     = id_k.created;
            mod_id_k.discarded   = id_k.discarded;
            mod_id_k.type_id     = id_k.type_id;

            success = FALSE;

        } else {

            mod_id_k.in_progress = FALSE;
            mod_id_k.created     = TRUE;
            mod_id_k.discarded   = id_k.discarded;
            mod_id_k.type_id     = type_id;
        }

        result = atomic_compare_exchange_strong(&(id_type_ptr->k), &id_k, mod_id_k);
        assert(result);
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iregister_type");
    }

    return(success);

} /* register_type() */


hbool_t clear_type(id_type_t * id_type_ptr, hbool_t force, hbool_t cs, hbool_t ds)
{
    hbool_t             success = TRUE; /* will set to FALSE on failure */
    id_type_kernel_t id_k;

    assert(id_type_ptr);

    assert( ID_TYPE_T__TAG == id_type_ptr->tag );

    if ( cs ) {

        H5I_clear_stats();
    }

    id_k = atomic_load(&(id_type_ptr->k));

    if ( ( id_k.in_progress ) || ( ! id_k.created ) || ( id_k.discarded ) ) { 

        success = FALSE; 
    }

    if ( success ) {

        if ( H5Iclear_type((H5I_type_t)(id_k.type_id), force) != SUCCEED ) {

            success = FALSE;
            atomic_fetch_add(&(id_type_ptr->failed_clears), 1ULL);

        } else {

            atomic_fetch_add(&(id_type_ptr->successful_clears), 1ULL);
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iclear_type");
    }

    return(success);

} /* clear_type() */


hbool_t destroy_type(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds)
{
    hbool_t          success = TRUE; /* will set to FALSE on failure */
    hbool_t          destroy_succeeded;
    id_type_kernel_t id_k;
    id_type_kernel_t mod_id_k;

    assert(id_type_ptr);

    assert( ID_TYPE_T__TAG == id_type_ptr->tag );

    if ( cs ) {

        H5I_clear_stats();
    }

    id_k = atomic_load(&(id_type_ptr->k));

    assert( ID_TYPE_T__TAG == id_type_ptr->tag );

    if ( ( id_k.in_progress ) || ( ! id_k.created ) || ( id_k.discarded ) ) { 

        success = FALSE; 
    }

    if ( success ) {

        if ( H5Idestroy_type((H5I_type_t)(id_k.type_id)) != SUCCEED ) {

            success = FALSE;
            destroy_succeeded = FALSE;
            atomic_fetch_add(&(id_type_ptr->failed_destroys), 1ULL);

        } else {

            destroy_succeeded = TRUE;
            atomic_fetch_add(&(id_type_ptr->successful_destroys), 1ULL);
        }

        if ( destroy_succeeded ) { /* set the discarded flag */

            id_k = atomic_load(&(id_type_ptr->k));

            while ( ! id_k.discarded ) {

                mod_id_k.in_progress = id_k.in_progress;
                mod_id_k.created     = id_k.created;
                mod_id_k.discarded   = TRUE;
                mod_id_k.type_id     = 0;

                atomic_compare_exchange_strong(&(id_type_ptr->k), &id_k, mod_id_k);

                id_k = atomic_load(&(id_type_ptr->k));
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idestroy_type");
    }

    return(success);

} /* destroy_type() */

hbool_t register_id(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                    hbool_t cs, hbool_t ds)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    hbool_t              result;
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_instance_kernel_t mod_id_inst_k;
    id_object_kernel_t   id_obj_k;
    id_object_kernel_t   mod_id_obj_k;

    assert(id_type_ptr);
    assert(id_inst_ptr);
    assert(id_obj_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);
    assert(ID_INSTANCE_T__TAG == id_inst_ptr->tag);
    assert(ID_OBJECT_T__TAG == id_obj_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));
    id_obj_k  = atomic_load(&(id_obj_ptr->k));

    if ( ( id_type_k.in_progress) || ( ! id_type_k.created ) || ( id_type_k.discarded ) ) {

        success = FALSE;

    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success )
    {
        if ( ( id_inst_k.in_progress ) || ( id_inst_k.created ) || ( id_inst_k.discarded ) ) {

            success = FALSE;
        }
    }

    if ( success )
    {
        if ( ( id_obj_k.in_progress ) || ( id_obj_k.allocated ) || ( id_obj_k.discarded ) ) {

            success = FALSE;
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

            success = FALSE;

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

            success = FALSE;

            /* in progress flag is set in id_inst_ptr->k.  Must reset it */
            mod_id_inst_k.in_progress = FALSE;
            mod_id_inst_k.created     = id_inst_k.created;
            mod_id_inst_k.discarded   = id_inst_k.discarded;
            mod_id_inst_k.future      = id_inst_k.future;
            mod_id_inst_k.realized    = id_inst_k.realized;
            mod_id_inst_k.id          = id_inst_k.id;

            result = atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k);
            assert(result);

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
        } 

        result = atomic_compare_exchange_strong(&(id_obj_ptr->k), &id_obj_k, mod_id_obj_k);
        assert(result);

        result = atomic_compare_exchange_strong(&(id_inst_ptr->k), &id_inst_k, mod_id_inst_k);
        assert(result);
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iregister");
    }

    return(success);

} /* register_id() */ 

hbool_t object_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                      hbool_t cs, hbool_t ds)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_object_kernel_t   id_obj_k;

    assert(id_type_ptr);
    assert(id_inst_ptr);
    assert(id_obj_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);
    assert(ID_INSTANCE_T__TAG == id_inst_ptr->tag);
    assert(ID_OBJECT_T__TAG == id_obj_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));
    id_obj_k  = atomic_load(&(id_obj_ptr->k));

    if ( ! id_type_k.created ) {

        success = FALSE;

    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success )
    {
        if ( ! id_inst_k.created ) {

            success = FALSE;

        } else {

            id = id_inst_k.id;
        }
    }

    if ( success )
    {
        if ( ! id_obj_k.allocated ) {

            success = FALSE;
        }
    }

    if ( success ) {

        if ( (void *)id_obj_ptr != H5Iobject_verify(id, type) ) {

            success = FALSE;
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iobject_verify");
    }

    return(success);

} /* object_verify() */

hbool_t get_type(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;

    assert(id_type_ptr);
    assert(id_inst_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);
    assert(ID_INSTANCE_T__TAG == id_inst_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));

    if ( ! id_type_k.created ) {

        success = FALSE;

    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success )
    {
        if ( ! id_inst_k.created ) {

            success = FALSE;

        } else {

            id = id_inst_k.id;
        }
    }

    if ( success ) {

        if ( type != H5Iget_type(id) ) {

            success = FALSE;
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iget_type");
    }

    return(success);

} /* get_type() */

hbool_t remove_verify(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                      hbool_t cs, hbool_t ds)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    H5I_type_t           type;
    hid_t                id;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_instance_kernel_t mod_id_inst_k;
    id_object_kernel_t   id_obj_k;
    id_object_kernel_t   mod_id_obj_k;

    assert(id_type_ptr);
    assert(id_inst_ptr);
    assert(id_obj_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);
    assert(ID_INSTANCE_T__TAG == id_inst_ptr->tag);
    assert(ID_OBJECT_T__TAG == id_obj_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));
    id_obj_k = atomic_load(&(id_obj_ptr->k));

    if ( ! id_type_k.created ) {

        success = FALSE;

    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success )
    {
        if ( ! id_inst_k.created ) {

            success = FALSE;

        } else {

            id = id_inst_k.id;
        }
    }

    if ( success )
    {
        if ( ! id_obj_k.allocated ) {

            success = FALSE;
        }
    }

    if ( success ) {

        if ( (void *)id_obj_ptr != H5Iremove_verify(id, type) ) {

            success = FALSE;

        } else {

            id_obj_k  = atomic_load(&(id_obj_ptr->k));

            assert( id_obj_k.id == id );
        }
    }

    if ( success ) { /* must mark *id_inst_ptr as discarded */

        assert( ! id_inst_k.discarded );

        assert( ! id_obj_k.discarded );

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
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iremove_verify");
    }

    return(success);

} /* remove_verify() */

hbool_t dec_ref(id_type_t * id_type_ptr, id_instance_t * id_inst_ptr, id_object_t * id_obj_ptr, 
                hbool_t cs, hbool_t ds)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    hid_t                id;
    int                  ref_count;
    id_type_kernel_t     id_type_k;
    id_instance_kernel_t id_inst_k;
    id_instance_kernel_t mod_id_inst_k;
    id_object_kernel_t   id_obj_k;

    assert(id_type_ptr);
    assert(id_inst_ptr);
    assert(id_obj_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);
    assert(ID_INSTANCE_T__TAG == id_inst_ptr->tag);
    assert(ID_OBJECT_T__TAG == id_obj_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));
    id_inst_k = atomic_load(&(id_inst_ptr->k));
    id_obj_k = atomic_load(&(id_obj_ptr->k));

    if ( ! id_type_k.created ) {

        success = FALSE;
    } 

    if ( success )
    {
        if ( ! id_inst_k.created ) {

            success = FALSE;

        } else {

            id = id_inst_k.id;
        }
    }

    if ( success )
    {
        if ( ! id_obj_k.allocated ) {

            success = FALSE;
        }
    }

    if ( success ) {

        ref_count = H5Idec_ref(id);

        if ( ref_count < 0 ) {

            success = FALSE;

        } else if ( 0 == ref_count ) {

            id_obj_k = atomic_load(&(id_obj_ptr->k));

            assert(id_obj_k.discarded);

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
            }
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idec_ref");
    }

    return(success);

} /* dec_ref()() */


hbool_t inc_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds)
{
    hbool_t success = TRUE; /* will set to FALSE on failure */
    hid_t                id;
    int                  ref_count;
    id_instance_kernel_t id_inst_k;

    assert(id_inst_ptr);

    assert(ID_INSTANCE_T__TAG == id_inst_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_inst_k = atomic_load(&(id_inst_ptr->k));

    if ( ! id_inst_k.created ) {

        success = FALSE;

    } else {

            id = id_inst_k.id;
    }

    if ( success ) {

        ref_count = H5Iinc_ref(id);

        if ( ref_count < 0 ) {

            success = FALSE;
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iinc_ref");
    }

    return(success);

} /* inc_ref()() */

int get_ref(id_instance_t * id_inst_ptr, hbool_t cs, hbool_t ds)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    hid_t                id;
    int                  ref_count;
    id_instance_kernel_t id_inst_k;

    assert(id_inst_ptr);

    assert(ID_INSTANCE_T__TAG == id_inst_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_inst_k = atomic_load(&(id_inst_ptr->k));

    if ( ! id_inst_k.created ) {

        success = FALSE;
        ref_count = -1;

    } else {

        id = id_inst_k.id;
    }

    if ( success ) {

        ref_count = H5Iget_ref(id);
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iget_ref");
    }

    return(ref_count);

} /* get_ref()() */

int nmembers(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds)
{
    hbool_t              success = TRUE; /* will set to FALSE on failure */
    hsize_t              num_members;
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    assert(id_type_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( ! id_type_k.created ) {

        success = FALSE;

    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success ) {

        if ( H5Inmembers(type, &num_members) != SUCCEED ) {

            success = FALSE;
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

htri_t type_exists(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds)
{
    htri_t               result = TRUE; /* will set to FAIL on failure */
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    assert(id_type_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);

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

hbool_t inc_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds)
{
    hbool_t              success = TRUE; /* will set to FAIL on failure */
    int                  ref_count;
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    assert(id_type_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( ! id_type_k.created ) {

        success = FALSE;

    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success ) {

        ref_count = H5Iinc_type_ref(type);

        if ( ref_count == -1 ) {

            success = FALSE;
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Iinc_type_ref");
    }

    return(success);

} /* inc_type_ref() */

hbool_t dec_type_ref(id_type_t * id_type_ptr, hbool_t cs, hbool_t ds)
{
    hbool_t              success = TRUE; /* will set to FAIL on failure */
    herr_t               ref_count;
    H5I_type_t           type;
    id_type_kernel_t     id_type_k;

    assert(id_type_ptr);

    assert(ID_TYPE_T__TAG == id_type_ptr->tag);

    if ( cs ) {

        H5I_clear_stats();
    }

    id_type_k = atomic_load(&(id_type_ptr->k));

    if ( ! id_type_k.created ) {

        success = FALSE;

    } else {

        type = (H5I_type_t)id_type_k.type_id;
    } 

    if ( success ) {

        ref_count = H5Idec_type_ref(type);

        if ( ref_count == -1 ) {

            success = FALSE;
        }
    }

    if ( ds ) {

        H5I_dump_nz_stats(stdout, "H5Idec_type_ref");
    }

    return(success);

} /* dec_type_ref() */

void create_types(int types_start, int types_count, int types_stride)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result; 
    int i;

    for ( i = types_start; i < types_start + (types_count * types_stride); i += types_stride ) {

        assert(i >= 0);
        assert(i < NUM_ID_TYPES);

        result = register_type(&(types_array[i]), cs, ds);
        assert(result);
    }

    return;

} /* create_types() */

void dec_type_refs(int types_start, int types_count, int types_stride)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result;
    int i;

    for ( i = types_start; i < types_start + (types_count * types_stride); i += types_stride ) {

        assert(i >= 0);
        assert(i < NUM_ID_TYPES);

        result = dec_type_ref(&(types_array[i]), cs, ds);
        assert(result);
    }

    return;

} /* dec_type_refs() */

void inc_type_refs(int types_start, int types_count, int types_stride)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result;
    int i;

    for ( i = types_start; i < types_start + (types_count * types_stride); i += types_stride ) {

        assert(i >= 0);
        assert(i < NUM_ID_TYPES);

        result = inc_type_ref(&(types_array[i]), cs, ds);
        assert(result);
    }

    return;

} /* inc_type_refs() */

void destroy_types(int types_start, int types_count, int types_stride)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result;
    int i;

    for ( i = types_start; i < types_start + (types_count * types_stride); i += types_stride ) {

        assert(i >= 0);
        assert(i < NUM_ID_TYPES);

        result = destroy_type(&(types_array[i]), cs, ds);
        assert(result);
    }

    return;

} /* create_types() */

void register_ids(int types_start, int types_count, int types_stride, int ids_start, int ids_count, int ids_stride)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result;
    int i;
    int j;
    int k;

    for ( j = ids_start, k = 0; j < ids_start + (ids_count * ids_stride); j++, k = (k + 1) % types_count ) {

        assert( k >= 0);
        assert( k < types_stride);
        i = types_start + (k * types_stride);
        assert( i >= types_start );
        assert( i < types_start + (types_count * types_stride) );

        result = register_id(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds);
        assert(result);
    }

    return;

} /* register_ids() */

void dec_refs(int types_start, int types_count, int types_stride, int ids_start, int ids_count, int ids_stride)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result;
    int i;
    int j;
    int k;

    for ( j = ids_start, k = 0; 
          j < ids_start + (ids_count * ids_stride); 
          j += ids_stride, k = (k + 1) % types_count ) {

        assert( k >= 0);
        assert( k < types_stride);
        i = types_start + (k * types_stride);
        assert( i >= types_start );
        assert( i < types_start + (types_count * types_stride) );

        result = dec_ref(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds);
        assert(result);
    }

    return;

} /* inc_refs_ids() */

void inc_refs(int ids_start, int ids_count, int ids_stride)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result;
    int j;

    for ( j = ids_start; j < ids_start + (ids_count * ids_stride); j += ids_stride ) {

        result = inc_ref(&(id_instance_array[j]), cs, ds);
        assert(result);
    }

    return;

} /* inc_refs_ids() */

void verify_objects(int types_start, int types_count, int types_stride, int ids_start, int ids_count, int ids_stride)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result;
    int i;
    int j;
    int k;

    for ( j = ids_start, k = 0; 
          j < ids_start + (ids_count * ids_stride); 
          j += ids_stride, k = (k + 1) % types_count ) {

        assert( k >= 0);
        assert( k < types_stride);
        i = types_start + (k * types_stride);
        assert( i >= types_start );
        assert( i < types_start + (types_count * types_stride) );

        result = object_verify(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds);
        assert(result);
    }

    return;

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
    hbool_t result;
    int i;
    int j;
    int k;
    int l;
    int m;

    fprintf(stdout, "\n running serial test #1 ... ");

    assert(H5open() >= 0 );

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

        result = register_type(&(types_array[i]), cs, ds);
        assert(result);

        result = register_id(&(types_array[i]), &(id_instance_array[i]), &(objects_array[i]), cs, ds);
        assert(result);

        result = object_verify(&(types_array[i]), &(id_instance_array[i]), &(objects_array[i]), cs, ds);
        assert(result);

        result = get_type(&(types_array[i]), &(id_instance_array[i]), cs, ds);
        assert(result);

        result = remove_verify(&(types_array[i]), &(id_instance_array[i]), &(objects_array[i]), cs, ds);
        assert(result);

        result = register_id(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds);
        assert(result);
      
        result = register_id(&(types_array[i]), &(id_instance_array[k]), &(objects_array[k]), cs, ds);
        assert(result);
      
        result = (1 == get_ref(&(id_instance_array[j]), cs, ds));
        assert(result);

        result = (1 == get_ref(&(id_instance_array[k]), cs, ds));
        assert(result);

        result = inc_ref(&(id_instance_array[j]), cs, ds);
        assert(result);

        result = inc_ref(&(id_instance_array[k]), cs, ds);
        assert(result);

        result = (2 == get_ref(&(id_instance_array[j]), cs, ds));
        assert(result);

        result = (2 == get_ref(&(id_instance_array[k]), cs, ds));
        assert(result);

        result = dec_ref(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds);
        assert(result);

        result = (1 == get_ref(&(id_instance_array[j]), cs, ds));
        assert(result);

        result = dec_ref(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds);
        assert(result);


        result = (1 == nmembers(&(types_array[i]), cs, ds));
        assert(result);


        result = register_id(&(types_array[i]), &(id_instance_array[l]), &(objects_array[l]), cs, ds);
        assert(result);

        result = register_id(&(types_array[i]), &(id_instance_array[m]), &(objects_array[m]), cs, ds);
        assert(result);

        result = (1 == get_ref(&(id_instance_array[l]), cs, ds));
        assert(result);

        result = (1 == get_ref(&(id_instance_array[m]), cs, ds));
        assert(result);

        result = inc_ref(&(id_instance_array[l]), cs, ds);
        assert(result);

        result = inc_ref(&(id_instance_array[m]), cs, ds);
        assert(result);

        result = (2 == get_ref(&(id_instance_array[l]), cs, ds));
        assert(result);

        result = (2 == get_ref(&(id_instance_array[m]), cs, ds));
        assert(result);


        result = (3 == nmembers(&(types_array[i]), cs, ds));
        assert(result);


        result = clear_type(&(types_array[i]), FALSE, cs, ds);
        assert(result);


        result = (3 == nmembers(&(types_array[i]), cs, ds));
        assert(result);


        result = (TRUE == type_exists(&(types_array[i]), cs, ds));
        assert(result);


        result = inc_type_ref(&(types_array[i]), cs, ds);
        assert(result);


        result = dec_type_ref(&(types_array[i]), cs, ds);
        assert(result);


        result = (TRUE == type_exists(&(types_array[i]), cs, ds));
        assert(result);


        if ( (i % 2) > 0 ) {

            result = dec_type_ref(&(types_array[i]), cs, ds);
            assert(result);

        } else {

            result = destroy_type(&(types_array[i]), cs, ds);
            assert(result);
        }

        result = (FALSE == type_exists(&(types_array[i]), cs, ds));
        assert(result);
    }

    fprintf(stdout, "Done.\n");

    assert(H5close() >= 0 );

    return;

} /* serial_test_1() */


void serial_test_2(int types_start, int types_count, int ids_start, int ids_count)
{
    hbool_t cs = FALSE;
    hbool_t ds = FALSE;
    hbool_t result;
    int i;
    int j;

    fprintf(stdout, "\n running serial test #2 ... ");
    fflush(stdout);

    assert(H5open() >= 0 );

    H5I_clear_stats();

    for ( i = types_start; i < types_start + types_count; i++ ) {

        result = register_type(&(types_array[i]), cs, ds);
        assert(result);
    }

    for ( j = ids_start; j < ids_start + ids_count; j++ ) {

        i = types_start + (j % types_count);
        assert( i >= types_start );
        assert( i < types_start + types_count );

        result = register_id(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds);
        assert(result);
    }

    for ( j = ids_start + ids_count - 1; j >= ids_start; j-- ) {

        i = types_start + (j % types_count);
        assert( i >= types_start );
        assert( i < types_start + types_count );

        assert(object_verify(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds));
    }

    for ( i = types_start; i < types_start + types_count; i++ ) {

        result = ( nmembers(&(types_array[i]), cs, ds) == (ids_count / types_count) + 
                                                          (((ids_count % types_count) > i) ? 1 : 0) );
        assert(result);
    }

    for ( j = ids_start; j < ids_start + ids_count; j++ ) {

        i = types_start + (j % types_count);
        assert( i >= types_start );
        assert( i < types_start + types_count );

        if ( j % 2 > 0 ) {

            result = (inc_ref(&(id_instance_array[j]), cs, ds));
            assert(result);

        } else {

            assert(result);
        }
    }

    for ( i = types_start; i < types_start + types_count; i++ ) {

        result = clear_type(&(types_array[i]), FALSE, cs, ds);
        assert(result);
    }

    for ( j = ids_start + 1; j < ids_start + ids_count; j += 2 ) {

        i = types_start + (j % types_count);
        assert( i >= types_start );
        assert( i < types_start + types_count );

        result = object_verify(&(types_array[i]), &(id_instance_array[j]), &(objects_array[j]), cs, ds);
        assert(result);
    }

    for ( i = types_start; i < types_start + types_count; i++ ) {

        if ( (i % 2) > 0 ) {

            result = dec_type_ref(&(types_array[i]), cs, ds);
            assert(result);

        } else {

            result = destroy_type(&(types_array[i]), cs, ds);
            assert(result);
        }
    }

    fprintf(stdout, "Done.\n");
    fflush(stdout);

    // H5I_dump_stats(stdout);

    result = ( H5close() >= 0 );
    assert(result);

    return;

} /* serial_test_2() */

void serial_test_3(void)
{
    hbool_t display_op_stats = FALSE;

    fprintf(stdout, "\n running serial test #3 ... ");
    fflush(stdout);

    assert(H5open() >= 0 );

    H5I_clear_stats();

    create_types(0, 3, 3);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "create_types()");
        H5I_clear_stats();
    }

    register_ids(0, 3, 3, 0, 10000, 1);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "register_ids");
        H5I_clear_stats();
    }

    inc_type_refs(0, 2, 3);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "inc_type_refs()");
        H5I_clear_stats();
    }

    dec_refs(0, 1, 1, 0, 1000, 3);
    dec_refs(3, 1, 1, 1, 1000, 3);
    dec_refs(6, 1, 1, 2, 1000, 3);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "dec_refs()");
        H5I_clear_stats();
    }    

    inc_refs(3001, 3000, 1);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "inc_refs()");
        H5I_clear_stats();
    }

    verify_objects(0, 3, 3,  3000, 7000, 1);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "verify_objects()");
        H5I_clear_stats();
    }

    dec_type_refs(0, 3, 3);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "dec_type_refs()");
        H5I_clear_stats();
    }

    destroy_types(0, 2, 3);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "destroy_types()");
        H5I_clear_stats();
    }

    //H5I_dump_stats(stdout);

    assert(H5close() >= 0 );

    fprintf(stdout, "Done.\n");
    fflush(stdout);

    return;

} /* serial_test_3() */


void * mt_test_fcn_1(void * _params)
{
    hbool_t display_op_stats = FALSE;
    int                i;
    int                j;
    mt_test_params_t * params = (mt_test_params_t *)_params;

    assert(H5open() >= 0 );

    if ( display_op_stats ) {

        H5I_clear_stats();
    }

    register_ids(params->types_start, params->types_count, params->types_stride,
                 params->ids_start, params->ids_count, params->ids_stride);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "register_ids()");
        H5I_clear_stats();
    }

    inc_refs(params->ids_start, params->ids_count, params->ids_stride);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "inc_refs()");
        H5I_clear_stats();
    }

    verify_objects(params->types_start, params->types_count, params->types_stride,
                   params->ids_start, params->ids_count, params->ids_stride);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "verify_objects()");
        H5I_clear_stats();
    }

    inc_type_refs(params->types_start, params->types_count / 2, params->types_stride);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "inc_type_refs()");
        H5I_clear_stats();
    }

    for ( i = 0; i < params->types_count; i++ ) {

        j = params->types_start + (i * params->types_stride);

        dec_refs(j, 1, 1, params->ids_start + i, params->ids_count / (params->types_count * 2), 
                 params->ids_stride * params->types_count);
    }

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "dec_refs()");
        H5I_clear_stats();
    }

    verify_objects(params->types_start, params->types_count, params->types_stride,
                   params->ids_start, params->ids_count, params->ids_stride);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "verify_objects()");
        H5I_clear_stats();
    }
    
    dec_refs(params->types_start, params->types_count, params->types_stride,
             params->ids_start, params->ids_count, params->ids_stride);

    if ( display_op_stats ) {

        H5I_dump_nz_stats(stdout, "dec_refs()");
        H5I_clear_stats();
    }

    return(NULL);

} /* mt_test_fcn_1() */

void mt_test_fcn_1_serial_test(void)
{
    mt_test_params_t params = { /* types_start    = */     0,
                                /* types_count    = */     3,
                                /* types_stride   = */     3,
                                /* ids_start      = */     0,
                                /* ids_count      = */ 10000,
                                /* ids_stride     = */     1,
                                /* objects_start  = */     0,
                                /* objects_count  = */ 10000,
                                /* objects_stride = */     1
                              };

    fprintf(stdout, "\n running mt_test_fcn_1 serial test ... ");
    fflush(stdout);

    assert(H5open() >= 0 );

    create_types(params.types_start, params.types_count, params.types_stride);

    mt_test_fcn_1((void *)(&params));

    destroy_types(params.types_start, params.types_count, params.types_stride);

    // H5I_dump_stats(stdout);

    assert(H5close() >= 0 );

    fprintf(stdout, "Done.\n");
    fflush(stdout);

    return;

} /* mt_test_fcn_1_serial_test() */

void mt_test_1(int num_threads) 
{
    hbool_t          result;
    int              i;
    pthread_t        threads[MAX_NUM_THREADS];
    mt_test_params_t params[MAX_NUM_THREADS];

    assert( 1 <= num_threads );
    assert( num_threads <= MAX_NUM_THREADS );

    fprintf(stdout, "\n running mt_test_fcn_1 ... ");
    fflush(stdout);

    result = (H5open() >= 0 );
    assert(result);

    for ( i = 0; i < num_threads; i++ ) {

        params[i].types_start    = 0;
        params[i].types_count    = 3;
        params[i].types_stride   = 3;

        params[i].ids_start      = i * 20000;
        params[i].ids_count      = 20000;
        params[i].ids_stride     = 1;

        params[i].objects_start  = i * 20000;
        params[i].objects_count  = 20000;
        params[i].objects_stride = 1;
    }

    create_types(params[0].types_start, params[0].types_count, params[0].types_stride);

    for ( i = 0;  i < num_threads; i++ ) {

        result = (0 == pthread_create(&(threads[i]), NULL, &mt_test_fcn_1, (void *)(&(params[i]))));
        assert(result);
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < num_threads; i++) {

        result = (0 == pthread_join(threads[i], NULL));
        assert(result);
    }

    destroy_types(params[0].types_start, params[0].types_count, params[0].types_stride);

    // H5I_dump_stats(stdout);

    assert(H5close() >= 0);

    fprintf(stdout, "Done.\n");
    fflush(stdout);

    return;

} /* mt_test_1() */

int main() 
{
    H5open();

    init_globals();

    serial_test_1();

    reset_globals();

    serial_test_2(0, 32, 0, NUM_ID_INSTANCES);

    reset_globals();

    serial_test_3();

    reset_globals();

    mt_test_fcn_1_serial_test();

    reset_globals();

    mt_test_1(32);
    reset_globals();

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
