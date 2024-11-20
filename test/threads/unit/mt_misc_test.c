#include "../testmthdf5.h"
#include "h5test.h"
#include <pthread.h>

void *mt_test_library_init_helper(void *arg);

/* Test attempted concurrent library initialization/termination */
void mt_test_library_init(void) {
    int max_num_threads = GetTestMaxNumThreads();
    const mt_test_params *params = (const mt_test_params *)GetTestParameters();
    pthread_t *threads = NULL;
    int ret = 0;

    assert(params != NULL);
    alarm(params->subtest_timeout);

    if (max_num_threads <= 0) {
        TestErrPrintf("Invalid number of threads in MT test: %d\n", max_num_threads);
        goto done;
    }

    threads = calloc((size_t) max_num_threads, sizeof(pthread_t));
    CHECK(threads, NULL, "calloc");
    
    for (int num_threads = 1; num_threads <= max_num_threads; num_threads++) {
        for (int j = 0; j < num_threads; j++) {
            ret = pthread_create(&threads[j], NULL, mt_test_library_init_helper, (void*)params->num_repetitions);
            VERIFY(ret, 0, "pthread_create");
        }

        for (int j = 0; j < num_threads; j++) {
            ret = pthread_join(threads[j], NULL);
            VERIFY(ret, 0, "pthread_join");
        }
    }

    free(threads);

done:
    return;
}

void *mt_test_library_init_helper(void *arg) {
    size_t num_repetitions = (size_t)arg;

    for (size_t i = 0; i < num_repetitions; i++) {
        H5open();
        H5close();
    }

    return NULL;
}