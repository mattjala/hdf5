#include <pthread.h>
#include <stdlib.h>
#include "testhdf5.h"
#include "mt_test_util.h"

/* Run the provided test function independently in different threads */
void mt_test_run_helper_in_parallel(mt_test_cb mt_test_func, void *args) {
  pthread_t *threads = NULL;
  void *thread_return = NULL;
  int max_num_threads = GetTestMaxNumThreads();
  int ret = 0;

  if (max_num_threads <= 0) {
    printf("No threadcount specified with -maxthreads; skipping test\n");
    return;
  }

  threads = (pthread_t *)calloc((long unsigned int) max_num_threads, sizeof(pthread_t));
  assert(threads != NULL);

  for (int num_threads = 1; num_threads <= max_num_threads; num_threads++) {
    memset(threads, 0, sizeof(pthread_t) * (long unsigned int) num_threads);

    for (int i = 0; i < num_threads; i++) {
      ret = pthread_create(&threads[i], NULL, mt_test_func, args);
      VERIFY(ret, 0, "pthread_create");
    }

    for (int i = 0; i < num_threads; i++) {
      ret = pthread_join(threads[i], &thread_return);
      VERIFY(ret, 0, "pthread_join");
    }
  }

  free(threads);
  return;
}