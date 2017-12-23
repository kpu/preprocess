# Explicitly list the source files for this subdirectory
#
# If you add any source files to this subdirectory
#    that should be included in the kenlm library,
#        (this excludes any unit test files)
#    you should add them to the following list:
#
# Because we do not set PARENT_SCOPE in the following definition,
#    CMake files in the parent directory won't be able to access this variable.
#
set(KENLM_UTIL_SOURCE
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
		read_compressed.cc
		scoped.cc
    spaces.cc
		string_piece.cc
    utf8.cc
	)

set(READ_COMPRESSED_FLAGS)
set(READ_COMPRESSED_LIBS)
find_package(ZLIB)
if (ZLIB_FOUND)
  set(READ_COMPRESSED_FLAGS "${READ_COMPRESSED_FLAGS} -DHAVE_ZLIB")
  set(READ_COMPRESSED_LIBS ${READ_COMPRESSED_LIBS} ${ZLIB_LIBRARIES})
  include_directories(${ZLIB_INCLUDE_DIR})
endif()

find_package(BZip2)
if (BZIP2_FOUND)
  set(READ_COMPRESSED_FLAGS "${READ_COMPRESSED_FLAGS} -DHAVE_BZLIB")
  set(READ_COMPRESSED_LIBS ${READ_COMPRESSED_LIBS} ${BZIP2_LIBRARIES})
  include_directories(${BZIP2_INCLUDE_DIR})
endif()

find_package(LibLZMA)
if (LIBLZMA_FOUND)
  set(READ_COMPRESSED_FLAGS "${READ_COMPRESSED_FLAGS} -DHAVE_XZLIB")
  set(READ_COMPRESSED_LIBS ${READ_COMPRESSED_LIBS} ${LIBLZMA_LIBRARIES})
  include_directories(${LIBLZMA_INCLUDE_DIRS})
endif()
set_source_files_properties(read_compressed.cc PROPERTIES COMPILE_FLAGS ${READ_COMPRESSED_FLAGS})
set_source_files_properties(read_compressed_test.cc PROPERTIES COMPILE_FLAGS ${READ_COMPRESSED_FLAGS})
set_source_files_properties(file_piece_test.cc PROPERTIES COMPILE_FLAGS ${READ_COMPRESSED_FLAGS})

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

find_package(ICU REQUIRED COMPONENTS i18n uc data io)

# Group these objects together for later use.
add_library(kenlm_util ${KENLM_UTIL_DOUBLECONVERSION_SOURCE} ${KENLM_UTIL_STREAM_SOURCE} ${KENLM_UTIL_SOURCE})
target_link_libraries(kenlm_util ${Boost_LIBRARIES} ${READ_COMPRESSED_LIBS} ${ICU_LIBRARIES} ${THREADS} ${TIMER_LINK})

# Only compile and run unit tests if tests should be run
if(BUILD_TESTING)
  set(KENLM_BOOST_TESTS_LIST
    integer_to_string_test
    probing_hash_table_test
    read_compressed_test
    tokenize_piece_test
  )

  AddTests(TESTS ${KENLM_BOOST_TESTS_LIST}
           LIBRARIES kenlm_util ${Boost_LIBRARIES} ${THREADS})

  # file_piece_test requires an extra command line parameter
  KenLMAddTest(TEST file_piece_test
               LIBRARIES kenlm_util ${Boost_LIBRARIES} ${THREADS}
               TEST_ARGS ${CMAKE_CURRENT_SOURCE_DIR}/file_piece.cc)
endif()
