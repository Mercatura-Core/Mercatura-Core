#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura supported-network identity regression test."""

import json
import subprocess
import tempfile
import time
from pathlib import Path

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, rpc_port


NETWORKS = (
    {
        "arg": "main",
        "rpc_chain": "main",
        "genesis": "cd797c78731d68a82b664b3e359a2e69508ea873fe5747b686488589cc7d6f15",
        "address_prefix": "mca1z",
    },
    {
        "arg": "test",
        "rpc_chain": "test",
        "genesis": "0cee25abd571760687efbebbe8741873dc187ce46afe082282a47b2455320d73",
        "address_prefix": "tmca1z",
    },
    {
        "arg": "signet",
        "rpc_chain": "signet",
        "genesis": "eebe2b23469b0d91056cc9240387ba7ee601ce138160da3c508142e039e1f36b",
        "address_prefix": "tmca1z",
    },
    {
        "arg": "regtest",
        "rpc_chain": "regtest",
        "genesis": "8e2308efb3a16b126e69444329cc0ed81bea0596e99db1032ccd750e7028f685",
        "address_prefix": "mcrt1z",
    },
)


class MercaturaNetworkIdentityTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 0
        self.setup_clean_chain = True

    def setup_network(self):
        # This test starts isolated Mercatura daemons manually for each
        # supported network. The inherited framework sync logic assumes
        # at least one framework-managed node and cannot be used here.
        pass

    def daemon_path(self):
        return Path("build/bin/mercaturad").resolve()

    def cli_path(self):
        return Path("build/bin/mercatura-cli").resolve()

    def cli(self, datadir, chain, port, *args, check=True):
        cmd = [
            str(self.cli_path()),
            f"-datadir={datadir}",
            f"-chain={chain}",
            f"-rpcport={port}",
            *args,
        ]

        result = subprocess.run(
            cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

        if check and result.returncode != 0:
            raise AssertionError(
                f"mercatura-cli failed:\n"
                f"command: {' '.join(cmd)}\n"
                f"stdout: {result.stdout}\n"
                f"stderr: {result.stderr}"
            )

        return result

    def wait_for_rpc(self, datadir, chain, port, process):
        deadline = time.time() + 30

        while time.time() < deadline:
            if process.poll() is not None:
                stdout, stderr = process.communicate()
                raise AssertionError(
                    f"{chain} daemon exited during startup\n"
                    f"stdout: {stdout}\n"
                    f"stderr: {stderr}"
                )

            result = self.cli(
                datadir,
                chain,
                port,
                "getblockchaininfo",
                check=False,
            )

            if result.returncode == 0:
                return

            time.sleep(0.25)

        raise AssertionError(
            f"Timed out waiting for {chain} RPC"
        )

    def check_network(self, network, index):
        chain = network["arg"]
        port = rpc_port(index)

        with tempfile.TemporaryDirectory(
            prefix=f"mercatura-{chain}-",
            dir=self.options.tmpdir,
        ) as datadir:
            self.log.info(
                f"Checking Mercatura {chain} identity"
            )

            cmd = [
                str(self.daemon_path()),
                f"-datadir={datadir}",
                f"-chain={chain}",
                f"-rpcport={port}",
                "-server=1",
                "-listen=0",
                "-dnsseed=0",
                "-discover=0",
                "-printtoconsole=0",
            ]

            process = subprocess.Popen(
                cmd,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )

            try:
                self.wait_for_rpc(
                    datadir,
                    chain,
                    port,
                    process,
                )

                info = json.loads(
                    self.cli(
                        datadir,
                        chain,
                        port,
                        "getblockchaininfo",
                    ).stdout
                )

                assert_equal(
                    info["chain"],
                    network["rpc_chain"],
                )

                genesis = self.cli(
                    datadir,
                    chain,
                    port,
                    "getblockhash",
                    "0",
                ).stdout.strip()

                assert_equal(
                    genesis,
                    network["genesis"],
                )

                self.cli(
                    datadir,
                    chain,
                    port,
                    "createwallet",
                    "identity",
                )

                address = self.cli(
                    datadir,
                    chain,
                    port,
                    "-rpcwallet=identity",
                    "getnewaddress",
                ).stdout.strip()

                if not address.startswith(
                    network["address_prefix"]
                ):
                    raise AssertionError(
                        f"{chain} address {address} does not "
                        f"start with expected prefix "
                        f"{network['address_prefix']}"
                    )

                for forbidden in (
                    "bc1",
                    "tb1",
                    "bcrt1",
                ):
                    if address.startswith(forbidden):
                        raise AssertionError(
                            f"{chain} leaked Bitcoin address "
                            f"prefix: {address}"
                        )

            finally:
                if process.poll() is None:
                    self.cli(
                        datadir,
                        chain,
                        port,
                        "stop",
                        check=False,
                    )

                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)

    def check_testnet4_rejected(self):
        with tempfile.TemporaryDirectory(
            prefix="mercatura-testnet4-",
            dir=self.options.tmpdir,
        ) as datadir:
            result = subprocess.run(
                [
                    str(self.daemon_path()),
                    f"-datadir={datadir}",
                    "-chain=testnet4",
                    "-printtoconsole=1",
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=15,
                check=False,
            )

            combined = result.stdout + result.stderr

            if result.returncode == 0:
                raise AssertionError(
                    "Inherited Testnet4 unexpectedly started"
                )

            if "Unknown chain testnet4" not in combined:
                raise AssertionError(
                    "Testnet4 was rejected for an unexpected "
                    f"reason:\n{combined}"
                )

    def check_cli_help(self):
        result = subprocess.run(
            [
                str(self.cli_path()),
                "-help",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=True,
        )

        help_text = result.stdout + result.stderr

        if "testnet4" in help_text.lower():
            raise AssertionError(
                "Mercatura CLI still advertises Testnet4"
            )

    def run_test(self):
        self.check_cli_help()
        self.check_testnet4_rejected()

        for index, network in enumerate(
            NETWORKS,
            start=0,
        ):
            self.check_network(
                network,
                index,
            )

        self.log.info(
            "All supported Mercatura network identities passed"
        )


if __name__ == "__main__":
    MercaturaNetworkIdentityTest(__file__).main()
