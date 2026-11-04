"""Deterministic manifests, integer accounting and bounded campaign metrics."""

import dataclasses
import json
import os
import pathlib
import time
from decimal import Decimal

COUNTS = (100, 500, 1000, 5000, 10000)
EXTENDED = (50000, 100000)


def units(value):
    scaled = Decimal(str(value)) * 100
    if scaled != scaled.to_integral_value():
        raise ValueError("MCA has two decimal places")
    return int(scaled)


def mca(value):
    return Decimal(value) / 100


def write_json(path, data):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    with temp.open("w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    temp.replace(path)


@dataclasses.dataclass(frozen=True)
class Manifest:
    seed: int = 7
    transactions: int = 100
    nodes: int = 3
    epoch_size: int = 1000
    batch_size: int = 20
    custody: str = "independent"
    mining_interval: int = 0
    soak_seconds: int = 0
    max_soak_cycles: int = 10
    disruptions: bool = True
    extended: bool = False
    lifecycle: bool = False
    capacity_steps: bool = False
    bootstrap_blocks: int = 101

    def validate(self):
        for name in (
            "seed",
            "transactions",
            "nodes",
            "epoch_size",
            "batch_size",
            "mining_interval",
            "soak_seconds",
            "max_soak_cycles",
            "bootstrap_blocks",
        ):
            if type(getattr(self, name)) is not int:
                raise ValueError("integer manifest field: " + name)
        if not 0 <= self.seed <= 0xFFFFFFFF or not 101 <= self.bootstrap_blocks <= 400:
            raise ValueError("seed/bootstrap bounds")
        if not 1 <= self.transactions <= 100000:
            raise ValueError("transaction bound")
        if self.transactions > 10000 and not self.extended:
            raise ValueError("extended workloads require explicit opt-in")
        if not 2 <= self.nodes <= 4:
            raise ValueError("2-4 isolated nodes required")
        if not 1 <= self.epoch_size <= 1500 or not 1 <= self.batch_size <= 64:
            raise ValueError("epoch/batch bounds")
        if self.custody not in ("independent", "reused", "centralized"):
            raise ValueError("native custody model")
        if not 0 <= self.soak_seconds <= 86400 or not 1 <= self.max_soak_cycles <= 1000:
            raise ValueError("soak bounds")
        if not 0 <= self.mining_interval <= 1500:
            raise ValueError("mining interval")
        return self


class Ledger:
    """Independent tracked UTXOs, scripts and confirmed customer accounting."""

    def __init__(self):
        self.scripts = {}
        self.coins = {}
        self.pending = {}
        self.reserved = set()
        self.records = []
        self.credits = {}
        self.debits = {}
        self.height = 0
        self.total_fees = 0

    def own(self, script, owner):
        self.scripts[script] = owner

    def register(self, txid, inputs, outputs, fee, phase):
        points = [(r["txid"], r["vout"]) for r in inputs]
        if len(set(points)) != len(points) or any(
            p in self.reserved or p not in self.coins for p in points
        ):
            raise AssertionError("duplicate/unavailable funding")
        expected = sum(self.coins[p]["value"] for p in points)
        if expected != sum(o["value"] for o in outputs) + fee or fee < 1:
            raise AssertionError("input/output conservation")
        self.reserved.update(points)
        self.pending[txid] = dict(inputs=points, outputs=outputs, fee=fee, phase=phase)

    def block(self, block):
        self.height = block["height"]
        fees = 0
        coinbase_total = 0
        for index, tx in enumerate(block["tx"]):
            if index == 0:
                coinbase_total = sum(units(o["value"]) for o in tx["vout"])
            else:
                record = self.pending.pop(tx["txid"])
                observed = [
                    dict(value=units(o["value"]), script=o["scriptPubKey"]["hex"])
                    for o in tx["vout"]
                ]
                expected = [
                    dict(value=o["value"], script=o["script"])
                    for o in record["outputs"]
                ]
                if (
                    observed != expected
                    or [(v["txid"], v["vout"]) for v in tx["vin"]] != record["inputs"]
                ):
                    raise AssertionError("block contents differ from independent plan")
                for point in record["inputs"]:
                    del self.coins[point]
                    self.reserved.remove(point)
                fees += record["fee"]
                self.total_fees += record["fee"]
                if record["phase"] == "deposit":
                    for out in record["outputs"]:
                        if out.get("customer") is not None:
                            self.credits[out["customer"]] = (
                                self.credits.get(out["customer"], 0) + out["value"]
                            )
            for out in tx["vout"]:
                script = out["scriptPubKey"]["hex"]
                if script in self.scripts:
                    self.coins[(tx["txid"], out["n"])] = dict(
                        value=units(out["value"]),
                        script=script,
                        owner=self.scripts[script],
                        height=self.height,
                        coinbase=index == 0,
                    )
        return fees, coinbase_total

    def inventory(self, owner):
        return [
            dict(txid=p[0], vout=p[1], **c)
            for p, c in sorted(self.coins.items())
            if c["owner"] == owner
            and p not in self.reserved
            and (not c["coinbase"] or self.height - c["height"] >= 100)
        ]

    def balance(self, owner):
        return sum(c["value"] for c in self.inventory(owner))


class Metrics:
    def __init__(self, root):
        self.root = pathlib.Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.rows = []
        self.events = []
        self.samples = []
        self.start = time.monotonic()

    def event(self, kind, **data):
        self.events.append(
            dict(kind=kind, elapsed_s=time.monotonic() - self.start, **data)
        )

    def sample(self, nodes, datadir):
        data = []
        for node in nodes:
            pid = node.process.pid
            try:
                fields = pathlib.Path(f"/proc/{pid}/stat").read_text().split()
                status = pathlib.Path(f"/proc/{pid}/status").read_text()
                rss = int(
                    next(
                        x for x in status.splitlines() if x.startswith("VmRSS:")
                    ).split()[1]
                )
                ticks = os.sysconf("SC_CLK_TCK")
                hwm = int(
                    next(
                        x for x in status.splitlines() if x.startswith("VmHWM:")
                    ).split()[1]
                )
                data.append(
                    dict(
                        pid=pid,
                        rss_kib=rss,
                        high_water_kib=hwm,
                        cpu_seconds=(int(fields[13]) + int(fields[14])) / ticks,
                    )
                )
            except FileNotFoundError:
                continue
        # Logical file bytes within the dedicated campaign directory.
        disk = 0
        for p in pathlib.Path(datadir).rglob("*"):
            try:
                if p.is_file():
                    disk += p.stat().st_size
            except FileNotFoundError:
                continue
        self.samples.append(
            dict(elapsed_s=time.monotonic() - self.start, nodes=data, disk_bytes=disk)
        )

    def checkpoint(self, **state):
        write_json(
            self.root / "checkpoint.json",
            dict(
                schema=1,
                completed_rows=len(self.rows),
                elapsed_s=time.monotonic() - self.start,
                **state,
            ),
        )
        write_json(self.root / "transactions.json", self.rows)
        write_json(self.root / "events.json", self.events)
        write_json(self.root / "resources.json", self.samples)
