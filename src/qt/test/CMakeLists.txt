# Copyright (c) 2024-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

set(CMAKE_AUTOMOC_MOC_OPTIONS "-p${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(test_bitcoin-qt
  apptests.cpp
  optiontests.cpp
  rpcnestedtests.cpp
  test_main.cpp
  uritests.cpp
  util.cpp
  ../../init/bitcoin-qt.cpp
)

add_windows_application_manifest(test_bitcoin-qt)

target_link_libraries(test_bitcoin-qt
  core_interface
  bitcoinqt
  test_util
  bitcoin_node
  Boost::headers
  Qt6::Test
)

import_plugins(test_bitcoin-qt)

if(ENABLE_WALLET)
  target_sources(test_bitcoin-qt
    PRIVATE
      addressbooktests.cpp
      wallettests.cpp
      ../../wallet/test/wallet_test_fixture.cpp
  )
endif()

add_test(NAME test_bitcoin-qt
  COMMAND test_bitcoin-qt
)
if(WIN32 AND VCPKG_TARGET_TRIPLET)
  set(plugin_path "$<SHELL_PATH:$<PATH:GET_PARENT_PATH,$<PATH:GET_PARENT_PATH,$<TARGET_PROPERTY:Qt6::QWindowsIntegrationPlugin,LOCATION_$<CONFIG>>>>>")
  set_property(TEST test_bitcoin-qt APPEND PROPERTY
    ENVIRONMENT_MODIFICATION
      # On Windows, vcpkg configures Qt with `-opengl dynamic`, which makes
      # the "minimal" platform plugin unusable due to internal Qt bugs.
      QT_QPA_PLATFORM=set:windows
      QT_PLUGIN_PATH=set:${plugin_path}
  )
  unset(plugin_path)
endif()

install_binary_component(test_bitcoin-qt INTERNAL)

if(ENABLE_WALLET)
  # Focused native miner lifecycle test, independently runnable from inherited
  # GUI/wallet test cases and their legacy Bitcoin assumptions.
  add_executable(test_mining-qt miningtests.cpp ../../init/bitcoin-qt.cpp)
  target_link_libraries(test_mining-qt
    core_interface bitcoinqt test_util bitcoin_node bitcoin_wallet Boost::headers Qt6::Test
  )
  import_plugins(test_mining-qt)
  add_test(NAME test_mining-qt COMMAND test_mining-qt)
  if(WIN32 AND VCPKG_TARGET_TRIPLET)
    set(plugin_path "$<SHELL_PATH:$<PATH:GET_PARENT_PATH,$<PATH:GET_PARENT_PATH,$<TARGET_PROPERTY:Qt6::QWindowsIntegrationPlugin,LOCATION_$<CONFIG>>>>>")
    set_property(TEST test_mining-qt APPEND PROPERTY ENVIRONMENT_MODIFICATION
      QT_QPA_PLATFORM=set:windows
      QT_PLUGIN_PATH=set:${plugin_path}
    )
    unset(plugin_path)
  endif()
  add_executable(pool-native-test-client poolminer.cpp)
  target_link_libraries(pool-native-test-client PRIVATE core_interface bitcoinqt bitcoin_node bitcoin_wallet univalue Boost::headers Qt6::Network)
  import_plugins(pool-native-test-client)
  # Authoritative M3 fixtures use its accounting/job code and OpenSSL integers,
  # without the optional Linux service, CURL or coordinator database. This is
  # a test dependency only; ordinary GUI builds use Qt's TLS backend.
  find_package(OpenSSL QUIET)
  if(OpenSSL_FOUND)
    add_executable(test_poolclient-qt poolclienttests.cpp ../../pool/accounting.cpp ../../pool/job.cpp)
    target_link_libraries(test_poolclient-qt PRIVATE core_interface bitcoinqt test_util bitcoin_node bitcoin_wallet OpenSSL::Crypto Boost::headers Qt6::Test Qt6::Network)
    import_plugins(test_poolclient-qt)
    add_test(NAME test_poolclient-qt COMMAND test_poolclient-qt)
    if(WIN32 AND VCPKG_TARGET_TRIPLET)
      set(plugin_path "$<SHELL_PATH:$<PATH:GET_PARENT_PATH,$<PATH:GET_PARENT_PATH,$<TARGET_PROPERTY:Qt6::QWindowsIntegrationPlugin,LOCATION_$<CONFIG>>>>>")
      set_property(TEST test_poolclient-qt APPEND PROPERTY ENVIRONMENT_MODIFICATION
        QT_PLUGIN_PATH=set:${plugin_path}
      )
      unset(plugin_path)
    endif()
  else()
    message(STATUS "M4 authoritative pool-client tests omitted: OpenSSL fixture dependency unavailable")
  endif()
endif()
