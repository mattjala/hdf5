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
 * Purpose:    Provides support functions for the testing framework.
 *
 */

#include "testhdf5.h"

/*
 * Definitions for the testing structure.
 */
#define MAXTESTNAME 64
#define MAXTESTDESC 128

typedef void (*TestCall)(void);

typedef struct TestStruct {
    H5_ATOMIC(int)  NumErrors;
    char Description[MAXTESTDESC];
    int  SkipFlag;
    char Name[MAXTESTNAME];
    TestCall Call;
    void (*Cleanup)(void);
    const void *TestParameters;
    int64_t TestFrameworkFlags;
} TestStruct;

typedef struct TestThreadArgs {
    int    ThreadIndex;
    size_t num_tests;
    const char **test_descriptions;
    test_outcome_t *test_outcomes;
    TestCall Call;
} TestThreadArgs;

/*
 * Variables used by testing framework.
 */
static int         enable_error_stack               = 0;         /* enable error stack; disable=0 enable=1 */
H5_ATOMIC(int)         num_errs_g                         = 0;         /* Total number of errors during testing */
int                TestVerbosity                    = VERBO_DEF; /* Default Verbosity is Low */
static int         Summary                          = 0;         /* Show test summary. Default is no. */
static int         CleanUp                          = 1;         /* Do cleanup or not. Default is yes. */
static int         TestExpress                      = -1;   /* Do TestExpress or not. -1 means not set yet. */
static TestStruct *Test                             = NULL; /* Array of tests */
static unsigned    TestAlloc                        = 0;    /* Size of the Test array */
static unsigned    Index                            = 0;
static const void *Test_parameters                  = NULL;
static const char *TestProgName                     = NULL;
static void (*TestPrivateUsage)(void)               = NULL;
static int (*TestPrivateParser)(int ac, char *av[]) = NULL;

static int TestMaxNumThreads_g = -1; /* Max number of threads that can be spawned */
const char *test_path_prefix = NULL;

/*
 * Setup a test function and add it to the list of tests.
 *      It must have no parameters and returns void.
 * TheName--short test name.
 *    If the name starts with '-', do not run it by default.
 * TheCall--the test routine.
 * Cleanup--the cleanup routine for the test.
 * TheDescr--Long description of the test.
 * TestParameters--pointer to extra parameters for an individual test. Use NULL if none used.
 *    Since only the pointer is copied, the contents should not change.
 * TestFrameworkFlags--flags for the test framework that control the operation of the 
 *    individual test at a high level.
 * Return: Void
 *    exit EXIT_FAILURE if error is encountered.
 */
void
AddTest(const char *TheName, void (*TheCall)(void), void (*Cleanup)(void), const char *TheDescr,
        const void *TestParameters, const int64_t TestFrameworkFlags)
{
    /* Sanity checking */
    if (HDstrlen(TheDescr) >= MAXTESTDESC) {
        printf("Test description ('%s') too long, increase MAXTESTDESC(%d).\n", TheDescr, MAXTESTDESC);
        exit(EXIT_FAILURE);
    }
    if (HDstrlen(TheName) >= MAXTESTNAME) {
        printf("Test name too long, increase MAXTESTNAME(%d).\n", MAXTESTNAME);
        exit(EXIT_FAILURE);
    }

    /* Check for increasing the Test array size */
    if (Index >= TestAlloc) {
        TestStruct *newTest  = Test;                  /* New array of tests */
        unsigned    newAlloc = MAX(1, TestAlloc * 2); /* New array size */

        /* Reallocate array */
        if (NULL == (newTest = (TestStruct *)realloc(Test, newAlloc * sizeof(TestStruct)))) {
            printf("Out of memory for tests, Index = %u, TestAlloc = %u, newAlloc = %u\n", Index, TestAlloc,
                   newAlloc);
            exit(EXIT_FAILURE);
        }

        /* Update info */
        Test      = newTest;
        TestAlloc = newAlloc;
    }

    /* Set up test function */
    HDstrcpy(Test[Index].Description, TheDescr);
    if (*TheName != '-') {
        HDstrcpy(Test[Index].Name, TheName);
        Test[Index].SkipFlag = 0;
    }
    else { /* skip test by default */
        HDstrcpy(Test[Index].Name, TheName + 1);
        Test[Index].SkipFlag = 1;
    }
    Test[Index].Call       = TheCall;
    Test[Index].Cleanup    = Cleanup;

    H5_ATOMIC_STORE(Test[Index].NumErrors, -1);
    Test[Index].TestParameters = TestParameters;
    Test[Index].TestFrameworkFlags = TestFrameworkFlags;

    /* Increment test count */
    Index++;
}

/*
 * Initialize testing framework
 *
 * ProgName: Name of test program.
 * private_usage: Optional routine provided by test program to print the
 *      private portion of usage page.  Default to NULL which means none is
 *      provided.
 * private_parser: Optional routine provided by test program to parse the
 *      private options.  Default to NULL which means none is provided.
 *
 */
void
TestInit(const char *ProgName, void (*private_usage)(void), int (*private_parser)(int ac, char *av[]))
{
    /*
     * Turn off automatic error reporting since we do it ourselves.  Besides,
     * half the functions this test calls are private, so automatic error
     * reporting wouldn't do much good since it's triggered at the API layer.
     */
    if (enable_error_stack == 0)
        H5Eset_auto2(H5E_DEFAULT, NULL, NULL);

    /*
     * Record the program name and private routines if provided.
     */
    TestProgName = ProgName;
    if (NULL != private_usage)
        TestPrivateUsage = private_usage;
    if (NULL != private_parser)
        TestPrivateParser = private_parser;

    /* Set up test path prefix for filenames, with default being empty */
    if (test_path_prefix == NULL) {
        if ((test_path_prefix = HDgetenv(HDF5_API_TEST_PATH_PREFIX)) == NULL)
            test_path_prefix = (const char *)"";
    }

}

/*
 * Print test usage.
 *    First print the common test options, then the extra options if provided.
 */
void
TestUsage(void)
{
    unsigned i;

    print_func("Usage: %s [-v[erbose] (l[ow]|m[edium]|h[igh]|0-9)] %s\n", TestProgName,
               (TestPrivateUsage ? "<extra options>" : ""));
    print_func("              [-[e]x[clude] name]+ \n");
    print_func("              [-o[nly] name]+ \n");
    print_func("              [-b[egin] name] \n");
    print_func("              [-s[ummary]]  \n");
    print_func("              [-c[leanoff]]  \n");
    print_func("              [-h[elp]]  \n");
    print_func("\n\n");
    print_func("verbose   controls the amount of information displayed\n");
    print_func("exclude   to exclude tests by name\n");
    print_func("only      to name tests which should be run\n");
    print_func("begin     start at the name of the test given\n");
    print_func("summary   prints a summary of test results at the end\n");
    print_func("cleanoff  does not delete *.hdf files after execution of tests\n");
    print_func("help      print out this information\n");
    if (TestPrivateUsage) {
        print_func("\nExtra options\n");
        TestPrivateUsage();
    }
    print_func("\n\n");
    print_func("This program currently tests the following: \n\n");
    print_func("%16s %s\n", "Name", "Description");
    print_func("%16s %s\n", "----", "-----------");

    for (i = 0; i < Index; i++)
        print_func("%16s -- %s\n", Test[i].Name, Test[i].Description);

    print_func("\n\n");
}

/*
 * Print test info.
 */
void
TestInfo(const char *ProgName)
{
    unsigned major, minor, release;

    H5get_libversion(&major, &minor, &release);

    print_func("\nFor help use: %s -help\n", ProgName);
    print_func("Linked with hdf5 version %u.%u release %u\n", major, minor, release);
}

/*
 * Parse command line information.
 *      argc, argv: the usual command line argument count and strings
 *
 * Return: Void
 *    exit EXIT_FAILURE if error is encountered.
 */
void
TestParseCmdLine(int argc, char *argv[])
{
    hbool_t skipped_all = FALSE;
    int     ret_code;

    while ((void)argv++, --argc > 0) {
        if ((HDstrcmp(*argv, "-verbose") == 0) || (HDstrcmp(*argv, "-v") == 0)) {
            if (argc > 0) {
                --argc;
                ++argv;
                ParseTestVerbosity(*argv);
            }
            else {
                TestUsage();
                exit(EXIT_FAILURE);
            }
        }
        else if (((HDstrcmp(*argv, "-exclude") == 0) || (HDstrcmp(*argv, "-x") == 0))) {
            if (argc > 0) {
                --argc;
                ++argv;
                SetTest(*argv, SKIPTEST);
            }
            else {
                TestUsage();
                exit(EXIT_FAILURE);
            }
        }
        else if (((HDstrcmp(*argv, "-begin") == 0) || (HDstrcmp(*argv, "-b") == 0))) {
            if (argc > 0) {
                --argc;
                ++argv;
                SetTest(*argv, BEGINTEST);
            }
            else {
                TestUsage();
                exit(EXIT_FAILURE);
            }
        }
        else if (((HDstrcmp(*argv, "-only") == 0) || (HDstrcmp(*argv, "-o") == 0))) {
            if (argc > 0) {
                unsigned Loop;

                --argc;
                ++argv;

                /* Skip all tests, then activate only one. */
                if (!skipped_all) {
                    for (Loop = 0; Loop < Index; Loop++)
                        Test[Loop].SkipFlag = 1;
                    skipped_all = TRUE;
                } /* end if */
                SetTest(*argv, ONLYTEST);
            }
            else {
                TestUsage();
                exit(EXIT_FAILURE);
            }
        }
        else if ((HDstrcmp(*argv, "-summary") == 0) || (HDstrcmp(*argv, "-s") == 0))
            Summary = 1;
        else if (HDstrcmp(*argv, "-enable-error-stack") == 0)
            enable_error_stack = 1;
        else if ((HDstrcmp(*argv, "-help") == 0) || (HDstrcmp(*argv, "-h") == 0)) {
            TestUsage();
            exit(EXIT_SUCCESS);
        }
        else if ((HDstrcmp(*argv, "-cleanoff") == 0) || (HDstrcmp(*argv, "-c") == 0)) {
            SetTestNoCleanup();
        } else if ((strcmp(*argv, "-maxthreads") == 0) || (strcmp(*argv, "-t") == 0)) {
            if (argc > 0) {
                long max_threads;

                --argc;
                ++argv;

                errno       = 0;

                if (*argv == NULL) {
                    TestUsage();
                    exit(EXIT_FAILURE);
                }

                max_threads = strtol(*argv, NULL, 10);

                if (errno != 0 || max_threads <= 0 || max_threads > (long)INT_MAX) {
                    fprintf(stderr, "invalid value (%ld) specified for maximum number of threads\n", max_threads);
                    exit(EXIT_FAILURE);
                }

                SetTestMaxNumThreads((int)max_threads);
            } else {
                TestUsage();
                exit(EXIT_FAILURE);
            }
        }
        else {
            /* non-standard option.  Break out. */
            break;
        }
    }

    /* Call extra parsing function if provided. */
    if (NULL != TestPrivateParser) {
        ret_code = TestPrivateParser(argc + 1, argv - 1);
        if (ret_code != 0)
            exit(EXIT_FAILURE);
    }
}

/*
 * Perform Tests.
 */
void
PerformTests(void)
{
    unsigned Loop;
    bool is_test_threaded = false;
    bool mt_initialized = false;
    int old_num_errs = 0;

    /* Silence compiler warnings */
    (void) mt_initialized;

    for (Loop = 0; Loop < Index; Loop++) {
        is_test_threaded = (Test[Loop].TestFrameworkFlags & ALLOW_MULTITHREAD) && (TEST_EXECUTION_THREADED);
        old_num_errs = H5_ATOMIC_LOAD(Test[Loop].NumErrors);
                        
        if (Test[Loop].SkipFlag) {
            MESSAGE(2, ("Skipping -- %s (%s) \n", Test[Loop].Description, Test[Loop].Name));
        }
        else {
            MESSAGE(2, ("Testing %s -- %s (%s) \n", (is_test_threaded ? "(Threaded)" : ""),
                Test[Loop].Description, Test[Loop].Name));
            MESSAGE(5, ("===============================================\n"));
            H5_ATOMIC_STORE(Test[Loop].NumErrors, num_errs_g);
            Test_parameters      = Test[Loop].TestParameters;
            TestAlarmOn();

            if (!is_test_threaded) {
                Test[Loop].Call();
                TestAlarmOff();
                H5_ATOMIC_STORE(Test[Loop].NumErrors, num_errs_g - old_num_errs);
                MESSAGE(5, ("===============================================\n"));
                MESSAGE(5, ("There were %d errors detected.\n\n", (int)H5_ATOMIC_LOAD(Test[Loop].NumErrors)));
            } else {
#ifndef H5_HAVE_MULTITHREAD
                if (Test[Loop].TestFrameworkFlags & ALLOW_MULTITHREAD) {
                    MESSAGE(2, ("HDF5 was not built with multi-threaded support; Skipping test\n"));
                    TestAlarmOff();
                    continue;
                }      
#else
                pthread_t *threads;
                TestThreadArgs *thread_args;
                int ret = 0;
                test_outcome_t final_results[H5_MAX_NUM_SUBTESTS];

                memset(final_results, (int) TEST_UNINIT, H5_MAX_NUM_SUBTESTS * sizeof(test_outcome_t));

                if ((threads = (pthread_t *)calloc((size_t) GetTestMaxNumThreads(), sizeof(pthread_t))) == NULL) {
                    fprintf(stderr, "Error allocating memory for threads\n");
                    exit(EXIT_FAILURE);
                }

                if ((thread_args = (TestThreadArgs *)calloc((size_t) GetTestMaxNumThreads(), sizeof(TestThreadArgs))) == NULL) {
                    fprintf(stderr, "Error allocating memory for thread arguments\n");
                    exit(EXIT_FAILURE);
                }

                if (!mt_initialized) {
                    if (H5_mt_test_global_setup() < 0) {
                        fprintf(stderr, "Error setting up global MT test info\n");
                        exit(EXIT_FAILURE);
                    }

                    mt_initialized = true;
                }

                for (int i = 0; i < GetTestMaxNumThreads(); i++) {
                        thread_args[i].ThreadIndex = i;
                        thread_args[i].Call = Test[Loop].Call;
                        thread_args[i].num_tests = 0;

                        if ((thread_args[i].test_outcomes = calloc(H5_MAX_NUM_SUBTESTS, sizeof(test_outcome_t))) == NULL) {
                            fprintf(stderr, "Error allocating memory for thread outcomes\n");
                            exit(EXIT_FAILURE);
                        }

                        memset(thread_args[i].test_outcomes, (int) TEST_UNINIT, H5_MAX_NUM_SUBTESTS * sizeof(test_outcome_t));

                        if ((thread_args[i].test_descriptions = calloc(H5_MAX_NUM_SUBTESTS, sizeof(char*))) == NULL) {
                            fprintf(stderr, "Error allocating memory for thread test descriptions\n");
                            exit(EXIT_FAILURE);
                        }

                        memset(thread_args[i].test_descriptions, 0, H5_MAX_NUM_SUBTESTS * sizeof(char*));

                        ret = pthread_create(&threads[i], NULL, ThreadTestWrapper, (void*) &thread_args[i]);

                        if (ret != 0) {
                            fprintf(stderr, "Error creating thread %d\n", i);
                            exit(EXIT_FAILURE);
                        }
                }

                for (int i = 0; i < GetTestMaxNumThreads(); i++) {
                    ret = pthread_join(threads[i], NULL);

                    if (ret != 0) {
                        fprintf(stderr, "Error joining thread %d\n", i);
                        exit(EXIT_FAILURE);
                    }

                    if (thread_args[i].num_tests == 0) {
                        fprintf(stderr, "Empty test found for thread %d\n", i);
                        exit(EXIT_FAILURE);
                    }
                }
                
                /* Verify that each thread reported the same number of subtests */
                for (int i = 0; i < GetTestMaxNumThreads(); i++) {
                    if (thread_args[i].num_tests != thread_args[0].num_tests) {
                        fprintf(stderr, "Thread %d reported %ld subtests, but thread 0 reported %ld\n", i, thread_args[i].num_tests, thread_args[0].num_tests);
                        exit(EXIT_FAILURE);
                    }
                }

                /* Aggregate results - priority order is invalid > fail > pass > skip */
                H5_ATOMIC_ADD(n_tests_run_g, thread_args[0].num_tests);

                for (size_t j = 0; j < thread_args[0].num_tests; j++) {
                    for (int i = 0; i < GetTestMaxNumThreads(); i++)
                        final_results[j] = ((final_results[j] > thread_args[i].test_outcomes[j]) ? final_results[j] : thread_args[i].test_outcomes[j]);

                    /* Display subtest description, if result is from subtest */
                    if (thread_args[0].test_descriptions[j] != NULL)
                        TESTING_2_DISPLAY(thread_args[0].test_descriptions[j]);

                    switch (final_results[j]) {
                        case TEST_PASS:
                            PASSED_DISPLAY();
                            H5_ATOMIC_ADD(n_tests_passed_g, 1);
                            break;
                        case TEST_FAIL:
                            H5_FAILED_DISPLAY();
                            H5_ATOMIC_ADD(n_tests_failed_g, 1);
                            /* TBD - Neither multi-threaded nor single-threaded API tests increment the testframe error count.
                            * This would deal with the multi-threaded case, but the single-threaded case is trickier. */
                            /* H5_ATOMIC_ADD(num_errs_g, 1); */
                            break;
                        case TEST_SKIP:
                            SKIPPED_DISPLAY();
                            H5_ATOMIC_ADD(n_tests_skipped_g, 1);
                            break;
                        case TEST_UNINIT:
                            ERROR_DISPLAY();
                            exit(EXIT_FAILURE);
                            break;
                        case TEST_INVALID:
                        default:
                            ERROR_DISPLAY();
                            exit(EXIT_FAILURE);
                            break;
                    }
                }

                for (int i = 0; i < GetTestMaxNumThreads(); i++) {
                    free(thread_args[i].test_outcomes);
                    free(thread_args[i].test_descriptions);
                    thread_args[i].test_outcomes = NULL;
                    thread_args[i].test_descriptions = NULL;
                }

                free(threads);
                free(thread_args);
                
                threads = NULL;
                thread_args = NULL;

                TestAlarmOff();

                H5_ATOMIC_STORE(Test[Loop].NumErrors, num_errs_g - old_num_errs);
                MESSAGE(5, ("===============================================\n"));
                MESSAGE(5, ("There were %d errors detected.\n\n", (int)H5_ATOMIC_LOAD(Test[Loop].NumErrors)));
#endif /* H5_HAVE_MULTITHREAD */
            }
        }
    }

    Test_parameters = NULL; /* clear it. */

    if (num_errs_g)
        print_func("!!! %d Error(s) were detected !!!\n\n", (int)num_errs_g);
    else
        MESSAGE(VERBO_NONE, ("All tests were successful. \n\n"));
}

#ifdef H5_HAVE_MULTITHREAD
/*
 * Set up and execute a test flagged for threaded
 *   execution within a single thread.
 */
void *ThreadTestWrapper(void *test)
{
    TestCall test_call;
    int thread_idx;
    thread_info_t *tinfo = NULL;
    TestThreadArgs *test_args = (TestThreadArgs *)test;

    assert(test);

    thread_idx = test_args->ThreadIndex;
    test_call = test_args->Call;
    
    if (H5_mt_test_thread_setup((int)thread_idx) < 0) {
        fprintf(stderr, "Error setting up thread-local test info");
        return (void*)-1;
    }

    test_call();

    if ((tinfo = pthread_getspecific(test_thread_info_key_g)) == NULL) {
        memset(test_args->test_outcomes, (int) TEST_INVALID, H5_MAX_NUM_SUBTESTS * sizeof(test_outcome_t));
        memset(test_args->test_descriptions, 0, H5_MAX_NUM_SUBTESTS * sizeof(char*));
        test_args->num_tests = 0;
    } else {
        memcpy(test_args->test_outcomes, tinfo->test_outcomes, H5_MAX_NUM_SUBTESTS * sizeof(test_outcome_t));
        memcpy(test_args->test_descriptions, tinfo->test_descriptions, H5_MAX_NUM_SUBTESTS * sizeof(char*));
        test_args->num_tests = tinfo->num_tests;
    }    
    
    return NULL;
}

/* Set up any thread-local variables for individual API tests.
 * Must be run from each individual thread in multi-thread scenarios. */
int H5_mt_test_thread_setup(int thread_idx) {
    thread_info_t *tinfo = NULL;

    if (NULL == (tinfo = (thread_info_t *)calloc(1, sizeof(thread_info_t)))) {
        TestErrPrintf("    couldn't allocate memory for thread-specific data\n");
        goto error;
    }

    tinfo->thread_idx = thread_idx;
    tinfo->num_tests = 0;

    /* TBD: This is currently only useful for API tests. Modification of existing testframe tests would be necessary
     * for them to use thread-local filenames to avoid conflicts during multi-threaded execution */
    if (NULL == (tinfo->test_thread_filename = generate_threadlocal_filename(test_path_prefix, thread_idx, TEST_FILE_NAME))) {
        TestErrPrintf("    couldn't allocate memory for test file name\n");
        goto error;
    }

    if ((tinfo->test_outcomes = (test_outcome_t *)calloc(H5_MAX_NUM_SUBTESTS, sizeof(test_outcome_t))) == NULL) {
        TestErrPrintf("    couldn't allocate memory for test outcomes\n");
        goto error;
    }

    if ((tinfo->test_descriptions = (const char **)calloc(H5_MAX_NUM_SUBTESTS, sizeof(char*))) == NULL) {
        TestErrPrintf("    couldn't allocate memory for test descriptions\n");
        goto error;
    }

    if (pthread_setspecific(test_thread_info_key_g, (void *) tinfo) != 0) {
        TestErrPrintf("    couldn't set thread-specific data\n");
        goto error;
    }

    return 0;

error:
    free(tinfo->test_thread_filename);
    free(tinfo->test_outcomes);
    free(tinfo->test_descriptions);
    free(tinfo);
    return -1;
}

/* Destructor for the API-test managed threadlocal value */
void H5_test_thread_info_key_destructor(void *value) {
    thread_info_t *tinfo = (thread_info_t *)value;

    if (tinfo) {
        free(tinfo->test_thread_filename);
        free(tinfo->test_outcomes);
        free(tinfo->test_descriptions);
    }
    
    free(tinfo);

    return;
}

#endif
/*
 * Display test summary.
 */
void
TestSummary(void)
{
    unsigned Loop;

    print_func("Summary of Test Results:\n");
    print_func("Name of Test     Errors Description of Test\n");
    print_func("---------------- ------ --------------------------------------\n");

    for (Loop = 0; Loop < Index; Loop++) {
        if (H5_ATOMIC_LOAD(Test[Loop].NumErrors) == -1)
            print_func("%16s %6s %s\n", Test[Loop].Name, "N/A", Test[Loop].Description);
        else
            print_func("%16s %6d %s\n", Test[Loop].Name, (int)H5_ATOMIC_LOAD(Test[Loop].NumErrors), Test[Loop].Description);
    }

    print_func("\n\n");
}

/*
 * Cleanup files from testing
 */
void
TestCleanup(void)
{
    unsigned Loop;

    MESSAGE(2, ("\nCleaning Up temp files...\n\n"));

    /* call individual cleanup routines in each source module */
    for (Loop = 0; Loop < Index; Loop++)
        if (!Test[Loop].SkipFlag && Test[Loop].Cleanup != NULL)
            Test[Loop].Cleanup();
}

/*
 * Shutdown the test infrastructure
 */
void
TestShutdown(void)
{
    if (Test)
        free(Test);
}

/*
 * Retrieve the verbosity level for the testing framework
 */
H5_ATTR_PURE int
GetTestVerbosity(void)
{
    return (TestVerbosity);
}

/*
 * Set the verbosity level for the testing framework.
 * Return previous verbosity level.
 */
int
SetTestVerbosity(int newval)
{
    int oldval;

    oldval        = TestVerbosity;
    TestVerbosity = newval;
    return (oldval);
}

/*
 * Retrieve the TestExpress mode for the testing framework
 Values:
 0: Exhaustive run
    Tests should take as long as necessary
 1: Full run.  Default if H5_TEST_EXPRESS_LEVEL_DEFAULT
    and HDF5TestExpress are not defined
    Tests should take no more than 30 minutes
 2: Quick run
    Tests should take no more than 10 minutes
 3: Smoke test.
    Default if HDF5TestExpress is set to a value other than 0-3
    Tests should take less than 1 minute

 Design:
 If the environment variable $HDF5TestExpress is defined,
 or if a default testing level > 1 has been set via
 H5_TEST_EXPRESS_LEVEL_DEFAULT, then test programs should
 skip some tests so that they
 complete sooner.

 Terms:
 A "test" is a single executable, even if it contains multiple
 sub-tests.
 The standard system for test times is a Linux machine running in
 NFS space (to catch tests that involve a great deal of disk I/O).

 Implementation:
 I think this can be easily implemented in the test library (libh5test.a)
 so that all tests can just call it to check the status of $HDF5TestExpress.
 */
int
GetTestExpress(void)
{
    char *env_val;

    /* set it here for now.  Should be done in something like h5test_init(). */
    if (TestExpress == -1) {
        int express_val = 1;

        /* Check if a default test express level is defined (e.g., by build system) */
#ifdef H5_TEST_EXPRESS_LEVEL_DEFAULT
        express_val = H5_TEST_EXPRESS_LEVEL_DEFAULT;
#endif

        /* Check if HDF5TestExpress is set to override the default level */
        env_val = HDgetenv("HDF5TestExpress");
        if (env_val) {
            if (HDstrcmp(env_val, "0") == 0)
                express_val = 0;
            else if (HDstrcmp(env_val, "1") == 0)
                express_val = 1;
            else if (HDstrcmp(env_val, "2") == 0)
                express_val = 2;
            else
                express_val = 3;
        }

        SetTestExpress(express_val);
    }

    return (TestExpress);
}

/*
 * Set the TestExpress mode for the testing framework.
 * Return previous TestExpress mode.
 * Values: non-zero means TestExpress mode is on, 0 means off.
 */
int
SetTestExpress(int newval)
{
    int oldval;

    oldval      = TestExpress;
    TestExpress = newval;
    return (oldval);
}

/*
 * Retrieve Summary request value.
 *     0 means no summary, 1 means yes.
 */
H5_ATTR_PURE int
GetTestSummary(void)
{
    return (Summary);
}

/*
 * Retrieve Cleanup request value.
 *     0 means no Cleanup, 1 means yes.
 */
H5_ATTR_PURE int
GetTestCleanup(void)
{
    return (CleanUp);
}

/*
 * Set cleanup to no.
 * Return previous cleanup value.
 */
int
SetTestNoCleanup(void)
{
    int oldval;

    oldval  = CleanUp;
    CleanUp = 0;
    return (oldval);
}

/*
 * Parse an argument string for verbosity level and set it.
 */
void
ParseTestVerbosity(char *argv)
{
    if (*argv == 'l')
        SetTestVerbosity(VERBO_LO);
    else if (*argv == 'm')
        SetTestVerbosity(VERBO_MED);
    else if (*argv == 'h')
        SetTestVerbosity(VERBO_HI);
    else
        SetTestVerbosity(atoi(argv));
}

/*
 * Retrieve the number of testing errors for the testing framework
 */
H5_ATTR_PURE int
GetTestNumErrs(void)
{
    return (num_errs_g);
}

/*
 * Increment the number of testing errors
 */
void
IncTestNumErrs(void)
{
    H5_ATOMIC_ADD(num_errs_g, 1);
}

/*
 * Retrieve the current Test Parameters pointer.
 */
H5_ATTR_PURE const void *
GetTestParameters(void)
{
    return (Test_parameters);
}

/*
 * This routine is designed to provide equivalent functionality to 'printf'
 * and also increment the error count for the testing framework.
 */
int
TestErrPrintf(const char *format, ...)
{
    va_list arglist;
    int     ret_value;

    /* Increment the error count */
    H5_ATOMIC_ADD(num_errs_g, 1);

    /* Print the requested information */
    va_start(arglist, format);
    ret_value = HDvprintf(format, arglist);
    va_end(arglist);

    /* Return the length of the string produced (like printf() does) */
    return ret_value;
}

/*
 * Set (control) which test will be tested.
 * SKIPTEST: skip this test
 * ONLYTEST: do only this test
 * BEGINETEST: skip all tests before this test
 *
 */
void
SetTest(const char *testname, int action)
{
    unsigned Loop;

    switch (action) {
        case SKIPTEST:
            for (Loop = 0; Loop < Index; Loop++)
                if (HDstrcmp(testname, Test[Loop].Name) == 0) {
                    Test[Loop].SkipFlag = 1;
                    break;
                }
            break;
        case BEGINTEST:
            for (Loop = 0; Loop < Index; Loop++) {
                if (HDstrcmp(testname, Test[Loop].Name) != 0)
                    Test[Loop].SkipFlag = 1;
                else {
                    /* Found it. Set it to run.  Done. */
                    Test[Loop].SkipFlag = 0;
                    break;
                }
            }
            break;
        case ONLYTEST:
            for (Loop = 0; Loop < Index; Loop++) {
                if (HDstrcmp(testname, Test[Loop].Name) == 0) {
                    /* Found it. Set it to run. Break to skip the rest. */
                    Test[Loop].SkipFlag = 0;
                    break;
                }
            }
            break;
        default:
            /* error */
            printf("*** ERROR: Unknown action (%d) for SetTest\n", action);
            break;
    }
}

#ifdef H5_HAVE_MULTITHREAD
/* Set up global variables used for API tests */
int H5_mt_test_global_setup(void) {

    /* Set up pthread key */
    if (pthread_key_create(&test_thread_info_key_g, H5_test_thread_info_key_destructor) != 0) {
        fprintf(stderr, "Error creating threadlocal key\n");
        goto error;
    }

    return 0;
error:
    return -1;
}
#endif

/* Enable a test timer that will kill long-running tests, the time is configurable
 * via an environment variable.
 *
 * Only useful on POSIX systems where alarm(2) is present.
 */
void
TestAlarmOn(void)
{
#ifdef H5_HAVE_ALARM
    char         *env_val   = HDgetenv("HDF5_ALARM_SECONDS"); /* Alarm environment */
    unsigned long alarm_sec = H5_ALARM_SEC;                   /* Number of seconds before alarm goes off */

    /* Get the alarm value from the environment variable, if set */
    if (env_val != NULL)
        alarm_sec = (unsigned)strtoul(env_val, (char **)NULL, 10);

    /* Set the number of seconds before alarm goes off */
    alarm((unsigned)alarm_sec);
#endif
}

/* Disable the test timer */
void
TestAlarmOff(void)
{
#ifdef H5_HAVE_ALARM
    /* Set the number of seconds to zero */
    alarm(0);
#endif
}

/*
 * Returns the value set for the maximum number of threads that a test
 * program can spawn in addition to the main thread.
 */
H5_ATTR_PURE int
GetTestMaxNumThreads(void)
{
    return TestMaxNumThreads_g;
}

/*
 * Set the value for the maximum number of threads that a test program
 * can spawn in addition to the main thread.
 */
void
SetTestMaxNumThreads(int max_num_threads)
{
    TestMaxNumThreads_g = max_num_threads;
    return;
}