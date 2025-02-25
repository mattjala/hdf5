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

#include "H5_api_group_test.h"

static herr_t print_group_test_header(TestParams_t *params);
static herr_t test_create_group_under_root(TestParams_t *params);
static herr_t test_create_group_under_existing_group(TestParams_t *params);
static herr_t test_create_many_groups(TestParams_t *params);
static herr_t test_create_deep_groups(TestParams_t *params);
static herr_t test_create_intermediate_group(TestParams_t *params);
static herr_t test_create_group_invalid_params(TestParams_t *params);
static herr_t test_create_anonymous_group(TestParams_t *params);
static herr_t test_create_anonymous_group_invalid_params(TestParams_t *params);
static herr_t test_open_nonexistent_group(TestParams_t *params);
static herr_t test_open_group_invalid_params(TestParams_t *params);
static herr_t test_close_group_invalid_id(TestParams_t *params);
static herr_t test_group_property_lists(TestParams_t *params);
static herr_t test_get_group_info(TestParams_t *params);
static herr_t test_get_group_info_invalid_params(TestParams_t *params);
static herr_t test_flush_group(TestParams_t *params);
static herr_t test_flush_group_invalid_params(TestParams_t *params);
static herr_t test_refresh_group(TestParams_t *params);
static herr_t test_refresh_group_invalid_params(TestParams_t *params);
static int  create_group_recursive(TestParams_t *params, hid_t parent_gid, unsigned counter);

static herr_t
print_group_test_header(TestParams_t H5_ATTR_UNUSED *params)
{
    printf("\n");
    printf("**********************************************\n");
    printf("*                                            *\n");
    printf("*              API Group Tests               *\n");
    printf("*                                            *\n");
    printf("**********************************************\n\n");

    return SUCCEED;
}

/*
 * A test to check that a group can be created under the root group.
 */
static herr_t
test_create_group_under_root(TestParams_t *params)
{
    hid_t file_id    = H5I_INVALID_HID;
    hid_t parent_gid = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    /* Create the group under the root group of the file */
    if ((parent_gid =
             H5Gcreate2(file_id, GROUP_CREATE_UNDER_ROOT_GNAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        printf("    couldn't create group '%s'\n", GROUP_CREATE_UNDER_ROOT_GNAME);
        goto error;
    }

    if (H5Gclose(parent_gid) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(parent_gid);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that a group can be created under an existing
 * group which is not the root group.
 */
static herr_t
test_create_group_under_existing_group(TestParams_t *params)
{
    hid_t file_id         = H5I_INVALID_HID;
    hid_t parent_group_id = H5I_INVALID_HID, child_group_id = H5I_INVALID_HID,
          grandchild_group_id = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file\n");
        goto error;
    }

    /* Open the already-existing group (/group_tests) in the file as the parent */
    if ((parent_group_id = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open group\n");
        goto error;
    }

    /* Create a new group (/group_tests/child_group) under the already-existing parent Group using a relative
     * path */
    if ((child_group_id = H5Gcreate2(parent_group_id, GROUP_CREATE_UNDER_GROUP_REL_GNAME, H5P_DEFAULT,
                                     H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        printf("    couldn't create group using relative path: %s\n", GROUP_CREATE_UNDER_GROUP_REL_GNAME);
        goto error;
    }

    /* Create a new group (child_group/grandchild_group) under the already-existing parent Group using an
     * absolute path */
    if ((grandchild_group_id = H5Gcreate2(parent_group_id, GROUP_CREATE_UNDER_GROUP_ABS_GNAME, H5P_DEFAULT,
                                          H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        printf("    couldn't create group using absolute path: %s\n", GROUP_CREATE_UNDER_GROUP_ABS_GNAME);
        goto error;
    }

    if (H5Gclose(grandchild_group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(child_group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(parent_group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(grandchild_group_id);
        H5Gclose(child_group_id);
        H5Gclose(parent_group_id);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to create many (one million) groups
 */
static herr_t
test_create_many_groups(TestParams_t *params)
{
    hid_t    file_id         = H5I_INVALID_HID;
    hid_t    container_group = H5I_INVALID_HID;
    hid_t    parent_group_id = H5I_INVALID_HID, child_group_id = H5I_INVALID_HID;
    char     group_name[NAME_BUF_SIZE];
    unsigned i;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((parent_group_id = H5Gcreate2(container_group, MANY_GROUP_CREATIONS_GNAME, H5P_DEFAULT, H5P_DEFAULT,
                                      H5P_DEFAULT)) < 0) {
        printf("    couldn't create group '%s'\n", MANY_GROUP_CREATIONS_GNAME);
        goto error;
    }

    /* Create multiple groups under the parent group */
    if (IsTestOutputPrinter(params))
        printf("\n");
    for (i = 0; i < GROUP_NUMB_MANY; i++) {
        if (IsTestOutputPrinter(params))
            printf("\r %u/%u", i + 1, GROUP_NUMB_MANY);
        snprintf(group_name, sizeof(group_name), "group %02u", i);

        if ((child_group_id =
                 H5Gcreate2(parent_group_id, group_name, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            printf("    couldn't create group '%s'\n", group_name);
            goto error;
        }

        if (H5Gclose(child_group_id) < 0)
            TESTFRAME_TEST_ERROR(params);
    }
    if (IsTestOutputPrinter(params))
        printf("\n");

    if (H5Gclose(parent_group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(child_group_id);
        H5Gclose(parent_group_id);
        H5Gclose(container_group);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to create groups of the depth GROUP_DEPTH.
 */
static herr_t
test_create_deep_groups(TestParams_t *params)
{
    hid_t file_id         = H5I_INVALID_HID;
    hid_t container_group = H5I_INVALID_HID;
    hid_t group_id        = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open container group\n");
        goto error;
    }

    /* Create the group under the root group of the file */
    if ((group_id = H5Gcreate2(container_group, DEEP_GROUP_CREATIONS_GNAME, H5P_DEFAULT, H5P_DEFAULT,
                               H5P_DEFAULT)) < 0) {
        printf("    couldn't create group '%s'\n", DEEP_GROUP_CREATIONS_GNAME);
        goto error;
    }

    if (IsTestOutputPrinter(params))
        printf("\n");
    if (create_group_recursive(params, group_id, 1) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (IsTestOutputPrinter(params))
        printf("\n");

    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * Recursive function to create groups of the depth GROUP_DEPTH.
 */
static int
create_group_recursive(TestParams_t *params, hid_t parent_gid, unsigned counter)
{
    hid_t child_gid = H5I_INVALID_HID;
    char  gname[NAME_BUF_SIZE];

    if (IsTestOutputPrinter(params))
        printf("\r %u/%u", counter, GROUP_DEPTH);
    if (counter == 1)
        snprintf(gname, sizeof(gname), "2nd_child_group");
    else if (counter == 2)
        snprintf(gname, sizeof(gname), "3rd_child_group");
    else
        snprintf(gname, sizeof(gname), "%dth_child_group", counter + 1);
    if ((child_gid = H5Gcreate2(parent_gid, gname, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        printf("    couldn't create group '%s'\n", gname);
        goto error;
    }

    if (counter < GROUP_DEPTH) {
        if (create_group_recursive(params, child_gid, counter + 1) < 0)
            TESTFRAME_TEST_ERROR(params);
    }

    if (H5Gclose(child_gid) < 0)
        TESTFRAME_TEST_ERROR(params);

    return 0;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(child_gid);
    }
    H5E_END_TRY

    return 1;
}

/*
 * A test to create groups automatically using H5Pset_create_intermediate_group
 */
static herr_t
test_create_intermediate_group(TestParams_t *params)
{
    hid_t file_id           = H5I_INVALID_HID;
    hid_t container_group   = H5I_INVALID_HID;
    hid_t group_id          = H5I_INVALID_HID;
    hid_t crt_intmd_lcpl_id = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open container group\n");
        goto error;
    }

    /* Set up plist for creating intermediate groups */
    if ((crt_intmd_lcpl_id = H5Pcreate(H5P_LINK_CREATE)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Pset_create_intermediate_group(crt_intmd_lcpl_id, true) < 0)
        TESTFRAME_TEST_ERROR(params);

    /* Create an intermediate group using a relative path */
    if ((group_id = H5Gcreate2(container_group,
                               GROUP_CREATE_INTMD_REL_INTMD_NAME "/" GROUP_CREATE_INTMD_REL_END_NAME,
                               crt_intmd_lcpl_id, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;

    /* Verify both groups were created */
    if ((group_id =
             H5Gopen2(file_id, GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_REL_INTMD_NAME, H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;
    if ((group_id = H5Gopen2(file_id,
                             GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_REL_INTMD_NAME
                                                   "/" GROUP_CREATE_INTMD_REL_END_NAME,
                             H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;

    /* Create an intermediate group using an absolute path */
    if ((group_id = H5Gcreate2(container_group,
                               "/" GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_ABS_INTMD_NAME
                               "/" GROUP_CREATE_INTMD_ABS_END_NAME,
                               crt_intmd_lcpl_id, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;

    /* Verify both groups were created */
    if ((group_id =
             H5Gopen2(file_id, GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_ABS_INTMD_NAME, H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;
    if ((group_id = H5Gopen2(file_id,
                             GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_ABS_INTMD_NAME
                                                   "/" GROUP_CREATE_INTMD_ABS_END_NAME,
                             H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;

    /* Create two intermediate groups using an absolute path */
    if ((group_id = H5Gcreate2(container_group,
                               "/" GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_MULT_INTMD1_NAME
                               "/" GROUP_CREATE_INTMD_MULT_INTMD2_NAME "/" GROUP_CREATE_INTMD_MULT_END_NAME,
                               crt_intmd_lcpl_id, H5P_DEFAULT, H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;

    /* Verify all three groups were created */
    if ((group_id = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_MULT_INTMD1_NAME,
                             H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;
    if ((group_id = H5Gopen2(file_id,
                             GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_MULT_INTMD1_NAME
                                                   "/" GROUP_CREATE_INTMD_MULT_INTMD2_NAME,
                             H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;
    if ((group_id = H5Gopen2(file_id,
                             GROUP_TEST_GROUP_NAME "/" GROUP_CREATE_INTMD_MULT_INTMD1_NAME
                                                   "/" GROUP_CREATE_INTMD_MULT_INTMD2_NAME
                                                   "/" GROUP_CREATE_INTMD_MULT_END_NAME,
                             H5P_DEFAULT)) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    group_id = H5I_INVALID_HID;

    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Pclose(crt_intmd_lcpl_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Fclose(file_id);
        H5Pclose(crt_intmd_lcpl_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that a group can't be created when H5Gcreate
 * is passed invalid parameters.
 */
static herr_t
test_create_group_invalid_params(TestParams_t *params)
{
    hid_t file_id  = H5I_INVALID_HID;
    hid_t group_id = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    SUBTEST_BEGIN(params, "H5Gcreate with an invalid loc_id")
    {
        H5E_BEGIN_TRY
        {
            group_id = H5Gcreate2(H5I_INVALID_HID, GROUP_CREATE_INVALID_PARAMS_GROUP_NAME, H5P_DEFAULT,
                                  H5P_DEFAULT, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    created group with invalid loc_id!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gcreate with an invalid group name")
    {
        H5E_BEGIN_TRY
        {
            group_id = H5Gcreate2(file_id, NULL, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    created group with a NULL name!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }

        H5E_BEGIN_TRY
        {
            group_id = H5Gcreate2(file_id, "", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    created group with an invalid group name of ''!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gcreate with an invalid LCPL")
    {
        H5E_BEGIN_TRY
        {
            group_id = H5Gcreate2(file_id, GROUP_CREATE_INVALID_PARAMS_GROUP_NAME, H5I_INVALID_HID,
                                  H5P_DEFAULT, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    created group with invalid LCPL!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gcreate with an invalid GCPL")
    {
        H5E_BEGIN_TRY
        {
            group_id = H5Gcreate2(file_id, GROUP_CREATE_INVALID_PARAMS_GROUP_NAME, H5P_DEFAULT,
                                  H5I_INVALID_HID, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    created group with invalid GCPL!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gcreate with an invalid GAPL")
    {
        H5E_BEGIN_TRY
        {
            group_id = H5Gcreate2(file_id, GROUP_CREATE_INVALID_PARAMS_GROUP_NAME, H5P_DEFAULT,
                                  H5P_DEFAULT, H5I_INVALID_HID);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    created group with invalid GAPL!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that an anonymous group can be created with
 * H5Gcreate_anon.
 */
static herr_t
test_create_anonymous_group(TestParams_t *params)
{
    hid_t file_id         = H5I_INVALID_HID;
    hid_t container_group = H5I_INVALID_HID, new_group_id = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open group\n");
        goto error;
    }

    if ((new_group_id = H5Gcreate_anon(file_id, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        printf("    couldn't create anonymous group\n");
        goto error;
    }

    if (H5Gclose(new_group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(new_group_id);
        H5Gclose(container_group);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that an anonymous group can't be created
 * when H5Gcreate_anon is passed invalid parameters.
 */
static herr_t
test_create_anonymous_group_invalid_params(TestParams_t *params)
{
    hid_t file_id         = H5I_INVALID_HID;
    hid_t container_group = H5I_INVALID_HID, new_group_id = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open group\n");
        goto error;
    }

    SUBTEST_BEGIN(params, "H5Gcreate_anon with an invalid loc_id")
    {
        H5E_BEGIN_TRY
        {
            new_group_id = H5Gcreate_anon(H5I_INVALID_HID, H5P_DEFAULT, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (new_group_id >= 0) {
            printf("    created anonymous group with invalid loc_id!\n");
            H5Gclose(new_group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gcreate_anon with an invalid GCPL")
    {
        H5E_BEGIN_TRY
        {
            new_group_id = H5Gcreate_anon(container_group, H5I_INVALID_HID, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (new_group_id >= 0) {
            printf("    created anonymous group with invalid GCPL!\n");
            H5Gclose(new_group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gcreate_anon with an invalid GAPL")
    {
        H5E_BEGIN_TRY
        {
            new_group_id = H5Gcreate_anon(container_group, H5P_DEFAULT, H5I_INVALID_HID);
        }
        H5E_END_TRY

        if (new_group_id >= 0) {
            printf("    created anonymous group with invalid GAPL!\n");
            H5Gclose(new_group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(new_group_id);
        H5Gclose(container_group);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that a group which doesn't exist cannot
 * be opened.
 */
static herr_t
test_open_nonexistent_group(TestParams_t *params)
{
    hid_t file_id  = H5I_INVALID_HID;
    hid_t group_id = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file\n");
        goto error;
    }

    H5E_BEGIN_TRY
    {
        group_id = H5Gopen2(file_id, OPEN_NONEXISTENT_GROUP_TEST_GNAME, H5P_DEFAULT);
    }
    H5E_END_TRY

    if (group_id >= 0) {
        printf("    opened non-existent group!\n");
        goto error;
    }

    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that a group can't be opened when H5Gopen
 * is passed invalid parameters.
 */
static herr_t
test_open_group_invalid_params(TestParams_t *params)
{
    hid_t file_id  = H5I_INVALID_HID;
    hid_t group_id = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    SUBTEST_BEGIN(params, "H5Gopen with an invalid loc_id")
    {
        H5E_BEGIN_TRY
        {
            group_id = H5Gopen2(H5I_INVALID_HID, GROUP_TEST_GROUP_NAME, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    opened group using an invalid loc_id!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gopen with an invalid group name")
    {
        H5E_BEGIN_TRY
        {
            group_id = H5Gopen2(file_id, NULL, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    opened group using a NULL name!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }

        H5E_BEGIN_TRY
        {
            group_id = H5Gopen2(file_id, "", H5P_DEFAULT);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    opened group using an invalid name of ''!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gopen with an invalid GAPL")
    {
        H5E_BEGIN_TRY
        {
            group_id = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5I_INVALID_HID);
        }
        H5E_END_TRY

        if (group_id >= 0) {
            printf("    opened group using an invalid GAPL!\n");
            H5Gclose(group_id);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that H5Gclose doesn't succeed for an
 * invalid group ID.
 */
static herr_t
test_close_group_invalid_id(TestParams_t H5_ATTR_UNUSED *params)
{
    herr_t err_ret = -1;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC)) {
        printf("    API functions for basic group aren't supported with this connector\n");
        return SKIP;
    }

    H5E_BEGIN_TRY
    {
        err_ret = H5Gclose(H5I_INVALID_HID);
    }
    H5E_END_TRY

    if (err_ret >= 0) {
        printf("    close a group with an invalid ID!\n");
        goto error;
    }

    return SUCCEED;

error:
    return FAIL;
}

/*
 * A test to check that a GCPL used for group creation can
 * be persisted and that a valid copy of that GCPL can be
 * retrieved later with a call to H5Gget_create_plist.
 */
static herr_t
test_group_property_lists(TestParams_t *params)
{
    unsigned dummy_prop_val  = GROUP_PROPERTY_LIST_TEST_DUMMY_VAL;
    hid_t    file_id         = H5I_INVALID_HID;
    hid_t    container_group = H5I_INVALID_HID;
    hid_t    group_id1 = H5I_INVALID_HID, group_id2 = H5I_INVALID_HID;
    hid_t    gcpl_id1 = H5I_INVALID_HID, gcpl_id2 = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC) ||
        !(vol_cap_flags_g & H5VL_CAP_FLAG_GET_PLIST) || !(vol_cap_flags_g & H5VL_CAP_FLAG_CREATION_ORDER)) {
        printf("    API functions for basic file, group, property list, or creation order aren't supported "
               "with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file\n");
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((gcpl_id1 = H5Pcreate(H5P_GROUP_CREATE)) < 0) {
        printf("    couldn't create GCPL\n");
        goto error;
    }

    if (H5Pset_link_creation_order(gcpl_id1, dummy_prop_val) < 0) {
        printf("    couldn't set property on GCPL\n");
        goto error;
    }

    /* Create the group in the file */
    if ((group_id1 = H5Gcreate2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME1, H5P_DEFAULT, gcpl_id1,
                                H5P_DEFAULT)) < 0) {
        printf("    couldn't create group\n");
        goto error;
    }

    /* Create the second group using H5P_DEFAULT for the GCPL */
    if ((group_id2 = H5Gcreate2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME2, H5P_DEFAULT,
                                H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        printf("    couldn't create group\n");
        goto error;
    }

    if (H5Pclose(gcpl_id1) < 0)
        TESTFRAME_TEST_ERROR(params);

    SUBTEST_BEGIN(params, "H5Gget_create_plist")
    {
        /* Try to retrieve copies of the two property lists, one which has the property set and one which
         * does not */
        if ((gcpl_id1 = H5Gget_create_plist(group_id1)) < 0) {
            printf("    couldn't get GCPL\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if ((gcpl_id2 = H5Gget_create_plist(group_id2)) < 0) {
            printf("    couldn't get GCPL\n");
            TESTFRAME_TEST_ERROR(params);
        }

        /* Ensure that property list 1 has the property set and property list 2 does not */
        dummy_prop_val = 0;

        if (H5Pget_link_creation_order(gcpl_id1, &dummy_prop_val) < 0) {
            printf("    couldn't retrieve GCPL property value\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if (dummy_prop_val != GROUP_PROPERTY_LIST_TEST_DUMMY_VAL) {
            printf("    retrieved GCPL property value '%llu' did not match expected value '%llu'\n",
                   (unsigned long long)dummy_prop_val,
                   (unsigned long long)GROUP_PROPERTY_LIST_TEST_DUMMY_VAL);
            TESTFRAME_TEST_ERROR(params);
        }

        dummy_prop_val = 0;

        if (H5Pget_link_creation_order(gcpl_id2, &dummy_prop_val) < 0) {
            printf("    couldn't retrieve GCPL property value\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if (dummy_prop_val == GROUP_PROPERTY_LIST_TEST_DUMMY_VAL) {
            printf("    retrieved GCPL property value '%llu' matched control value '%llu' when it "
                   "shouldn't have\n",
                   (unsigned long long)dummy_prop_val,
                   (unsigned long long)GROUP_PROPERTY_LIST_TEST_DUMMY_VAL);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    /* Now see if we can still retrieve copies of the property lists upon opening
     * (instead of creating) a group. If they were reconstructed properly upon file
     * open, the creation property lists should also have the same test values
     * as set before.
     */
    if (gcpl_id1 >= 0) {
        H5E_BEGIN_TRY
        {
            H5Pclose(gcpl_id1);
        }
        H5E_END_TRY
        gcpl_id1 = H5I_INVALID_HID;
    }
    if (gcpl_id2 >= 0) {
        H5E_BEGIN_TRY
        {
            H5Pclose(gcpl_id2);
        }
        H5E_END_TRY
        gcpl_id2 = H5I_INVALID_HID;
    }
    if (group_id1 >= 0) {
        H5E_BEGIN_TRY
        {
            H5Gclose(group_id1);
        }
        H5E_END_TRY
        group_id1 = H5I_INVALID_HID;
    }
    if (group_id2 >= 0) {
        H5E_BEGIN_TRY
        {
            H5Gclose(group_id2);
        }
        H5E_END_TRY
        group_id2 = H5I_INVALID_HID;
    }

    SUBTEST_BEGIN(params, "H5Gget_create_plist after re-opening a group")
    {
        if ((group_id1 = H5Gopen2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME1, H5P_DEFAULT)) <
            0) {
            printf("    couldn't open group\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if ((group_id2 = H5Gopen2(container_group, GROUP_PROPERTY_LIST_TEST_GROUP_NAME2, H5P_DEFAULT)) <
            0) {
            printf("    couldn't open group\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if ((gcpl_id1 = H5Gget_create_plist(group_id1)) < 0) {
            printf("    couldn't get property list\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if ((gcpl_id2 = H5Gget_create_plist(group_id2)) < 0) {
            printf("    couldn't get property list\n");
            TESTFRAME_TEST_ERROR(params);
        }

        /* Re-check the property values */
        dummy_prop_val = 0;

        if (H5Pget_link_creation_order(gcpl_id1, &dummy_prop_val) < 0) {
            printf("    couldn't retrieve GCPL property value\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if (dummy_prop_val != GROUP_PROPERTY_LIST_TEST_DUMMY_VAL) {
            printf("    retrieved GCPL property value '%llu' did not match expected value '%llu'\n",
                   (unsigned long long)dummy_prop_val,
                   (unsigned long long)GROUP_PROPERTY_LIST_TEST_DUMMY_VAL);
            TESTFRAME_TEST_ERROR(params);
        }

        dummy_prop_val = 0;

        if (H5Pget_link_creation_order(gcpl_id2, &dummy_prop_val) < 0) {
            printf("    couldn't retrieve GCPL property value\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if (dummy_prop_val == GROUP_PROPERTY_LIST_TEST_DUMMY_VAL) {
            printf("    retrieved GCPL property value '%llu' matched control value '%llu' when it "
                   "shouldn't have\n",
                   (unsigned long long)dummy_prop_val,
                   (unsigned long long)GROUP_PROPERTY_LIST_TEST_DUMMY_VAL);
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    if (H5Pclose(gcpl_id1) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Pclose(gcpl_id2) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id1) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(group_id2) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(gcpl_id1);
        H5Pclose(gcpl_id2);
        H5Gclose(group_id1);
        H5Gclose(group_id2);
        H5Gclose(container_group);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test for the functionality of H5Gget_info(_by_idx).
 */
static herr_t
test_get_group_info(TestParams_t *params)
{
    H5G_info_t group_info;
    unsigned   i;
    hid_t      file_id         = H5I_INVALID_HID;
    hid_t      container_group = H5I_INVALID_HID;
    hid_t      parent_group_id = H5I_INVALID_HID, group_id = H5I_INVALID_HID;
    hid_t      gcpl_id = H5I_INVALID_HID;
    char       group_name[NAME_BUF_SIZE];

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC) ||
        !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_MORE)) {
        printf("    API functions for basic file or group aren't supported with this connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((gcpl_id = H5Pcreate(H5P_GROUP_CREATE)) < 0) {
        printf("    couldn't create a GCPL\n");
        goto error;
    }

    if (vol_cap_flags_g & H5VL_CAP_FLAG_CREATION_ORDER) {
        if (H5Pset_link_creation_order(gcpl_id, H5P_CRT_ORDER_TRACKED | H5P_CRT_ORDER_INDEXED) < 0) {
            printf("    couldn't enable link creation order tracking and indexing on GCPL\n");
            goto error;
        }
    }

    if ((parent_group_id = H5Gcreate2(container_group, GROUP_GET_INFO_TEST_GROUP_NAME, H5P_DEFAULT, gcpl_id,
                                      H5P_DEFAULT)) < 0) {
        printf("    couldn't create group '%s'\n", GROUP_GET_INFO_TEST_GROUP_NAME);
        goto error;
    }

    /* Create multiple groups under the parent group */
    for (i = 0; i < GROUP_GET_INFO_TEST_GROUP_NUMB; i++) {
        /* Create the groups with a reverse-ordering naming scheme to test creation order */
        snprintf(group_name, NAME_BUF_SIZE, "group %02u", (unsigned)(GROUP_GET_INFO_TEST_GROUP_NUMB - i - 1));

        if ((group_id = H5Gcreate2(parent_group_id, group_name, H5P_DEFAULT, gcpl_id, H5P_DEFAULT)) < 0) {
            printf("    couldn't create group '%s'\n", group_name);
            goto error;
        }

        if (H5Gclose(group_id) < 0)
            TESTFRAME_TEST_ERROR(params);
    }

    SUBTEST_BEGIN(params, "retrieval of group info with H5Gget_info")
    {
        memset(&group_info, 0, sizeof(group_info));

        /* Retrieve information about the parent group */
        if (H5Gget_info(parent_group_id, &group_info) < 0) {
            printf("    couldn't get group info\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if (group_info.nlinks != GROUP_GET_INFO_TEST_GROUP_NUMB) {
            printf("    group's number of links '%lu' doesn't match expected value '%u'\n",
                   group_info.nlinks, (unsigned int)GROUP_GET_INFO_TEST_GROUP_NUMB);
            TESTFRAME_TEST_ERROR(params);
        }

        if (vol_cap_flags_g & H5VL_CAP_FLAG_CREATION_ORDER) {
            /*
             * For the purpose of this test, the max creation order should match
             * the number of links in the group.
             */
            if (group_info.max_corder != GROUP_GET_INFO_TEST_GROUP_NUMB) {
                printf("    group's max creation order '%lld' doesn't match expected value '%lld'\n",
                       (long long)group_info.max_corder, (long long)GROUP_GET_INFO_TEST_GROUP_NUMB);
                TESTFRAME_TEST_ERROR(params);
            }
        }

        /* Ensure that the storage_type field is at least set to a meaningful value */
        if (group_info.storage_type != H5G_STORAGE_TYPE_SYMBOL_TABLE &&
            group_info.storage_type != H5G_STORAGE_TYPE_COMPACT &&
            group_info.storage_type != H5G_STORAGE_TYPE_DENSE &&
            group_info.storage_type != H5G_STORAGE_TYPE_UNKNOWN) {
            printf("    group info's 'storage_type' field wasn't set to a meaningful value\n");
            TESTFRAME_TEST_ERROR(params);
        }

        /* Assume that mounted should be false in this case */
        if (group_info.mounted != false) {
            printf("    group info's 'mounted' field was true when it should have been false\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "retrieval of group info with H5Gget_info_by_name")
    {
        memset(&group_info, 0, sizeof(group_info));

        /* Retrieve information about the parent group */
        if (H5Gget_info_by_name(container_group, GROUP_GET_INFO_TEST_GROUP_NAME, &group_info,
                                H5P_DEFAULT) < 0) {
            printf("    couldn't get group info by name\n");
            TESTFRAME_TEST_ERROR(params);
        }

        if (group_info.nlinks != GROUP_GET_INFO_TEST_GROUP_NUMB) {
            printf("    group's number of links '%lu' doesn't match expected value '%u'\n",
                   group_info.nlinks, (unsigned int)GROUP_GET_INFO_TEST_GROUP_NUMB);
            TESTFRAME_TEST_ERROR(params);
        }

        if (vol_cap_flags_g & H5VL_CAP_FLAG_CREATION_ORDER) {
            /*
             * For the purpose of this test, the max creation order should match
             * the number of links in the group.
             */
            if (group_info.max_corder != GROUP_GET_INFO_TEST_GROUP_NUMB) {
                printf("    group's max creation order '%lld' doesn't match expected value '%lld'\n",
                       (long long)group_info.max_corder, (long long)GROUP_GET_INFO_TEST_GROUP_NUMB);
                TESTFRAME_TEST_ERROR(params);
            }
        }

        /* Ensure that the storage_type field is at least set to a meaningful value */
        if (group_info.storage_type != H5G_STORAGE_TYPE_SYMBOL_TABLE &&
            group_info.storage_type != H5G_STORAGE_TYPE_COMPACT &&
            group_info.storage_type != H5G_STORAGE_TYPE_DENSE &&
            group_info.storage_type != H5G_STORAGE_TYPE_UNKNOWN) {
            printf("    group info's 'storage_type' field wasn't set to a meaningful value\n");
            TESTFRAME_TEST_ERROR(params);
        }

        /* Assume that mounted should be false in this case */
        if (group_info.mounted != false) {
            printf("    group info's 'mounted' field was true when it should have been false\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx by creation order in increasing order")
    {
        if (!(vol_cap_flags_g & H5VL_CAP_FLAG_CREATION_ORDER)) {
            TESTFRAME_SKIPPED(params);
            printf("    creation order tracking isn't supported with this VOL connector\n");
        }

        for (i = 0; i < GROUP_GET_INFO_TEST_GROUP_NUMB; i++) {
            memset(&group_info, 0, sizeof(group_info));

            /* Retrieve information about each group under the parent group */
            if (H5Gget_info_by_idx(container_group, GROUP_GET_INFO_TEST_GROUP_NAME, H5_INDEX_CRT_ORDER,
                                   H5_ITER_INC, (hsize_t)i, &group_info, H5P_DEFAULT) < 0) {
                printf("    couldn't get group info for group at index %u\n", i);
                TESTFRAME_TEST_ERROR(params);
            }

            if (group_info.nlinks != 0) {
                printf("    group's number of links '%lu' doesn't match expected value '%d'\n",
                       group_info.nlinks, 0);
                TESTFRAME_TEST_ERROR(params);
            }

            if (group_info.max_corder != 0) {
                printf("    group's max creation order '%lld' doesn't match expected value '%d'\n",
                       (long long)group_info.max_corder, 0);
                TESTFRAME_TEST_ERROR(params);
            }

            /* Ensure that the storage_type field is at least set to a meaningful value */
            if (group_info.storage_type != H5G_STORAGE_TYPE_SYMBOL_TABLE &&
                group_info.storage_type != H5G_STORAGE_TYPE_COMPACT &&
                group_info.storage_type != H5G_STORAGE_TYPE_DENSE &&
                group_info.storage_type != H5G_STORAGE_TYPE_UNKNOWN) {
                printf("    group info's 'storage_type' field wasn't set to a meaningful value\n");
                TESTFRAME_TEST_ERROR(params);
            }

            /* Assume that mounted should be false in this case */
            if (group_info.mounted != false) {
                printf("    group info's 'mounted' field was true when it should have been false\n");
                TESTFRAME_TEST_ERROR(params);
            }
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx by creation order in decreasing order")
    {
        if (!(vol_cap_flags_g & H5VL_CAP_FLAG_CREATION_ORDER)) {
            TESTFRAME_SKIPPED(params);
            printf("    creation order tracking isn't supported with this VOL connector\n");
        }

        for (i = 0; i < GROUP_GET_INFO_TEST_GROUP_NUMB; i++) {
            memset(&group_info, 0, sizeof(group_info));

            /* Retrieve information about each group under the parent group */
            if (H5Gget_info_by_idx(container_group, GROUP_GET_INFO_TEST_GROUP_NAME, H5_INDEX_CRT_ORDER,
                                   H5_ITER_DEC, (hsize_t)i, &group_info, H5P_DEFAULT) < 0) {
                printf("    couldn't get group info for group at index %u\n", i);
                TESTFRAME_TEST_ERROR(params);
            }

            if (group_info.nlinks != 0) {
                printf("    group's number of links '%lu' doesn't match expected value '%d'\n",
                       group_info.nlinks, 0);
                TESTFRAME_TEST_ERROR(params);
            }

            if (group_info.max_corder != 0) {
                printf("    group's max creation order '%lld' doesn't match expected value '%d'\n",
                       (long long)group_info.max_corder, 0);
                TESTFRAME_TEST_ERROR(params);
            }

            /* Ensure that the storage_type field is at least set to a meaningful value */
            if (group_info.storage_type != H5G_STORAGE_TYPE_SYMBOL_TABLE &&
                group_info.storage_type != H5G_STORAGE_TYPE_COMPACT &&
                group_info.storage_type != H5G_STORAGE_TYPE_DENSE &&
                group_info.storage_type != H5G_STORAGE_TYPE_UNKNOWN) {
                printf("    group info's 'storage_type' field wasn't set to a meaningful value\n");
                TESTFRAME_TEST_ERROR(params);
            }

            /* Assume that mounted should be false in this case */
            if (group_info.mounted != false) {
                printf("    group info's 'mounted' field was true when it should have been false\n");
                TESTFRAME_TEST_ERROR(params);
            }
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx by alphabetical order in increasing order")
    {
        for (i = 0; i < GROUP_GET_INFO_TEST_GROUP_NUMB; i++) {
            memset(&group_info, 0, sizeof(group_info));

            /* Retrieve information about each group under the parent group */
            if (H5Gget_info_by_idx(container_group, GROUP_GET_INFO_TEST_GROUP_NAME, H5_INDEX_NAME,
                                   H5_ITER_INC, (hsize_t)i, &group_info, H5P_DEFAULT) < 0) {
                printf("    couldn't get group info for group at index %u\n", i);
                TESTFRAME_TEST_ERROR(params);
            }

            if (group_info.nlinks != 0) {
                printf("    group's number of links '%lu' doesn't match expected value '%d'\n",
                       group_info.nlinks, 0);
                TESTFRAME_TEST_ERROR(params);
            }

            if (group_info.max_corder != 0) {
                printf("    group's max creation order '%lld' doesn't match expected value '%d'\n",
                       (long long)group_info.max_corder, 0);
                TESTFRAME_TEST_ERROR(params);
            }

            /* Ensure that the storage_type field is at least set to a meaningful value */
            if (group_info.storage_type != H5G_STORAGE_TYPE_SYMBOL_TABLE &&
                group_info.storage_type != H5G_STORAGE_TYPE_COMPACT &&
                group_info.storage_type != H5G_STORAGE_TYPE_DENSE &&
                group_info.storage_type != H5G_STORAGE_TYPE_UNKNOWN) {
                printf("    group info's 'storage_type' field wasn't set to a meaningful value\n");
                TESTFRAME_TEST_ERROR(params);
            }

            /* Assume that mounted should be false in this case */
            if (group_info.mounted != false) {
                printf("    group info's 'mounted' field was true when it should have been false\n");
                TESTFRAME_TEST_ERROR(params);
            }
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx by alphabetical order in decreasing order")
    {
        for (i = 0; i < GROUP_GET_INFO_TEST_GROUP_NUMB; i++) {
            memset(&group_info, 0, sizeof(group_info));

            /* Retrieve information about each group under the parent group */
            if (H5Gget_info_by_idx(container_group, GROUP_GET_INFO_TEST_GROUP_NAME, H5_INDEX_NAME,
                                   H5_ITER_DEC, (hsize_t)i, &group_info, H5P_DEFAULT) < 0) {
                printf("    couldn't get group info for group at index %u\n", i);
                TESTFRAME_TEST_ERROR(params);
            }

            if (group_info.nlinks != 0) {
                printf("    group's number of links '%" PRIuHSIZE "' doesn't match expected value '%d'\n",
                       group_info.nlinks, 0);
                TESTFRAME_TEST_ERROR(params);
            }

            if (group_info.max_corder != 0) {
                printf("    group's max creation order '%lld' doesn't match expected value '%d'\n",
                       (long long)group_info.max_corder, 0);
                TESTFRAME_TEST_ERROR(params);
            }

            /* Ensure that the storage_type field is at least set to a meaningful value */
            if (group_info.storage_type != H5G_STORAGE_TYPE_SYMBOL_TABLE &&
                group_info.storage_type != H5G_STORAGE_TYPE_COMPACT &&
                group_info.storage_type != H5G_STORAGE_TYPE_DENSE &&
                group_info.storage_type != H5G_STORAGE_TYPE_UNKNOWN) {
                printf("    group info's 'storage_type' field wasn't set to a meaningful value\n");
                TESTFRAME_TEST_ERROR(params);
            }

            /* Assume that mounted should be false in this case */
            if (group_info.mounted != false) {
                printf("    group info's 'mounted' field was true when it should have been false\n");
                TESTFRAME_TEST_ERROR(params);
            }
        }
    }
    SUBTEST_END(params);

    if (H5Pclose(gcpl_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(parent_group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Pclose(gcpl_id);
        H5Gclose(parent_group_id);
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that a group's info can't be retrieved when
 * H5Gget_info(_by_name/_by_idx) is passed invalid parameters.
 */
static herr_t
test_get_group_info_invalid_params(TestParams_t *params)
{
    H5G_info_t group_info;
    herr_t     err_ret = -1;
    hid_t      file_id = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_MORE)) {
        printf("    API functions for basic file, or more group aren't supported with this "
               "connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    SUBTEST_BEGIN(params, "H5Gget_info with an invalid loc_id")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info(H5I_INVALID_HID, &group_info);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info with an invalid loc_id!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info with an invalid group info pointer")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info(file_id, NULL);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info with invalid group info pointer!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_name with an invalid loc_id")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_name(H5I_INVALID_HID, ".", &group_info, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_name with an invalid loc_id!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_name with an invalid group name")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_name(file_id, NULL, &group_info, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_name with a NULL name!\n");
            TESTFRAME_TEST_ERROR(params);
        }

        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_name(file_id, "", &group_info, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_name with an invalid name of ''!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_name with an invalid group info pointer")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_name(file_id, ".", NULL, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_name with an invalid group info "
                   "pointer!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_name with an invalid LAPL")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_name(file_id, ".", &group_info, H5I_INVALID_HID);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_name with an invalid LAPL!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx with an invalid loc_id")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_idx(H5I_INVALID_HID, ".", H5_INDEX_NAME, H5_ITER_INC, 0, &group_info,
                                         H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with an invalid loc_id!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx with an invalid group name")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_idx(file_id, NULL, H5_INDEX_NAME, H5_ITER_INC, 0, &group_info,
                                         H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with a NULL group name!\n");
            TESTFRAME_TEST_ERROR(params);
        }

        H5E_BEGIN_TRY
        {
            err_ret =
                H5Gget_info_by_idx(file_id, "", H5_INDEX_NAME, H5_ITER_INC, 0, &group_info, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with an invalid group name of "
                   "''!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx with an invalid index type")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_idx(file_id, ".", H5_INDEX_UNKNOWN, H5_ITER_INC, 0, &group_info,
                                         H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with invalid index type "
                   "H5_INDEX_UNKNOWN!\n");
            TESTFRAME_TEST_ERROR(params);
        }

        H5E_BEGIN_TRY
        {
            err_ret =
                H5Gget_info_by_idx(file_id, ".", H5_INDEX_N, H5_ITER_INC, 0, &group_info, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with invalid index type "
                   "H5_INDEX_N!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx with an invalid iteration order")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_idx(file_id, ".", H5_INDEX_NAME, H5_ITER_UNKNOWN, 0, &group_info,
                                         H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with invalid iteration order "
                   "H5_ITER_UNKNOWN!\n");
            TESTFRAME_TEST_ERROR(params);
        }

        H5E_BEGIN_TRY
        {
            err_ret =
                H5Gget_info_by_idx(file_id, ".", H5_INDEX_NAME, H5_ITER_N, 0, &group_info, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with invalid iteration order "
                   "H5_ITER_N!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx with an invalid group info pointer")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_idx(file_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, NULL, H5P_DEFAULT);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with an invalid group info "
                   "pointer!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    SUBTEST_BEGIN(params, "H5Gget_info_by_idx with an invalid LAPL")
    {
        H5E_BEGIN_TRY
        {
            err_ret = H5Gget_info_by_idx(file_id, ".", H5_INDEX_NAME, H5_ITER_INC, 0, &group_info,
                                         H5I_INVALID_HID);
        }
        H5E_END_TRY

        if (err_ret >= 0) {
            printf("    retrieved info of group using H5Gget_info_by_idx with an invalid LAPL!\n");
            TESTFRAME_TEST_ERROR(params);
        }
    }
    SUBTEST_END(params);

    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test for H5Gflush.
 */
static herr_t
test_flush_group(TestParams_t *params)
{
    hid_t file_id         = H5I_INVALID_HID;
    hid_t container_group = H5I_INVALID_HID;
    hid_t group_id        = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC) ||
        !(vol_cap_flags_g & H5VL_CAP_FLAG_FLUSH_REFRESH)) {
        printf("    API functions for basic file, group, or flush refresh aren't supported with this "
               "connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, GROUP_FLUSH_GNAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) <
        0) {
        printf("    couldn't create group '%s'\n", GROUP_FLUSH_GNAME);
        goto error;
    }

    /* Flush the group */
    if (H5Gflush(group_id) < 0) {
        printf("    couldn't flush the group '%s'\n", GROUP_FLUSH_GNAME);
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that H5Gflush fails when it
 * is passed invalid parameters.
 */
static herr_t
test_flush_group_invalid_params(TestParams_t H5_ATTR_UNUSED *params)
{
    herr_t status;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & H5VL_CAP_FLAG_FLUSH_REFRESH)) {
        printf("    API functions for group flush aren't supported with this connector\n");
        return SKIP;
    }

    H5E_BEGIN_TRY
    {
        status = H5Gflush(H5I_INVALID_HID);
    }
    H5E_END_TRY

    if (status >= 0) {
        printf("    flushed group with invalid ID!\n");
        goto error;
    }

    return SUCCEED;

error:
    return FAIL;
}

/*
 * A test for H5Grefresh.
 */
static herr_t
test_refresh_group(TestParams_t *params)
{
    hid_t file_id         = H5I_INVALID_HID;
    hid_t container_group = H5I_INVALID_HID;
    hid_t group_id        = H5I_INVALID_HID;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & (H5VL_CAP_FLAG_FILE_BASIC)) || !(vol_cap_flags_g & H5VL_CAP_FLAG_GROUP_BASIC) ||
        !(vol_cap_flags_g & H5VL_CAP_FLAG_FLUSH_REFRESH)) {
        printf("    API functions for basic file, group, or flush refresh aren't supported with this "
               "connector\n");
        return SKIP;
    }

    if ((file_id = H5Fopen(H5_API_TEST_FILENAME(params), H5F_ACC_RDWR, H5P_DEFAULT)) < 0) {
        printf("    couldn't open file '%s'\n", H5_API_TEST_FILENAME(params));
        goto error;
    }

    if ((container_group = H5Gopen2(file_id, GROUP_TEST_GROUP_NAME, H5P_DEFAULT)) < 0) {
        printf("    couldn't open container group\n");
        goto error;
    }

    if ((group_id = H5Gcreate2(container_group, GROUP_REFRESH_GNAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT)) <
        0) {
        printf("    couldn't create group '%s'\n", GROUP_REFRESH_GNAME);
        goto error;
    }

    /* Refresh the group */
    if (H5Grefresh(group_id) < 0) {
        printf("    couldn't refresh the group '%s'\n", GROUP_REFRESH_GNAME);
        goto error;
    }

    if (H5Gclose(group_id) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Gclose(container_group) < 0)
        TESTFRAME_TEST_ERROR(params);
    if (H5Fclose(file_id) < 0)
        TESTFRAME_TEST_ERROR(params);

    return SUCCEED;

error:
    H5E_BEGIN_TRY
    {
        H5Gclose(group_id);
        H5Gclose(container_group);
        H5Fclose(file_id);
    }
    H5E_END_TRY

    return FAIL;
}

/*
 * A test to check that H5Grefresh fails when it
 * is passed invalid parameters.
 */
static herr_t
test_refresh_group_invalid_params(TestParams_t H5_ATTR_UNUSED *params)
{
    herr_t status;

    /* Make sure the connector supports the API functions being tested */
    if (!(vol_cap_flags_g & H5VL_CAP_FLAG_FLUSH_REFRESH)) {
        printf("    API functions for group refresh aren't supported with this connector\n");
        return SKIP;
    }

    H5E_BEGIN_TRY
    {
        status = H5Grefresh(H5I_INVALID_HID);
    }
    H5E_END_TRY

    if (status >= 0) {
        printf("    refreshed group with invalid ID!\n");
        goto error;
    }

    return SUCCEED;

error:
    return FAIL;
}

void
H5_api_group_test_add(void)
{
    uint64_t testframe_flags = ALLOW_MULTITHREAD;

    /* Add a fake test to print out a header to distinguish different test interfaces */
    AddTest("print_group_test_header", print_group_test_header, NULL, NULL, NULL, 0, 0,
            "Prints header for group tests");

    AddTest("test_create_group_under_root", test_create_group_under_root, NULL, NULL, NULL, 0,
            testframe_flags, "creation of group under the root group");
    AddTest("test_create_group_under_existing_group", test_create_group_under_existing_group, NULL, NULL,
            NULL, 0, testframe_flags, "creation of group under existing group using a relative path");
    AddTest("test_create_many_groups", test_create_many_groups, NULL, NULL, NULL, 0,
            testframe_flags, "H5Gcreate many groups");
    AddTest("test_create_deep_groups", test_create_deep_groups, NULL, NULL, NULL, 0,
            testframe_flags, "H5Gcreate groups of great depths");
    AddTest("test_create_intermediate_group", test_create_intermediate_group, NULL, NULL, NULL, 0,
            testframe_flags, "H5Gcreate group with intermediate group creation");
    AddTest("test_create_group_invalid_params", test_create_group_invalid_params, NULL, NULL, NULL, 0,
            testframe_flags, "H5Gcreate with invalid parameters");
    AddTest("test_create_anonymous_group", test_create_anonymous_group, NULL, NULL, NULL, 0,
            testframe_flags, "creation of anonymous group");
    AddTest("test_create_anonymous_group_invalid_params", test_create_anonymous_group_invalid_params, NULL,
            NULL, NULL, 0, testframe_flags, "H5Gcreate_anon with invalid parameters");
    AddTest("test_open_nonexistent_group", test_open_nonexistent_group, NULL, NULL, NULL, 0,
            testframe_flags, "for invalid opening of a nonexistent group");
    AddTest("test_open_group_invalid_params", test_open_group_invalid_params, NULL, NULL, NULL, 0,
            testframe_flags, "H5Gopen with invalid parameters");
    AddTest("test_close_group_invalid_id", test_close_group_invalid_id, NULL, NULL, NULL, 0,
            testframe_flags, "H5Gclose with an invalid group ID");
    AddTest("test_group_property_lists", test_group_property_lists, NULL, NULL, NULL, 0,
            testframe_flags, "group property list operations");
    AddTest("test_get_group_info", test_get_group_info, NULL, NULL, NULL, 0,
            testframe_flags, "retrieval of group info");
    AddTest("test_get_group_info_invalid_params", test_get_group_info_invalid_params, NULL, NULL, NULL, 0,
            testframe_flags, "retrieval of group info with invalid parameters");
    AddTest("test_flush_group", test_flush_group, NULL, NULL, NULL, 0,
            testframe_flags, "H5Gflush");
    AddTest("test_flush_group_invalid_params", test_flush_group_invalid_params, NULL, NULL, NULL, 0,
            testframe_flags, "H5Gflush with invalid parameters");
    AddTest("test_refresh_group", test_refresh_group, NULL, NULL, NULL, 0,
            testframe_flags, "H5Grefresh");
    AddTest("test_refresh_group_invalid_params", test_refresh_group_invalid_params, NULL, NULL, NULL, 0,
            testframe_flags, "H5Grefresh with invalid parameters");
}
