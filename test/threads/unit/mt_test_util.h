#ifndef MT_TEST_UTIL_H
#define MT_TEST_UTIL_H

typedef void *(*mt_test_cb)(void *arg);

void mt_test_run_helper_in_parallel(mt_test_cb mt_test_func, void *args);

#endif /* MT_TEST_UTIL_H */