# Seeds

Utility to generate the seeds.txt list that is compiled into the client
(see [src/chainparamsseeds.h](/src/chainparamsseeds.h) and other utilities in [contrib/seeds](/contrib/seeds)).

Be sure to update `PATTERN_AGENT` in `makeseeds.py` to include the current version,
and remove old versions as necessary (at a minimum when SeedsServiceFlags()
changes its default return value, as those are the services which seeds are added
to addrman with).

Update `MIN_BLOCKS` in  `makeseeds.py` and the `-m`/`--minblocks` arguments below, as needed.

The seeds compiled into the release are created from sipa's and achow101's
DNS seed, virtu's crawler, and asmap community AS map data. Run the following commands
from the `/contrib/seeds` directory:

```
curl https://bitcoin.sipa.be/seeds.txt.gz | gzip -dc > seeds_main.txt
curl https://21.ninja/seeds.txt.gz | gzip -dc >> seeds_main.txt
curl https://mainnet.achownodes.xyz/seeds.txt.gz | gzip -dc >> seeds_main.txt
curl https://signet.achownodes.xyz/seeds.txt.gz | gzip -dc > seeds_signet.txt
curl https://testnet.achownodes.xyz/seeds.txt.gz | gzip -dc > seeds_test.txt
curl https://testnet4.achownodes.xyz/seeds.txt.gz | gzip -dc > seeds_testnet4.txt
curl https://raw.githubusercontent.com/asmap/asmap-data/main/latest_asmap.dat > asmap-filled.dat
python3 makeseeds.py -a asmap-filled.dat -s seeds_main.txt > nodes_main.txt
python3 makeseeds.py -a asmap-filled.dat -s seeds_signet.txt -m 266000 > nodes_signet.txt
python3 makeseeds.py -a asmap-filled.dat -s seeds_test.txt -m 4650000 > nodes_test.txt
python3 makeseeds.py -a asmap-filled.dat -s seeds_testnet4.txt -m 100000 > nodes_testnet4.txt
python3 generate-seeds.py . > ../../src/chainparamsseeds.h
```

## Mercatura compulsory-PQ bootstrap

The inherited Bitcoin seed lists and `makeseeds.py` filters (Satoshi agents,
Bitcoin height thresholds) are not Mercatura seed qualification tools. Do not
publish those addresses in Mercatura chain parameters.

`mercatura-bootstrap.py` renders a public-testnet configuration from an explicit
operator inventory. It performs no DNS lookup, probe, installation, deployment,
or independent verification of the inventory's claims. Require at least two
independent operators, verify their endpoints over PQ, record UTC verification
and full source commits, and refresh stale inventories before a coordinated
cutover. Experimental service bit 24 is an unauthenticated availability hint;
absence or collision never permits classical transport or proves identity.
The main/test production seed sets remain empty until independently verified
infrastructure is approved. See the P11 deployment qualification playbook.

Inventory schema (fill values with actual verified evidence; no sample public
hosts or operators are supplied): top-level `chain` is `test`, `genesis` is the
existing public-testnet hash, and `operators` contains 2..16 records with distinct
`operator` names and 1..4 `endpoints` each. Endpoint fields are `host`, `port`,
`pq_verified_at_utc`, `source_commit`, and optional public `fingerprint`.
Fingerprints must come from an independent trusted binding. The output uses
`seednode` for bounded PQ address fetching and exact destination `pqp2ppin`
bindings when supplied. File creation is exclusive; no existing config is
replaced. `--lab` accepts only numeric loopback addresses and labels the output.

```
python3 contrib/seeds/mercatura-bootstrap.py --inventory VERIFIED.json --output NEW.conf
```

DNS operators must serve the unchanged public-testnet port and compatible
full-node addresses, cap response sizes, monitor actual authenticated PQ
availability, diversify operators/ASNs and IPv4/IPv6, and retain operator
emergency peers. DNS information is not a cryptographic trust root. Core's
existing addrman grouping, selection, retry and capacity bounds are retained.
