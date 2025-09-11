# ~~~
# SPDX-FileCopyrightText: Hudson River Trading
# SPDX-License-Identifier: MIT
# ~~~

file(GLOB SERVER_TESTS "*.cpp")

add_executable(
  server_unittests utils/ServerHarness.cpp
                   main.cpp ${SERVER_TESTS})

target_link_libraries(server_unittests PRIVATE slang_server_obj_lib)

target_include_directories(server_unittests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/external/reflect-cpp/include)
target_link_libraries(server_unittests PRIVATE reflectcpp)

target_link_libraries(server_unittests PRIVATE slang::slang Catch2::Catch2)
target_compile_definitions(server_unittests PRIVATE UNITTESTS SLANG_SERVER_TESTS)
target_include_directories(server_unittests PRIVATE ${PROJECT_SOURCE_DIR}/external/slang/tests/unittests)

add_test(NAME server_unittests COMMAND server_unittests)

add_custom_command(
  TARGET server_unittests
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/../data
    ${CMAKE_CURRENT_BINARY_DIR}/../data)

add_custom_command(
  TARGET server_unittests
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/golden
          ${CMAKE_CURRENT_BINARY_DIR}/golden)

if(CMAKE_SYSTEM_NAME MATCHES "Windows")
  target_sources(server_unittests
                 PRIVATE ${PROJECT_SOURCE_DIR}/scripts/win32.manifest)
endif()
