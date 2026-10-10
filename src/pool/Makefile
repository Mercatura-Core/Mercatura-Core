# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
# Optional standalone build without configuring the full node/Qt dependency set.
CXX ?= c++
BUILD ?= ../../work/build-pool
CPPFLAGS += -I.. -I../univalue/include -DDISABLE_OPTIMIZED_SHA256
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++20 -UNDEBUG -Wall -Wextra -Wpedantic -pthread
SQLITE_LIBS ?= -lsqlite3
LDLIBS += -lssl -lcrypto -lcurl $(SQLITE_LIBS) -pthread
CORE = arith_uint256 uint256 primitives/block primitives/transaction script/script \
  consensus/merkle crypto/hex_base crypto/sha256 crypto/ripemd160 crypto/sha3 \
  crypto/mercahash util/strencodings mining/worker_limits univalue/lib/univalue univalue/lib/univalue_get \
  univalue/lib/univalue_read univalue/lib/univalue_write
POOL = accounting config job ledger protocol rpc service transport verifier
OBJECTS = $(addprefix $(BUILD)/core/,$(addsuffix .o,$(CORE))) $(addprefix $(BUILD)/pool/,$(addsuffix .o,$(POOL)))
.PHONY: all test clean
all: $(BUILD)/mercatura-pool $(BUILD)/test_mercatura_pool $(BUILD)/pool-hash-test-client
$(BUILD)/core/%.o: ../%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/pool/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/mercatura-pool: $(OBJECTS) $(BUILD)/pool/main.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD)/test_mercatura_pool: $(OBJECTS) $(BUILD)/pool/tests.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD)/pool-hash-test-client: $(OBJECTS) $(BUILD)/pool/hash_client.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
test: $(BUILD)/test_mercatura_pool
	$(BUILD)/test_mercatura_pool
clean:
	rm -rf $(BUILD)/core $(BUILD)/pool $(BUILD)/mercatura-pool $(BUILD)/test_mercatura_pool $(BUILD)/pool-hash-test-client
-include $(OBJECTS:.o=.d) $(BUILD)/pool/main.d $(BUILD)/pool/tests.d $(BUILD)/pool/hash_client.d
