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
  add_windows_application_manifest(test_mining-qt)
  target_link_libraries(test_mining-qt
    core_interface bitcoinqt test_util bitcoin_node bitcoin_wallet Boost::headers Qt6::Test
  )
  import_plugins(test_mining-qt)
  # Retain diagnostics through the large notification burst; heartbeat and
  # lifecycle assertions keep their existing deadlines.
  add_test(NAME test_mining-qt COMMAND test_mining-qt -maxwarnings 10000)
  if(WIN32 AND VCPKG_TARGET_TRIPLET)
    set(plugin_path "$<SHELL_PATH:$<PATH:GET_PARENT_PATH,$<PATH:GET_PARENT_PATH,$<TARGET_PROPERTY:Qt6::QWindowsIntegrationPlugin,LOCATION_$<CONFIG>>>>>")
    set_property(TEST test_mining-qt APPEND PROPERTY ENVIRONMENT_MODIFICATION
      QT_QPA_PLATFORM=set:windows
      QT_PLUGIN_PATH=set:${plugin_path}
    )
    unset(plugin_path)
  endif()
endif()

if(ENABLE_WALLET)
  add_executable(test_mining_app-qt miningapptests.cpp ../../init/bitcoin-qt.cpp)
  add_windows_application_manifest(test_mining_app-qt)
  target_link_libraries(test_mining_app-qt core_interface bitcoinqt test_util bitcoin_node bitcoin_wallet Boost::headers Qt6::Test)
  import_plugins(test_mining_app-qt)
  foreach(workers IN ITEMS 1 2)
    add_test(NAME test_mining_app-qt-${workers} COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/mining/qt_restart.py $<TARGET_FILE:test_mining_app-qt>)
    set_tests_properties(test_mining_app-qt-${workers} PROPERTIES TIMEOUT 200 SKIP_RETURN_CODE 77
      ENVIRONMENT "MERCATURA_TEST_WORKERS=${workers}")
  endforeach()
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND BUILD_DAEMON)
    foreach(schedule IN ITEMS v2 transition)
      add_test(NAME test_mining_app-qt-pq-${schedule}
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/contrib/devtools/qualify-mercahash.py
          --build-dir ${PROJECT_BINARY_DIR} --mode cpu --schedule ${schedule}
          --workers 1 --seconds 10 --deadline 180 --offscreen --pq
          --output-parent ${PROJECT_BINARY_DIR}/test/mining-pq)
      set_tests_properties(test_mining_app-qt-pq-${schedule} PROPERTIES
        TIMEOUT 200 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
    endforeach()
  endif()
  option(MERCATURA_ENABLE_EXTREME_QT_STRESS_TESTS "Register extreme rapid-wallet stress test" OFF)
  if(MERCATURA_ENABLE_EXTREME_QT_STRESS_TESTS)
    add_test(NAME test_mining_app-qt-rapid-wallet
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/test/mining/qt_restart.py $<TARGET_FILE:test_mining_app-qt>)
    set_tests_properties(test_mining_app-qt-rapid-wallet PROPERTIES TIMEOUT 200 SKIP_RETURN_CODE 77
      ENVIRONMENT "MERCATURA_TEST_WORKERS=1;MERCATURA_TEST_SECONDS=30;MERCATURA_TEST_STOP_FIRST=1;MERCATURA_TEST_RAPID_WALLET=1")
  endif()
endif()
