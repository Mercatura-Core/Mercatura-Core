# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
cmake_minimum_required(VERSION 3.22)
project(mercaweave_research LANGUAGES C CXX)
# Standalone configure only; never included by production CMakeLists.txt.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
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
add_library(mercaweave_context STATIC auth_context.cpp)
target_link_libraries(mercaweave_context PUBLIC mercaweave_native)
add_executable(mercaweave_context_tests context_tests.cpp fixtures.cpp)
target_link_libraries(mercaweave_context_tests PRIVATE mercaweave_context Threads::Threads)
add_test(NAME fail_closed_context COMMAND mercaweave_context_tests)
add_library(mercaweave_block_work STATIC block_work.cpp)
target_link_libraries(mercaweave_block_work PUBLIC mercaweave_context)
add_executable(mercaweave_block_tests block_tests.cpp fixtures.cpp)
target_link_libraries(mercaweave_block_tests PRIVATE mercaweave_block_work)
add_test(NAME checked_block_work COMMAND mercaweave_block_tests)
add_executable(mercaweave_readiness_bench readiness_bench.cpp allocation_probe.cpp fixtures.cpp)
target_link_libraries(mercaweave_readiness_bench PRIVATE mercaweave_block_work)
add_test(NAME block_measurement_smoke COMMAND mercaweave_readiness_bench block small --smoke)
# Link selected unmodified policy routines. Function sections allow the standalone
# executable to discard CCoinsView/node-dependent routines it never invokes.
add_executable(mercaweave_malleability_tests malleability_tests.cpp fixtures.cpp
  ${MCA_SRC}/script/solver.cpp ${MCA_SRC}/policy/policy.cpp ${MCA_SRC}/policy/feerate.cpp
  ${MCA_SRC}/util/check.cpp ${MCA_SRC}/clientversion.cpp)
# Debug policy code needs real production assertion support. Generate diagnostic
# metadata from the existing project values, without configuring a node build.
file(STRINGS "${MCA_SRC}/../CMakeLists.txt" research_metadata
  REGEX "^set\\((CLIENT_NAME|CLIENT_VERSION_(MAJOR|MINOR|BUILD|IS_RELEASE)|MERCATURA_VERSION_(MAJOR|MINOR|BUILD|STRING)|COPYRIGHT_YEAR) ")
foreach(metadata_line IN LISTS research_metadata)
  cmake_language(EVAL CODE "${metadata_line}")
endforeach()
set(COPYRIGHT_HOLDERS "The %s developers")
set(COPYRIGHT_HOLDERS_FINAL "The ${CLIENT_NAME} developers")
set(CLIENT_BUGREPORT "https://github.com/Mercatura-Core/Mercatura-Core/issues")
set(PROJECT_HOMEPAGE_URL "https://github.com/Mercatura-Core/Mercatura-Core")
set(CLIENT_VERSION_STRING "${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_BUILD}")
configure_file(${MCA_SRC}/../cmake/bitcoin-build-config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/diagnostic/bitcoin-build-config.h @ONLY)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/diagnostic/bitcoin-build-info.h "// Standalone research diagnostics only.\n")
target_include_directories(mercaweave_malleability_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/diagnostic)
target_compile_options(mercaweave_malleability_tests PRIVATE -ffunction-sections -fdata-sections)
target_link_options(mercaweave_malleability_tests PRIVATE -Wl,--gc-sections)
target_link_libraries(mercaweave_malleability_tests PRIVATE mercaweave_context)
# GCC retains UBSan vptr metadata for discarded CCoinsView-dependent functions
# in policy.cpp, requiring node RTTI that this standalone target does not link.
# Exclude ONLY vptr for that unmodified production TU. All other ASan/UBSan
# checks, and every check on new research code, remain enabled.
if(MERCAWEAVE_SANITIZERS)
  set_source_files_properties(${MCA_SRC}/policy/policy.cpp PROPERTIES COMPILE_OPTIONS
    "$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,mercaweave_malleability_tests>:-fno-sanitize=vptr>")
endif()
add_test(NAME malleability_children_and_policy COMMAND mercaweave_malleability_tests)

# WO04: isolated checker ABI is never linked with mercaweave_native in one binary.
# This standalone project is not included by normal production CMake.
add_library(mercaweave_native_isolated STATIC
  native_v2.cpp ${MCA_SRC}/script/interpreter.cpp ${MCA_SRC}/pubkey.cpp
  ${MCA_SRC}/hash.cpp ${MCA_SRC}/crypto/sha1.cpp ${MCA_SRC}/crypto/sha512.cpp
  ${MCA_SRC}/crypto/hmac_sha512.cpp)
target_compile_definitions(mercaweave_native_isolated PUBLIC MERCATURA_MERCAWEAVE_RESEARCH_TEST_ONLY=1)
target_link_libraries(mercaweave_native_isolated PUBLIC mercaweave PRIVATE secp256k1)
add_library(mercaweave_context_isolated STATIC auth_context.cpp)
target_link_libraries(mercaweave_context_isolated PUBLIC mercaweave_native_isolated)
add_library(mercaweave_block_isolated STATIC block_work.cpp)
target_link_libraries(mercaweave_block_isolated PUBLIC mercaweave_context_isolated)
add_executable(mercaweave_normal_control inactive_control.cpp fixtures.cpp)
target_link_libraries(mercaweave_normal_control PRIVATE mercaweave_native)
add_executable(mercaweave_isolated_control inactive_control.cpp fixtures.cpp)
target_link_libraries(mercaweave_isolated_control PRIVATE mercaweave_context_isolated)
add_test(NAME normal_inactive_control COMMAND mercaweave_normal_control)
add_test(NAME isolated_missing_context_control COMMAND mercaweave_isolated_control)

add_library(mercaweave_coin_validation STATIC
  ${MCA_SRC}/coins.cpp ${MCA_SRC}/consensus/tx_check.cpp ${MCA_SRC}/consensus/tx_verify.cpp
  ${MCA_SRC}/policy/policy.cpp ${MCA_SRC}/policy/feerate.cpp ${MCA_SRC}/script/solver.cpp
  ${MCA_SRC}/util/check.cpp ${MCA_SRC}/clientversion.cpp ${MCA_SRC}/util/moneystr.cpp
  ${MCA_SRC}/util/hasher.cpp ${MCA_SRC}/crypto/siphash.cpp
  ${MCA_SRC}/random.cpp ${MCA_SRC}/randomenv.cpp ${MCA_SRC}/crypto/chacha20.cpp
  ${MCA_SRC}/logging.cpp ${MCA_SRC}/util/time.cpp ${MCA_SRC}/util/threadnames.cpp
  ${MCA_SRC}/support/lockedpool.cpp ${MCA_SRC}/util/fs.cpp)
target_include_directories(mercaweave_coin_validation PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/diagnostic)
target_compile_options(mercaweave_coin_validation PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(mercaweave_coin_validation PUBLIC mercaweave_context_isolated Threads::Threads)
target_link_options(mercaweave_coin_validation INTERFACE -Wl,--gc-sections)
add_executable(mercaweave_coin_probe coin_view_probe.cpp fixtures.cpp)
target_link_libraries(mercaweave_coin_probe PRIVATE mercaweave_coin_validation)
add_test(NAME authoritative_coin_view_probe COMMAND mercaweave_coin_probe)
add_library(mercaweave_integration STATIC isolated_validation.cpp)
target_link_libraries(mercaweave_integration PUBLIC mercaweave_coin_validation mercaweave_block_isolated)
add_executable(mercaweave_integration_tests integration_tests.cpp fixtures.cpp)
target_link_libraries(mercaweave_integration_tests PRIVATE mercaweave_integration)
add_test(NAME isolated_validation_integration COMMAND mercaweave_integration_tests)
add_test(NAME research_compile_and_cache_isolation
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_isolation.py ${CMAKE_CURRENT_BINARY_DIR})
add_executable(mercaweave_integration_bench integration_bench.cpp allocation_probe.cpp fixtures.cpp)
target_link_libraries(mercaweave_integration_bench PRIVATE mercaweave_integration)
add_test(NAME integrated_resource_measurement_smoke COMMAND mercaweave_integration_bench small --smoke)

# WO05: no normal wallet/node target includes this standalone project.
add_library(mercaweave_wallet_primitives STATIC
  ${MCA_SRC}/wallet/wallet.cpp ${MCA_SRC}/wallet/crypter.cpp
  ${MCA_SRC}/wallet/coinselection.cpp ${MCA_SRC}/psbt.cpp
  ${MCA_SRC}/crypto/aes.cpp ${MCA_SRC}/crypto/ctaes/ctaes.c
  ${MCA_SRC}/bech32.cpp ${MCA_SRC}/script/signingprovider.cpp
  # UBSan retains native signature-creator RTTI; link its real implementation
  # and miniscript static-initializer dependencies, never substitute test RTTI.
  ${MCA_SRC}/script/sign.cpp ${MCA_SRC}/script/miniscript.cpp)
target_include_directories(mercaweave_wallet_primitives PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/diagnostic)
target_compile_definitions(mercaweave_wallet_primitives PUBLIC MERCATURA_MERCAWEAVE_WALLET_RESEARCH_TEST_ONLY=1)
target_compile_options(mercaweave_wallet_primitives PRIVATE -ffunction-sections -fdata-sections)
target_link_libraries(mercaweave_wallet_primitives PUBLIC mercaweave_integration)
add_executable(mercaweave_wallet_primitive_probe wallet_primitive_probe.cpp fixtures.cpp)
target_link_libraries(mercaweave_wallet_primitive_probe PRIVATE mercaweave_wallet_primitives)
add_test(NAME native_wallet_primitives COMMAND mercaweave_wallet_primitive_probe)
add_library(mercaweave_wallet STATIC research_wallet.cpp wallet_psbt.cpp wallet_funding.cpp)
target_compile_definitions(mercaweave_wallet PRIVATE MERCAWEAVE_RESEARCH_STORAGE_ROOT="${MCA_SRC}/../work/wo05-wallets")
target_link_libraries(mercaweave_wallet PUBLIC mercaweave_wallet_primitives)
add_executable(mercaweave_wallet_tests wallet_tests.cpp)
target_link_libraries(mercaweave_wallet_tests PRIVATE mercaweave_wallet)
add_test(NAME grouped_wallet_keys_and_storage COMMAND mercaweave_wallet_tests)
add_test(NAME research_wallet_compile_isolation
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_wallet_isolation.py ${CMAKE_CURRENT_BINARY_DIR})
add_executable(mercaweave_psbt_tests psbt_tests.cpp)
target_link_libraries(mercaweave_psbt_tests PRIVATE mercaweave_wallet)
add_test(NAME grouped_partial_psbt_and_signing COMMAND mercaweave_psbt_tests)
add_executable(mercaweave_wallet_bench wallet_bench.cpp)
target_link_libraries(mercaweave_wallet_bench PRIVATE mercaweave_wallet)
add_test(NAME wallet_workload_measurement_smoke COMMAND mercaweave_wallet_bench --smoke)

# WO06: production-facing Qt objects have no research wallet/checker definitions.
option(MERCAWEAVE_QT_TESTS "Build offline Qt presentation and planning tests" OFF)
if(MERCAWEAVE_QT_TESTS)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Test Concurrent Network)
  add_library(mercaweave_qt_normal STATIC ${MCA_SRC}/qt/mercaweavemodel.cpp ${MCA_SRC}/qt/mercaweavepanel.cpp)
  set_target_properties(mercaweave_qt_normal PROPERTIES AUTOMOC ON)
  target_compile_definitions(mercaweave_qt_normal PUBLIC QT_NO_KEYWORDS)
  target_link_libraries(mercaweave_qt_normal PUBLIC Qt6::Widgets)
  add_library(mercaweave_qt_isolated STATIC ${MCA_SRC}/qt/mercaweavemodel.cpp ${MCA_SRC}/qt/mercaweavepanel.cpp)
  set_target_properties(mercaweave_qt_isolated PROPERTIES AUTOMOC ON)
  target_compile_definitions(mercaweave_qt_isolated PUBLIC QT_NO_KEYWORDS MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY=1)
  target_link_libraries(mercaweave_qt_isolated PUBLIC Qt6::Widgets Qt6::Concurrent)
  foreach(target IN ITEMS mercaweave_qt_normal mercaweave_qt_isolated)
    target_include_directories(${target} PUBLIC ${MCA_SRC})
  endforeach()
endif()
if(MERCAWEAVE_QT_TESTS)
  add_executable(mercaweave_qt_normal_tests qt_model_tests.cpp)
  target_link_libraries(mercaweave_qt_normal_tests PRIVATE mercaweave_qt_normal Qt6::Test)
  add_test(NAME qt_production_capability_closed COMMAND mercaweave_qt_normal_tests)
  add_executable(mercaweave_qt_model_tests qt_model_tests.cpp)
  target_link_libraries(mercaweave_qt_model_tests PRIVATE mercaweave_qt_isolated Qt6::Test)
  add_test(NAME qt_capability_lifetime_and_previews COMMAND mercaweave_qt_model_tests)
endif()
if(MERCAWEAVE_QT_TESTS)
  add_library(mercaweave_reward_planner STATIC reward_planner.cpp)
  target_compile_definitions(mercaweave_reward_planner PUBLIC MERCATURA_MERCAWEAVE_QT_RESEARCH_TEST_ONLY=1)
  target_link_libraries(mercaweave_reward_planner PUBLIC mercaweave_wallet)
  add_executable(mercaweave_reward_tests reward_tests.cpp)
  target_link_libraries(mercaweave_reward_tests PRIVATE mercaweave_reward_planner)
  add_test(NAME grouped_reward_policy_and_scheduler COMMAND mercaweave_reward_tests)
endif()
