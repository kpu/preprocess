cmake_minimum_required(VERSION 2.8.12)

# Define a single cmake project
project(preprocess)

#Set for FindICU.cmake
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
set(CMAKE_CXX_STANDARD 17)

# Compile all executables into bin/
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)

# Compile all libraries into lib/
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(COMPILE_TESTS "Compile tests" OFF)
if (COMPILE_TESTS)
  # Tell cmake that we want unit tests to be compiled
  include(CTest)
  enable_testing()
endif()

if(MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} /w34716")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /w34716")
endif()

set(BOOST_LIBS program_options)
if (COMPILE_TESTS)
  set(BOOST_LIBS ${BOOST_LIBS} unit_test_framework)
endif()

# We need boost for now to do program_options.
find_package(Boost 1.41.0 REQUIRED COMPONENTS ${BOOST_LIBS})

find_package(ICU COMPONENTS i18n uc data io)
include(CMakeDependentOption)
cmake_dependent_option(USE_ICU "Build programs that use ICU" ON ICU_FOUND OFF)

# Define where include files live
include_directories(
  ${PROJECT_SOURCE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${ICU_INCLUDE_DIRS}
)

# Process subdirectories
add_subdirectory(util)
add_subdirectory(preprocess)
add_subdirectory(moses)

