# Public-testnet PQ activation options — founder decision pending

This study responds to the revised P9–P11 handoff. It is a recommendation and
test plan, not an approved activation mechanism, height, date, release or rollout.
No live service, DNS record or public participant has been changed.

## Separate decisions

Public-testnet capacity is already fixed: 1 MiB through 7,999, 4 MiB at 8,000,
then the existing absolute doubling schedule. This remains unchanged.
Public-testnet MercaHash V2 has no configured activation height at B3; its future
height is a separate founder decision. Mainnet V2-from-genesis remains an intended
future consensus/release decision, outside this networking change.
PQ enforcement must not be implicitly tied to either capacity or PoW activation.
No actual current public-testnet height has been obtained in this assignment.

## Options

| Mechanism | Fresh/lagging node behavior | Existing sockets and reorgs | Coordination/rollback |
|---|---|---|---|
| Local tip reaches H | A fresh/behind node may still expect classical transport while updated peers require PQ, preventing the very synchronization needed to reach H | Tip changes/reindex/reorg can repeatedly change policy; established sockets need explicit migration rules | Requires an independent way to learn network epoch and bootstrap; a bare local-height gate is unsuitable |
| Coordinated required release | Every node running the approved enforcing build speaks PQ from its first connection, including genesis/old data/reindex/IBD | Policy is independent of tip and stays required through reorgs; restart replaces old sockets | Requires an agreed operator cohort, verified bootstrap and approved window; rollback is a coordinated binary/config operation |
| Date/time gate | New clients need correct clocks and a durable epoch rule; wrong-clock/offline nodes may disagree | Wall-clock skew creates split behavior; established sockets need bounded close/reconnect | Easier to announce but requires clock, grace-period and rollback rules; not implemented or recommended as an automatic trigger here |
| Hybrid coordination milestone | H or a date can be an announcement/planning milestone, while the approved enforcing release itself remains tip-independent | Avoids policy oscillation on chain reorg/reindex | Keeps a practical coordination reference without converting local chain height into a downgrade condition |

## Recommendation — not a finalized mechanism

Prefer a founder-approved coordinated required release, optionally announced
against a separately approved planning milestone. Enforcement in that candidate
should be immutable for its process/build and independent of local tip height.
There should be no `prefer` period or operator off switch in an enforcing build.
This recommendation still needs founder approval and the historical, bootstrap,
security and release qualifications listed in the decision package.

Prepare two clearly distinguished development profiles: a default pending profile
that does not automatically enforce PQ on existing testnet, and an explicitly
selected compulsory qualification profile for controlled testing and candidate
packaging. A pending profile may use explicit `-pqp2p=required` for isolated tests;
an enforcing profile must reject `-pqp2p=off`. Profile selection is a build-time
qualification capability, not authorization to deploy or an approved trigger.
Do not publish an enforcing build as an official release before the founder acts.
Mainnet's architectural rule remains required from its first connection.

## Height-based designs: unresolved bootstrap problem

With local policies `P(tip<H)=classical` and `P(tip>=H)=PQ-only`, a synchronized
peer and a genesis/lagging peer have incompatible expectations. Neither application
block relay nor a claimed service bit can repair an incompatible transport gate.
Letting failure fall back to classical would violate required-mode downgrade
resistance. Letting the peer claim a height to select local policy would hand an
unauthenticated party control of the gate. A reindex can temporarily return local
height to genesis, and a reorg can cross a threshold; these operations must not
silently weaken an already required network policy.

A possible future height-linked coordination scheme would therefore still need a
founder-approved network-wide epoch/bootstrap rule known before connection and
persisted across restart/reindex. That adds design/state/recovery questions and is
not justified merely by choosing an integer H. No such production trigger is
implemented in this assignment.

## Required scenarios and evidence boundaries

| Scenario | Tip-independent required-policy expectation | Executed qualification |
|---|---|---|
| Synchronized node / miner crosses a synthetic boundary | Existing PQ session remains required | PASS: feature_pq_activation.py, established PQ session retained while mining through synthetic boundary |
| Peer several blocks behind / temporarily partitioned | PQ handshake first, then normal most-work catchup | PASS: four-node discovery rehearsal and offline height-2 to height-9 activation fixture |
| Fresh node at genesis / newly installed IBD client | Same PQ transport from first connection; needs verified bootstrap | PASS: actual fresh-datadir discovery/join; actual public identity at genesis only |
| Old datadir restore / offline reconnect | No height-derived return to classical; fresh session | PASS: actual v0.1.2 generated-history upgrade/rollback; public historical restore unexecuted |
| Full reindex / chainstate reindex | Transport requirement unchanged by transient local chain state | PASS: both reindex paths in migration/capacity fixtures; policy independent of transient tip |
| Reorg across a proposed H | Policy does not toggle with tip | PASS: actual competing-most-work regtest reorg through synthetic boundary; required policy retained |
| Established classical connection at rollout | Enforcing software requires restart and fresh PQ socket; no in-place reinterpretation | PASS: partial old/new migration rejects classical attempts; healthy PQ backbone retained |
| Older independent release | Classical attempt rejected; no PQ-to-classical retry | PASS: actual published v0.1.2 fixture and required-mode rejection tests |

These controlled cases have matching executed receipts in the P9–P11 evidence
JSON and passed again in exact-source CI. Actual historical public-testnet data
was unavailable. The lagging/reorg cases use explicit required regtest policy;
final native testnet enforcing builds separately pass default/off rejection and
required connection checks. A synthetic boundary in a model/regtest is never a proposed
public activation height. Genesis-only public tests do not establish historical
replay, independently operated public bootstrap, Tor/NAT behavior or deployment.

## If the founder chooses a height-based coordination milestone

Obtain actual height/hash/chainwork with timestamps from authenticated, authorized
operator observations, preferably multiple independent nodes. Record the source;
do not infer current height from wall time, a release date or capacity height.
Estimate a planning increment using the 150-second target interval:
`ceil((software availability + operator lead time + safety buffer) / 150 seconds)`.
This is a planning estimate, not a reliable predicted height: hashrate, difficulty,
partitions and observed block-time variance create substantial uncertainty.

Before proposing H, require available verified software for all target platforms,
a reviewed operator inventory, independent bootstrap, a tested historical restore
and rollback path, reorg behavior, remaining security-review disposition and an
explicit coordination window. The founder must approve any actual H/mechanism.
Until then, leave production activation parameters unset and default enforcement
pending. No live testnet queries, fabricated current heights or triggers are used.
