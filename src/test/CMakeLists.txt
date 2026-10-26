# Copyright (c) 2023-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Do not use generator expressions in test sources because the
# SOURCES property is processed to gather test suite macros.
add_executable(test_bitcoin
  main.cpp
  addrman_tests.cpp
  allocator_tests.cpp
  amount_tests.cpp
  argsman_tests.cpp
  arith_uint256_tests.cpp
  banman_tests.cpp
  base32_tests.cpp
  base58_tests.cpp
  base64_tests.cpp
  bech32_tests.cpp
  bip32_tests.cpp
  bip324_tests.cpp
  blockchain_tests.cpp
  blockencodings_tests.cpp
  blockfilter_index_tests.cpp
  blockfilter_tests.cpp
  blockmanager_tests.cpp
  blockstorage_durability_tests.cpp
  bloom_tests.cpp
  bswap_tests.cpp
  caches_tests.cpp
  chain_tests.cpp
  chainstate_write_tests.cpp
  checkqueue_tests.cpp
  cluster_linearize_tests.cpp
  coins_tests.cpp
  coinscachepair_tests.cpp
  coinstatsindex_tests.cpp
  coinsviewoverlay_tests.cpp
  common_url_tests.cpp
  compress_tests.cpp
  crypto_tests.cpp
  mercahash_v2_tests.cpp
  mercahash_activation_tests.cpp
  cpu_miner_tests.cpp
  worker_limits_tests.cpp
  witness_resource_tests.cpp
  mining_statistics_tests.cpp
  gpu_capacity_tests.cpp
  gpu_scheduling_tests.cpp
  gpu_miner_tests.cpp
  mercahash_gpu_tests.cpp
  mercahash_gpu_sync_tests.cpp
  cuda_binding_tests.cpp
  cuckoocache_tests.cpp
  dbwrapper_tests.cpp
  denialofservice_tests.cpp
  descriptor_tests.cpp
  disconnected_transactions.cpp
  feefrac_tests.cpp
  feerounder_tests.cpp
  flatfile_tests.cpp
  fs_tests.cpp
  getarg_tests.cpp
  hash_tests.cpp
  headers_sync_chainwork_tests.cpp
  headers_replay_tests.cpp
  httpserver_tests.cpp
  i2p_tests.cpp
  interfaces_tests.cpp
  key_io_tests.cpp
  key_tests.cpp
  logging_tests.cpp
  mempool_tests.cpp
  merkle_tests.cpp
  merkleblock_tests.cpp
  miner_tests.cpp
  miniminer_tests.cpp
  miniscript_tests.cpp
  minisketch_tests.cpp
  mock_process.cpp
  multisig_tests.cpp
  bip328_tests.cpp
  net_peer_connection_tests.cpp
  net_peer_eviction_tests.cpp
  net_tests.cpp
  pq_p2p_tests.cpp
  pq_identity_tests.cpp
  pq_transport_tests.cpp
  netbase_tests.cpp
  node_init_tests.cpp
  node_warnings_tests.cpp
  orphanage_tests.cpp
  pcp_tests.cpp
  peerman_tests.cpp
  pmt_tests.cpp
  policyestimator_tests.cpp
  pool_tests.cpp
  block_capacity_tests.cpp
  testnet_capacity_tests.cpp
  blockchunk_tests.cpp
  pow_tests.cpp
  prevector_tests.cpp
  private_broadcast_tests.cpp
  raii_event_tests.cpp
  random_tests.cpp
  rbf_tests.cpp
  rest_tests.cpp
  result_tests.cpp
  reverselock_tests.cpp
  rpc_tests.cpp
  sanity_tests.cpp
  scheduler_tests.cpp
  script_assets_tests.cpp
  script_p2sh_tests.cpp
  script_parse_tests.cpp
  script_segwit_tests.cpp
  script_standard_tests.cpp
  script_tests.cpp
  scriptnum_tests.cpp
  serfloat_tests.cpp
  serialize_tests.cpp
  settings_tests.cpp
  sighash_tests.cpp
  sigopcount_tests.cpp
  skiplist_tests.cpp
  sock_tests.cpp
  span_tests.cpp
  streams_tests.cpp
  sync_tests.cpp
  system_ram_tests.cpp
  system_tests.cpp
  testnet4_miner_tests.cpp
  threadpool_tests.cpp
  timeoffsets_tests.cpp
  torcontrol_tests.cpp
  transaction_tests.cpp
  translation_tests.cpp
  txdownload_tests.cpp
  txgraph_tests.cpp
  txindex_tests.cpp
  txospenderindex_tests.cpp
  txpackage_tests.cpp
  txreconciliation_tests.cpp
  txrequest_tests.cpp
  txvalidation_tests.cpp
  txvalidationcache_tests.cpp
  uint256_tests.cpp
  util_check_tests.cpp
  util_expected_tests.cpp
  util_string_tests.cpp
  util_tests.cpp
  util_threadnames_tests.cpp
  util_trace_tests.cpp
  mercatura_controller_tests.cpp
  mercatura_emission_tests.cpp
  mercatura_fixedpoint_tests.cpp
  validation_block_tests.cpp
  validation_chainstate_tests.cpp
  validation_chainstatemanager_tests.cpp
  validation_flush_tests.cpp
  validation_tests.cpp
  validationinterface_tests.cpp
  versionbits_tests.cpp
)

include(TargetDataSources)
target_json_data_sources(test_bitcoin
  data/pq_p2p_candidate1.json
  data/pq_p2p_packets.json
  data/base58_encode_decode.json
  data/bip341_wallet_vectors.json
  data/blockfilters.json
  data/key_io_invalid.json
  data/key_io_valid.json
  data/script_tests.json
  data/sighash.json
  data/tx_invalid.json
  data/tx_valid.json
)
target_raw_data_sources(test_bitcoin NAMESPACE test::data
  data/asmap.raw
)

add_windows_application_manifest(test_bitcoin)

# Small independent runner: exercises the very same permanent protocol tests
# without waiting for the complete node/unit-test executable to link.
add_executable(pq_stagea_tests EXCLUDE_FROM_ALL
  ../../contrib/devtools/pq-stagea-test-main.cpp
  pq_p2p_tests.cpp
  pq_identity_tests.cpp
  ../net_pq_protocol.cpp
  ../net_pq_identity_format.cpp
)
target_link_libraries(pq_stagea_tests PRIVATE core_interface bitcoin_crypto univalue Boost::headers)
target_json_data_sources(pq_stagea_tests
  data/pq_p2p_candidate1.json
  data/pq_p2p_packets.json
)

add_executable(pq_transport_runner EXCLUDE_FROM_ALL
  ../../contrib/devtools/pq-stagea-test-main.cpp
  pq_transport_tests.cpp
)
target_link_libraries(pq_transport_runner PRIVATE core_interface test_util bitcoin_cli bitcoin_node bitcoin_consensus minisketch secp256k1 Boost::headers libevent::extra)

add_executable(pq_resource_probe EXCLUDE_FROM_ALL ../../contrib/devtools/pq-resource-probe.cpp)
target_link_libraries(pq_resource_probe PRIVATE core_interface test_util bitcoin_cli bitcoin_node bitcoin_consensus minisketch secp256k1 Boost::headers libevent::extra)

add_executable(pq_fuzz_smoke EXCLUDE_FROM_ALL
  ../../contrib/devtools/pq-fuzz-smoke.cpp
  fuzz/pq_transport.cpp
  ../net_pq_protocol.cpp
  ../net_pq_identity_format.cpp
)
target_link_libraries(pq_fuzz_smoke PRIVATE core_interface bitcoin_crypto)

# A focused genuine libFuzzer binary. Configure a separate build with Clang and
# -DBUILD_FUZZ_BINARY=ON -DSANITIZERS=fuzzer,address,undefined.
# Normal node builds do not build this.
if(BUILD_FUZZ_BINARY AND SANITIZERS MATCHES "(^|,)fuzzer(,|$)")
  add_executable(pq_fuzz EXCLUDE_FROM_ALL
    ../../contrib/devtools/pq-libfuzzer-main.cpp
    fuzz/pq_transport.cpp
    ../net_pq_protocol.cpp
    ../net_pq_identity_format.cpp
  )
  target_link_libraries(pq_fuzz PRIVATE core_interface bitcoin_crypto fuzzer_interface)
endif()

target_link_libraries(test_bitcoin
  core_interface
  test_util
  bitcoin_cli
  bitcoin_node
  bitcoin_consensus
  minisketch
  secp256k1
  Boost::headers
  libevent::extra
  $<TARGET_NAME_IF_EXISTS:USDT::headers>
)

if(ENABLE_WALLET)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/wallet/test wallet)
endif()

if(ENABLE_IPC)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/ipc/test ipc)
endif()

function(add_boost_test source_file)
  if(NOT EXISTS ${source_file})
    return()
  endif()

  file(READ "${source_file}" source_file_content)
  string(REGEX
    MATCHALL "(BOOST_FIXTURE_TEST_SUITE|BOOST_AUTO_TEST_SUITE)\\(([A-Za-z0-9_]+)"
    test_suite_macro "${source_file_content}"
  )
  list(TRANSFORM test_suite_macro
    REPLACE "(BOOST_FIXTURE_TEST_SUITE|BOOST_AUTO_TEST_SUITE)\\(" ""
  )
  # The mock_process test suite does not contain unit tests.
  list(REMOVE_ITEM test_suite_macro "mock_process")
  foreach(test_suite_name IN LISTS test_suite_macro)
    add_test(NAME ${test_suite_name}
      COMMAND test_bitcoin --run_test=${test_suite_name} --catch_system_error=no --log_level=test_suite -- DEBUG_LOG_OUT
    )
    set_property(TEST ${test_suite_name} PROPERTY
      SKIP_REGULAR_EXPRESSION
        "no test cases matching filter"
        "skipping script_assets_test"
        "skipping total_ram"
    )
  endforeach()
endfunction()

function(add_all_test_targets)
  get_target_property(test_source_dir test_bitcoin SOURCE_DIR)
  get_target_property(test_sources test_bitcoin SOURCES)
  foreach(test_source ${test_sources})
    cmake_path(IS_RELATIVE test_source result)
    if(result)
      cmake_path(APPEND test_source_dir ${test_source} OUTPUT_VARIABLE test_source)
    endif()
    add_boost_test(${test_source})
  endforeach()
endfunction()

add_all_test_targets()

install_binary_component(test_bitcoin INTERNAL)
