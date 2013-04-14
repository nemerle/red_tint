# CMake build system for mruby
# License: released under the same license as mruby
# Author: jonforums@gmail.com
# Author: beoran@gmail.com
#
# Usage example:
#   1. Ensure CMake, Bison, and a build toolchain are on `PATH`
#   2. Change to a build directory outside source tree, eg - `build` subdir
#   3. Create build Makefiles or project files.
#        `cmake ..`  (UNIX-like system)
#        `cmake -G "MSYS Makefiles" ..`
#        `cmake -G "Visual Studio 10" ..`
#        `cmake -G "NMake Makefiles" ..`
#      ** to cross-compile: add -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain/file
#      ** to set install dir: add -DCMAKE_INSTALL_PREFIX=/path/to/installdir
#  4a. Build: `make`  (to make noisy, add `VERBOSE=1`)
#  4b. Build and test: `make all test`
#  4c. Build, test, and install: `make all test install`
#  4d. Build, test, and package: `make all test package`

cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)
IF(CMAKE_CXX_COMPILER MATCHES cl.exe)
    ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -D_CRT_NONSTDC_NO_WARNINGS)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3" )
ELSE()
    SET(WARNINGS "-Wall -Wno-missing-field-initializers -Werror-implicit-function-declaration -Wextra -Wno-unused-parameter -Wno-sign-compare -Wstrict-aliasing=2 -Wdisabled-optimization -Wpointer-arith -Wdeclaration-after-statement -Wbad-function-cast -Wcast-align -Wredundant-decls")
    ADD_DEFINITIONS(-D__STDC_LIMIT_MACROS)
ENDIF()
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 ${WARNINGS}" )
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNINGS}" )

# Default build mode is Release With Debug Info unless specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      "Choose build type: empty Debug Release RelWithDebInfo MinSizeRel"
      FORCE)
  message(STATUS "Build type not set, defaulting to 'RelWithDebInfo'")
endif()

project(red_tint)

# TODO stop polluting source tree with CMakeFiles/ and CMakeCache.txt
#      on build location check failure
# Make sure we are not trying to generate in in-tree build unless building
# with an MSVC or Xcode IDE where it's OK.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND NOT (MSVC_IDE OR XCODE))
  message(FATAL_ERROR
          "\nIn-source builds are not allowed as CMake would overwrite the "
          "Makefiles distributed with mruby. Delete any created 'CMakeFiles' "
          "subdirectory and 'CMakeCache.txt' file from the current directory, "
          "change to the 'build' subdirectory, and re-run CMake from there.")
endif()

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)  # don't split absolute link paths
  cmake_policy(SET CMP0012 NEW)  # recognize number & boolean literals
  cmake_policy(SET CMP0015 NEW)  # convert relative link paths to absolute
endif(COMMAND cmake_policy)

# Match original Makefile's default in-tree install.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR} CACHE PATH
    "Install path prefix prepended to install directories."
    FORCE
    )
endif()

# TODO refactor to use an option when adding shared lib support
#set(BUILD_SHARED_LIBS OFF)

# Version of mruby, useful for versoning .so and .dll files.
# TODO automate by parsing src/version.h -or- extract git info?
set(MRUBY_VERSION 1.0.0dev)
string(REGEX MATCH "^[0-9]+[.][0-9]+" MRUBY_SOVERSION ${MRUBY_VERSION})
string(REPLACE "." "" MRUBY_DLL_SHORTVER ${MRUBY_SOVERSION})

# Search in the `cmake` directory for custom CMake helper modules.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
include(IntrospectSystem)
include(Gem)
InitGems()
AddGem(array-ext)
AddGem(sprintf)
AddGem(print)
AddGem(math)
AddGem(struct)


#message(GEM_RB_FILES " ${GEM_RB_FILES}")
#message(GEM_SRC_FILES " ${GEM_SRC_FILES}")

# Search for C header files in these directories.
include_directories("${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src")

# TODO re-enable (and implement) if needed
# On some 64-bit platforms, libraries should be installed into `lib64'
# instead of `lib'.  Set this to 64 to do that.
#set(LIB_SUFFIX "" CACHE STRING "Suffix for 'lib' directories, e.g. '64'")

# build the components
add_subdirectory(src)
add_subdirectory(mrblib)
add_subdirectory(tools)
add_subdirectory(test)

# install the header files
install(FILES include/mruby.h DESTINATION include)
install(FILES include/mrbconf.h DESTINATION include)
install(DIRECTORY include/mruby DESTINATION include FILES_MATCHING PATTERN "*.h")

# TODO refactor once proper versioning scheme implemented
# archive packaging
set(CPACK_GENERATOR "TGZ;ZIP")
string(TOLOWER ${CMAKE_SYSTEM_NAME} MRUBY_HOST)
if(CMAKE_C_COMPILER_VERSION)
  string(REPLACE "." "" MRUBY_GCC_VERSION ${CMAKE_C_COMPILER_VERSION})
endif()

# TODO add build info suffix for non-Windows builds?
if(MINGW)
  set(MRUBY_BUILD "-mingw${MRUBY_GCC_VERSION}")
elseif(MSVC)
  set(MRUBY_BUILD "-msvc${MSVC_VERSION}")
endif()
set(CPACK_PACKAGE_FILE_NAME
  "${CMAKE_PROJECT_NAME}-${MRUBY_VERSION}-${MRUBY_HOST}${MRUBY_BUILD}"
  )
include(CPack)
