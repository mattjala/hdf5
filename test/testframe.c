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
 * Purpose: Implements a basic testing framework for HDF5 tests to use.
 */

#include "testframe.h"
#include "h5test.h"

/* Default number of threads to reserve space for */
#define DEFAULT_MAX_NUM_THREADS 32

/* Whether or not the tests are configured to execute using threaded
 * infrastructure. Note that if GetTestMaxNumThreads() == 1, then the
 * tests are still only run in a single thread, but that thread is a
 * new thread spawned by the main thread.
 */
#define TEST_EXECUTION_THREADED (GetTestMaxNumThreads() >= 1)

/* Column width in characters when printing out test results */
#define TEST_RESULTS_COLUMN_WIDTH 100

/*
 * Definitions for the testing structure.
 */
typedef struct TestStruct {
    char           Name[MAXTESTNAME];
    char           Description[MAXTESTDESC];
    herr_t       (*TestFunc)(TestParams_t *);
    herr_t       (*TestSetupFunc)(TestParams_t *);
    herr_t       (*TestCleanupFunc)(TestParams_t *);
    void         (*HeaderFunc)(TestParams_t *);
    TestParams_t   TestParameters;
    H5_ATOMIC(int) TestNumErrors;
    int            TestSkipFlag;
    uint64_t       TestFlags;
} TestStruct;

#ifdef H5_HAVE_MULTITHREAD

/*
 * Arguments passed to threads spawned by the testing
 * framework when executing multi-threaded tests.
 */
typedef struct TestThreadArgs_t {
    TestStruct   *Test;
    TestParams_t *ThreadLocalParams;
    herr_t        TestRet;
} TestThreadArgs_t;

#endif /* H5_HAVE_MULTITHREAD */

/*
 * Global variables used by testing framework.
 */

static TestStruct *TestArray = NULL; /* Array of tests */
static unsigned    TestAlloc = 0;    /* Size of the Test array */
static unsigned    TestCount = 0;    /* Number of tests currently added to test array */

static const char *TestProgName                              = NULL;
static void (*TestPrivateUsage_g)(FILE *stream)              = NULL;
static herr_t (*TestPrivateParser_g)(int argc, char *argv[]) = NULL;
static herr_t (*TestCleanupFunc_g)(void)                     = NULL;

static H5_ATOMIC(int) TestNumErrs_g        = 0;    /* Total number of errors that occurred for whole test program */
static bool           TestEnableErrorStack = true; /* Whether to show error stacks from the library */

static int TestMaxNumThreads_g = -1; /* Max number of threads that can be spawned */

static bool TestDoSummary_g = false; /* Show test summary. Default is no. */
static bool TestDoCleanUp_g = true;  /* Do cleanup or not. Default is yes. */

static int TestFrameworkProcessID_g = 0;         /* MPI process rank value for parallel tests */
static int TestVerbosity_g          = VERBO_DEF; /* Default Verbosity is Low */

static uint64_t TestFrameworkFlags_g = 0; /* Flags that testing framework was initialized with */

/* Track the number of tests that ran, passed, failed or were skipped */
static size_t TestsExecuted_g = 0;
static size_t TestsPassed_g   = 0;
static size_t TestsFailed_g   = 0;
static size_t TestsSkipped_g  = 0;

#ifdef H5_HAVE_MULTITHREAD

static herr_t PerformThreadedTest(TestStruct *threaded_test, int num_threads, pthread_t *threads,
                                  herr_t *test_ret);
static void  *ThreadTestWrapper(void *test);

#endif /* H5_HAVE_MULTITHREAD */

/*
 * Add a new test to the list of tests to be executed
 */
herr_t
AddTest(const char *TestName, herr_t (*TestFunc)(TestParams_t *), herr_t (*TestSetupFunc)(TestParams_t *),
        herr_t (*TestCleanupFunc)(TestParams_t *), const void *TestData, size_t TestDataSize,
        uint64_t TestFlags, const char *TestDescr)
{
    void *new_test_data = NULL;

    if (*TestName == '\0') {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: empty string given for test name\n", __func__);
        return FAIL;
    }
    if (strlen(TestName) >= MAXTESTNAME) {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: test name ('%s') too long, increase MAXTESTNAME(%d).\n", __func__, TestName,
                    MAXTESTNAME);
        return FAIL;
    }
    if (strlen(TestDescr) >= MAXTESTDESC) {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: test description ('%s') too long, increase MAXTESTDESC(%d).\n", __func__,
                    TestDescr, MAXTESTDESC);
        return FAIL;
    }
    if ((TestData && (0 == TestDataSize)) || (!TestData && (0 != TestDataSize))) {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: invalid test data size (%zu)\n", __func__, TestDataSize);
        return FAIL;
    }

    /* Re-allocate test array if necessary */
    if (TestCount >= TestAlloc) {
        TestStruct *newTest  = TestArray;
        unsigned    newAlloc = MAX(1, TestAlloc * 2);

        if (NULL == (newTest = realloc(TestArray, newAlloc * sizeof(TestStruct)))) {
            if (TestFrameworkProcessID_g == 0)
                fprintf(stderr,
                        "%s: couldn't reallocate test array, TestCount = %u, TestAlloc = %u, newAlloc = %u\n",
                        __func__, TestCount, TestAlloc, newAlloc);
            return FAIL;
        }

        TestArray = newTest;
        TestAlloc = newAlloc;
    }

    /* If the test name begins with '-', skip the test by default */
    if (*TestName == '-') {
        TestArray[TestCount].TestSkipFlag = 1;
        TestName++;
    }
    else
        TestArray[TestCount].TestSkipFlag = 0;

    strcpy(TestArray[TestCount].Name, TestName);
    strcpy(TestArray[TestCount].Description, TestDescr);

    /*
     * Initialize testing parameters for the test
     */
    memset(&TestArray[TestCount].TestParameters, 0, sizeof(TestArray[TestCount].TestParameters));

    /* Make a copy of the additional test data given */
    if (TestData) {
        if (NULL == (new_test_data = malloc(TestDataSize))) {
            if (TestFrameworkProcessID_g == 0)
                fprintf(stderr, "%s: couldn't allocate space for additional test data\n", __func__);
            return FAIL;
        }

        memcpy(new_test_data, TestData, TestDataSize);
    }
    TestArray[TestCount].TestParameters.TestParams     = new_test_data;
    TestArray[TestCount].TestParameters.TestParamsSize = TestDataSize;

    TestArray[TestCount].TestFunc        = TestFunc;
    TestArray[TestCount].TestSetupFunc   = TestSetupFunc;
    TestArray[TestCount].TestCleanupFunc = TestCleanupFunc;
    TestArray[TestCount].HeaderFunc      = NULL;

    TestArray[TestCount].TestFlags = TestFlags;

    H5_ATOMIC_STORE(TestArray[TestCount].TestNumErrors, 0);

    TestCount++;

    return SUCCEED;
}

/*
 * Add a header function to a test that will run before the test
 */
herr_t
AddTestHeaderFunc(const char *TestName, void (*HeaderFunc)(TestParams_t *))
{
    bool test_found = false;

    if (*TestName == '\0') {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: empty string given for test name\n", __func__);
        return FAIL;
    }
    if (!HeaderFunc) {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: no function specified\n", __func__);
        return FAIL;
    }

    for (unsigned Loop = 0; Loop < TestCount; Loop++) {
        if (0 == strcmp(TestName, TestArray[Loop].Name)) {
            test_found = true;
            TestArray[Loop].HeaderFunc = HeaderFunc;
            break;
        }
    }

    if (!test_found) {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: no test found by name '%s'\n", __func__, TestName);
        return FAIL;
    }

    return SUCCEED;
}

/*
 * Initialize the testing framework
 */
herr_t
TestInit(const char *ProgName, void (*TestPrivateUsage)(FILE *stream),
         herr_t (*TestPrivateParser)(int argc, char *argv[]), herr_t (*TestSetupFunc)(void),
         herr_t (*TestCleanupFunc)(void), uint64_t TestFrameworkFlags, int TestProcessID)
{
    char *env_var = NULL;

    /* Turn off automatic error reporting if requested */
    if (!TestEnableErrorStack) {
        if (H5Eset_auto2(H5E_DEFAULT, NULL, NULL) < 0) {
            if (TestFrameworkProcessID_g == 0)
                fprintf(stderr, "%s: can't disable error stack\n", __func__);
            return FAIL;
        }
    }

    /* Initialize value for TestExpress functionality */
    h5_get_testexpress();

    /* Record the program name and private routines if provided. */
    TestProgName = ProgName;
    if (NULL != TestPrivateUsage)
        TestPrivateUsage_g = TestPrivateUsage;
    if (NULL != TestPrivateParser)
        TestPrivateParser_g = TestPrivateParser;
    TestCleanupFunc_g = TestCleanupFunc;

    TestFrameworkFlags_g = TestFrameworkFlags;

    /* Set process ID for later use */
    TestFrameworkProcessID_g = TestProcessID;

    /* Get maximum number of test threads from environment, if set */
    if ((env_var = getenv(HDF5_TEST_MAX_NUM_THREADS))) {
        long max_threads;

        errno = 0;
        max_threads = strtol(env_var, NULL, 10);

        if (errno != 0) {
            if (TestFrameworkProcessID_g == 0)
                fprintf(stderr,"error while parsing value (%s) specified for maximum number of threads\n", env_var);
            return FAIL;
        }
        if (max_threads > (long)INT_MAX) {
            if (TestFrameworkProcessID_g == 0)
                fprintf(stderr, "value (%ld) specified for maximum number of threads too large\n",
                        max_threads);
            return FAIL;
        }

        SetTestMaxNumThreads((int)max_threads);
    }

    /* Set/reset global variables that may be used by
     * tests integrated with the testing framework
     */
    TestsExecuted_g = 0;
    TestsPassed_g   = 0;
    TestsFailed_g   = 0;
    TestsSkipped_g  = 0;

    /* Call test framework setup callback if provided */
    if (TestSetupFunc && TestSetupFunc() < 0) {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: error occurred in test framework initialization callback\n", __func__);
        return FAIL;
    }

    return SUCCEED;
}

/*
 * Print out test program usage help text
 */
void
TestUsage(FILE *stream)
{
    size_t max_test_name_len = 0;

    /* If running in parallel, only print output from a single MPI process */
    if (TestFrameworkProcessID_g != 0)
        return;

    if (!stream)
        stream = stdout;

    fprintf(stream, "Usage: %s [-v[erbose] (l[ow]|m[edium]|h[igh]|0-9)] %s\n", TestProgName,
            (TestPrivateUsage_g ? "<extra options>" : ""));
    fprintf(stream, "              [-[e]x[clude] name]+ \n");
    fprintf(stream, "              [-o[nly] name]+ \n");
    fprintf(stream, "              [-b[egin] name] \n");
    fprintf(stream, "              [-[max]t[hreads]]  \n");
    fprintf(stream, "              [-[testex]p[ress] (0-3)]  \n");
    fprintf(stream, "              [-s[ummary]]  \n");
    fprintf(stream, "              [-c[leanoff]]  \n");
    fprintf(stream, "              [-h[elp]]  \n");
    fprintf(stream, "\n\n");
    fprintf(stream, "verbose      controls the amount of information displayed\n");
    fprintf(stream, "exclude      to exclude tests by name\n");
    fprintf(stream, "only         to name tests which should be run\n");
    fprintf(stream, "begin        start at the name of the test given\n");
    fprintf(stream, "maxthreads   maximum number of threads to be used by multi-thread tests\n");
    fprintf(stream, "testexpress  set the TestExpress level for expediting tests\n");
    fprintf(stream, "               lower values run more tests; higher values skip more tests\n");
    fprintf(stream, "summary      prints a summary of test results at the end\n");
    fprintf(stream, "cleanoff     does not delete *.hdf files after execution of tests\n");
    fprintf(stream, "help         print out this information\n");
    if (TestPrivateUsage_g) {
        fprintf(stream, "\nExtra options\n");
        TestPrivateUsage_g(stream);
    }
    fprintf(stream, "\n\n");

    /* Collect some information for cleaner printing */
    for (unsigned Loop = 0; Loop < TestCount; Loop++) {
        size_t test_name_len = strlen(TestArray[Loop].Name);

        if (test_name_len > max_test_name_len)
            max_test_name_len = test_name_len;
    }

    fprintf(stream, "This program currently tests the following: \n\n");
    fprintf(stream, "%*s %s\n", (int)max_test_name_len, "Name", " Description");
    fprintf(stream, "%*s %s\n", (int)max_test_name_len, "----", " -----------");

    for (unsigned i = 0; i < TestCount; i++)
        fprintf(stream, "%*s  %s\n", (int)max_test_name_len, TestArray[i].Name, TestArray[i].Description);

    fprintf(stream, "\n\n");
}

/*
 * Print out miscellaneous test program information
 */
void
TestInfo(FILE *stream)
{
    unsigned major, minor, release;

    /* If running in parallel, only print output from a single MPI process */
    if (TestFrameworkProcessID_g != 0)
        return;

    if (!stream)
        stream = stdout;

    H5get_libversion(&major, &minor, &release);

    fprintf(stream, "\nFor help use: %s -help\n", TestProgName);
    fprintf(stream, "Linked with hdf5 version %u.%u release %u\n", major, minor, release);
}

/*
 * Parse command line information
 */
herr_t
TestParseCmdLine(int argc, char *argv[])
{
    herr_t ret_value = SUCCEED;

    while ((void)argv++, --argc > 0) {
        if ((strcmp(*argv, "-verbose") == 0) || (strcmp(*argv, "-v") == 0)) {
            if (argc > 0) {
                --argc;
                ++argv;

                if (ParseTestVerbosity(*argv) < 0) {
                    ret_value = FAIL;
                    goto done;
                }
            }
            else {
                ret_value = FAIL;
                goto done;
            }
        }
        else if (((strcmp(*argv, "-exclude") == 0) || (strcmp(*argv, "-x") == 0))) {
            if (argc > 0) {
                --argc;
                ++argv;

                if (SetTest(*argv, SKIPTEST) < 0) {
                    ret_value = FAIL;
                    goto done;
                }
            }
            else {
                ret_value = FAIL;
                goto done;
            }
        }
        else if (((strcmp(*argv, "-begin") == 0) || (strcmp(*argv, "-b") == 0))) {
            if (argc > 0) {
                --argc;
                ++argv;

                if (SetTest(*argv, BEGINTEST) < 0) {
                    ret_value = FAIL;
                    goto done;
                }
            }
            else {
                ret_value = FAIL;
                goto done;
            }
        }
        else if (((strcmp(*argv, "-only") == 0) || (strcmp(*argv, "-o") == 0))) {
            if (argc > 0) {
                --argc;
                ++argv;

                if (SetTest(*argv, ONLYTEST) < 0) {
                    ret_value = FAIL;
                    goto done;
                }
            }
            else {
                ret_value = FAIL;
                goto done;
            }
        }
        else if ((strcmp(*argv, "-summary") == 0) || (strcmp(*argv, "-s") == 0))
            TestDoSummary_g = true;
        else if (strcmp(*argv, "-disable-error-stack") == 0) {
            TestEnableErrorStack = false;
        }
        else if ((strcmp(*argv, "-help") == 0) || (strcmp(*argv, "-h") == 0)) {
            TestUsage(stdout);
            exit(EXIT_SUCCESS);
        }
        else if ((strcmp(*argv, "-cleanoff") == 0) || (strcmp(*argv, "-c") == 0)) {
            SetTestNoCleanup();
        }
        else if ((strcmp(*argv, "-maxthreads") == 0) || (strcmp(*argv, "-t") == 0)) {
            if (argc > 0) {
                long max_threads;

                --argc;
                ++argv;

                if (*argv == NULL) {
                    TestUsage(stdout);
                    ret_value = FAIL;
                    goto done;
                }

                errno       = 0;
                max_threads = strtol(*argv, NULL, 10);

                if (errno != 0) {
                    if (TestFrameworkProcessID_g == 0)
                        fprintf(stderr,
                                "error while parsing value (%s) specified for maximum number of threads\n",
                                *argv);
                    ret_value = FAIL;
                    goto done;
                }
                if (max_threads > (long)INT_MAX) {
                    if (TestFrameworkProcessID_g == 0)
                        fprintf(stderr, "value (%ld) specified for maximum number of threads too large\n",
                                max_threads);
                    ret_value = FAIL;
                    goto done;
                }

                SetTestMaxNumThreads((int)max_threads);
            }
            else {
                TestUsage(stdout);
                ret_value = FAIL;
                goto done;
            }
        }
        else if ((strcmp(*argv, "-testexpress") == 0) || (strcmp(*argv, "-p") == 0)) {
            long test_express_level;

            if (argc <= 0 || !argv[1]) {
                if (TestFrameworkProcessID_g == 0)
                    fprintf(stderr, "no argument given to -testexpress option\n");
                ret_value = FAIL;
                goto done;
            }

            --argc;
            ++argv;

            errno              = 0;
            test_express_level = strtol(*argv, NULL, 10);
            if (errno != 0) {
                if (TestFrameworkProcessID_g == 0)
                    fprintf(stderr, "error while parsing value (%s) specified for TestExpress level\n",
                            *argv);
                ret_value = FAIL;
                goto done;
            }
            if (test_express_level < 0) {
                if (TestFrameworkProcessID_g == 0)
                    fprintf(stderr, "invalid value (%ld) specified for TestExpress level\n",
                            test_express_level);
                ret_value = FAIL;
                goto done;
            }

            /* Clamp value to current highest TestExpress level */
            if (test_express_level > H5_TEST_EXPRESS_SMOKE_TEST)
                test_express_level = H5_TEST_EXPRESS_SMOKE_TEST;

            SetTestExpress((int)test_express_level);
        }
        else {
            /* non-standard option.  Break out. */
            break;
        }
    }

    /* Call extra parsing function if provided. */
    if (NULL != TestPrivateParser_g) {
        if (TestPrivateParser_g(argc + 1, argv - 1) < 0) {
            ret_value = FAIL;
            goto done;
        }
    }

done:
    if (ret_value < 0)
        TestUsage(stderr);

    return ret_value;
}

/*
 * Execute all tests that aren't being skipped
 */
herr_t
PerformTests(void)
{
#ifdef H5_HAVE_MULTITHREAD
    pthread_t *TestThreads = NULL;
    int        num_threads = 0;
#endif
    herr_t ret_value = SUCCEED;

    if (GetTestExpress() > H5_TEST_EXPRESS_EXHAUSTIVE)
        MESSAGE(0, ("** TestExpress level is %d. Some tests may be expedited.\n", GetTestExpress()));

    /* Enable alarm timer for tests */
    if (TestAlarmOn() < 0)
        MESSAGE(5, ("Couldn't enable test alarm timer\n"));

#ifdef H5_HAVE_MULTITHREAD
    /* Setup for potential threaded tests */
    if (TestFrameworkFlags_g & H5_MULTITHREAD_TEST) {
        num_threads = GetTestMaxNumThreads();
        if (num_threads < 0)
            num_threads = DEFAULT_MAX_NUM_THREADS;

        if (NULL == (TestThreads = malloc((size_t)num_threads * sizeof(*TestThreads)))) {
            MESSAGE(2, ("Couldn't allocate space for test threads\n"));
            ret_value = FAIL;
            goto done;
        }
        for (int thread_idx = 0; thread_idx < num_threads; thread_idx++)
            TestThreads[thread_idx] = pthread_self();
    }
#endif

    for (unsigned Loop = 0; Loop < TestCount; Loop++) {
        int    curr_num_errors;
        bool   is_test_mt = (TestFrameworkFlags_g & H5_MULTITHREAD_TEST) &&
                            (TestArray[Loop].TestFlags & ALLOW_MULTITHREAD) &&
                            TEST_EXECUTION_THREADED;
        herr_t test_ret   = SUCCEED;

        /* If test has a header function to call, do so now */
        if (TestArray[Loop].HeaderFunc)
            TestArray[Loop].HeaderFunc(&TestArray[Loop].TestParameters);

        if (TestArray[Loop].TestSkipFlag) {
            MESSAGE(2, ("Skipping -- %s (%s) \n", TestArray[Loop].Description, TestArray[Loop].Name));
            TestsSkipped_g++;
            continue;
        }

#ifndef H5_HAVE_MULTITHREAD
        if (is_test_mt) {
            MESSAGE(2, ("HDF5 was not built with multi-threaded support; Skipping test %s (%s)\n",
                    TestArray[Loop].Name, TestArray[Loop].Description));
            TestsSkipped_g++;
            continue;
        }
#endif

        TestsExecuted_g++;

        /* Print header with test description for default verbosity level. Add
         * in test name for higher verbosity level.
         */
        MESSAGE(2, ("Testing %s-- %s ", (is_test_mt ? "(Multi-threaded) " : ""),
                TestArray[Loop].Description));
        MESSAGE(4, ("(%s) ", TestArray[Loop].Name));
        MESSAGE(2, ("\n"));
        MESSAGE(5, ("===============================================\n"));

        curr_num_errors = H5_ATOMIC_LOAD(TestNumErrs_g);

        if (!is_test_mt) {
            herr_t setup_ret   = SUCCEED;
            herr_t cleanup_ret = SUCCEED;

            if (TestArray[Loop].TestSetupFunc) {
                setup_ret = TestArray[Loop].TestSetupFunc(&TestArray[Loop].TestParameters);
                if (setup_ret < 0)
                    MESSAGE(2, ("Test setup failed for test %s (%s)\n", TestArray[Loop].Name,
                            TestArray[Loop].Description));
            }

            if (setup_ret >= 0)
                test_ret = TestArray[Loop].TestFunc(&TestArray[Loop].TestParameters);

            if (TestArray[Loop].TestCleanupFunc) {
                cleanup_ret = TestArray[Loop].TestCleanupFunc(&TestArray[Loop].TestParameters);
                if (cleanup_ret < 0)
                    MESSAGE(2, ("Test cleanup failed for test %s (%s)\n", TestArray[Loop].Name,
                            TestArray[Loop].Description));
            }

            if (setup_ret < 0 || cleanup_ret < 0)
                test_ret = FAIL;
        }
#ifdef H5_HAVE_MULTITHREAD
        else {
            if (PerformThreadedTest(&TestArray[Loop], num_threads, TestThreads, &test_ret) < 0)
                MESSAGE(7, ("Error occurred while executing threaded test\n"));
        }
#endif

        H5_ATOMIC_STORE(TestArray[Loop].TestNumErrors, TestNumErrs_g - curr_num_errors);

        MESSAGE(5, ("===============================================\n"));

        /* Capture test results and record statistics */
        switch (test_ret) {
            case SUCCEED:
                if (H5_ATOMIC_LOAD(TestArray[Loop].TestNumErrors) == 0)
                    TestsPassed_g++;
                else {
                    TestsFailed_g++;
                    test_ret = FAIL;
                }
                break;
            case FAIL:
                TestsFailed_g++;
                /* If test didn't increment error count, store 1 error for it */
                if (H5_ATOMIC_LOAD(TestArray[Loop].TestNumErrors) == 0)
                    H5_ATOMIC_STORE(TestArray[Loop].TestNumErrors, 1);
                break;
            case SKIP:
                TestsSkipped_g++;
                break;
            default:
                TestsFailed_g++;
                /* If test didn't increment error count, store 1 error for it */
                if (H5_ATOMIC_LOAD(TestArray[Loop].TestNumErrors) == 0)
                    H5_ATOMIC_STORE(TestArray[Loop].TestNumErrors, 1);
                MESSAGE(2, ("Invalid return value (%d) from test %s (%s) \n",
                    test_ret, TestArray[Loop].Name, TestArray[Loop].Description));
                break;
        }

        /* Print out PASSED/FAILED/-SKIP- in a similar fashion to h5test tests,
         * but with a column width of TEST_RESULTS_COLUMN_WIDTH characters
         */
        if (VERBOSE_DEF) {
            int n_err_digits = 1;
            int n_err        = H5_ATOMIC_LOAD(TestArray[Loop].TestNumErrors);
            int str_width;

            /* No need to involve math library */
            if (n_err > 9)
                n_err_digits++;
            if (n_err > 99)
                n_err_digits++;
            if (n_err > 999)
                n_err_digits++;
            if (n_err > 9999)
                /* Something is majorly wrong at this point,
                 * so don't worry about the formatting too much.
                 */
                n_err_digits++;

            /* remaining string width (with blank space) =
             * TEST_RESULTS_COLUMN_WIDTH character width -
             * strlen(leading message) -
             * #digits in error count
             */
            str_width = TEST_RESULTS_COLUMN_WIDTH - 28 - n_err_digits;

            MESSAGE(2, ("There were %d errors detected.", H5_ATOMIC_LOAD(TestArray[Loop].TestNumErrors)));

            switch (test_ret) {
                case SUCCEED:
                    MESSAGE(2, ("%*s\n\n", str_width, "PASSED"));
                    break;
                case FAIL:
                    MESSAGE(2, ("%*s\n\n", str_width, "FAILED"));
                    TestsFailed_g++;
                    break;
                case SKIP:
                    MESSAGE(2, ("%*s\n\n", str_width, "-SKIP-"));
                    TestsSkipped_g++;
                    break;
                default:
                    MESSAGE(2, ("%*s\n\n", str_width, "*ERROR*"));
                    break;
            }
        }
    }

    /* If tests failed but didn't record any errors, make sure
     * we exit with at least one error to signal that tests
     * failed for programs which rely on the value of
     * GetTestNumErrs().
     */
    if (TestsFailed_g > 0 && GetTestNumErrs() <= 0)
        H5_ATOMIC_STORE(TestNumErrs_g, (int)TestsFailed_g);

    TestAlarmOff();

    MESSAGE(VERBO_NONE, ("\n\n"));
    if (H5_ATOMIC_LOAD(TestNumErrs_g))
        MESSAGE(VERBO_NONE, ("!!! %d Error(s) were detected !!!\n\n", H5_ATOMIC_LOAD(TestNumErrs_g)));
    else
        MESSAGE(VERBO_NONE, ("All tests were successful. \n\n"));

done:
#ifdef H5_HAVE_MULTITHREAD
    if (TestFrameworkFlags_g & H5_MULTITHREAD_TEST)
        free(TestThreads);
#endif

    return ret_value;
}

#ifdef H5_HAVE_MULTITHREAD

static herr_t
PerformThreadedTest(TestStruct *threaded_test, int num_threads, pthread_t *threads,
                    herr_t *test_ret)
{
    struct ThreadPrivData_t *thread_priv  = NULL;
    TestThreadArgs_t        *thread_args  = NULL;
    size_t                   min_subtests = 0;
    bool                     thread_fail  = false;
    bool                     all_skip     = true;
    int                      ret          = 0;
    herr_t                   ret_value    = SUCCEED;

    if (NULL == (thread_args = calloc((size_t)num_threads, sizeof(*thread_args)))) {
        TestErrPrintf("** error allocating memory for thread arguments array **\n");
        ret_value = FAIL;
        goto done;
    }

    /* TODO: allow running tests from N to max_num_threads, possibly with a flag */
    for (int thread_idx = 0; thread_idx < num_threads; thread_idx++) {
        TestThreadArgs_t *args_ptr = &thread_args[thread_idx];

        args_ptr->Test    = threaded_test;
        args_ptr->TestRet = SUCCEED;

        /* Make a thread-local copy of the testing parameters for each thread */
        if (NULL == (args_ptr->ThreadLocalParams = calloc(1, sizeof(*args_ptr->ThreadLocalParams)))) {
            TestErrPrintf("** error allocating memory for thread-local test parameters **\n");
            ret_value = FAIL;
            goto done;
        }
        *args_ptr->ThreadLocalParams = threaded_test->TestParameters;

        /* Allocate private thread data for later use by testing framework */
        if (NULL == (thread_priv = calloc(1, sizeof(struct ThreadPrivData_t)))) {
            TestErrPrintf("** error allocating memory for thread-local private data **\n");
            ret_value = FAIL;
            goto done;
        }
        args_ptr->ThreadLocalParams->MtTestParams.ThreadPrivData = thread_priv;
        thread_priv = NULL;

        args_ptr->ThreadLocalParams->IsMtTest              = true;
        args_ptr->ThreadLocalParams->MtTestParams.ThreadID = thread_idx;

        ret = pthread_create(&threads[thread_idx], NULL, ThreadTestWrapper, args_ptr);
        if (ret != 0) {
            TestErrPrintf("** error creating thread %d: ret %d **\n", thread_idx, ret);
            ret_value = FAIL;
            goto done;
        }
    }

    for (int thread_idx = 0; thread_idx < num_threads; thread_idx++) {
        ret = pthread_join(threads[thread_idx], NULL);
        threads[thread_idx] = pthread_self();

        if (ret != 0) {
            TestErrPrintf("** error joining thread %d: ret %d **\n", thread_idx, ret);
            ret_value = FAIL;
            goto done;
        }
    }

    /* Aggregate test results from threads */
    for (int thread_idx = 0; thread_idx < num_threads; thread_idx++) {
        struct ThreadPrivData_t *priv_data;
        size_t                   thread_err_cnt;

        priv_data      = thread_args[thread_idx].ThreadLocalParams->MtTestParams.ThreadPrivData;
        thread_err_cnt = thread_args[thread_idx].ThreadLocalParams->MtTestParams.ThreadErrCnt;

        if (thread_args[thread_idx].TestRet < 0 || thread_err_cnt > 0)
            thread_fail = true;

        if (thread_args[thread_idx].TestRet != SKIP)
            all_skip = false;

        if (thread_args[thread_idx].TestRet != SUCCEED &&
            thread_args[thread_idx].TestRet != FAIL &&
            thread_args[thread_idx].TestRet != SKIP)
            MESSAGE(2, ("** invalid return value (%d) from thread %d for test %s (%s) \n",
                thread_args[thread_idx].TestRet, thread_idx, threaded_test->Name,
                threaded_test->Description));

        if (thread_idx == 0)
            min_subtests = priv_data->subtest_count;
        else
            min_subtests = MIN(min_subtests, priv_data->subtest_count);
    }

    /* Ensure value is within range */
    min_subtests = MIN(min_subtests, TESTFRAME_MAX_NUM_SUBTESTS);

    /* Print delayed sub-test headers, if any, up to the minimum number ran among all threads */
    if (!all_skip && min_subtests > 0) {
        for (size_t subtest_idx = 0; subtest_idx < min_subtests; subtest_idx++) {
            struct ThreadPrivData_t *priv_data;

            priv_data = thread_args[0].ThreadLocalParams->MtTestParams.ThreadPrivData;
            if (priv_data->subtest_descriptions[subtest_idx])
                SUBTEST_BANNER(priv_data->subtest_descriptions[subtest_idx]);
        }
    }

    if (thread_fail) {
        TestErrPrintf("** errors occurred in one or more threads **\n");
        ret_value = FAIL;
        goto done;
    }

    if (all_skip)
        *test_ret = SKIP;
    else
        *test_ret = SUCCEED;

done:
    if (ret_value < 0)
        *test_ret = FAIL;

    /* Join any unjoined threads on error */
    for (int thread_idx = 0; thread_idx < num_threads; thread_idx++) {
        if (pthread_equal(threads[thread_idx], pthread_self()))
            continue;

        ret = pthread_join(threads[thread_idx], NULL);
        threads[thread_idx] = pthread_self();

        if (0 != ret)
            MESSAGE(2, ("** couldn't join thread %d: ret %d **\n", thread_idx, ret));
    }

    if (thread_args) {
        for (int thread_idx = 0; thread_idx < num_threads; thread_idx++) {
            if (thread_args[thread_idx].ThreadLocalParams) {
                if (thread_args[thread_idx].TestRet < 0 ||
                        thread_args[thread_idx].ThreadLocalParams->MtTestParams.ThreadErrCnt != 0)
                    MESSAGE(2, ("Error message from thread %d: %s\n", thread_idx,
                            thread_args[thread_idx].ThreadLocalParams->MtTestParams.ThreadErrMsg));

                free(thread_args[thread_idx].ThreadLocalParams->MtTestParams.ThreadPrivData);
            }
            free(thread_args[thread_idx].ThreadLocalParams);
        }
    }

    free(thread_priv);
    free(thread_args);

    return ret_value;
}

/*
 * Set up and execute a test flagged for threaded
 * execution within a single thread.
 */
static void *
ThreadTestWrapper(void *test)
{
    TestThreadArgs_t *test_args   = (TestThreadArgs_t *)test;
    herr_t            test_ret    = SUCCEED;
    herr_t            setup_ret   = SUCCEED;
    herr_t            cleanup_ret = SUCCEED;

    /* This setup/cleanup pattern requires that each
     * thread that delegates threading to the test framework
     * must not have any form of "shared" setup or cleanup.
     *
     * This is usually accomplished by having thread-specific filenames.
     *
     * If a test requires shared setup/cleanup, then the test must
     * handle its own threading internally
     */
    if (test_args->Test->TestSetupFunc)
        setup_ret = test_args->Test->TestSetupFunc(test_args->ThreadLocalParams);

    if (setup_ret >= 0)
        test_ret = test_args->Test->TestFunc(test_args->ThreadLocalParams);

    if (test_args->Test->TestCleanupFunc)
        cleanup_ret = test_args->Test->TestCleanupFunc(test_args->ThreadLocalParams);

    if (setup_ret < 0 || cleanup_ret < 0)
        test_ret = FAIL;

    test_args->TestRet = test_ret;

    return NULL;
}

#endif /* H5_HAVE_MULTITHREAD */

/*
 * Display a summary of running tests
 */
void
TestSummary(FILE *stream)
{
    size_t max_test_name_len    = 0;
    size_t max_test_desc_len    = 0;
    size_t test_name_header_len = 0;
    size_t test_desc_header_len = 0;

    /* If running in parallel, only print output from a single MPI process */
    if (TestFrameworkProcessID_g != 0)
        return;

    if (!stream)
        stream = stdout;

    /* Collect some information for cleaner printing */
    for (unsigned Loop = 0; Loop < TestCount; Loop++) {
        size_t test_name_len = strlen(TestArray[Loop].Name);
        size_t test_desc_len = strlen(TestArray[Loop].Description);

        if (test_name_len > max_test_name_len)
            max_test_name_len = test_name_len;
        if (test_desc_len > max_test_desc_len)
            max_test_desc_len = test_desc_len;
    }

    test_name_header_len = MAX(max_test_name_len, strlen("Name of Test"));
    test_desc_header_len = MAX(max_test_desc_len, strlen("Description of Test"));

    /* Print out test statistics */
    if (GetTestsExecutedCount() > 0) {
        size_t n_tests_passed  = GetTestsPassedCount();
        size_t n_tests_failed  = GetTestsFailedCount();
        size_t n_tests_skipped = GetTestsSkippedCount();

        fprintf(stream, "%zu/%zu (%.2f%%) tests passed\n",
                n_tests_passed, (size_t)TestCount,
                ((double)n_tests_passed / (double)TestCount * 100.0));
        fprintf(stream, "%zu/%zu (%.2f%%) tests did not pass\n",
                n_tests_failed, (size_t)TestCount,
                ((double)n_tests_failed / (double)TestCount * 100.0));
        fprintf(stream, "%zu/%zu (%.2f%%) tests were skipped\n",
                n_tests_skipped, (size_t)TestCount,
                ((double)n_tests_skipped / (double)TestCount * 100.0));
        fputs("\n", stream);
    }

    /* Print header, adjusted to maximum test name and description lengths */
    fprintf(stream, "Summary of Test Results:\n");
    fprintf(stream, "%-*s  Errors  %-*s\n", (int)test_name_header_len, "Name of Test",
            (int)test_desc_header_len, "Description of Test");

    /* Print a separating line row for each column header, adjusted to maximum
     * test name and description lengths
     */
    for (size_t i = 0; i < test_name_header_len; i++) /* 'Name of Test' */
        putc('-', stream);
    putc(' ', stream);
    putc(' ', stream);
    for (size_t i = 0; i < 6; i++) /* 'Errors' */
        putc('-', stream);
    putc(' ', stream);
    putc(' ', stream);
    for (size_t i = 0; i < test_desc_header_len; i++) /* 'Description of Test' */
        putc('-', stream);
    putc('\n', stream);

    for (unsigned Loop = 0; Loop < TestCount; Loop++) {
        if (TestArray[Loop].TestSkipFlag)
            fprintf(stream, "%-*s  %-6s  %-*s\n", (int)test_name_header_len, TestArray[Loop].Name, "N/A",
                    (int)test_desc_header_len, TestArray[Loop].Description);
        else
            fprintf(stream, "%-*s  %-6d  %-*s\n", (int)test_name_header_len, TestArray[Loop].Name,
                    H5_ATOMIC_LOAD(TestArray[Loop].TestNumErrors), (int)test_desc_header_len,
                    TestArray[Loop].Description);
    }

    fprintf(stream, "\n\n");
}

/*
 * Shutdown the test infrastructure
 */
herr_t
TestShutdown(void)
{
    /* Clean up test state first before tearing down testing framework */
    if (TestCleanupFunc_g && TestCleanupFunc_g() < 0) {
        if (TestFrameworkProcessID_g == 0)
            fprintf(stderr, "%s: error occurred in test framework shutdown callback\n", __func__);
        return FAIL;
    }

    if (TestArray)
        for (unsigned Loop = 0; Loop < TestCount; Loop++) {
            free(TestArray[Loop].TestParameters.TestParams);
            TestArray[Loop].TestParameters.TestParams = NULL;
        }

    free(TestArray);

    return SUCCEED;
}

/*
 * Retrieve the MPI rank for this process.
 */
H5_ATTR_PURE int
GetTestFrameworkProcessID(void)
{
    return TestFrameworkProcessID_g;
}

/*
 * Retrieve the verbosity level for the testing framework
 */
H5_ATTR_PURE int
GetTestVerbosity(void)
{
    return TestVerbosity_g;
}

/*
 * Set the verbosity level for the testing framework
 */
int
SetTestVerbosity(int newval)
{
    int oldval;

    if (newval < 0)
        newval = VERBO_NONE;
    else if (newval > VERBO_HI)
        newval = VERBO_HI;

    oldval          = TestVerbosity_g;
    TestVerbosity_g = newval;

    return oldval;
}

/*
 * Retrieve the TestExpress mode for the testing framework
 */
int
GetTestExpress(void)
{
    return h5_get_testexpress();
}

/*
 * Set the TestExpress mode for the testing framework.
 */
void
SetTestExpress(int newval)
{
    h5_set_testexpress(newval);
}

/*
 * Retrieve test summary request value.
 */
H5_ATTR_PURE bool
GetTestSummary(void)
{
    return TestDoSummary_g;
}

/*
 * Retrieve test file cleanup status value
 */
H5_ATTR_PURE bool
GetTestCleanup(void)
{
    /* Don't cleanup files if the HDF5_NOCLEANUP environment
     * variable is defined to anything
     */
    if (getenv(HDF5_NOCLEANUP))
        SetTestNoCleanup();

    return TestDoCleanUp_g;
}

/*
 * Set test file cleanup status to "don't clean up temporary files"
 */
void
SetTestNoCleanup(void)
{
    TestDoCleanUp_g = false;
}

/*
 * Parse an argument string for verbosity level and set it.
 */
herr_t
ParseTestVerbosity(char *argv)
{
    if (*argv == 'l')
        SetTestVerbosity(VERBO_LO);
    else if (*argv == 'm')
        SetTestVerbosity(VERBO_MED);
    else if (*argv == 'h')
        SetTestVerbosity(VERBO_HI);
    else {
        long verb_level;

        errno      = 0;
        verb_level = strtol(argv, NULL, 10);
        if (errno != 0) {
            if (TestFrameworkProcessID_g == 0)
                fprintf(stderr, "%s: error while parsing value (%s) specified for test verbosity\n", __func__,
                        argv);
            return FAIL;
        }

        if (verb_level < 0)
            verb_level = VERBO_DEF;
        else if (verb_level > VERBO_HI)
            verb_level = VERBO_HI;

        SetTestVerbosity((int)verb_level);
    }

    return SUCCEED;
}

/*
 * Retrieve the number of testing errors for the testing framework
 */
H5_ATTR_PURE int
GetTestNumErrs(void)
{
    return TestNumErrs_g;
}

/*
 * Increment the number of testing errors
 */
void
IncTestNumErrs(void)
{
    H5_ATOMIC_ADD(TestNumErrs_g, 1);
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
    IncTestNumErrs();

    /* Print the requested information */
    va_start(arglist, format);
    ret_value = vfprintf(stderr, format, arglist);
    va_end(arglist);

    /* Return the length of the string produced (like printf() does) */
    return ret_value;
}

/*
 * Change testing behavior in relation to a specific test
 */
herr_t
SetTest(const char *testname, int action)
{
    static bool skipped_all = false;

    switch (action) {
        case SKIPTEST:
            for (unsigned Loop = 0; Loop < TestCount; Loop++)
                if (strcmp(testname, TestArray[Loop].Name) == 0) {
                    TestArray[Loop].TestSkipFlag = 1;
                    break;
                }
            break;
        case BEGINTEST:
            for (unsigned Loop = 0; Loop < TestCount; Loop++) {
                if (strcmp(testname, TestArray[Loop].Name) != 0)
                    TestArray[Loop].TestSkipFlag = 1;
                else {
                    /* Found it. Set it to run.  Done. */
                    TestArray[Loop].TestSkipFlag = 0;
                    break;
                }
            }
            break;
        case ONLYTEST:
            /* Skip all tests, then keep track that we did that.
             * Some testing prefers the convenience of being
             * able to specify multiple tests to "only" run
             * rather than specifying (possibly many more) tests
             * to exclude, but we only want to skip all the
             * tests a single time to facilitate this.
             */
            if (!skipped_all) {
                for (unsigned Loop = 0; Loop < TestCount; Loop++)
                    TestArray[Loop].TestSkipFlag = 1;
                skipped_all = true;
            }

            for (unsigned Loop = 0; Loop < TestCount; Loop++) {
                if (strcmp(testname, TestArray[Loop].Name) == 0) {
                    /* Found it. Set it to run. Break to skip the rest. */
                    TestArray[Loop].TestSkipFlag = 0;
                    break;
                }
            }
            break;
        default:
            /* error */
            if (TestFrameworkProcessID_g == 0)
                fprintf(stderr, "%s: invalid action %d specified\n", __func__, action);
            return FAIL;
    }

    return SUCCEED;
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
herr_t
SetTestMaxNumThreads(int max_num_threads)
{
    TestMaxNumThreads_g = max_num_threads;

    return SUCCEED;
}

/*
 * Get the number of tests that were executed
 */
H5_ATTR_PURE size_t
GetTestsExecutedCount(void)
{
    return TestsExecuted_g;
}

/*
 * Get the number of tests that passed
 */
H5_ATTR_PURE size_t
GetTestsPassedCount(void)
{
    return TestsPassed_g;
}

/*
 * Get the number of tests that failed
 */
H5_ATTR_PURE size_t
GetTestsFailedCount(void)
{
    return TestsFailed_g;
}

/*
 * Get the number of tests that were skipped
 */
H5_ATTR_PURE size_t
GetTestsSkippedCount(void)
{
    return TestsSkipped_g;
}

/*
 * Determine whether current process/thread should be allowed
 * to print output from the testing framework or from a test
 */
H5_ATTR_PURE bool
IsTestOutputPrinter(TestParams_t *TestParams)
{
    /*
     * If parallel/multi-threading are involved, test framework
     * output should only come from the "main" thread on MPI
     * rank 0.
     */
    return (GetTestFrameworkProcessID() == 0 &&
            (!TestParams->IsMtTest || TestParams->MtTestParams.ThreadID == 0));
}

/* Enable a test timer that will kill long-running tests, the time is configurable
 * via an environment variable.
 *
 * Only useful on POSIX systems where alarm(2) is present.
 */
herr_t
TestAlarmOn(void)
{
    /* A TestExpress setting of H5_TEST_EXPRESS_EXHAUSTIVE should allow
     * tests to run for as long as necessary, so avoid enabling an
     * alarm-style timer here that would, by default, kill the test.
     */
    if (GetTestExpress() == H5_TEST_EXPRESS_EXHAUSTIVE)
        return SUCCEED;
#ifdef H5_HAVE_ALARM
    else {
        char         *env_val   = getenv("HDF5_ALARM_SECONDS"); /* Alarm environment */
        unsigned long alarm_sec = H5_ALARM_SEC;                 /* Number of seconds before alarm goes off */

        /* Get the alarm value from the environment variable, if set */
        if (env_val != NULL) {
            errno     = 0;
            alarm_sec = strtoul(env_val, NULL, 10);
            if (errno != 0) {
                if (TestFrameworkProcessID_g == 0)
                    fprintf(stderr, "%s: error while parsing value (%s) specified for alarm timeout\n",
                            __func__, env_val);
                return FAIL;
            }
            else if (alarm_sec > (unsigned long)UINT_MAX) {
                if (TestFrameworkProcessID_g == 0)
                    fprintf(stderr, "%s: value (%lu) specified for alarm timeout too large\n", __func__,
                            alarm_sec);
                return FAIL;
            }
        }

        /* Set the number of seconds before alarm goes off */
        alarm((unsigned)alarm_sec);
    }
#endif

    return SUCCEED;
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
