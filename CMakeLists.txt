# ~~~
# SPDX-FileCopyrightText: Hudson River Trading
# SPDX-License-Identifier: MIT
# ~~~

# slang-server cmake entry point
cmake_minimum_required(VERSION 3.20...3.29)

# Determine if slang-server is built as a subproject (using add_subdirectory) or
# if it is the master project.
if(NOT DEFINED SLANG_SERVER_MASTER_PROJECT)
  set(SLANG_SERVER_MASTER_PROJECT OFF)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(SLANG_SERVER_MASTER_PROJECT ON)
    message(STATUS "CMake version: ${CMAKE_VERSION}")
  endif()
endif()

# Protect against in-tree builds.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(
    FATAL_ERROR "In-source builds are not supported. You may need to delete "
                "'CMakeCache.txt' and 'CMakeFiles/' before building again.")
endif()

# Read version from VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SLANG_SERVER_VERSION_STRING)
string(STRIP "${SLANG_SERVER_VERSION_STRING}" SLANG_SERVER_VERSION_STRING)

# Parse version components
string(REPLACE "." ";" VERSION_LIST "${SLANG_SERVER_VERSION_STRING}")
list(GET VERSION_LIST 0 SLANG_SERVER_VERSION_MAJOR)
list(GET VERSION_LIST 1 SLANG_SERVER_VERSION_MINOR)
list(GET VERSION_LIST 2 SLANG_SERVER_VERSION_PATCH)

# Append git hash if available
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
if(NOT GIT_FOUND)
  find_package(Git QUIET)
endif()
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE SLANG_SERVER_VERSION_HASH
    ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  message(
    STATUS
      "slang-server version: ${SLANG_SERVER_VERSION_STRING}+${SLANG_SERVER_VERSION_HASH}"
  )
else()
  message(STATUS "slang-server version: ${SLANG_SERVER_VERSION_STRING}")
endif()

# Set the default build type if none is set explicitly, but only for
# single-config generators.
get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(SLANG_SERVER_MASTER_PROJECT
   AND NOT isMultiConfig
   AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      Debug
      CACHE
        STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

project(
  slang-server
  VERSION ${SLANG_SERVER_VERSION_STRING}
  LANGUAGES CXX
  HOMEPAGE_URL https://github.com/hudson-trading/slang-server
  DESCRIPTION "SystemVerilog language server based on slang")

if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
  # This ensures that *all* targets (including slang and reflect-cpp) use the
  # default Apple C++ runtime, preventing dynamic link conflicts.
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -stdlib=libc++"
      CACHE STRING "" FORCE)
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++"
      CACHE STRING "" FORCE)
  message(STATUS "macOS build: Forcing use of libc++ standard library.")
endif()

option(SLANG_SERVER_INCLUDE_TESTS "Include test targets in the build"
       ${SLANG_SERVER_MASTER_PROJECT})
option(SLANG_SERVER_INCLUDE_INSTALL "Include installation targets"
       ${SLANG_SERVER_MASTER_PROJECT})
option(SLANG_SERVER_STATIC_LINK_STDLIB
       "Statically link C++ standard library (libstdc++/libc++) and libgcc" OFF)

# Always require C++20 or later, no extensions.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  add_compile_definitions(WIN32 _WINDOWS NTDDI_VERSION=0x06010000
                          _WIN32_WINNT=0x0601)
  add_compile_definitions(_SCL_SECURE_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
  add_compile_definitions(_CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_WARNINGS)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  add_compile_options(/utf-8 /bigobj /permissive-)
  add_compile_options(/Zc:preprocessor)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "WASI")
  add_compile_options(-fexceptions)
  # Disable pedantic warnings for WASI since they're not as useful in this
  # context
  add_compile_options(-Wno-pedantic -Wno-sign-compare -Wno-reorder-ctor
                      -Wno-unused-but-set-variable)

  add_compile_definitions(_WASI_EMULATED_SIGNAL)
  add_link_options(-lwasi-emulated-signal)
  set(CATCH_CONFIG_NO_POSIX_SIGNALS ON)

  # https://github.com/fmtlib/fmt/issues/4496#issuecomment-3094630184
  add_compile_definitions(FMT_USE_FCNTL=0)

  # Disable boost threading features for WASI
  add_compile_definitions(BOOST_DISABLE_THREADS)

  # to make stack overflows crash instead of silently corrupt memory
  add_link_options(-Wl,--stack-first -Wl,-z,stack-size=1048576)

  # required for growable memory in multithreaded builds
  add_link_options(-Wl,--max-memory=4294967296)

  # to have CTest invoke a Wasm engine when running tests
  set(mappings --dir=/ --dir=${CMAKE_CURRENT_SOURCE_DIR}/tests::tests)
  set(CMAKE_TEST_LAUNCHER
      wasmtime run -S threads ${mappings}
      CACHE STRING "")
endif()

# Add compiler flags to catch cross-platform issues early
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wno-missing-declarations
                      -Wno-missing-field-initializers)
  # Warn about potential portability issues add_compile_options(-Wconversion)
  # Warn about missing includes that might work on some platforms but not others
  add_compile_options(-Wmissing-include-dirs)
endif()

# Configure static linking of C++ standard library if requested Note: macOS uses
# system libc++ which can't be statically linked and is generally compatible
# across versions
if(SLANG_SERVER_STATIC_LINK_STDLIB)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT CMAKE_SYSTEM_NAME
                                                   MATCHES "Darwin")
    # On Linux, statically link libstdc++/libc++ and libgcc for better
    # portability
    add_link_options(-static-libstdc++ -static-libgcc)
    message(
      STATUS
        "Static linking enabled for C++ standard library (libstdc++/libgcc)")
  elseif(MSVC)
    # For MSVC, set the runtime library to static
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    message(STATUS "Static linking enabled for MSVC runtime library")
  endif()
endif()

# Include GNUInstallDirs for standard installation directories
include(GNUInstallDirs)

# Include FetchContent for external dependencies
include(FetchContent)

# Set saner / consistent build directories on all platforms
foreach(
  var
  CMAKE_RUNTIME_OUTPUT_DIRECTORY
  CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE
  CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
  CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
  CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG)
  if(NOT ${var})
    set(${var} ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})
  endif()
endforeach()
foreach(
  var
  CMAKE_ARCHIVE_OUTPUT_DIRECTORY
  CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE
  CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO
  CMAKE_ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL
  CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG
  CMAKE_LIBRARY_OUTPUT_DIRECTORY
  CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE
  CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO
  CMAKE_LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL
  CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG)
  if(NOT ${var})
    set(${var} ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
  endif()
endforeach()

# Add the slang library as a subdirectory Set slang options for server usage
set(SLANG_INCLUDE_TOOLS
    OFF
    CACHE BOOL "Don't build slang tools" FORCE)
set(SLANG_INCLUDE_DOCS
    OFF
    CACHE BOOL "Don't build slang docs" FORCE)
set(SLANG_INCLUDE_PYLIB
    OFF
    CACHE BOOL "Don't build slang Python bindings" FORCE)
set(SLANG_INCLUDE_INSTALL
    OFF
    CACHE BOOL "Don't install slang separately" FORCE)

# Disable slang tests to avoid ctest pollution, but keep Catch2 available
set(SLANG_INCLUDE_TESTS
    OFF
    CACHE BOOL "Don't build slang tests" FORCE)

# Set up Catch2 for testing
set(find_pkg_args "")

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24.0")
  set(find_pkg_args "FIND_PACKAGE_ARGS" "${catch2_min_version}")
endif()

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.10.0
  GIT_SHALLOW ON
  ${find_pkg_args})
FetchContent_MakeAvailable(Catch2)

if(NOT TARGET Catch2::Catch2)
  message(
    FATAL_ERROR
      "Could not find Catch2 package, min version: ${catch2_min_version}")
endif()

if(Catch2_FOUND)
  get_target_property(Catch2_INCLUDE_DIR Catch2::Catch2
                      INTERFACE_INCLUDE_DIRECTORIES)
  message(STATUS "Found system Catch2 version: ${Catch2_VERSION}")
  message(STATUS "Using system Catch2 include: ${Catch2_INCLUDE_DIR}")
else()
  message(STATUS "Using remote Catch2 library")
endif()

add_subdirectory(external/slang)
add_subdirectory(external/reflect-cpp)

# Suppress warnings in reflect-cpp library
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(reflectcpp PRIVATE -Wno-missing-field-initializers
                                            -Wno-unused-parameter -fexceptions)
endif()

# Generate version header
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/src/util/VersionInfo.cpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.cpp @ONLY)

# Build the slang-server library
add_library(
  slang_server_obj_lib OBJECT
  ${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.cpp
  src/ast/ServerCompilation.cpp
  src/ast/WcpClient.cpp
  src/document/ShallowAnalysis.cpp
  src/document/SlangDoc.cpp
  src/document/SymbolIndexer.cpp
  src/document/InlayHintCollector.cpp
  src/document/SymbolTreeVisitor.cpp
  src/document/SyntaxIndexer.cpp
  src/completions/Completions.cpp
  src/completions/CompletionDispatch.cpp
  src/util/Converters.cpp
  src/util/Formatting.cpp
  src/Config.cpp
  src/Indexer.cpp
  src/ServerDriver.cpp
  src/SlangServer.cpp
  src/ServerDiagClient.cpp)

target_include_directories(slang_server_obj_lib PUBLIC include)
target_include_directories(slang_server_obj_lib SYSTEM
                           PRIVATE external/reflect-cpp/include)

target_precompile_headers(slang_server_obj_lib PRIVATE
                          "${PROJECT_SOURCE_DIR}/include/lsp/LspTypes.h")

target_link_libraries(slang_server_obj_lib PUBLIC slang::slang)

# Apply pedantic warnings only to slang-server code, not external libraries
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(
    slang_server_obj_lib
    PRIVATE -Wpedantic -Wunused-variable -Wpessimizing-move
            -Wunused-but-set-variable -Wno-nonnull -Werror)
endif()

# Build the slang-server executable
add_executable(slang_server src/server_main.cpp)
add_executable(slang::server ALIAS slang_server)

target_link_libraries(slang_server PRIVATE reflectcpp)
target_link_libraries(slang_server PRIVATE slang_server_obj_lib)
set_target_properties(slang_server PROPERTIES OUTPUT_NAME "slang-server")

# Build the gen_config_schema tool (standalone tool to generate config schema)
add_executable(gen_config_schema src/gen_config_main.cpp)
target_include_directories(gen_config_schema PRIVATE include)
target_include_directories(gen_config_schema SYSTEM
                           PRIVATE external/reflect-cpp/include)
target_link_libraries(gen_config_schema PRIVATE reflectcpp)

if(SLANG_SERVER_COVERAGE)
  include(cmake/coverage.cmake)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  # Check if the manifest file exists
  if(EXISTS ${PROJECT_SOURCE_DIR}/scripts/win32.manifest)
    target_sources(slang_server
                   PRIVATE ${PROJECT_SOURCE_DIR}/scripts/win32.manifest)
  endif()
endif()

if(SLANG_SERVER_INCLUDE_INSTALL)
  install(TARGETS slang_server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(SLANG_SERVER_INCLUDE_TESTS)
  enable_testing()
  add_subdirectory(tests/cpp)
endif()
