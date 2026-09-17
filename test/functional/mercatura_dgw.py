#!/usr/bin/env python3
# Copyright (c) 2026 The Mercatura developers
# Distributed under the MIT software license.

"""Mercatura native DGWv3 functional test on isolated testnet."""

from test_framework.messages import uint256_from_compact
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


DGW_WINDOW = 24
TARGET_SPACING = 150
TARGET_TIMESPAN = 3600
MIN_TIMESPAN = 1200
MAX_TIMESPAN = 10800

FAST_SPACING = 50
SLOW_SPACING = 200


def compact_from_target(target):
    """Encode a positive integer target using Bitcoin compact format."""
    assert target > 0

    size = (target.bit_length() + 7) // 8

    if size <= 3:
        compact = target << (8 * (3 - size))
    else:
        compact = target >> (8 * (size - 3))

    if compact & 0x00800000:
        compact >>= 8
        size += 1

    compact &= 0x007fffff
    compact |= size << 24

    return compact


class MercaturaDGWTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.uses_wallet = False

        # Regtest deliberately disables retargeting. Isolated Mercatura
        # testnet exercises the real DGWv3 consensus path.
        self.chain = "testnet"

        self.extra_args = [[
            "-dnsseed=0",
            "-fixedseeds=0",
        ]]

    @staticmethod
    def bits_to_int(bits):
        if isinstance(bits, str):
            return int(bits, 16)

        return bits

    @staticmethod
    def target_from_bits(bits):
        if isinstance(bits, str):
            bits = int(bits, 16)

        return uint256_from_compact(bits)

    def calculate_dgw_bits(self, history):
        """
        Independently calculate the nBits required for the next block.

        history contains accepted headers in ascending height order,
        including genesis and ending at the active tip.
        """
        last = history[-1]

        #
        # Launch difficulty remains unchanged until a complete 24-block
        # DGW window exists. Block 25 is the first retargeted block.
        #
        if last["height"] < DGW_WINDOW:
            return self.bits_to_int(last["bits"])

        #
        # DGW averages targets H through H-23.
        #
        target_headers = history[-DGW_WINDOW:]

        average = None

        for count, header in enumerate(
            reversed(target_headers),
            start=1,
        ):
            target = self.target_from_bits(
                header["bits"]
            )

            if count == 1:
                average = target
                continue

            #
            # Match Mercatura's established DGWv3 recurrence exactly.
            #
            divisor = count + 1

            if target >= average:
                delta = target - average
                average += delta // divisor
            else:
                delta = average - target

                quotient = delta // divisor

                if delta % divisor:
                    quotient += 1

                average -= quotient

        assert average is not None

        #
        # Measure H back to H-24 so the window contains 24 complete
        # block intervals.
        #
        time_start = history[-DGW_WINDOW - 1]["time"]

        actual_timespan = (
            history[-1]["time"]
            - time_start
        )

        actual_timespan = max(
            MIN_TIMESPAN,
            min(MAX_TIMESPAN, actual_timespan),
        )

        #
        # Match Mercatura's overflow-safe quotient/remainder scaling.
        #
        quotient, remainder = divmod(
            average,
            TARGET_TIMESPAN,
        )

        tail = (
            remainder
            * actual_timespan
            // TARGET_TIMESPAN
        )

        pow_limit = self.pow_limit_target
        quotient_limit = pow_limit // actual_timespan

        if quotient > quotient_limit:
            return compact_from_target(pow_limit)

        scaled_quotient = quotient * actual_timespan

        if tail > pow_limit - scaled_quotient:
            return compact_from_target(pow_limit)

        new_target = scaled_quotient + tail

        if new_target > pow_limit:
            new_target = pow_limit

        return compact_from_target(new_target)

    def mine_at_time(self, history, timestamp):
        previous = history[-1]

        gap = timestamp - previous["time"]

        assert gap > 0

        #
        # Testnet's minimum-difficulty exception requires a delay strictly
        # greater than two target spacings. Keep every test gap <= 300s.
        #
        assert gap <= TARGET_SPACING * 2

        expected_bits = self.calculate_dgw_bits(
            history
        )

        #
        # setmocktime RPC is regtest-only, but startup -mocktime is available
        # on testnet. Restart with the desired block timestamp.
        #
        self.restart_node(
            0,
            extra_args=[
                f"-mocktime={timestamp}",
            ],
        )

        node = self.nodes[0]

        hashes = self.generatetodescriptor(
            node,
            1,
            "raw(51)",
        )

        assert_equal(
            len(hashes),
            1,
        )

        header = node.getblockheader(
            hashes[0],
        )

        assert_equal(
            header["height"],
            previous["height"] + 1,
        )

        assert_equal(
            header["time"],
            timestamp,
        )

        actual_bits = int(
            header["bits"],
            16,
        )

        self.log.debug(
            f"height={header['height']} "
            f"time={timestamp} "
            f"expected_bits={expected_bits:08x} "
            f"actual_bits={actual_bits:08x}"
        )

        assert_equal(
            actual_bits,
            expected_bits,
        )

        history.append(header)

        return header

    def run_test(self):
        node = self.nodes[0]

        self.log.info(
            "Initializing Mercatura testnet DGW history"
        )

        genesis_hash = node.getblockhash(0)
        genesis = node.getblockheader(
            genesis_hash,
        )

        history = [genesis]

        launch_bits = genesis["bits"]

        self.pow_limit_target = self.target_from_bits(
            launch_bits
        )

        self.log.info(
            f"Launch nBits: {launch_bits}"
        )

        timestamp = genesis["time"]

        #
        # Heights 1..24: startup target remains unchanged.
        #
        self.log.info(
            "Mining 24-block nominal startup window"
        )

        for _ in range(DGW_WINDOW):
            timestamp += TARGET_SPACING

            header = self.mine_at_time(
                history,
                timestamp,
            )

            assert_equal(
                header["bits"],
                launch_bits,
            )

        assert_equal(
            history[-1]["height"],
            24,
        )

        #
        # Height 25: first full DGW calculation.
        #
        self.log.info(
            "Checking first DGW calculation at height 25"
        )

        timestamp += TARGET_SPACING

        block25 = self.mine_at_time(
            history,
            timestamp,
        )

        assert_equal(
            block25["height"],
            25,
        )

        assert_equal(
            block25["bits"],
            launch_bits,
        )

        nominal_target = self.target_from_bits(
            block25["bits"]
        )

        #
        # Feed fast 50-second intervals into the rolling window.
        #
        self.log.info(
            "Mining fast-spacing DGW window"
        )

        fast_headers = []
        saw_per_block_retarget = False
        previous_bits = block25["bits"]

        for _ in range(DGW_WINDOW):
            timestamp += FAST_SPACING

            header = self.mine_at_time(
                history,
                timestamp,
            )

            fast_headers.append(header)

            if header["bits"] != previous_bits:
                saw_per_block_retarget = True

            previous_bits = header["bits"]

        assert saw_per_block_retarget

        fast_tip = fast_headers[-1]

        fast_target = self.target_from_bits(
            fast_tip["bits"]
        )

        self.log.info(
            f"Fast-window tip nBits: {fast_tip['bits']}"
        )

        # Lower target means greater difficulty.
        assert fast_target < nominal_target

        #
        # The completed historical window is exactly the DGW lower clamp
        # boundary: 24 * 50 = 1200 seconds.
        #
        fast_window_span = (
            history[-1]["time"]
            - history[-DGW_WINDOW - 1]["time"]
        )

        assert_equal(
            fast_window_span,
            MIN_TIMESPAN,
        )

        #
        # Mine one additional fast block so its nBits is calculated from that
        # exact 1200-second historical window.
        #
        self.log.info(
            "Checking DGW lower-timespan boundary"
        )

        timestamp += FAST_SPACING

        lower_boundary_block = self.mine_at_time(
            history,
            timestamp,
        )

        lower_boundary_target = self.target_from_bits(
            lower_boundary_block["bits"]
        )

        assert lower_boundary_target <= fast_target

        #
        # Feed slower 200-second blocks into the rolling window. This is still
        # below testnet's >300-second minimum-difficulty exception.
        #
        self.log.info(
            "Mining slow-spacing DGW window"
        )

        slow_headers = []

        for _ in range(DGW_WINDOW):
            timestamp += SLOW_SPACING

            header = self.mine_at_time(
                history,
                timestamp,
            )

            slow_headers.append(header)

        slow_tip = slow_headers[-1]

        slow_target = self.target_from_bits(
            slow_tip["bits"]
        )

        self.log.info(
            f"Slow-window tip nBits: {slow_tip['bits']}"
        )

        # Greater target means easier difficulty.
        assert slow_target > lower_boundary_target

        #
        # Verify the slow test intervals remained outside the special
        # testnet delayed-block minimum-difficulty path.
        #
        for previous, current in zip(
            slow_headers,
            slow_headers[1:],
        ):
            assert_equal(
                current["time"] - previous["time"],
                SLOW_SPACING,
            )

            assert (
                current["time"] - previous["time"]
                <= TARGET_SPACING * 2
            )

        self.log.info(
            "Mercatura native DGWv3 functional test passed"
        )


if __name__ == "__main__":
    MercaturaDGWTest(__file__).main()
