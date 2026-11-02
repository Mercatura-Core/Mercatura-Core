# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
cmake_minimum_required(VERSION 3.22)
project(mercaweave_research LANGUAGES C CXX)
# Standalone configure only; never included by production CMakeLists.txt.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Match Mercatura's requirement that verification is built with assertions.
add_compile_options(-UNDEBUG)
option(MERCAWEAVE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
get_filename_component(MCA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(MERCAWEAVE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
add_library(mercaweave STATIC
  mercaweave.cpp
  ${MCA_SRC}/primitives/transaction.cpp
  ${MCA_SRC}/script/script.cpp
  ${MCA_SRC}/uint256.cpp
  ${MCA_SRC}/crypto/hex_base.cpp
  ${MCA_SRC}/crypto/sha256.cpp
  ${MCA_SRC}/crypto/ripemd160.cpp
  ${MCA_SRC}/crypto/sha3.cpp
  ${MCA_SRC}/crypto/hmac_sha3_384.cpp
  ${MCA_SRC}/crypto/hkdf_sha3_384.cpp
  ${MCA_SRC}/crypto/mercatura_pqderive.cpp
  ${MCA_SRC}/crypto/mercatura_pqhash.cpp
  ${MCA_SRC}/crypto/mercatura_pqkey.cpp
  ${MCA_SRC}/support/cleanse.cpp
  ${MCA_SRC}/crypto/mercatura_mldsa.c
  ${MCA_SRC}/crypto/mldsa_native/mldsa/mldsa_native.c)
target_include_directories(mercaweave PUBLIC ${MCA_SRC} PRIVATE ${MCA_SRC}/crypto/mldsa_native/mldsa)
target_compile_definitions(mercaweave PRIVATE DISABLE_OPTIMIZED_SHA256 MLD_CONFIG_PARAMETER_SET=65)
set_source_files_properties(${MCA_SRC}/crypto/mldsa_native/mldsa/mldsa_native.c
  PROPERTIES COMPILE_DEFINITIONS MLD_CONFIG_CORE_API_ONLY)
target_compile_options(mercaweave PRIVATE -Wall -Wextra -Wpedantic)
add_executable(mercaweave_tests tests.cpp fixtures.cpp)
target_link_libraries(mercaweave_tests PRIVATE mercaweave)
add_executable(mercaweave_bench bench.cpp fixtures.cpp)
target_link_libraries(mercaweave_bench PRIVATE mercaweave)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME locked_python_vectors COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reference.py)
add_test(NAME mercaweave_tests COMMAND mercaweave_tests)
add_test(NAME cross_language_vectors COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/crosscheck.py $<TARGET_FILE:mercaweave_tests>)

# Test-only link to unmodified production verification. Vendored secp256k1 is
# needed by interpreter/pubkey objects even though Mercatura forbids classical
# signature ownership in Script. No new third-party dependency is downloaded.
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_CTIME_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
set(SECP256K1_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${MCA_SRC}/secp256k1 ${CMAKE_CURRENT_BINARY_DIR}/secp256k1 EXCLUDE_FROM_ALL)
add_library(mercaweave_native STATIC
  native_v2.cpp
  ${MCA_SRC}/script/interpreter.cpp
  ${MCA_SRC}/pubkey.cpp
  ${MCA_SRC}/hash.cpp
  ${MCA_SRC}/crypto/sha1.cpp
  ${MCA_SRC}/crypto/sha512.cpp
  ${MCA_SRC}/crypto/hmac_sha512.cpp)
target_link_libraries(mercaweave_native PUBLIC mercaweave PRIVATE secp256k1)
add_executable(mercaweave_native_tests native_tests.cpp fixtures.cpp)
target_link_libraries(mercaweave_native_tests PRIVATE mercaweave_native)
add_test(NAME native_v2_interoperability COMMAND mercaweave_native_tests)
add_executable(mercaweave_parser_tests parser_tests.cpp fixtures.cpp)
target_link_libraries(mercaweave_parser_tests PRIVATE mercaweave)
add_test(NAME bounded_parsers COMMAND mercaweave_parser_tests)
find_package(Threads REQUIRED)
add_executable(mercaweave_attack_tests attack_tests.cpp fixtures.cpp)
target_link_libraries(mercaweave_attack_tests PRIVATE mercaweave Threads::Threads)
add_test(NAME hostile_followers_and_context COMMAND mercaweave_attack_tests)
add_executable(mercaweave_qualification_bench qualification_bench.cpp fixtures.cpp)
target_link_libraries(mercaweave_qualification_bench PRIVATE mercaweave)
add_test(NAME resource_qualification_smoke COMMAND mercaweave_qualification_bench 10 1)
