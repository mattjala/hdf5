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

# Programs to be built
noinst_PROGRAMS = testmthdf5

# Programs to be built when testing
check_PROGRAMS = testmthdf5

testmthdf5_SOURCES=testmthdf5.c testmthdf5.h unit/mt_vl_test.c unit/mt_misc_test.c unit/mt_test_util.c

testmthdf5_LDADD = $(LIBH5TEST) $(LIBHDF5)

include $(top_srcdir)/config/conclude.am