#include "../testmthdf5.h"
#include "h5test.h"
#include <pthread.h>

#ifdef H5_HAVE_MULTITHREAD

/* Test attempted concurrent library initialization/termination */
herr_t mt_test_library_init(TestParams_t *args) {
    const mt_test_params *params = (const mt_test_params *)args->UserParams;
    size_t num_repetitions = (size_t)params->num_repetitions;

    for (size_t i = 0; i < num_repetitions; i++) {
        H5open();
        H5close();
    }

    return SUCCEED;
}

#endif /* H5_HAVE_MULTITHREAD */
