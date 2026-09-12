#!/usr/bin/env bash
#
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit.

export LC_ALL=C.UTF-8

export CONTAINER_NAME=ci_native_linux
export CI_IMAGE_NAME_TAG="mirror.gcr.io/ubuntu:24.04"

# Match the known-good Phase 12 / Phase 13A headless Linux configuration.
# Build dependencies through depends, but omit components intentionally
# disabled for this checkpoint.
export DEP_OPTS="NO_QT=1 NO_ZMQ=1 NO_USDT=1 NO_IPC=1"

export GOAL="install"

# First Phase 13 Linux CI checkpoint is build-only. Tests are enabled in the
# build so the test binaries compile, but execution is added separately after
# the cross-platform build path itself is known-good.
export RUN_UNIT_TESTS=false
export RUN_FUNCTIONAL_TESTS=false

export BITCOIN_CONFIG="\
 -DCMAKE_BUILD_TYPE=RelWithDebInfo \
 -DBUILD_GUI=OFF \
 -DENABLE_WALLET=ON \
 -DENABLE_IPC=OFF \
 -DWITH_USDT=OFF \
 -DWITH_ZMQ=OFF \
 -DBUILD_TESTS=ON \
"
