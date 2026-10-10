# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
cmake_minimum_required(VERSION 3.22)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(MercaturaPool VERSION 1.0 LANGUAGES CXX)
  enable_testing()
endif()
if(NOT UNIX)
  message(FATAL_ERROR "M3 pool transport currently requires Linux/macOS POSIX sockets; use WSL2 on Windows.")
endif()
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
  add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
get_filename_component(MERCATURA_SRC "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
# Compile Core's existing primitives and frozen hash implementation directly.
# This small target links no node, chainstate, wallet, or blockchain database.
add_library(mercatura_pool_core STATIC
  ${MERCATURA_SRC}/arith_uint256.cpp
  ${MERCATURA_SRC}/uint256.cpp
  ${MERCATURA_SRC}/primitives/block.cpp
  ${MERCATURA_SRC}/primitives/transaction.cpp
  ${MERCATURA_SRC}/script/script.cpp
  ${MERCATURA_SRC}/consensus/merkle.cpp
  ${MERCATURA_SRC}/crypto/hex_base.cpp
  ${MERCATURA_SRC}/crypto/sha256.cpp
  ${MERCATURA_SRC}/crypto/ripemd160.cpp
  ${MERCATURA_SRC}/crypto/sha3.cpp
  ${MERCATURA_SRC}/crypto/mercahash.cpp
  ${MERCATURA_SRC}/util/strencodings.cpp
  ${MERCATURA_SRC}/mining/worker_limits.cpp
  ${MERCATURA_SRC}/univalue/lib/univalue.cpp
  ${MERCATURA_SRC}/univalue/lib/univalue_get.cpp
  ${MERCATURA_SRC}/univalue/lib/univalue_read.cpp
  ${MERCATURA_SRC}/univalue/lib/univalue_write.cpp
)
target_compile_features(mercatura_pool_core PUBLIC cxx_std_20)
target_include_directories(mercatura_pool_core PUBLIC "${MERCATURA_SRC}" "${MERCATURA_SRC}/univalue/include")
# Use Core's portable SHA256 path, avoiding architecture-specific build probes.
target_compile_definitions(mercatura_pool_core PRIVATE DISABLE_OPTIMIZED_SHA256)
target_compile_options(mercatura_pool_core PUBLIC -UNDEBUG)
add_library(mercatura_pool STATIC accounting.cpp config.cpp job.cpp ledger.cpp protocol.cpp rpc.cpp service.cpp transport.cpp verifier.cpp)
target_link_libraries(mercatura_pool PUBLIC mercatura_pool_core OpenSSL::SSL OpenSSL::Crypto SQLite3::SQLite3 CURL::libcurl Threads::Threads)
target_compile_options(mercatura_pool PRIVATE -Wall -Wextra -Wpedantic)
add_executable(mercatura-pool main.cpp)
target_link_libraries(mercatura-pool PRIVATE mercatura_pool)
add_executable(test_mercatura_pool tests.cpp)
target_link_libraries(test_mercatura_pool PRIVATE mercatura_pool)
add_test(NAME mercatura_pool COMMAND test_mercatura_pool)
add_executable(pool-hash-test-client hash_client.cpp)
target_link_libraries(pool-hash-test-client PRIVATE mercatura_pool)
install(TARGETS mercatura-pool RUNTIME DESTINATION bin)
