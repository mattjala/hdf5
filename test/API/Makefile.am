#
# Copyright by The HDF Group.
# All rights reserved.
#
# This file is part of HDF5.  The full HDF5 copyright notice, including
# terms governing use, modification, and redistribution, is contained in
# the COPYING file, which can be found at the root of the source code
# distribution tree, or in https://www.hdfgroup.org/licenses.
# If you do not have access to either file, you may request a copy from
# help@hdfgroup.org.
##
## Makefile.am
## Run automake to generate a Makefile.in from this file.
##
#
# HDF5 Library Test Makefile(.in)
#

include $(top_srcdir)/config/commence.am

AM_CFLAGS+=-I$(top_srcdir)/src -I$(top_srcdir)/test

check_PROGRAMS = api_tests

api_tests_SOURCES = H5_api_test.c H5_api_test.h H5_api_async_test.c H5_api_async_test.h \
                    H5_api_attribute_test.c H5_api_attribute_test.h \
                    H5_api_dataset_test.c H5_api_dataset_test.h \
                    H5_api_datatype_test.c H5_api_datatype_test.h \
                    H5_api_file_test.c H5_api_file_test.h \
                    H5_api_group_test.c H5_api_group_test.h \
                    H5_api_link_test.c H5_api_link_test.h \
                    H5_api_misc_test.c H5_api_misc_test.h \
                    H5_api_object_test.c H5_api_object_test.h \
                    H5_api_test_util.c H5_api_test_util.h $(top_srcdir)/src/hdf5.h

api_tests_LDADD =$(LIBH5TEST) $(LIBHDF5)

include $(top_srcdir)/config/conclude.am