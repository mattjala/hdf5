cmake_minimum_required (VERSION 3.18)
project (HDF5_TEST_MT C)

# Define sources
set (HDF5_TEST_MT_SOURCES
  testmthdf5.c 
  unit/mt_vl_test.c 
  unit/mt_misc_test.c
  unit/mt_test_util.c
)

set (HDF5_TEST_MT_HEADERS
    testmthdf5.h
)

#--------------------------------
# Build the main MT test executable
#--------------------------------

add_executable(testmthdf5 ${HDF5_TEST_MT_SOURCES})
target_include_directories (
    testmthdf5
    PRIVATE
        "${HDF5_SRC_INCLUDE_DIRS}"
        "${HDF5_TEST_SRC_DIR}"
        "${HDF5_SRC_BINARY_DIR}"
        "${HDF5_TEST_BINARY_DIR}"
)

target_compile_options (
  testmthdf5
  PRIVATE
    "${HDF5_CMAKE_C_FLAGS}"
)
if (NOT BUILD_SHARED_LIBS)
  TARGET_C_PROPERTIES (testmthdf5 STATIC)
  target_link_libraries (
    testmthdf5
    PRIVATE
      ${HDF5_TEST_LIB_TARGET}
    )
else ()
  TARGET_C_PROPERTIES (testmthdf5 SHARED)
  target_link_libraries (
    testmthdf5
    PRIVATE
      ${HDF5_TEST_LIBSH_TARGET}
  )
endif ()
set_target_properties(
    testmthdf5
    PROPERTIES
        FOLDER test/threads
)
# Add Target to clang-format
if (HDF5_ENABLE_FORMATTERS)
    clang_format (testmthdf5)
endif ()

if (HDF5_TEST_MT_INSTALL)
  install (
    TARGETS
      testmthdf5
    EXPORT
      ${HDF5_EXPORTED_TARGETS}
    DESTINATION
      ${HDF5_INSTALL_BIN_DIR}
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    COMPONENT
      tests
  )
endif ()