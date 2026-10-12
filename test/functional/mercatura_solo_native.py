#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.

"""Validate production Solo-controller blocks on a separate normal RPC node.

The in-process producer checks owned PQ rewards, depth-100/101 maturity,
wallet signing, exact subsidy plus fees, and witness/merkle commitments. This
test imports its public block bytes through submitblock, never generatetoaddress.
"""

import json
import os
from pathlib import Path
import subprocess

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class MercaturaSoloNativeTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def run_test(self):
        build = Path(self.config["environment"]["BUILDDIR"])
        suffix = ".exe" if os.name == "nt" else ""
        candidates = [build / "bin" / f"test_bitcoin{suffix}",
                      build / "bin" / "Release" / f"test_bitcoin{suffix}"]
        binary = next((path for path in candidates if path.is_file()), None)
        assert binary is not None, "Native Solo qualification requires the test_bitcoin target"
        proof_path = Path(self.options.tmpdir) / "native-solo-proof.json"
        log_path = Path(self.options.tmpdir) / "native-solo-producer.log"
        environment = dict(os.environ, MERCATURA_SOLO_QUALIFICATION_OUTPUT=str(proof_path))
        self.log.info("Producing 102 native controller blocks and spending a matured PQ reward")
        with log_path.open("w", encoding="utf8") as log:
            subprocess.run([str(binary), "--run_test=mining_tests_solo/native_rewards_maturity_fees_and_pq_spend",
                            "--log_level=test_suite"], env=environment,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)
        proof = json.loads(proof_path.read_text(encoding="utf8"))
        assert_equal(proof["accepted"], 102)
        assert_equal(len(proof["blocks"]), 102)
        node = self.nodes[0]
        # Respect producer time without changing difficulty or any consensus rule.
        timestamp = max(int.from_bytes(bytes.fromhex(block)[68:72], "little")
                        for block in proof["blocks"])
        node.setmocktime(timestamp + 1)
        for height, block in enumerate(proof["blocks"], start=1):
            assert_equal(node.submitblock(block), None)
            assert_equal(node.getblockcount(), height)
            reward = node.getblock(node.getbestblockhash(), 2)["tx"][0]
            assert_equal(reward["vout"][0]["scriptPubKey"]["address"], proof["destination"])
            assert all(output["value"] == 0 for output in reward["vout"][1:])
        final = node.getblock(node.getbestblockhash(), 2)
        assert_equal(final["tx"][1]["txid"], proof["spend"])
        stats = node.getblockstats(102, ["subsidy", "totalfee"])
        assert_equal(stats["totalfee"], proof["fee_base_units"])
        reward_base_units = sum(output["value"] for output in final["tx"][0]["vout"]) * 100
        assert_equal(reward_base_units, stats["subsidy"] + stats["totalfee"])
        assert_equal(node.gettxout(proof["first_coinbase"], 0), None)
        # Duplicate import is idempotent and does not advance the chain.
        assert_equal(node.submitblock(proof["blocks"][-1]), "duplicate")
        assert_equal(node.getblockcount(), 102)


if __name__ == "__main__":
    MercaturaSoloNativeTest(__file__).main()
