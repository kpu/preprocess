# Explicitly list the source files for this subdirectory
#
# If you add any source files to this subdirectory
#    that should be included in the preprocess library,
#        (this excludes any unit test files)
#    you should add them to the following list:
#
# Because we do not set PARENT_SCOPE in the following definition,
#    CMake files in the parent directory won't be able to access this variable.
#
set(PREPROCESS_UTIL_SOURCE
		compress.cc
		ersatz_progress.cc
		exception.cc
		file.cc
		file_piece.cc
		float_to_string.cc
		integer_to_string.cc
		mmap.cc
		murmur_hash.cc
    mutable_vocab.cc
		pool.cc
		scoped.cc
    spaces.cc
		string_piece.cc
    utf8.cc
	)

set(COMPRESS_FLAGS)
set(COMPRESS_LIBS)
find_package(ZLIB)
if (ZLIB_FOUND)
  set(COMPRESS_FLAGS "${COMPRESS_FLAGS} -DHAVE_ZLIB")
  set(COMPRESS_LIBS ${COMPRESS_LIBS} ${ZLIB_LIBRARIES})
  include_directories(${ZLIB_INCLUDE_DIR})
endif()

find_package(BZip2)
if (BZIP2_FOUND)
  set(COMPRESS_FLAGS "${COMPRESS_FLAGS} -DHAVE_BZLIB")
  set(COMPRESS_LIBS ${COMPRESS_LIBS} ${BZIP2_LIBRARIES})
  include_directories(${BZIP2_INCLUDE_DIR})
endif()

find_package(LibLZMA)
if (LIBLZMA_FOUND)
  set(COMPRESS_FLAGS "${COMPRESS_FLAGS} -DHAVE_XZLIB")
  set(COMPRESS_LIBS ${COMPRESS_LIBS} ${LIBLZMA_LIBRARIES})
  include_directories(${LIBLZMA_INCLUDE_DIRS})
endif()
set_source_files_properties(compress.cc PROPERTIES COMPILE_FLAGS ${COMPRESS_FLAGS})
set_source_files_properties(compress_test.cc PROPERTIES COMPILE_FLAGS ${COMPRESS_FLAGS})
set_source_files_properties(file_piece_test.cc PROPERTIES COMPILE_FLAGS ${COMPRESS_FLAGS})

# This directory has children that need to be processed
add_subdirectory(double-conversion)

if (UNIX AND NOT APPLE)
  set(TIMER_LINK rt)
else()
  set(TIMER_LINK)
endif()

if (NOT MSVC)
	set(THREADS pthread)
endif()

# Group these objects together for later use.
add_library(preprocess_util ${PREPROCESS_UTIL_DOUBLECONVERSION_SOURCE} ${PREPROCESS_UTIL_STREAM_SOURCE} ${PREPROCESS_UTIL_SOURCE})
target_link_libraries(preprocess_util ${Boost_LIBRARIES} ${COMPRESS_LIBS} ${ICU_LIBRARIES} ${THREADS} ${TIMER_LINK})

# Only compile and run unit tests if tests should be run
if(BUILD_TESTING)
  set(PREPROCESS_BOOST_TESTS_LIST
    integer_to_string_test
    pcqueue_test
    probing_hash_table_test
    compress_test
    string_stream_test
    tokenize_piece_test
  )

# Adds a single test to the build, depending on the specified dependent
# object files, linking against the specified libraries, and with the
# specified command line arguments
function(PreprocessAddTest)
  cmake_parse_arguments(PreprocessAddTest "" "TEST"
                        "DEPENDS;LIBRARIES;TEST_ARGS" ${ARGN})

  # Compile the executable, linking against the requisite dependent object files
  add_executable(${PreprocessAddTest_TEST}
                 ${PreprocessAddTest_TEST}.cc
                 ${PreprocessAddTest_DEPENDS})

  if (Boost_USE_STATIC_LIBS)
    set(DYNLINK_FLAGS)
  else()
    set(DYNLINK_FLAGS COMPILE_FLAGS -DBOOST_TEST_DYN_LINK)
  endif()

  # Require the following compile flag
  set_target_properties(${PreprocessAddTest_TEST} PROPERTIES
                        ${DYNLINK_FLAGS}
                        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/tests)

  target_link_libraries(${PreprocessAddTest_TEST} ${PreprocessAddTest_LIBRARIES} ${TIMER_LINK})

  set(test_params "")
  if(PreprocessAddTest_TEST_ARGS)
    set(test_params ${PreprocessAddTest_TEST_ARGS})
  endif()

  # Specify command arguments for how to run each unit test
  #
  # Assuming that foo was defined via add_executable(foo ...),
  #   the syntax $<TARGET_FILE:foo> gives the full path to the executable.
  #
  add_test(NAME ${PreprocessAddTest_TEST}
           COMMAND $<TARGET_FILE:${PreprocessAddTest_TEST}> ${test_params})

  # Group unit tests together
  set_target_properties(${PreprocessAddTest_TEST} PROPERTIES FOLDER "unit_tests")
endfunction()

# Adds a bunch of tests to the build, each depending on the specified
# dependent object files and linking against the specified libraries
function(AddTests)
  set(multiValueArgs TESTS DEPENDS LIBRARIES TEST_ARGS)
  cmake_parse_arguments(AddTests "" "" "${multiValueArgs}" ${ARGN})

  # Iterate through the Boost tests list
  foreach(test ${AddTests_TESTS})
    PreprocessAddTest(TEST ${test}
                 DEPENDS ${AddTests_DEPENDS}
                 LIBRARIES ${AddTests_LIBRARIES}
                 TEST_ARGS ${AddTests_TEST_ARGS})
  endforeach(test)
endfunction()


  AddTests(TESTS ${PREPROCESS_BOOST_TESTS_LIST}
           LIBRARIES preprocess_util ${Boost_LIBRARIES} ${THREADS})

  # file_piece_test requires an extra command line parameter
  PreprocessAddTest(TEST file_piece_test
               LIBRARIES preprocess_util ${Boost_LIBRARIES} ${THREADS}
               TEST_ARGS ${CMAKE_CURRENT_SOURCE_DIR}/file_piece.cc)
endif()
