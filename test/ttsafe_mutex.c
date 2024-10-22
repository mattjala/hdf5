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

/********************************************************************
 *
 * Testing the application-level mutex control operations.
 ********************************************************************/

#include "H5VLnative_private.h"
#include "ttsafe.h"

#ifdef H5_HAVE_THREADSAFE

#define DELAYED_FILENAME "delayed_file.h5"
#define DELAYED_FILENAME_2 "delayed_file_2.h5"
#define DELAYED_FILENAME_3 "delayed_file_3.h5"

#define LINK_ITERATION_FILENAME "link_iteration.h5"
#define LINK_NAME "link"
#define NUM_THREADS 2

/* Acquire library lock to prevent ops in other threads */
void *tts_get_lock(void *args);

/* Attempt to make a file while blocked by lock */
void *tts_file_create(void *args);

/* Unlock the global mutex within a library operation to yield to other threads
 */
void *tts_link_iter_unlock(void *args);
herr_t tts_link_iter_unlock_cb(hid_t group, const char *name,
                               const H5L_info2_t *info, void *op_data);

/* Release the global mutex within a library operation to yield to other threads
 */
void *tts_link_iter_release(void *args);
herr_t tts_link_iter_release_cb(hid_t group, const char *name,
                                const H5L_info2_t *info, void *op_data);

/* Test routines */
void tts_mutex_block(void);
void tts_mutex_yield(void);
void tts_mutex_release(void);

void *tts_get_lock(void H5_ATTR_UNUSED *args) {
  herr_t ret = 0;
  ret = H5TSmutex_lock();
  VERIFY(ret, 0, "H5TSmutex_lock");
  return NULL;
}

void *tts_file_create(void H5_ATTR_UNUSED *args) {
  hid_t file_id = H5I_INVALID_HID;

  file_id =
      H5Fcreate(DELAYED_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  CHECK(file_id, H5I_INVALID_HID, "H5Fcreate");
  H5Fclose(file_id);
  return NULL;
}

void *tts_link_iter_unlock(void *args) {
  hid_t file_id = H5I_INVALID_HID;
  herr_t ret = 0;

  file_id = *((hid_t *)args);

  ret = H5Literate2(file_id, H5_INDEX_NAME, H5_ITER_INC, NULL,
                    tts_link_iter_unlock_cb, NULL);
  VERIFY(ret, 0, "H5Literate2");

  return NULL;
}

void *tts_link_iter_release(void *args) {
  hid_t file_id = H5I_INVALID_HID;
  herr_t ret = 0;

  file_id = *((hid_t *)args);

  ret = H5Literate2(file_id, H5_INDEX_NAME, H5_ITER_INC, NULL,
                    tts_link_iter_release_cb, NULL);
  VERIFY(ret, 0, "H5Literate2");

  return NULL;
}

herr_t tts_link_iter_unlock_cb(hid_t H5_ATTR_UNUSED group,
                               const char H5_ATTR_UNUSED *name,
                               const H5L_info2_t H5_ATTR_UNUSED *info,
                               void H5_ATTR_UNUSED *op_data) {
  herr_t ret = 0;
  long sleep_time = 100000L;

  /* Yield to thread that will create file */
  ret = H5TSmutex_unlock();
  VERIFY(ret, 0, "H5TSmutex_unlock");

  H5E_BEGIN_TRY {
    while (!H5Fis_accessible(DELAYED_FILENAME_2, H5P_DEFAULT)) {
      /* Wait for the file to be created */
      nanosleep((const struct timespec[]){{0, sleep_time}}, NULL);
      sleep_time *= 2;
    }
  }
  H5E_END_TRY;

  /* Re-acquire lock for consistency before re-entering library */
  ret = H5TSmutex_lock();
  VERIFY(ret, 0, "H5TSmutex_lock");

  return 0;
}

herr_t tts_link_iter_release_cb(hid_t H5_ATTR_UNUSED group,
                                const char H5_ATTR_UNUSED *name,
                                const H5L_info2_t H5_ATTR_UNUSED *info,
                                void H5_ATTR_UNUSED *op_data) {
  herr_t ret = 0;
  bool lock_acquired = false;
  unsigned int lock_count = 0;
  long sleep_time = 100000L;

  ret = H5TSmutex_release(&lock_count);
  VERIFY(ret, 0, "H5TSmutex_release");

  /* Wait until file is created in another thread before re-acquiring mutex */
  H5E_BEGIN_TRY {
    while (!H5Fis_accessible(DELAYED_FILENAME_3, H5P_DEFAULT)) {
      nanosleep((const struct timespec[]){{0, sleep_time}}, NULL);
      sleep_time *= 2;
    }
  }
  H5E_END_TRY;
  /* Re-acquire mutex before re-entering library */
  while (!lock_acquired) {
    ret = H5TSmutex_acquire(lock_count, &lock_acquired);
    VERIFY(ret, 0, "H5TSmutex_acquire");
  }

  return 0;
}

void cleanup_mutex(void) {
  H5Fdelete(DELAYED_FILENAME, H5P_DEFAULT);
  H5Fdelete(DELAYED_FILENAME_2, H5P_DEFAULT);
  H5Fdelete(DELAYED_FILENAME_3, H5P_DEFAULT);

  H5Fdelete(LINK_ITERATION_FILENAME, H5P_DEFAULT);
}

void tts_mutex(void) {
  hid_t file_id = H5I_INVALID_HID;
  herr_t ret = 0;

  file_id = H5Fcreate(LINK_ITERATION_FILENAME, H5F_ACC_TRUNC, H5P_DEFAULT,
                      H5P_DEFAULT);
  CHECK(file_id, H5I_INVALID_HID, "H5Fcreate");

  ret = H5Lcreate_soft(DELAYED_FILENAME, file_id, LINK_NAME, H5P_DEFAULT,
                       H5P_DEFAULT);
  VERIFY(ret, 0, "H5Lcreate_soft");

  ret = H5Fclose(file_id);
  CHECK(ret, FAIL, "H5Fclose");

  tts_mutex_block();

  tts_mutex_yield();

  tts_mutex_release();
}

/* Test that application can lock mutex to block library operations */
void tts_mutex_block(void) {
  H5TS_thread_t threads[NUM_THREADS] = {0};
  herr_t ret = 0;
  H5VL_file_specific_args_t vol_cb_args;
  hbool_t is_accessible = FALSE;

  /* Spawn a thread that acquires the global lock */
  threads[0] = H5TS_create_thread(tts_get_lock, NULL, NULL);
  CHECK(threads[0], 0, "H5TS_create_thread");

  /* Spawn a thread that tries to create a file but is blocked by the first
   * thread */
  threads[1] = H5TS_create_thread(tts_file_create, NULL, NULL);
  CHECK(threads[1], 0, "H5TS_create_thread");

  /* Wait for the threads to end */
  ret = H5TS_wait_for_thread(threads[0]);
  VERIFY(ret, 0, "H5TS_wait_for_thread");

  /* Verify that the file was not created */
  /* Use internal routine to bypass the (held) API lock */
  vol_cb_args.op_type = H5VL_FILE_IS_ACCESSIBLE;
  vol_cb_args.args.is_accessible.filename = DELAYED_FILENAME;
  vol_cb_args.args.is_accessible.fapl_id = H5P_DEFAULT;
  vol_cb_args.args.is_accessible.accessible = &is_accessible;

  ret = H5VL__native_file_specific(NULL, &vol_cb_args, H5P_DEFAULT, NULL);
  /* Routine should fail */
  CHECK(ret, 0, "H5VL__native_file_specific");

  /* Release the global lock and finish file creation */
  ret = H5TSmutex_unlock();
  VERIFY(ret, 0, "H5TSmutex_unlock");

  ret = H5TS_wait_for_thread(threads[1]);
  VERIFY(ret, 0, "H5TS_wait_for_thread");

  /* Verify that the file was created */
  ret = H5Fis_accessible(DELAYED_FILENAME, H5P_DEFAULT);
  CHECK(ret, 0, "H5Fis_accessible");
  CHECK(ret, -1, "H5Fis_accessible");
}

/* Test that application can unlock mutex to yield to library operations in
 * other threads */
void tts_mutex_yield(void) {
  hid_t file_id = H5I_INVALID_HID, file_id2 = H5I_INVALID_HID;
  herr_t ret = 0;
  H5TS_thread_t thread;

  file_id = H5Fopen(LINK_ITERATION_FILENAME, H5F_ACC_RDONLY, H5P_DEFAULT);
  CHECK(file_id, H5I_INVALID_HID, "H5Fopen");

  /* Iterate over links in separate thread */
  thread = H5TS_create_thread(tts_link_iter_unlock, NULL, (void *)&file_id);
  CHECK(thread, 0, "H5TS_create_thread");

  /* Attempt to create file with API lock */
  file_id2 =
      H5Fcreate(DELAYED_FILENAME_2, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  CHECK(ret, H5I_INVALID_HID, "H5Fcreate");

  /* Wait for the thread to end */
  ret = H5TS_wait_for_thread(thread);
  VERIFY(ret, 0, "H5TS_wait_for_thread");

  ret = H5Fclose(file_id);
  CHECK(ret, FAIL, "H5Fclose");

  ret = H5Fclose(file_id2);
  CHECK(ret, FAIL, "H5Fclose");
}

/* Test that application can directly release mutex to yield to library
 * operations in other threads */
void tts_mutex_release(void) {
  hid_t file_id = H5I_INVALID_HID, file_id2 = H5I_INVALID_HID;
  herr_t ret = 0;
  H5TS_thread_t thread;

  file_id = H5Fopen(LINK_ITERATION_FILENAME, H5F_ACC_RDONLY, H5P_DEFAULT);
  CHECK(file_id, H5I_INVALID_HID, "H5Fopen");

  /* Iterate over links in separate thread */
  thread = H5TS_create_thread(tts_link_iter_release, NULL, (void *)&file_id);
  CHECK(thread, 0, "H5TS_create_thread");

  /* Create file while iteration callback releases mutex */
  file_id2 =
      H5Fcreate(DELAYED_FILENAME_3, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  CHECK(file_id2, H5I_INVALID_HID, "H5Fcreate");

  /* Wait for the thread to end */
  ret = H5TS_wait_for_thread(thread);
  VERIFY(ret, 0, "H5TS_wait_for_thread");

  ret = H5Fclose(file_id);
  CHECK(ret, FAIL, "H5Fclose");
}

#endif /* H5_HAVE_THREADSAFE */