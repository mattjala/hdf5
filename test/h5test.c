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
 * Purpose:  Provides support functions for most of the hdf5 tests cases.
 *
 */

#undef NDEBUG /*override -DNDEBUG      */

#include "h5test.h"
#include "H5srcdir.h"
#include "H5srcdir_str.h"

/* Necessary for h5_verify_cached_stabs() */
#define H5G_FRIEND /*suppress error about including H5Gpkg      */
#define H5G_TESTING
#include "H5Gpkg.h"

#ifdef H5_HAVE_WIN32_API
#include <process.h>
#endif /* H5_HAVE_WIN32_API */

/*
 * Define these environment variables or constants to influence functions in
 * this test support library.  The environment variable is used in preference
 * to the cpp constant.  If neither is defined then use some default value.
 *
 * HDF5_DRIVER:    This string describes what low level file driver to
 *      use for HDF5 file access.
 *
 * HDF5_LIBVER_BOUNDS:    This string describes what library version bounds to
 *      use for HDF5 file access.  See h5_get_libver_fapl() for details.
 *
 * HDF5_PREFIX:    A string to add to the beginning of all serial test
 *      file names.  This can be used to run tests in a
 *      different file system (e.g., "/tmp" or "/tmp/myname").
 *      The prefix will be separated from the base file name
 *      by a slash. See h5_fixname() for details.
 *
 * HDF5_PARAPREFIX:  A string to add to the beginning of all parallel test
 *      file names.  This can be used to tell MPIO what driver
 *      to use (e.g., "gfs:", "ufs:", or "nfs:") or to use a
 *      different file system (e.g., "/tmp" or "/tmp/myname").
 *      The prefix will be separated from the base file name
 *      by a slash. See h5_fixname() for details.
 *
 */
/*
 * In a parallel machine, the filesystem suitable for compiling is
 * unlikely a parallel file system that is suitable for parallel I/O.
 * There is no standard pathname for the parallel file system.  /tmp
 * is about the best guess.
 */
#ifndef HDF5_PARAPREFIX
#define HDF5_PARAPREFIX ""
#endif
char *paraprefix = NULL; /* for command line option para-prefix */
#ifdef H5_HAVE_PARALLEL
MPI_Info h5_io_info_g = MPI_INFO_NULL; /* MPI INFO object for IO */
#endif

#define READ_BUF_SIZE 65536

/*
 * These are the letters that are appended to the file name when generating
 * names for the split and multi drivers. They are:
 *
 *   m: All meta data when using the split driver.
 *  s: The userblock, superblock, and driver info block
 *  b: B-tree nodes
 *  r: Dataset raw data
 *  g: Global heap
 *  l: local heap (object names)
 *  o: object headers
 */
static const char *multi_letters = "msbrglo";

/* Temporary file for sending signal messages */
#define TMP_SIGNAL_FILE "tmp_signal_file"

/* The # of seconds to wait for the message file--used by h5_wait_message() */
#define MESSAGE_TIMEOUT 300 /* Timeout in seconds */

/* Buffer to construct path in and return pointer to */
static char srcdir_path[1024];

/* Buffer to construct file in and return pointer to */
static char srcdir_testpath[1024];

/*  The strings that correspond to library version bounds H5F_libver_t in H5Fpublic.h */
/*  This is used by h5_get_version_string() */
const char *LIBVER_NAMES[] = {"earliest", /* H5F_LIBVER_EARLIEST = 0  */
                              "v18",      /* H5F_LIBVER_V18 = 1       */
                              "v110",     /* H5F_LIBVER_V110 = 2      */
                              "v112",     /* H5F_LIBVER_V112 = 3      */
                              "latest",   /* H5F_LIBVER_V114 = 4      */
                              NULL};

/* Previous error reporting function */
static H5E_auto2_t err_func = NULL;

/* Global variables for testing */
static int TestExpress_g            = -1; /* Whether to expedite testing. -1 means not set yet. */
H5_ATOMIC(size_t) n_tests_run_g     = 0;
H5_ATOMIC(size_t) n_tests_passed_g  = 0;
H5_ATOMIC(size_t) n_tests_failed_g  = 0;
H5_ATOMIC(size_t) n_tests_skipped_g = 0;
uint64_t vol_cap_flags_g            = H5VL_CAP_FLAG_NONE;

/* Value of currently registered optional dynamic VOL operation */
int reg_opt_curr_op_val = 0;

#ifdef H5_HAVE_MULTITHREAD
pthread_key_t test_thread_info_key_g;
#endif

/* Whether h5_cleanup should clean up temporary testing files */
static bool do_test_file_cleanup_g = true;

static herr_t h5_errors(hid_t estack, void *client_data);
static char  *h5_fixname_real(const char *base_name, hid_t fapl, const char *_suffix, char *fullname,
                              size_t size, hbool_t nest_printf, hbool_t subst_for_superblock);

herr_t reg_opt_op_optional_verify(void *obj, H5VL_optional_args_t *args);

/* A fake VOL connector to verify registration of dynamic operations */
const H5VL_class_t reg_opt_vol_g = {
    H5VL_VERSION,       /* VOL class struct version */
    REG_OPT_VOL_VALUE,  /* value        */
    REG_OPT_VOL_NAME,   /* name         */
    0,                  /* version      */
    H5VL_CAP_FLAG_NONE, /* capability flags */
    NULL,               /* initialize   */
    NULL,               /* terminate    */
    {
        /* info_cls */
        (size_t)0, /* size    */
        NULL,      /* copy    */
        NULL,      /* compare */
        NULL,      /* free    */
        NULL,      /* to_str  */
        NULL,      /* from_str */
    },
    {
        /* wrap_cls */
        NULL, /* get_object   */
        NULL, /* get_wrap_ctx */
        NULL, /* wrap_object  */
        NULL, /* unwrap_object */
        NULL, /* free_wrap_ctx */
    },
    {
        /* attribute_cls */
        NULL,                /* create       */
        NULL,                /* open         */
        NULL,                /* read         */
        NULL,                /* write        */
        NULL,                /* get          */
        NULL,                /* specific     */
        reg_opt_op_optional, /* optional     */
        NULL                 /* close        */
    },
    {
        /* dataset_cls */
        NULL,                /* create       */
        NULL,                /* open         */
        NULL,                /* read         */
        NULL,                /* write        */
        NULL,                /* get          */
        NULL,                /* specific     */
        reg_opt_op_optional, /* optional     */
        NULL                 /* close        */
    },
    {
        /* datatype_cls */
        NULL,                 /* commit       */
        NULL,                 /* open         */
        reg_opt_datatype_get, /* get          */
        NULL,                 /* specific     */
        reg_opt_op_optional,  /* optional     */
        NULL                  /* close        */
    },
    {
        /* file_cls */
        NULL,                /* create       */
        NULL,                /* open         */
        NULL,                /* get          */
        NULL,                /* specific     */
        reg_opt_op_optional, /* optional     */
        NULL                 /* close        */
    },
    {
        /* group_cls */
        NULL,                /* create       */
        NULL,                /* open         */
        NULL,                /* get          */
        NULL,                /* specific     */
        reg_opt_op_optional, /* optional     */
        NULL                 /* close        */
    },
    {
        /* link_cls */
        NULL,                 /* create       */
        NULL,                 /* copy         */
        NULL,                 /* move         */
        NULL,                 /* get          */
        NULL,                 /* specific     */
        reg_opt_link_optional /* optional     */
    },
    {
        /* object_cls */
        NULL,                 /* open         */
        NULL,                 /* copy         */
        NULL,                 /* get          */
        NULL,                 /* specific     */
        reg_opt_link_optional /* optional     */
    },
    {
        /* introspect_cls */
        NULL, /* get_conn_cls */
        NULL, /* get_cap_flags */
        NULL, /* opt_query    */
    },
    {
        /* request_cls */
        NULL, /* wait         */
        NULL, /* notify       */
        NULL, /* cancel       */
        NULL, /* specific     */
        NULL, /* optional     */
        NULL  /* free         */
    },
    {
        /* blob_cls */
        NULL, /* put          */
        NULL, /* get          */
        NULL, /* specific     */
        NULL  /* optional     */
    },
    {
        /* token_cls */
        NULL, /* cmp              */
        NULL, /* to_str           */
        NULL  /* from_str         */
    },
    NULL /* optional     */
};

/*-------------------------------------------------------------------------
 * Function:  h5_errors
 *
 * Purpose:  Displays the error stack after printing "*FAILED*".
 *
 * Return:  Success:  0
 *
 *    Failure:  -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
h5_errors(hid_t estack, void H5_ATTR_UNUSED *client_data)
{
    H5_FAILED();
    H5Eprint2(estack, stdout);
    return 0;
}

/*-------------------------------------------------------------------------
 * Function:  h5_clean_files
 *
 * Purpose:  Cleanup temporary test files (always).
 *    base_name contains the list of test file names.
 *
 * Return:  void
 *
 *-------------------------------------------------------------------------
 */
void
h5_clean_files(const char *base_name[], hid_t fapl)
{
    int i;

    for (i = 0; base_name[i]; i++) {
        h5_delete_test_file(base_name[i], fapl);
    }

    /* Close the FAPL used to access the file */
    H5Pclose(fapl);
} /* end h5_clean_files() */

/*
 * Cleans up a single temporary testing file and does
 * NOT close 'fapl'
 */
void
h5_delete_test_file(const char *base_name, hid_t fapl)
{
    char filename[1024]; /* VFD-dependent filename to delete */

    /* Get the VFD-dependent filename */
    if (NULL == h5_fixname(base_name, fapl, filename, sizeof(filename)))
        return;

    H5E_BEGIN_TRY
    {
        H5Fdelete(filename, fapl);
    }
    H5E_END_TRY

} /* end h5_delete_test_file() */

/*
 * Cleans up temporary testing files and does NOT close
 * 'fapl'
 */
void
h5_delete_all_test_files(const char *base_name[], hid_t fapl)
{
    int i; /* iterator                             */

    for (i = 0; base_name[i]; i++) {
        h5_delete_test_file(base_name[i], fapl);
    } /* end for */

} /* end h5_delete_all_test_files() */

/*
 * Cleans up temporary testing files and closes 'fapl'
 */
int
h5_cleanup(const char *base_name[], hid_t fapl)
{
    int retval = 0;

    if (do_test_file_cleanup_g) {
        /* Clean up files in base_name, and the FAPL */
        h5_clean_files(base_name, fapl);

        retval = 1;
    } /* end if */

    /* Restore the original error reporting routine */
    h5_restore_err();

    return retval;
} /* end h5_cleanup() */

/*
 * Restores HDF5's default error handler function
 */
void
h5_restore_err(void)
{
    /* Restore the original error reporting routine */
    assert(err_func != NULL);
    H5Eset_auto2(H5E_DEFAULT, err_func, NULL);
    err_func = NULL;
}

/*
 * Reset the library by closing it
 */
void
h5_reset(void)
{
    fflush(stdout);
    fflush(stderr);
    H5close();

    /* Save current error stack reporting routine and redirect to our local one */
    assert(err_func == NULL);
    H5Eget_auto2(H5E_DEFAULT, &err_func, NULL);
    H5Eset_auto2(H5E_DEFAULT, h5_errors, NULL);
}

/*
 * Performs test framework initialization
 */
void
h5_test_init(void)
{
    fflush(stdout);
    fflush(stderr);
    H5close();

    /* Save current error stack reporting routine and redirect to our local one */
    assert(err_func == NULL);
    H5Eget_auto2(H5E_DEFAULT, &err_func, NULL);
    H5Eset_auto2(H5E_DEFAULT, h5_errors, NULL);

    /* Retrieve the TestExpress mode */
    TestExpress_g = h5_get_testexpress();
} /* end h5_test_init() */

/*
 * Creates a VFD-dependent filename from a base filename
 * without a suffix and a File Access Property List
 */
char *
h5_fixname(const char *base_name, hid_t fapl, char *fullname, size_t size)
{
    return (h5_fixname_real(base_name, fapl, ".h5", fullname, size, FALSE, FALSE));
}

/*
 * Creates a VFD-dependent filename for a superblock file
 * from a base filename without a suffix and a File Access
 * Property List
 */
char *
h5_fixname_superblock(const char *base_name, hid_t fapl_id, char *fullname, size_t size)
{
    return (h5_fixname_real(base_name, fapl_id, ".h5", fullname, size, FALSE, TRUE));
}

/*
 * Creates a VFD-dependent filename without a suffix from a
 * base filename without a suffix and a File Access Property
 * List
 */
char *
h5_fixname_no_suffix(const char *base_name, hid_t fapl, char *fullname, size_t size)
{
    return (h5_fixname_real(base_name, fapl, NULL, fullname, size, FALSE, FALSE));
}

/*
 * Creates a VFD-dependent printf-style filename from a base
 * filename without a suffix and a File Access Property List
 */
char *
h5_fixname_printf(const char *base_name, hid_t fapl, char *fullname, size_t size)
{
    return (h5_fixname_real(base_name, fapl, ".h5", fullname, size, TRUE, FALSE));
}

/*-------------------------------------------------------------------------
 * Function:  h5_fixname_real
 *
 * Purpose:  Create a file name from a file base name like `test' and
 *    return it through the FULLNAME (at most SIZE characters
 *    counting the null terminator). The full name is created by
 *    prepending the contents of HDF5_PREFIX (separated from the
 *    base name by a slash) and appending a file extension based on
 *    the driver supplied, resulting in something like
 *    `ufs:/u/matzke/test.h5'.
 *
 * Return:  Success:  The FULLNAME pointer.
 *
 *    Failure:  NULL if BASENAME or FULLNAME is the null
 *        pointer or if FULLNAME isn't large enough for
 *        the result.
 *
 *-------------------------------------------------------------------------
 */
static char *
h5_fixname_real(const char *base_name, hid_t fapl, const char *_suffix, char *fullname, size_t size,
                hbool_t nest_printf, hbool_t subst_for_superblock)
{
    const char *prefix         = NULL;
    const char *driver_env_var = NULL; /* HDF5_DRIVER environment variable     */
    char       *ptr, last = '\0';
    const char *suffix = _suffix;
    size_t      i, j;
    hid_t       driver     = -1;
    int         isppdriver = 0; /* if the driver is MPI parallel */

    if (!base_name || !fullname || size < 1)
        return NULL;

    memset(fullname, 0, size);

    /* Determine if driver is set by environment variable. If it is,
     * only generate a suffix if fixing the filename for the superblock
     * file. */
    driver_env_var = HDgetenv(HDF5_DRIVER);
    if (driver_env_var && (H5P_DEFAULT == fapl) && subst_for_superblock)
        fapl = H5P_FILE_ACCESS_DEFAULT;

    /* figure out the suffix */
    if (H5P_DEFAULT != fapl) {
        if ((driver = H5Pget_driver(fapl)) < 0)
            return NULL;

        if (suffix) {
            if (H5FD_FAMILY == driver) {
                if (subst_for_superblock)
                    suffix = "-000000.h5";
                else
                    suffix = nest_printf ? "-%%06d.h5" : "-%06d.h5";
            }
            else if (H5FD_MULTI == driver) {

                /* Check the HDF5_DRIVER environment variable in case
                 * we are using the split driver since both of those
                 * use the multi VFD under the hood.
                 */
#ifdef HDF5_DRIVER
                /* Use the environment variable, then the compile-time constant */
                if (!driver_env_var)
                    driver_env_var = HDF5_DRIVER;
#endif
                if (driver_env_var && !HDstrcmp(driver_env_var, "split")) {
                    /* split VFD */
                    if (subst_for_superblock)
                        suffix = ".h5.meta";
                }
                else {
                    /* multi VFD */
                    if (subst_for_superblock)
                        suffix = "-s.h5";
                    else
                        suffix = NULL;
                }
            }
        }
    }

    /* Must first check fapl is not H5P_DEFAULT (-1) because H5FD_XXX
     * could be of value -1 if it is not defined.
     */
    isppdriver = ((H5P_DEFAULT != fapl) || driver_env_var) && (H5FD_MPIO == driver);

    /* Check HDF5_NOCLEANUP environment setting.
     * (The #ifdef is needed to prevent compile failure in case MPI is not
     * configured.)
     */
    if (isppdriver) {
#ifdef H5_HAVE_PARALLEL
        if (getenv_all(MPI_COMM_WORLD, 0, HDF5_NOCLEANUP))
            do_test_file_cleanup_g = false;
#endif /* H5_HAVE_PARALLEL */
    }
    else {
        if (getenv(HDF5_NOCLEANUP))
            do_test_file_cleanup_g = false;
    }

    /* Check what prefix to use for test files. Process HDF5_PARAPREFIX and
     * HDF5_PREFIX.
     * Use different ones depending on parallel or serial driver used.
     * (The #ifdef is needed to prevent compile failure in case MPI is not
     * configured.)
     */
    if (isppdriver) {
#ifdef H5_HAVE_PARALLEL
        /*
         * For parallel:
         *      First use command line option, then the environment
         *      variable, then try the constant
         */
        static int explained = 0;

        prefix = (paraprefix ? paraprefix : getenv_all(MPI_COMM_WORLD, 0, "HDF5_PARAPREFIX"));

        if (!prefix && !explained) {
            /* print hint by process 0 once. */
            int mpi_rank;

            MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

            if (mpi_rank == 0)
                printf("*** Hint ***\n"
                       "You can use environment variable HDF5_PARAPREFIX to "
                       "run parallel test files in a\n"
                       "different directory or to add file type prefix. e.g.,\n"
                       "   HDF5_PARAPREFIX=pfs:/PFS/user/me\n"
                       "   export HDF5_PARAPREFIX\n"
                       "*** End of Hint ***\n");

            explained = TRUE;
#ifdef HDF5_PARAPREFIX
            prefix = HDF5_PARAPREFIX;
#endif /* HDF5_PARAPREFIX */
        }
#endif /* H5_HAVE_PARALLEL */
    }
    else {
        /*
         * For serial:
         *      First use the environment variable, then try the constant
         */
        prefix = HDgetenv("HDF5_PREFIX");

#ifdef HDF5_PREFIX
        if (!prefix)
            prefix = HDF5_PREFIX;
#endif /* HDF5_PREFIX */
    }

    /* Prepend the prefix value to the base name */
    if (prefix && *prefix) {
        if (isppdriver) {
            /* This is a parallel system */
            char *subdir;

            if (!HDstrcmp(prefix, HDF5_PARAPREFIX)) {
                /*
                 * If the prefix specifies the HDF5_PARAPREFIX directory, then
                 * default to using the "/tmp/$USER" or "/tmp/$LOGIN"
                 * directory instead.
                 */
                char *user, *login;

                user   = HDgetenv("USER");
                login  = HDgetenv("LOGIN");
                subdir = (user ? user : login);

                if (subdir) {
                    for (i = 0; i < size && prefix[i]; i++)
                        fullname[i] = prefix[i];

                    fullname[i++] = '/';

                    for (j = 0; i < size && subdir[j]; ++i, ++j)
                        fullname[i] = subdir[j];
                }
            }

            if (!fullname[0]) {
                /* We didn't append the prefix yet */
                HDstrncpy(fullname, prefix, size);
                fullname[size - 1] = '\0';
            }

            if (HDstrlen(fullname) + HDstrlen(base_name) + 1 < size) {
                /*
                 * Append the base_name with a slash first. Multiple
                 * slashes are handled below.
                 */
                h5_stat_t buf;

                if (HDstat(fullname, &buf) < 0)
                    /* The directory doesn't exist just yet */
                    if (HDmkdir(fullname, (mode_t)0755) < 0 && errno != EEXIST)
                        /*
                         * We couldn't make the "/tmp/${USER,LOGIN}"
                         * subdirectory.  Default to PREFIX's original
                         * prefix value.
                         */
                        HDstrcpy(fullname, prefix);

                HDstrcat(fullname, "/");
                HDstrcat(fullname, base_name);
            }
            else {
                /* Buffer is too small */
                return NULL;
            }
        }
        else {
            if (HDsnprintf(fullname, size, "%s/%s", prefix, base_name) == (int)size)
                /* Buffer is too small */
                return NULL;
        }
    }
    else if (HDstrlen(base_name) >= size) {
        /* Buffer is too small */
        return NULL;
    }
    else {
        HDstrcpy(fullname, base_name);
    }

    /* Append a suffix */
    if (suffix) {
        if (HDstrlen(fullname) + HDstrlen(suffix) >= size)
            return NULL;

        HDstrcat(fullname, suffix);
    }

    /* Remove any double slashes in the filename */
    for (ptr = fullname, i = j = 0; ptr && i < size; i++, ptr++) {
        if (*ptr != '/' || last != '/')
            fullname[j++] = *ptr;

        last = *ptr;
    }

    return fullname;
}

/*
 * "Removes" a ':'-delimited prefix from a filename
 */
H5_ATTR_PURE const char *
h5_rmprefix(const char *filename)
{
    const char *ret_ptr;

    if ((ret_ptr = HDstrstr(filename, ":")) == NULL)
        ret_ptr = filename;
    else
        ret_ptr++;

    return (ret_ptr);
}

/*
 * Creates and returns a File Access Property List that
 * may have a modified File Driver and/or library version
 * bounds setting
 */
hid_t
h5_fileaccess(void)
{
    hid_t fapl_id = H5I_INVALID_HID;

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        goto error;

    /* Check for libver bounds */
    if (h5_get_libver_fapl(fapl_id) < 0)
        goto error;

    return fapl_id;

error:
    if (fapl_id != H5I_INVALID_HID)
        H5Pclose(fapl_id);
    return H5I_INVALID_HID;
} /* end h5_fileaccess() */

/*
 * Creates and returns a File Access Property List that
 * may have a modified File Driver and/or library version
 * bounds setting, according to the specified flags
 */
hid_t
h5_fileaccess_flags(unsigned flags)
{
    hid_t fapl_id = H5I_INVALID_HID;

    if ((fapl_id = H5Pcreate(H5P_FILE_ACCESS)) < 0)
        goto error;

    /* Check for libver bounds */
    if ((flags & H5_FILEACCESS_LIBVER) && h5_get_libver_fapl(fapl_id) < 0)
        goto error;

    return fapl_id;

error:
    if (fapl_id != H5I_INVALID_HID)
        H5Pclose(fapl_id);
    return H5I_INVALID_HID;
} /* end h5_fileaccess_flags() */

/*
 * Modifies the File Driver set on the given File
 * Access Property List according to the HDF5_DRIVER
 * or HDF5_TEST_DRIVER environment variables
 */
herr_t
h5_get_vfd_fapl(hid_t fapl)
{
    const char *env   = NULL; /* HDF5_DRIVER/HDF5_TEST_DRIVER environment variable */
    const char *tok   = NULL; /* strtok pointer                       */
    char       *lasts = NULL; /* Context pointer for strtok_r() call */
    char        buf[1024];    /* buffer for tokenizing HDF5_DRIVER    */

    /* Get the environment variable, if it exists */
    env = getenv(HDF5_DRIVER);
    if (!env)
        env = getenv("HDF5_TEST_DRIVER");

    /* If the environment variable was not set, just return
     * without modifying the FAPL.
     */
    if (!env || !*env)
        goto done;

    /* Get the first 'word' of the environment variable.
     * If it's nothing (environment variable was whitespace)
     * just return the default fapl.
     */
    strncpy(buf, env, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    if (NULL == (tok = HDstrtok_r(buf, " \t\n\r", &lasts)))
        goto done;

    if (!strcmp(tok, "sec2")) {
        /* POSIX (section 2) read() and write() system calls */
        if (H5Pset_fapl_sec2(fapl) < 0)
            goto error;
    }
    else if (!strcmp(tok, "stdio")) {
        /* Standard C fread() and fwrite() system calls */
        if (H5Pset_fapl_stdio(fapl) < 0)
            goto error;
    }
    else if (!strcmp(tok, "core")) {
        /* In-memory driver settings (backing store on, 1 MB increment) */
        if (H5Pset_fapl_core(fapl, (size_t)H5_MB, true) < 0)
            goto error;
    }
    else if (!strcmp(tok, "core_paged")) {
        /* In-memory driver with write tracking and paging on */
        if (H5Pset_fapl_core(fapl, (size_t)H5_MB, true) < 0)
            goto error;
        if (H5Pset_core_write_tracking(fapl, true, 4096) < 0)
            goto error;
    }
    else if (!strcmp(tok, "split")) {
        /* Split meta data and raw data each using default driver */
        if (H5Pset_fapl_split(fapl, ".meta", H5P_DEFAULT, ".raw", H5P_DEFAULT) < 0)
            goto error;
    }
    else if (!strcmp(tok, "multi")) {
        /* Multi-file driver, general case of the split driver */
        H5FD_mem_t   memb_map[H5FD_MEM_NTYPES];
        hid_t        memb_fapl[H5FD_MEM_NTYPES];
        const char  *memb_name[H5FD_MEM_NTYPES] = {0};
        char        *sv[H5FD_MEM_NTYPES];
        haddr_t      memb_addr[H5FD_MEM_NTYPES];
        H5FD_mem_t   mt;
        const size_t multi_memname_maxlen = 1024;

        memset(memb_map, 0, sizeof(memb_map));
        memset(memb_fapl, 0, sizeof(memb_fapl));
        memset(memb_addr, 0, sizeof(memb_addr));

        assert(strlen(multi_letters) == H5FD_MEM_NTYPES);
        for (mt = H5FD_MEM_DEFAULT; mt < H5FD_MEM_NTYPES; mt++) {
            memb_fapl[mt] = H5P_DEFAULT;
            if (NULL == (sv[mt] = malloc(multi_memname_maxlen)))
                goto error;
            snprintf(sv[mt], multi_memname_maxlen, "%%s-%c.h5", multi_letters[mt]);
            memb_name[mt] = sv[mt];
            memb_addr[mt] = (haddr_t)MAX(mt - 1, 0) * (HADDR_MAX / 10);
        }

        if (H5Pset_fapl_multi(fapl, memb_map, memb_fapl, memb_name, memb_addr, false) < 0)
            goto error;

        for (mt = H5FD_MEM_DEFAULT; mt < H5FD_MEM_NTYPES; mt++)
            free(sv[mt]);
    }
    else if (!strcmp(tok, "family")) {
        /* Family of files, each 1MB and using the default driver */
        hsize_t fam_size = 100 * 1024 * 1024; /* 100 MB */

        /* Was a family size specified in the environment variable? */
        if ((tok = HDstrtok_r(NULL, " \t\n\r", &lasts)))
            fam_size = (hsize_t)(strtod(tok, NULL) * 1024 * 1024);
        if (H5Pset_fapl_family(fapl, fam_size, H5P_DEFAULT) < 0)
            goto error;
    }
    else if (!strcmp(tok, "log")) {
        /* Log file access */
        unsigned log_flags = H5FD_LOG_LOC_IO | H5FD_LOG_ALLOC;

        /* Were special log file flags specified in the environment variable? */
        if ((tok = HDstrtok_r(NULL, " \t\n\r", &lasts)))
            log_flags = (unsigned)strtol(tok, NULL, 0);

        if (H5Pset_fapl_log(fapl, NULL, log_flags, 0) < 0)
            goto error;
    }
#ifdef H5_HAVE_DIRECT
    else if (!strcmp(tok, "direct")) {
        /* Linux direct read() and write() system calls.  Set memory boundary,
         * file block size, and copy buffer size to the default values.
         */
        if (H5Pset_fapl_direct(fapl, 1024, 4096, 8 * 4096) < 0)
            goto error;
    }
#endif
    else if (!strcmp(tok, "splitter")) {
        H5FD_splitter_vfd_config_t *splitter_config;
        static size_t               file_count = 0;

        if (NULL == (splitter_config = malloc(sizeof(*splitter_config))))
            goto error;

        splitter_config->magic          = H5FD_SPLITTER_MAGIC;
        splitter_config->version        = H5FD_CURR_SPLITTER_VFD_CONFIG_VERSION;
        splitter_config->ignore_wo_errs = false;
        memset(splitter_config->log_file_path, 0, H5FD_SPLITTER_PATH_MAX + 1);

        /*
         * We need access to the base filename to generate a unique name
         * for the W/O file for this FAPL. Until this is refactored, just
         * generate unique names with a counter.
         */
        snprintf(splitter_config->wo_path, H5FD_SPLITTER_PATH_MAX + 1, "splitter_wo_file_%zu.h5",
                 file_count++);

        /* Setup R/W and W/O channel FAPLs since the default FAPL
         * has the splitter driver set on it from the environment
         */
        if ((splitter_config->rw_fapl_id = H5Pcopy(H5P_FILE_ACCESS_DEFAULT)) < 0) {
            free(splitter_config);
            goto error;
        }
        if ((splitter_config->wo_fapl_id = H5Pcopy(H5P_FILE_ACCESS_DEFAULT)) < 0) {
            H5Pclose(splitter_config->rw_fapl_id);
            free(splitter_config);
            goto error;
        }
        if (H5Pset_fapl_sec2(splitter_config->rw_fapl_id) < 0) {
            H5Pclose(splitter_config->rw_fapl_id);
            H5Pclose(splitter_config->wo_fapl_id);
            free(splitter_config);
            goto error;
        }
        if (H5Pset_fapl_sec2(splitter_config->wo_fapl_id) < 0) {
            H5Pclose(splitter_config->rw_fapl_id);
            H5Pclose(splitter_config->wo_fapl_id);
            free(splitter_config);
            goto error;
        }

        if (H5Pset_fapl_splitter(fapl, splitter_config) < 0) {
            H5Pclose(splitter_config->rw_fapl_id);
            H5Pclose(splitter_config->wo_fapl_id);
            free(splitter_config);
            goto error;
        }

        free(splitter_config);
    }
    else if (!strcmp(tok, "onion")) {
        /* TODO */
        return 0;
    }
#ifdef H5_HAVE_SUBFILING_VFD
    else if (!strcmp(tok, H5FD_SUBFILING_NAME)) {
        /* Use default subfiling configuration */
        if (H5Pset_fapl_subfiling(fapl, NULL) < 0)
            goto error;
    }
#endif
#ifdef H5_HAVE_PARALLEL
    else if (!strcmp(tok, "mpio")) {
        int mpi_initialized, mpi_finalized;

        MPI_Initialized(&mpi_initialized);
        MPI_Finalized(&mpi_finalized);

        if (mpi_initialized && !mpi_finalized) {
            if (H5Pset_fapl_mpio(fapl, MPI_COMM_WORLD, MPI_INFO_NULL) < 0)
                goto error;
        }
    }
#endif
#ifdef H5_HAVE_MIRROR_VFD
    else if (!strcmp(tok, "mirror")) {
        /* TODO */
        return 0;
    }
#endif
#ifdef H5_HAVE_LIBHDFS
    else if (!strcmp(tok, "hdfs")) {
        /* TODO */
        return 0;
    }
#endif
#ifdef H5_HAVE_ROS3_VFD
    else if (!strcmp(tok, "ros3")) {
        /* TODO */
        return 0;
    }
#endif
    else {
        /* Unknown driver */
        goto error;
    }

done:
    return 0;

error:
    return -1;
} /* end h5_get_vfd_fapl() */

/*
 * Modifies the library version bounds set on the given
 * File Access Property List according to the HDF5_LIBVER_BOUNDS
 * environment variable
 */
herr_t
h5_get_libver_fapl(hid_t fapl)
{
    const char *env   = NULL; /* HDF5_DRIVER environment variable     */
    const char *tok   = NULL; /* strtok pointer                       */
    char       *lasts = NULL; /* Context pointer for strtok_r() call */
    char        buf[1024];    /* buffer for tokenizing HDF5_DRIVER    */

    /* Get the environment variable, if it exists */
    env = HDgetenv("HDF5_LIBVER_BOUNDS");
#ifdef HDF5_LIBVER_BOUNDS
    /* Use the environment variable, then the compile-time constant */
    if (!env)
        env = HDF5_LIBVER_BOUNDS;
#endif

    /* If the environment variable was not set, just return
     * without modifying the FAPL.
     */
    if (!env || !*env)
        goto done;

    /* Get the first 'word' of the environment variable.
     * If it's nothing (environment variable was whitespace)
     * just return the default fapl.
     */
    HDstrncpy(buf, env, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';
    if (NULL == (tok = HDstrtok_r(buf, " \t\n\r", &lasts)))
        goto done;

    if (!HDstrcmp(tok, "latest")) {
        /* use the latest format */
        if (H5Pset_libver_bounds(fapl, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST) < 0)
            goto error;
    } /* end if */
    else {
        /* Unknown setting */
        goto error;
    } /* end else */

done:
    return 0;

error:
    return -1;
} /* end h5_get_libver_fapl() */

/*
 * Returns the current TestExpress functionality level
 */
int
h5_get_testexpress(void)
{
    char *env_val;
    int   express_val = TestExpress_g;

    /* TestExpress_g is uninitialized if it has a negative value */
    if (express_val < 0) {
        /* Default to level 1 if not overridden */
        express_val = 1;

        /* Check if a default test express level is defined (e.g., by build system) */
#ifdef H5_TEST_EXPRESS_LEVEL_DEFAULT
        express_val = H5_TEST_EXPRESS_LEVEL_DEFAULT;
        if (express_val < 0)
            express_val = 1; /* Reset to default */
        else if (express_val > 3)
            express_val = 3;
#endif
    }

    /* Check if the HDF5TestExpress environment variable is set to
     * override the default level
     */
    env_val = getenv("HDF5TestExpress");
    if (env_val) {
        if (strcmp(env_val, "0") == 0)
            express_val = 0;
        else if (strcmp(env_val, "1") == 0)
            express_val = 1;
        else if (strcmp(env_val, "2") == 0)
            express_val = 2;
        else
            express_val = 3;
    }

    return express_val;
}

/*
 * Sets the TextExpress functionality level
 */
void
h5_set_testexpress(int new_val)
{
    if (new_val < 0)
        new_val = 1; /* Reset to default */
    else if (new_val > 3)
        new_val = 3;

    TestExpress_g = new_val;
}

/*
 * Temporarily turns off hardware data type conversions
 */
void
h5_no_hwconv(void)
{
    H5Tunregister(H5T_PERS_HARD, NULL, (hid_t)-1, (hid_t)-1, NULL);
}

/*
 * Prints out hostname(1)-like information, MPI process
 * IDs and/or thread IDs
 */
void
h5_show_hostname(void)
{
    char hostname[80];
#ifdef H5_HAVE_WIN32_API
    WSADATA wsaData;
    int     err;
#endif

    /* try show the process or thread id in multiple processes cases*/
#ifdef H5_HAVE_PARALLEL
    {
        int mpi_rank, mpi_initialized, mpi_finalized;

        MPI_Initialized(&mpi_initialized);
        MPI_Finalized(&mpi_finalized);

        if (mpi_initialized && !mpi_finalized) {
            MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
            printf("MPI-process %d.", mpi_rank);
        }
        else
            printf("thread 0.");
    }
#else
    printf("thread %" PRIu64 ".", H5TS_thread_id());
#endif
#ifdef H5_HAVE_WIN32_API

    err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (err != 0) {
        /* could not find a usable WinSock DLL */
        return;
    }

    /* Confirm that the WinSock DLL supports 2.2.*/
    /* Note that if the DLL supports versions greater    */
    /* than 2.2 in addition to 2.2, it will still return */
    /* 2.2 in wVersion since that is the version we      */
    /* requested.                                        */

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        /* could not find a usable WinSock DLL */
        WSACleanup();
        return;
    }

#endif
#ifdef H5_HAVE_GETHOSTNAME
    if (gethostname(hostname, (size_t)80) < 0)
        printf(" gethostname failed\n");
    else
        printf(" hostname=%s\n", hostname);
#else
    printf(" gethostname not supported\n");
#endif
#ifdef H5_HAVE_WIN32_API
    WSACleanup();
#endif
}

#ifdef H5_HAVE_PARALLEL
/*
 * Function:    h5_set_info_object
 * Purpose:     Process environment variables setting to set up MPI Info
 *              object.
 * Return:      0 if all is fine; otherwise non-zero.
 */
int
h5_set_info_object(void)
{
    char *envp; /* environment pointer */
    int   ret_value = 0;

    /* handle any MPI INFO hints via $HDF5_MPI_INFO */
    if ((envp = HDgetenv("HDF5_MPI_INFO")) != NULL) {
        char *next, *valp;

        valp = envp = next = HDstrdup(envp);

        if (!valp)
            return 0;

        /* create an INFO object if not created yet */
        if (h5_io_info_g == MPI_INFO_NULL)
            MPI_Info_create(&h5_io_info_g);

        do {
            size_t len;
            char  *key_val, *endp, *namep;

            if (*valp == ';')
                valp++;

            /* copy key/value pair into temporary buffer */
            len  = HDstrcspn(valp, ";");
            next = &valp[len];
            if (NULL == (key_val = (char *)calloc(1, len + 1)))
                return -1;

            /* increment the next pointer past the terminating semicolon */
            if (*next == ';')
                ++next;

            namep = HDstrncpy(key_val, valp, len);

            /* pass up any beginning whitespaces */
            while (*namep && (*namep == ' ' || *namep == '\t'))
                namep++;

            if (!*namep)
                continue; /* was all white space, so move to next k/v pair */

            /* eat up any ending white spaces */
            endp = &namep[HDstrlen(namep) - 1];

            while (endp && (*endp == ' ' || *endp == '\t'))
                *endp-- = '\0';

            /* find the '=' */
            valp = HDstrchr(namep, '=');

            if (valp != NULL) { /* it's a valid key/value pairing */
                char *tmp_val = valp + 1;

                /* change '=' to \0, move valp down one */
                *valp-- = '\0';

                /* eat up ending whitespace on the "key" part */
                while (*valp == ' ' || *valp == '\t')
                    *valp-- = '\0';

                valp = tmp_val;

                /* eat up beginning whitespace on the "value" part */
                while (*valp == ' ' || *valp == '\t')
                    *valp++ = '\0';

                /* actually set the darned thing */
                if (MPI_SUCCESS != MPI_Info_set(h5_io_info_g, namep, valp)) {
                    printf("MPI_Info_set failed\n");
                    ret_value = -1;
                }
            }

            valp = next;
            free(key_val);
        } while (next && *next);

        free(envp);
    }

    return ret_value;
}

/*
 * Function:    h5_dump_info_object
 * Purpose:     Display content of an MPI Info object
 * Return:      void
 */
void
h5_dump_info_object(MPI_Info info)
{
    char key[MPI_MAX_INFO_KEY + 1];
    char value[MPI_MAX_INFO_VAL + 1];
    int  flag;
    int  i, nkeys;

    printf("Dumping MPI Info Object (up to %d bytes per item):\n", MPI_MAX_INFO_VAL);
    if (info == MPI_INFO_NULL) {
        printf("object is MPI_INFO_NULL\n");
    }
    else {
        MPI_Info_get_nkeys(info, &nkeys);
        printf("object has %d items\n", nkeys);
        for (i = 0; i < nkeys; i++) {
            MPI_Info_get_nthkey(info, i, key);
            MPI_Info_get(info, key, MPI_MAX_INFO_VAL, value, &flag);
            printf("%s=%s\n", key, value);
        }
    }
}
#endif /* H5_HAVE_PARALLEL */

/*
 * Gets the current size of a file (in bytes)
 */
/* Disable warning for "format not a string literal" here -QAK */
/*
 *      This pragma only needs to surround the snprintf() calls with
 *      temp in the code below, but early (4.4.7, at least) gcc only
 *      allows diagnostic pragmas to be toggled outside of functions.
 */
H5_GCC_CLANG_DIAG_OFF("format-nonliteral")
h5_stat_size_t
h5_get_file_size(const char *filename, hid_t fapl)
{
    char      temp[2048]; /* Temporary buffer for file names */
    h5_stat_t sb;         /* Structure for querying file info */
    int       j = 0;

    if (fapl == H5P_DEFAULT) {
        /* Get the file's statistics */
        if (0 == HDstat(filename, &sb))
            return ((h5_stat_size_t)sb.st_size);
    } /* end if */
    else {
        hid_t driver; /* VFD used for file */

        /* Get the driver used when creating the file */
        if ((driver = H5Pget_driver(fapl)) < 0)
            return (-1);

        /* Check for simple cases */
        if (driver == H5FD_SEC2 || driver == H5FD_STDIO || driver == H5FD_CORE ||
#ifdef H5_HAVE_WINDOWS
            driver == H5FD_WINDOWS ||
#endif /* H5_HAVE_WINDOWS */
#ifdef H5_HAVE_DIRECT
            driver == H5FD_DIRECT ||
#endif /* H5_HAVE_DIRECT */
            driver == H5FD_LOG || driver == H5FD_SPLITTER) {
            /* Get the file's statistics */
            if (0 == HDstat(filename, &sb))
                return ((h5_stat_size_t)sb.st_size);
        } /* end if */
        else if (driver == H5FD_MULTI) {
            H5FD_mem_t     mt;
            h5_stat_size_t tot_size       = 0;
            char          *driver_env_var = NULL;

            driver_env_var = HDgetenv(HDF5_DRIVER);
            if (driver_env_var && !HDstrcmp(driver_env_var, "split")) {
                for (mt = H5FD_MEM_DEFAULT; mt < H5FD_MEM_NTYPES; mt++) {
                    if (mt != H5FD_MEM_DRAW && mt != H5FD_MEM_SUPER)
                        continue;

                    /* Create the filename to query */
                    if (mt == H5FD_MEM_DRAW) {
                        HDsnprintf(temp, sizeof temp, "%s.raw", filename);
                    }
                    else {
                        HDsnprintf(temp, sizeof temp, "%s.meta", filename);
                    }

                    /* Check for existence of file */
                    if (0 == HDaccess(temp, F_OK)) {
                        /* Get the file's statistics */
                        if (0 != HDstat(temp, &sb))
                            return (-1);

                        /* Add to total size */
                        tot_size += (h5_stat_size_t)sb.st_size;
                    } /* end if */
                }     /* end for */
            }
            else {
                assert(HDstrlen(multi_letters) == H5FD_MEM_NTYPES);
                for (mt = H5FD_MEM_DEFAULT; mt < H5FD_MEM_NTYPES; mt++) {
                    /* Create the filename to query */
                    HDsnprintf(temp, sizeof temp, "%s-%c.h5", filename, multi_letters[mt]);

                    /* Check for existence of file */
                    if (0 == HDaccess(temp, F_OK)) {
                        /* Get the file's statistics */
                        if (0 != HDstat(temp, &sb))
                            return (-1);

                        /* Add to total size */
                        tot_size += (h5_stat_size_t)sb.st_size;
                    } /* end if */
                }     /* end for */
            }

            /* Return total size */
            return (tot_size);
        } /* end if */
#ifdef H5_HAVE_PARALLEL
        else if (driver == H5FD_MPIO) {
            MPI_File   fh; /* MPI file handle used to open the file and verify its size */
            int        mpi_ret;
            MPI_Offset file_size;

            mpi_ret = MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);
            if (mpi_ret != MPI_SUCCESS)
                return -1;
            mpi_ret = MPI_File_get_size(fh, &file_size);
            if (mpi_ret != MPI_SUCCESS)
                return -1;
            mpi_ret = MPI_File_close(&fh);
            if (mpi_ret != MPI_SUCCESS)
                return -1;

            return file_size;
        }
#endif /* H5_HAVE_PARALLEL */
        else if (driver == H5FD_FAMILY) {
            h5_stat_size_t tot_size = 0;

            /* Try all filenames possible, until we find one that's missing */
            for (j = 0; /*void*/; j++) {
                /* Create the filename to query */
                HDsnprintf(temp, sizeof temp, filename, j);

                /* Check for existence of file */
                if (HDaccess(temp, F_OK) < 0)
                    break;

                /* Get the file's statistics */
                if (0 != HDstat(temp, &sb))
                    return (-1);

                /* Add to total size */
                tot_size += (h5_stat_size_t)sb.st_size;
            } /* end for */

            /* Return total size */
            return (tot_size);
        } /* end if */
        else if (driver == H5FD_SUBFILING) {
            hsize_t size;
            hid_t   fid = H5I_INVALID_HID;

            if ((fid = H5Fopen(filename, H5F_ACC_RDONLY, fapl)) < 0)
                return -1;
            if (H5Fget_filesize(fid, &size) < 0) {
                H5Fclose(fid);
                return -1;
            }

            if (H5Fclose(fid) < 0)
                return -1;

            return (h5_stat_size_t)size;
        }
        else {
            /* Get the file's statistics */
            if (0 == HDstat(filename, &sb))
                return ((h5_stat_size_t)sb.st_size);
        } /* end else */
    }     /* end else */

    return (-1);
} /* end get_file_size() */
H5_GCC_CLANG_DIAG_ON("format-nonliteral")

#ifdef H5_HAVE_FILTER_SZIP
/*
 * Determines whether the library's SZIP filter has encoding/decoding
 * functionality enabled.
 */
int
h5_szip_can_encode(void)
{
    unsigned int filter_config_flags;

    H5Zget_filter_info(H5Z_FILTER_SZIP, &filter_config_flags);
    if ((filter_config_flags & (H5Z_FILTER_CONFIG_ENCODE_ENABLED | H5Z_FILTER_CONFIG_DECODE_ENABLED)) == 0) {
        /* filter present but neither encode nor decode is supported (???) */
        return -1;
    }
    else if ((filter_config_flags & (H5Z_FILTER_CONFIG_ENCODE_ENABLED | H5Z_FILTER_CONFIG_DECODE_ENABLED)) ==
             H5Z_FILTER_CONFIG_DECODE_ENABLED) {
        /* decoder only: read but not write */
        return 0;
    }
    else if ((filter_config_flags & (H5Z_FILTER_CONFIG_ENCODE_ENABLED | H5Z_FILTER_CONFIG_DECODE_ENABLED)) ==
             H5Z_FILTER_CONFIG_ENCODE_ENABLED) {
        /* encoder only: write but not read (???) */
        return -1;
    }
    else if ((filter_config_flags & (H5Z_FILTER_CONFIG_ENCODE_ENABLED | H5Z_FILTER_CONFIG_DECODE_ENABLED)) ==
             (H5Z_FILTER_CONFIG_ENCODE_ENABLED | H5Z_FILTER_CONFIG_DECODE_ENABLED)) {
        return 1;
    }
    return (-1);
}
#endif /* H5_HAVE_FILTER_SZIP */

#ifdef H5_HAVE_PARALLEL
/*
 * Retrieves the value of an environment variable and broadcasts
 * it to other MPI processes to ensure all processes see the same
 * value
 */
char *
getenv_all(MPI_Comm comm, int root, const char *name)
{
    int          mpi_size, mpi_rank, mpi_initialized, mpi_finalized;
    int          len;
    static char *env = NULL;

    assert(name);

    MPI_Initialized(&mpi_initialized);
    MPI_Finalized(&mpi_finalized);

    if (mpi_initialized && !mpi_finalized) {
        MPI_Comm_rank(comm, &mpi_rank);
        MPI_Comm_size(comm, &mpi_size);
        assert(root < mpi_size);

        /* The root task does the getenv call
         * and sends the result to the other tasks */
        if (mpi_rank == root) {
            env = HDgetenv(name);
            if (env) {
                len = (int)HDstrlen(env);
                MPI_Bcast(&len, 1, MPI_INT, root, comm);
                MPI_Bcast(env, len, MPI_CHAR, root, comm);
            }
            else {
                /* len -1 indicates that the variable was not in the environment */
                len = -1;
                MPI_Bcast(&len, 1, MPI_INT, root, comm);
            }
        }
        else {
            MPI_Bcast(&len, 1, MPI_INT, root, comm);
            if (len >= 0) {
                if (env == NULL)
                    env = (char *)malloc((size_t)len + 1);
                else if (HDstrlen(env) < (size_t)len)
                    env = (char *)realloc(env, (size_t)len + 1);

                MPI_Bcast(env, len, MPI_CHAR, root, comm);
                env[len] = '\0';
            }
            else {
                if (env)
                    free(env);
                env = NULL;
            }
        }
#ifndef NDEBUG
        MPI_Barrier(comm);
#endif
    }
    else {
        /* use original getenv */
        if (env)
            free(env);
        env = HDgetenv(name);
    } /* end if */

    return env;
}

#endif

/*
 * Makes a byte-for-byte copy of a file
 */
int
h5_make_local_copy(const char *origfilename, const char *local_copy_name)
{
    int         fd_old = (-1), fd_new = (-1);                    /* File descriptors for copying data */
    ssize_t     nread;                                           /* Number of bytes read in */
    void       *buf      = NULL;                                 /* Buffer for copying data */
    const char *filename = H5_get_srcdir_filename(origfilename); /* Get the test file name to copy */

    if (!filename)
        goto error;

    /* Allocate copy buffer */
    if (NULL == (buf = calloc((size_t)1, (size_t)READ_BUF_SIZE)))
        goto error;

    /* Copy old file into temporary file */
    if ((fd_old = HDopen(filename, O_RDONLY)) < 0)
        goto error;
    if ((fd_new = HDopen(local_copy_name, O_RDWR | O_CREAT | O_TRUNC, H5_POSIX_CREATE_MODE_RW)) < 0)
        goto error;

    /* Copy data */
    while ((nread = HDread(fd_old, buf, (size_t)READ_BUF_SIZE)) > 0)
        if (HDwrite(fd_new, buf, (size_t)nread) < 0)
            goto error;

    /* Close files */
    if (HDclose(fd_old) < 0)
        goto error;
    if (HDclose(fd_new) < 0)
        goto error;

    /* Release memory */
    free(buf);

    return 0;

error:
    /* ignore return values since we're already noted the problem */
    if (fd_old > 0)
        HDclose(fd_old);
    if (fd_new > 0)
        HDclose(fd_new);
    free(buf);
    return -1;
} /* end h5_make_local_copy() */

/*-------------------------------------------------------------------------
 * Function:    h5_verify_cached_stabs_cb
 *
 * Purpose:     Callback function for h5_verify_cached_stabs.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
h5_verify_cached_stabs_cb(hid_t oid, const char H5_ATTR_UNUSED *name, const H5O_info2_t *oinfo,
                          void H5_ATTR_UNUSED *udata)
{
    if (oinfo->type == H5O_TYPE_GROUP)
        return H5G__verify_cached_stabs_test(oid);
    else
        return SUCCEED;
} /* end h5_verify_cached_stabs_cb() */

/*
 * Checks a list of files to ensure that groups in those files
 * have their symbol table information cached, if present and
 * if their parent group also uses a symbol table. Does not
 * check that the root group's symbol table information is
 * cached in the superblock.
 */
herr_t
h5_verify_cached_stabs(const char *base_name[], hid_t fapl)
{
    hid_t file = -1;
    char  filename[1024];
    int   i = 0;

    while (base_name[i]) {
        if (h5_fixname(base_name[i], fapl, filename, sizeof(filename)) == NULL)
            continue;

        H5E_BEGIN_TRY
        {
            file = H5Fopen(filename, H5F_ACC_RDONLY, fapl);
        }
        H5E_END_TRY
        if (file < 0) {
            i++;
            continue;
        } /* end if */

        if (H5Ovisit3(file, H5_INDEX_NAME, H5_ITER_NATIVE, h5_verify_cached_stabs_cb, NULL, H5O_INFO_BASIC) <
            0)
            goto error;

        if (H5Fclose(file) < 0)
            goto error;
        file = -1;

        i++;
    } /* end while */

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Fclose(file);
    }
    H5E_END_TRY

    return -1;
}

/*
 * "Sends" a message to another testing process using a temporary file
 */
void
h5_send_message(const char *send, const char *arg1, const char *arg2)
{
    FILE *signalfile = NULL;

    /* Create signal file (which will send signal to some other process) */
    signalfile = fopen(TMP_SIGNAL_FILE, "w+");

    /* Write messages to signal file, if provided */
    if (arg2 != NULL) {
        assert(arg1);
        fprintf(signalfile, "%s\n%s\n", arg1, arg2);
    } /* end if */
    else if (arg1 != NULL) {
        assert(arg2 == NULL);
        fprintf(signalfile, "%s\n", arg1);
    } /* end if */
    else {
        assert(arg1 == NULL);
        assert(arg2 == NULL);
    } /* end else */

    fclose(signalfile);

    HDrename(TMP_SIGNAL_FILE, send);
} /* h5_send_message() */

/*
 * Waits for a message from another testing process to be available
 */
herr_t
h5_wait_message(const char *waitfor)
{
    FILE  *returnfile;
    time_t t0, t1;

    /* Start timer. If this function runs for too long (i.e.,
        expected signal is never received), it will
        return failure */
    HDtime(&t0);

    /* Wait for return signal from some other process */
    while ((returnfile = fopen(waitfor, "r")) == NULL) {

        /* make note of current time. */
        HDtime(&t1);

        /* If we've been waiting for a signal for too long, then
            it was likely never sent and we should fail rather
            than loop infinitely */
        if (HDdifftime(t1, t0) > MESSAGE_TIMEOUT) {
            fprintf(stdout, "Error communicating between processes. Make sure test script is running.\n");
            TEST_ERROR;
        } /* end if */
    }     /* end while */

    fclose(returnfile);
    HDunlink(waitfor);

    return SUCCEED;

error:
    return FAIL;
} /* h5_wait_message() */

/* Functions for the dummy VFD class (see below).
 *
 * Useful for testing things like ID handling where we shouldn't mess with the
 * real VFDs.
 */
static H5FD_t *dummy_vfd_open(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr);
static H5FD_t *
dummy_vfd_open(const char H5_ATTR_UNUSED *name, unsigned H5_ATTR_UNUSED flags, hid_t H5_ATTR_UNUSED fapl_id,
               haddr_t H5_ATTR_UNUSED maxaddr)
{
    return NULL;
}

static herr_t dummy_vfd_close(H5FD_t *_file);
static herr_t
dummy_vfd_close(H5FD_t H5_ATTR_UNUSED *_file)
{
    return FAIL;
}

static haddr_t dummy_vfd_get_eoa(const H5FD_t *file, H5FD_mem_t type);
static haddr_t
dummy_vfd_get_eoa(const H5FD_t H5_ATTR_UNUSED *file, H5FD_mem_t H5_ATTR_UNUSED type)
{
    return HADDR_UNDEF;
}

static herr_t dummy_vfd_set_eoa(H5FD_t *_file, H5FD_mem_t type, haddr_t addr);
static herr_t
dummy_vfd_set_eoa(H5FD_t H5_ATTR_UNUSED *_file, H5FD_mem_t H5_ATTR_UNUSED type, haddr_t H5_ATTR_UNUSED addr)
{
    return FAIL;
}

static haddr_t dummy_vfd_get_eof(const H5FD_t *file, H5FD_mem_t type);
static haddr_t
dummy_vfd_get_eof(const H5FD_t H5_ATTR_UNUSED *file, H5FD_mem_t H5_ATTR_UNUSED type)
{
    return HADDR_UNDEF;
}

static herr_t dummy_vfd_read(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size,
                             void *buf);
static herr_t
dummy_vfd_read(H5FD_t H5_ATTR_UNUSED *_file, H5FD_mem_t H5_ATTR_UNUSED type, hid_t H5_ATTR_UNUSED fapl_id,
               haddr_t H5_ATTR_UNUSED addr, size_t H5_ATTR_UNUSED size, void H5_ATTR_UNUSED *buf)
{
    return FAIL;
}

static herr_t dummy_vfd_write(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size,
                              const void *buf);
static herr_t
dummy_vfd_write(H5FD_t H5_ATTR_UNUSED *_file, H5FD_mem_t H5_ATTR_UNUSED type, hid_t H5_ATTR_UNUSED fapl_id,
                haddr_t H5_ATTR_UNUSED addr, size_t H5_ATTR_UNUSED size, const void H5_ATTR_UNUSED *buf)
{
    return FAIL;
}

/* Dummy VFD with the minimum parameters to make a VFD that can be registered */
#define DUMMY_VFD_VALUE (H5FD_class_value_t)155
static const H5FD_class_t H5FD_dummy_g = {
    H5FD_CLASS_VERSION,  /* struct version  */
    DUMMY_VFD_VALUE,     /* value           */
    "dummy",             /* name            */
    1,                   /* maxaddr         */
    H5F_CLOSE_WEAK,      /* fc_degree       */
    NULL,                /* terminate       */
    NULL,                /* sb_size         */
    NULL,                /* sb_encode       */
    NULL,                /* sb_decode       */
    0,                   /* fapl_size       */
    NULL,                /* fapl_get        */
    NULL,                /* fapl_copy       */
    NULL,                /* fapl_free       */
    0,                   /* dxpl_size       */
    NULL,                /* dxpl_copy       */
    NULL,                /* dxpl_free       */
    dummy_vfd_open,      /* open            */
    dummy_vfd_close,     /* close           */
    NULL,                /* cmp             */
    NULL,                /* query           */
    NULL,                /* get_type_map    */
    NULL,                /* alloc           */
    NULL,                /* free            */
    dummy_vfd_get_eoa,   /* get_eoa         */
    dummy_vfd_set_eoa,   /* set_eoa         */
    dummy_vfd_get_eof,   /* get_eof         */
    NULL,                /* get_handle      */
    dummy_vfd_read,      /* read            */
    dummy_vfd_write,     /* write           */
    NULL,                /* read_vector     */
    NULL,                /* write_vector    */
    NULL,                /* read_selection  */
    NULL,                /* write_selection */
    NULL,                /* flush           */
    NULL,                /* truncate        */
    NULL,                /* lock            */
    NULL,                /* unlock          */
    NULL,                /* del             */
    NULL,                /* ctl             */
    H5FD_FLMAP_DICHOTOMY /* fl_map          */
};

/*
 * Returns a disposable, generally non-functional,
 * VFD class struct.
 */
H5FD_class_t *
h5_get_dummy_vfd_class(void)
{
    H5FD_class_t *vfd_class = NULL; /* Dummy VFD that will be returned */

    /* Create the class and initialize everything to zero/NULL */
    if (NULL == (vfd_class = (H5FD_class_t *)malloc(sizeof(H5FD_class_t))))
        TEST_ERROR;

    /* Copy the dummy VFD */
    memcpy(vfd_class, &H5FD_dummy_g, sizeof(H5FD_class_t));

    return vfd_class;

error:
    if (vfd_class)
        free(vfd_class);
    return NULL;
} /* h5_get_dummy_vfd_class */

/*
 * Returns a disposable, generally non-functional,
 * VOL class struct.
 */
H5VL_class_t *
h5_get_dummy_vol_class(void)
{
    H5VL_class_t *vol_class = NULL; /* Dummy VOL class that will be returned */

    /* Create the class and initialize everything to zero/NULL */
    if (NULL == (vol_class = (H5VL_class_t *)calloc((size_t)1, sizeof(H5VL_class_t))))
        TEST_ERROR;

    /* Fill in the minimum parameters to make a VOL connector class that
     * can be registered.
     */
    vol_class->version = H5VL_VERSION;
    vol_class->name    = "dummy";

    return vol_class;

error:
    if (vol_class)
        free(vol_class);
    return NULL;
} /* h5_get_dummy_vol_class */

/*
 * Get the canonical string that corresponds to the
 * given library version bound
 */
H5_ATTR_PURE const char *
h5_get_version_string(H5F_libver_t libver)
{
    return (LIBVER_NAMES[libver]);
} /* end of h5_get_version_string */

/*
 * Performs a byte-for-byte comparison between two files
 */
int
h5_compare_file_bytes(char *f1name, char *f2name)
{
    FILE *f1ptr     = NULL; /* two file pointers */
    FILE *f2ptr     = NULL;
    off_t f1size    = 0; /* size of the files */
    off_t f2size    = 0;
    char  f1char    = 0; /* one char from each file */
    char  f2char    = 0;
    off_t ii        = 0;
    int   ret_value = 0; /* for error handling */

    /* Open files for reading */
    f1ptr = fopen(f1name, "rb");
    if (f1ptr == NULL) {
        fprintf(stderr, "Unable to fopen() %s\n", f1name);
        ret_value = -1;
        goto done;
    }
    f2ptr = fopen(f2name, "rb");
    if (f2ptr == NULL) {
        fprintf(stderr, "Unable to fopen() %s\n", f2name);
        ret_value = -1;
        goto done;
    }

    /* Get the file sizes and verify that they equal */
    HDfseek(f1ptr, 0, SEEK_END);
    f1size = HDftell(f1ptr);

    HDfseek(f2ptr, 0, SEEK_END);
    f2size = HDftell(f2ptr);

    if (f1size != f2size) {
        fprintf(stderr, "Files differ in size, %" PRIuHSIZE " vs. %" PRIuHSIZE "\n", (hsize_t)f1size,
                (hsize_t)f2size);
        ret_value = -1;
        goto done;
    }

    /* Compare each byte and fail if a difference is found */
    HDrewind(f1ptr);
    HDrewind(f2ptr);
    for (ii = 0; ii < f1size; ii++) {
        if (fread(&f1char, 1, 1, f1ptr) != 1) {
            ret_value = -1;
            goto done;
        }
        if (fread(&f2char, 1, 1, f2ptr) != 1) {
            ret_value = -1;
            goto done;
        }
        if (f1char != f2char) {
            fprintf(stderr, "Mismatch @ 0x%" PRIXHSIZE ": 0x%X != 0x%X\n", (hsize_t)ii, f1char, f2char);
            ret_value = -1;
            goto done;
        }
    }

done:
    if (f1ptr)
        fclose(f1ptr);
    if (f2ptr)
        fclose(f2ptr);
    return ret_value;
} /* end h5_compare_file_bytes() */

/*-------------------------------------------------------------------------
 * Function:    H5_get_srcdir_filename
 *
 * Purpose:     Append the test file name to the srcdir path and return the whole string
 *
 * Return:      The string or NULL (errors or not enough space)
 *
 *-------------------------------------------------------------------------
 */
const char *
H5_get_srcdir_filename(const char *filename)
{
    const char *srcdir = H5_get_srcdir();

    /* Check for error */
    if (NULL == srcdir)
        return NULL;

    /* Build path to test file. We're checking the length so suppress
     * the gcc format-truncation warning.
     */
    if ((HDstrlen(srcdir) + HDstrlen(filename) + 1) < sizeof(srcdir_testpath)) {
        H5_GCC_DIAG_OFF("format-truncation")
        HDsnprintf(srcdir_testpath, sizeof(srcdir_testpath), "%s%s", srcdir, filename);
        H5_GCC_DIAG_ON("format-truncation")
        return srcdir_testpath;
    }

    /* If not enough space, just return NULL */
    return NULL;
} /* end H5_get_srcdir_filename() */

/*-------------------------------------------------------------------------
 * Function:    H5_get_srcdir
 *
 * Purpose:     Just return the srcdir path
 *
 * Return:      The string
 *
 *-------------------------------------------------------------------------
 */
const char *
H5_get_srcdir(void)
{
    const char *srcdir = HDgetenv("srcdir");

    /* Check for using the srcdir from configure time */
    if (NULL == srcdir)
        srcdir = config_srcdir;

    /* Build path to all test files */
    if ((HDstrlen(srcdir) + 2) < sizeof(srcdir_path)) {
        HDsnprintf(srcdir_path, sizeof(srcdir_path), "%s/", srcdir);
        return (srcdir_path);
    } /* end if */
    else
        return (NULL);
} /* end H5_get_srcdir() */

/*
 * Makes a byte-for-byte copy of a file
 */
int
h5_duplicate_file_by_bytes(const char *orig, const char *dest)
{
    FILE   *orig_ptr  = NULL;
    FILE   *dest_ptr  = NULL;
    hsize_t fsize     = 0;
    hsize_t read_size = 0;
    hsize_t max_buf   = 0;
    void   *dup_buf   = NULL;
    int     ret_value = 0;

    max_buf = 4096 * sizeof(char);

    orig_ptr = fopen(orig, "rb");
    if (NULL == orig_ptr) {
        ret_value = -1;
        goto done;
    }

    HDfseek(orig_ptr, 0, SEEK_END);
    fsize = (hsize_t)HDftell(orig_ptr);
    HDrewind(orig_ptr);

    dest_ptr = fopen(dest, "wb");
    if (NULL == dest_ptr) {
        ret_value = -1;
        goto done;
    }

    read_size = MIN(fsize, max_buf);
    dup_buf   = malloc(read_size);
    if (NULL == dup_buf) {
        ret_value = -1;
        goto done;
    }

    while (read_size > 0) {
        if (fread(dup_buf, read_size, 1, orig_ptr) != 1) {
            ret_value = -1;
            goto done;
        }
        fwrite(dup_buf, read_size, 1, dest_ptr);
        fsize -= read_size;
        read_size = MIN(fsize, max_buf);
    }

done:
    if (orig_ptr != NULL)
        fclose(orig_ptr);
    if (dest_ptr != NULL)
        fclose(dest_ptr);
    if (dup_buf != NULL)
        free(dup_buf);
    return ret_value;
} /* end h5_duplicate_file_by_bytes() */

/*
 * Checks if file locking is enabled on this file system.
 */
herr_t
h5_check_if_file_locking_enabled(hbool_t *is_enabled)
{
    const char *filename = "locking_test_file";
    int         pmode    = O_RDWR | O_CREAT | O_TRUNC;
    int         fd       = -1;

    *is_enabled = TRUE;

    if ((fd = HDopen(filename, pmode, H5_POSIX_CREATE_MODE_RW)) < 0)
        goto error;

    /* Test HDflock() to see if it works */
    if (HDflock(fd, LOCK_EX | LOCK_NB) < 0) {
        if (ENOSYS == errno) {
            /* When errno is set to ENOSYS, the file system does not support
             * locking, so ignore it. This is most frequently used on
             * Lustre. If we also want to check for disabled NFS locks
             * we'll need to check for ENOLCK, too. That isn't done by
             * default here since that could also represent an actual
             * error condition.
             */
            errno       = 0;
            *is_enabled = FALSE;
        }
        else
            goto error;
    }
    if (HDflock(fd, LOCK_UN) < 0)
        goto error;

    if (HDclose(fd) < 0)
        goto error;
    if (HDremove(filename) < 0)
        goto error;

    return SUCCEED;

error:
    *is_enabled = FALSE;
    if (fd > -1) {
        HDclose(fd);
        HDremove(filename);
    }
    return FAIL;
} /* end h5_check_if_file_locking_enabled() */

/*
 * Checks if the VOL connector being used is (or the VOL
 * connector stack being used resolves to) the native VOL
 * connector.
 */
herr_t
h5_using_native_vol(hid_t fapl_id, hid_t obj_id, bool *is_native_vol)
{
    hbool_t is_native = false;
    hid_t   native_id = H5I_INVALID_HID;
    hid_t   vol_id    = H5I_INVALID_HID;
    herr_t  ret_value = SUCCEED;

    assert((fapl_id >= 0) || (obj_id >= 0));
    assert(is_native_vol);

    if (fapl_id == H5P_DEFAULT)
        fapl_id = H5P_FILE_ACCESS_DEFAULT;

    if (obj_id >= 0) {
        if (H5VLobject_is_native(obj_id, &is_native) < 0) {
            ret_value = FAIL;
            goto done;
        }
    }
    else {
        if (true != H5VLis_connector_registered_by_value(H5VL_NATIVE_VALUE)) {
            ret_value = FAIL;
            goto done;
        }

        if ((native_id = H5VLget_connector_id_by_value(H5VL_NATIVE_VALUE)) < 0) {
            ret_value = FAIL;
            goto done;
        }

        if (H5Pget_vol_id(fapl_id, &vol_id) < 0) {
            ret_value = FAIL;
            goto done;
        }

        if (vol_id == native_id)
            is_native = true;
    }

    *is_native_vol = is_native;

done:
    if (vol_id != H5I_INVALID_HID)
        H5VLclose(vol_id);
    if (native_id != H5I_INVALID_HID)
        H5VLclose(native_id);

    return ret_value;
}

/*
 * Checks if the specified VFD name matches the library's
 * default VFD.
 */
hbool_t
h5_using_default_driver(const char *drv_name)
{
    hbool_t ret_val = TRUE;

    assert(H5_DEFAULT_VFD == H5FD_SEC2);

    if (!drv_name)
        drv_name = HDgetenv(HDF5_DRIVER);

    if (drv_name)
        return (!HDstrcmp(drv_name, "sec2") || !HDstrcmp(drv_name, "nomatch"));

    return ret_val;
}

/*
 * Checks if the current VFD set on the given FAPL is a
 * parallel-enabled VFD (The MPI I/O VFD, for example).
 */
herr_t
h5_using_parallel_driver(hid_t fapl_id, hbool_t *driver_is_parallel)
{
    unsigned long feat_flags = 0;
    hid_t         driver_id  = H5I_INVALID_HID;
    herr_t        ret_value  = SUCCEED;

    assert(fapl_id >= 0);
    assert(driver_is_parallel);

    if (fapl_id == H5P_DEFAULT)
        fapl_id = H5P_FILE_ACCESS_DEFAULT;

    if ((driver_id = H5Pget_driver(fapl_id)) < 0)
        return FAIL;

    if (H5FDdriver_query(driver_id, &feat_flags) < 0)
        return FAIL;

    *driver_is_parallel = (feat_flags & H5FD_FEAT_HAS_MPI);

    return ret_value;
}

/*
 * Checks if the current VFD set on the given FAPL creates a
 * file that is compatible with the default VFD.
 */
herr_t
h5_driver_is_default_vfd_compatible(hid_t fapl_id, hbool_t *default_vfd_compatible)
{
    unsigned long feat_flags = 0;
    hid_t         driver_id  = H5I_INVALID_HID;
    herr_t        ret_value  = SUCCEED;

    assert(fapl_id >= 0);
    assert(default_vfd_compatible);

    if (fapl_id == H5P_DEFAULT)
        fapl_id = H5P_FILE_ACCESS_DEFAULT;

    if ((driver_id = H5Pget_driver(fapl_id)) < 0)
        return FAIL;

    if (H5FDdriver_query(driver_id, &feat_flags) < 0)
        return FAIL;

    *default_vfd_compatible = (feat_flags & H5FD_FEAT_DEFAULT_VFD_COMPATIBLE);

    return ret_value;
} /* end h5_driver_is_default_vfd_compatible() */

/*
 * Checks if the specified VFD name matches a driver that
 * stores data using multiple files.
 */
hbool_t
h5_driver_uses_multiple_files(const char *drv_name, unsigned flags)
{
    hbool_t ret_val = FALSE;

    if (!drv_name)
        drv_name = HDgetenv(HDF5_DRIVER);

    if (drv_name) {
        if ((flags & H5_EXCLUDE_MULTIPART_DRIVERS) == 0) {
            if (!HDstrcmp(drv_name, "split") || !HDstrcmp(drv_name, "multi") ||
                !HDstrcmp(drv_name, "family") || !HDstrcmp(drv_name, H5FD_SUBFILING_NAME))
                return TRUE;
        }

        if ((flags & H5_EXCLUDE_NON_MULTIPART_DRIVERS) == 0) {
            if (!HDstrcmp(drv_name, "splitter"))
                return TRUE;
        }
    }

    return ret_val;
}

/* Allow up to 3-digit thread indexes (0-999)*/
#define MAX_THREAD_IDX 999
#define MAX_THREAD_IDX_LEN

/* Generate a heap-allocated filename of the form <prefix><thread_idx><filename> */
char *generate_threadlocal_filename(const char *prefix, int thread_idx, const char *filename) {
    int chars_written = 0;
    char *test_filename =  NULL;

    if (thread_idx > MAX_THREAD_IDX) {
        fprintf(stderr, "    thread index exceeded expected size\n");
        goto error;
    }

    if (MAX_THREAD_IDX_LEN + strlen(filename) >= H5_TEST_FILENAME_MAX_LENGTH) {
        fprintf(stderr, "    test file name exceeded expected size\n");
        goto error;
    }

    if (NULL == (test_filename = (char *)calloc(1, H5_TEST_FILENAME_MAX_LENGTH))) {
        fprintf(stderr, "    couldn't allocate memory for test file name\n");
        goto error;
    }

    /* Write prefix, thread index, and filename into buffer */
    if ((chars_written = snprintf(test_filename,
                                  H5_TEST_FILENAME_MAX_LENGTH, "%s%d%s",
                                  prefix, thread_idx, filename)) < 0) {
        fprintf(stderr, "    couldn't create test file name\n");
        goto error;
    }

    return test_filename;

error:
    free(test_filename);
    return NULL;
}
/*-------------------------------------------------------------------------
 * Function:    reg_opt_op_optional_verify
 *
 * Purpose:     Common verification routine for dynamic optional operations
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
reg_opt_op_optional_verify(void *obj, H5VL_optional_args_t *args)
{
    int *o = (int *)obj;
    int *op_args;

    /* Check for receiving correct operation value */
    if (args->op_type != reg_opt_curr_op_val)
        return -1;

    /* Check that the object is correct */
    if ((-1) != *o)
        return -1;

    /* Update the object, with the operation value */
    *o = args->op_type;

    /* Check that the argument is correct */
    op_args = args->args;
    if (NULL == op_args)
        return -1;
    if ((-1) != *op_args)
        return -1;

    /* Update the argument return parameter */
    *op_args = args->op_type;

    return 0;
} /* end reg_opt_op_optional_verify() */


/*-------------------------------------------------------------------------
 * Function:    reg_opt_op_optional
 *
 * Purpose:     Common callback to perform a connector-specific operation
 *              on an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
reg_opt_op_optional(void *obj, H5VL_optional_args_t *args, hid_t H5_ATTR_UNUSED dxpl_id,
                    void H5_ATTR_UNUSED **req)
{
    /* Invoke the common value verification routine */
    return reg_opt_op_optional_verify(obj, args);
} /* end reg_opt_op_optional() */

/*-------------------------------------------------------------------------
 * Function:    reg_opt_link_optional
 *
 * Purpose:     Callback to perform a connector-specific operation
 *              on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
reg_opt_link_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args,
                      hid_t H5_ATTR_UNUSED dxpl_id, void H5_ATTR_UNUSED **req)
{
    /* Check for receiving correct loc_params info */
    if (loc_params->type != H5VL_OBJECT_BY_NAME)
        return -1;
    if (loc_params->obj_type != H5I_GROUP)
        return -1;
    if (HDstrcmp(loc_params->loc_data.loc_by_name.name, ".") != 0)
        return -1;
    if (loc_params->loc_data.loc_by_name.lapl_id != H5P_LINK_ACCESS_DEFAULT)
        return -1;

    /* Invoke the common value verification routine */
    return reg_opt_op_optional_verify(obj, args);
} /* end reg_opt_link_optional() */

/*-------------------------------------------------------------------------
 * Function:    reg_opt_datatype_get
 *
 * Purpose:     Handles the datatype get callback
 *
 * Note:        This is _strictly_ a testing fixture to support the
 *              exercise_reg_opt_oper() testing routine.  It fakes just
 *              enough of the named datatype VOL callback for the
 *              H5VL_register_using_vol_id() call in that test routine to
 *              succeed.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
reg_opt_datatype_get(void H5_ATTR_UNUSED *obj, H5VL_datatype_get_args_t *args, hid_t H5_ATTR_UNUSED dxpl_id,
                     void H5_ATTR_UNUSED **req)
{
    herr_t ret_value = SUCCEED; /* Return value */

    if (H5VL_DATATYPE_GET_BINARY_SIZE == args->op_type) {
        if (H5Tencode(H5T_NATIVE_INT, NULL, args->args.get_binary_size.size) < 0)
            ret_value = FAIL;
    } /* end if */
    else if (H5VL_DATATYPE_GET_BINARY == args->op_type) {
        if (H5Tencode(H5T_NATIVE_INT, args->args.get_binary.buf, &args->args.get_binary.buf_size) < 0)
            ret_value = FAIL;
    } /* end if */
    else
        ret_value = FAIL;

    return ret_value;
} /* end reg_opt_datatype_get() */
