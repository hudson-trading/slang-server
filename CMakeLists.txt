# ~~~
# SPDX-FileCopyrightText: Hudson River Trading
# SPDX-License-Identifier: MIT
# ~~~

# slang-server cmake entry point
cmake_minimum_required(VERSION 3.20...3.29)

# Determine if slang-server is built as a subproject (using add_subdirectory) or if it
# is the master project.
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
  VERSION 0.1.0
  LANGUAGES CXX
  HOMEPAGE_URL https://github.com/hudson-trading/slang-server
  DESCRIPTION "SystemVerilog language server based on slang")

option(SLANG_SERVER_INCLUDE_TESTS "Include test targets in the build" ${SLANG_SERVER_MASTER_PROJECT})
option(SLANG_SERVER_INCLUDE_INSTALL "Include installation targets" ${SLANG_SERVER_MASTER_PROJECT})

# Always require C++20 or later, no extensions.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include GNUInstallDirs for standard installation directories
include(GNUInstallDirs)

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

# Defaults for a bunch of Windows-specific junk. These are required by all
# targets to build and run correctly and don't affect ABI so shouldn't need
# target_ specific commands.
if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  add_compile_definitions(WIN32 _WINDOWS NTDDI_VERSION=0x06010000
                          _WIN32_WINNT=0x0601)
  add_compile_definitions(_SCL_SECURE_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
  add_compile_definitions(_CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_WARNINGS)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  add_compile_options(/utf-8 /bigobj /permissive-)
endif()

# Add the slang library as a subdirectory
# Set slang options for server usage
set(SLANG_INCLUDE_TOOLS ON CACHE BOOL "Don't build slang tools" FORCE)
set(SLANG_INCLUDE_DOCS OFF CACHE BOOL "Don't build slang docs" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "Don't build slang Python bindings" FORCE)
set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "Don't install slang separately" FORCE)

# Enable slang tests only if we need them for Catch2
if(SLANG_SERVER_INCLUDE_TESTS)
  set(SLANG_INCLUDE_TESTS ON CACHE BOOL "Enable slang tests to get Catch2" FORCE)
else()
  set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "Don't build slang tests" FORCE)
endif()

add_subdirectory(external/slang)
add_subdirectory(external/reflect-cpp)

# Suppress warnings in reflect-cpp library
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(reflectcpp PRIVATE -Wno-missing-designated-field-initializers -Wno-missing-field-initializers -Wno-unused-parameter)
endif()

# Build the slang-server library
add_library(
  slang_server_obj_lib OBJECT
  src/ast/ServerCompilation.cpp
  src/document/ShallowAnalysis.cpp
  src/document/SlangDoc.cpp
  src/document/SymbolIndexer.cpp
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
  src/ServerDiagClient.cpp
)

target_include_directories(slang_server_obj_lib PUBLIC include)
target_include_directories(slang_server_obj_lib PRIVATE external/reflect-cpp/include)

target_precompile_headers(
  slang_server_obj_lib PRIVATE
  "${PROJECT_SOURCE_DIR}/include/lsp/LspTypes.h")

target_link_libraries(slang_server_obj_lib PUBLIC slang::slang)

# Build the slang-server executable
add_executable(slang_server src/server_main.cpp)
add_executable(slang::server ALIAS slang_server)

target_link_libraries(slang_server PRIVATE reflectcpp)
target_link_libraries(slang_server PRIVATE slang_server_obj_lib)
set_target_properties(slang_server PROPERTIES OUTPUT_NAME "slang-server")

if(SLANG_SERVER_COVERAGE)
  include(cmake/coverage.cmake)
endif()

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  # Check if the manifest file exists
  if(EXISTS ${PROJECT_SOURCE_DIR}/scripts/win32.manifest)
    target_sources(slang_server PRIVATE ${PROJECT_SOURCE_DIR}/scripts/win32.manifest)
  endif()
endif()

if(SLANG_SERVER_INCLUDE_INSTALL)
  install(TARGETS slang_server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(SLANG_SERVER_INCLUDE_TESTS)
  enable_testing()
  add_subdirectory(tests/cpp)
endif()