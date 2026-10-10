# Mercatura Pool coordinator (M3)

`mercatura-pool` is an optional C++20 service. It links Core's transaction,
block, script, Merkle, serialization, target and frozen MercaHash primitives,
and M2's worker-budget detector. It connects to one ordinary authenticated Core
node; it opens no chainstate or wallet. SQLite contains pool accounting only.
No private ownership keys, withdrawal endpoint, balances, fee switch, donation,
operator payout, or Qt pooled-mining client exist. Mining fees are permanently
zero. A TLS transport key authenticates the service, not ownership of rewards.

The coordinator controls job construction, transaction selection and accounting.
Direct coinbase payments remove reward custody; they do not make a centralized
coordinator decentralized or trustless. Deployment still needs independent review,
load testing, platform validation and careful resource sizing.

## Build and tests

On Ubuntu 22.04+ or Ubuntu under WSL2:

```sh
sudo apt-get install build-essential cmake ninja-build libssl-dev libcurl4-openssl-dev libsqlite3-dev
cmake -S src/pool -B work/build-pool -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build work/build-pool -j 2
ctest --test-dir work/build-pool --output-on-failure
python3 test/lint/lint-pool.py
python3 src/pool/test_service.py --pool-binary="$PWD/work/build-pool/mercatura-pool"
```

The standalone build requires no Boost, Qt, wallet or full-node dependencies.
An alternative is `make -C src/pool BUILD="$PWD/work/build-pool" -j2 test`.
Dependencies must be installed in that case too. The Makefile leaves sources
untouched and uses compiler dependency files for incremental rebuilds.

For an integrated Core build, add `-DBUILD_POOL_COORDINATOR=ON` to the existing
M2 CMake configuration. Default is OFF, so ordinary Qt builds acquire no pool
dependencies. The targets are `mercatura-pool`, `test_mercatura_pool`, and the
test-only `pool-hash-test-client`. Only the coordinator is installed.

```sh
sudo apt-get install libboost-dev libevent-dev qt6-base-dev qt6-tools-dev qt6-l10n-tools
cmake -S . -B work/build-core -G Ninja -DBUILD_GUI=ON -DBUILD_TESTS=ON \
  -DENABLE_IPC=OFF -DWITH_ZMQ=OFF -DWITH_QRENCODE=OFF -DWITH_DBUS=OFF \
  -DMERCATURA_PUBLIC_TESTNET_RELEASE=OFF -DBUILD_POOL_COORDINATOR=ON
cmake --build work/build-core --target bitcoind bitcoin-cli test_bitcoin \
  bitcoin-qt test_mining-qt mercatura-pool test_mercatura_pool pool-hash-test-client -j2
work/build-core/bin/test_mercatura_pool
work/build-core/bin/test_bitcoin --run_test=cpu_miner_tests,mining_tests_wallet,mining_tests_solo
QT_QPA_PLATFORM=minimal work/build-core/bin/test_mining-qt
python3 work/build-core/test/functional/test_runner.py --jobs=1 \
  mercatura_pool_coinbase.py mercatura_pool_coordinator.py
```

`test_service.py` uses explicitly synthetic RPC metadata. Hashes, sockets,
SQLite and TLS are real, but its node fixture rejects every block submission.
Its simulated chain-status transitions are not live validation.
It also SIGKILLs the actual coordinator while its fixture holds a submitblock
response, then checks atomic share/candidate persistence, replay and RPC retry.
Deterministic native service tests cover disconnect/resume with hashes pending,
arrival before expiry and parent changes while verification is queued. A bounded
OpenSSL BIO forces real write retries when another response appends; actual TLS
sockets also exercise a slow reader. Delayed RPC tests exercise pause/recovery.
These local fault tests do not qualify a geographically distributed WAN pool.
`mercatura_pool_coordinator.py` uses a real node and independently running
native hash workers. It deliberately seeds ordinary shares by skipping network
hits, then finds and submits a real network solution through the coordinator.
It checks three native wallets, persistence and replay, actual maturity, chain
invalidation/orphaning and reconsideration, and submits 100/1,000
recipient blocks built from synthetic equal-work inputs by the same production
coinbase builder. Larger-output tests validate actual consensus acceptance but
do not pretend those synthetic shares were earned in live pool sessions.

Linux is the initial deployment platform. The transport uses POSIX IPv4 sockets;
Windows native builds are explicitly refused; use WSL2. macOS uses the same
POSIX path and M2 resource detector, but must be compiled and runtime-tested
on native macOS before deployment. Signet and testnet4 are deliberately unsupported
because their additional rules and identities need explicit integration.

## Components and lifecycle

* `config`: strict JSON settings, network selection and TLS/resource constraints.
* `rpc`: authenticated bounded curl requests; Core network/genesis, IBD checks,
  PQ address validation, GBT, proposals, submitblock and chain observation.
* `job`: immutable blocks, frozen manifests, PQ coinbases and versioned marker.
* `accounting` / `number`: nonnegative arbitrary-precision OpenSSL BIGNUM integer
  arithmetic, PPLNS clipping and largest-remainder allocation.
* `ledger`: prepared SQLite statements, transactions, durable namespaces,
  sessions, jobs, shares, candidates and reconciliation events.
* `verifier`: persistent workers with one private reusable 128 MiB scratchpad
  each; Core `mercahash::HashV1` over Core's canonical header serialization.
* `service`: reactor-owned coordination. Hash workers touch neither SQL nor RPC.
* `transport`: bounded nonblocking connections, framing, TLS and request limits.

On startup the coordinator checks network/genesis and IBD through Core, reads
an authoritative template and reconciles prior candidates. Unavailable Core
pauses work; it does not erase accepted shares. Every second it checks the parent
and refreshes the template when due. Default template/job refresh is 15 seconds;
issued jobs live up to 45 seconds on the same observed parent. Parent changes
invalidate jobs and send notifications. Job freshness is distinct from PPLNS:
accepted work has no wall-clock expiry.
Job expiry checks the server's submission-receive time, not hash-completion time;
queueing or a bounded RPC delay cannot expire a timely received share. A changed
observed parent still makes pending work stale. Results belong to the connection
that requested them; a resumed session retrieves lost receipts through replay,
and inherits its outstanding-verification count.

SIGINT/SIGTERM stop admission, cancel queued hashes and join running workers
between complete hash calls. Completed hashes are accounted durably; interrupted
requests remain retryable. Shutdown may wait for a running hash or bounded RPC
request. No MercaHash cancellation or consensus code is altered.

## Exact accounting and allocation

A share receives the immutable server-assigned target's score:

```text
score(T) = floor(2^256 / (T + 1)), 0 <= T <= 2^256 - 1
score(0) = 2^256; score(1) = 2^255; score(2^256 - 1) = 1
```

BIGNUM handles the 257-bit numerator, arbitrary cumulative work and reward
products. Valid nonzero compact network targets use Core `arith_uint256::SetCompact`
and must match GBT's target. This score equals Core's `(~T / (T+1)) + 1` work
identity for valid targets, without its fixed-width maximum-target edge case.
No miner-supplied difficulty or score is accepted. Reporting estimates may use
floating point; accounting, allocation and serialization never do.

The default PPLNS limit is `2 * score(GBT_target)`. `window_blocks` is an integer
1..100. The reference is recomputed when each snapshot is issued from that
template's actual network target, and frozen with the snapshot. Starting at the
durable accepted-share cutoff, walk shares newest first until the work limit
is reached. Clip the oldest contributing share to the exact remaining work.
The manifest records its sequence and included portion. Shares retain their
original payout scripts across disconnection, address changes and restarts.
Network-difficulty changes may bring older work back into eligibility.

For reward `R` and total weight `W`, each recipient receives `floor(R*w/W)`.
Rank recipients by descending `(R*w) mod W` and distribute the remaining
individual base units once each. Ties rank ascending by the numeric text of
the SHA256d digest from Core `HashWriter` serializing these two strings:

```text
"MCA-PPLNS/1/remainder"
seed + ":" + script_hex
```

The seed is domain separated and commits to network/genesis, parent, complete
authoritative template identifier, cutoff, window limit and exact reward.
The parent changes the tie order between blocks. This removes fixed-address
ordering favoritism; it is not a cryptographic fairness guarantee against an
operator manipulating templates or payout identities. A digest collision uses
script order as a final total-order rule. Outputs use ascending canonical
lowercase script-hex order. Zero allocations remain visible in the manifest
but may have no output. Every positive amount goes to its intended native PQ
script and the sum equals GBT's exact subsidy plus selected-transaction fees.
There is no remainder sink or deferred balance. A tiny nominal payout can cost
more in later transaction fees than its value; that does not justify skimming it.

## Immutable snapshots and coinbases

The snapshot digest is SHA256d of Core serialization of the domain string
`MCA-POOL/1/snapshot` and the canonical, insertion-ordered JSON snapshot string.
The snapshot contains network/genesis, parent/height, complete GBT/template ID,
selected transaction set, reward, cutoff, reference/window/eligible work,
boundary share, sorted weights/scripts/amounts, rounding seed and version,
warm-up policy and zero fee marker. Snapshot contents are committed before
job publication. Per-job manifests add snapshot ID, actual coinbase txid and
serialized block size. Sessions get unique finalized coinbases while sharing
the same frozen entitlement snapshot when inputs match.

Coinbase input is null, final sequence, version 2, locktime 0, and has Core's
height script prefix. Append the marker and 16-byte extranonce described below.
The reserved witness value is 32 zero bytes. Decode all GBT transactions with
Core witness serialization, verify txid/wtxid and preserve their order and bytes.
Recompute the witness root with the coinbase leaf zero, create the standard
zero-valued witness commitment, and require it to equal GBT's commitment.
Payout changes therefore preserve the commitment. Recompute the transaction
Merkle root after finalizing the coinbase. Header version, parent, bits and
timestamp come from GBT; protocol v1 permits nonce changes only.

The full witness-serialized block must fit both GBT `sizelimit` and Core's
height-based byte capacity. Mercatura's GBT `weightlimit` is a nonbinding
compatibility field; serialized bytes are authoritative. PQ outputs add no
signature-check operations. A normal Core `getblocktemplate` proposal validates
the finalized block before publication. The real solution uses `submitblock`.
Nonempty `coinbaseaux` is conservatively refused until explicitly supported.

Snapshots never change after solution discovery. Later shares affect later
snapshots only. Restart loading checks snapshot digests and rebuilds the exact
persisted job to detect inconsistent block/manifest data.

## Empty windows and conservative admission

Default `warmup=false` refuses pooled jobs for empty eligible work (error 106).
That policy cannot bootstrap itself. Operators can explicitly choose
`warmup=true`: only during an empty window, each session gets a clearly marked
single-recipient job paying that session's own registered PQ script the entire
reward. Real accepted shares seed later shared snapshots. An already issued
warm-up job remains single-recipient through expiry even if later shares arrive.
`getjob` explicitly includes `warmup` and `fee_base_units`; clients must also
inspect the frozen audit manifest. No shared entitlement is
promised for that job and no contribution is redirected to an operator.

The identity cap defaults to 1,000 and can only be reduced. Admissions reserve
identities for the database's entire lifetime. Capacity is checked before new
identity registration and before accepting work; an admitted identity is never
evicted. This stricter policy solves a real conflict: if identities were recycled
when a low-difficulty window shrank, a later difficulty increase could include
older shares and exceed the cap. Lifetime reservation limits address churn;
an operator cannot simply remove old scripts or rotate databases while eligible
work remains. A restart rejects a changed cap/window policy rather than silently
altering entitlements.

Historical registrations, connected miners, current eligible-work identities,
frozen-snapshot identities and positive coinbase outputs are different sets.
Only the first set currently determines lifetime admission; zero allocations can
reduce actual output count without freeing a reservation. A presently ineligible
share may return when difficulty expands the work window. Safe permanent
retirement would need a proved bound on every future window (or a versioned
monotonic retention rule), plus protection for issued and pending jobs and
re-admission on old-session resume. With arbitrary supported 256-bit targets,
the universal work bound is impractically large. No retirement is implemented;
eligible history and every frozen snapshot remain intact.

This policy can exhaust through address-only registration, so durable rolling
registration quotas now apply. Default window is 3,600 seconds: at most 4 new
identities/IP and 32 globally, plus 16 new session namespaces/IP and 128 globally.
The session quota also prevents repeated known-address registration from growing
the ledger without bound per interval; it is checked before payout-validation RPC.
Known scripts avoid the identity quota; resume tokens avoid both registration
quotas. Refusal (111) writes no identity, namespace or quota record. Successful
registration/session records commit together and survive restart; future-dated
rows still count after clock rollback. Configure `registration_window_seconds`,
`new_identities_per_ip`, `new_identities_global`, `new_sessions_per_ip` and
`new_sessions_global` explicitly for the cohort. v1 records without recorded IPs
are grandfathered; migration does not invent historical quota usage.

Quotas slow but do not eliminate distributed Sybil exhaustion and can constrain
legitimate shared-NAT users. Lifetime exhaustion remains a launch limitation.
Use a controlled launch cohort/network allowlist and protect resume tokens;
monitor admission capacity and storage. Do not rotate or prune accounting
databases to conceal exhaustion. Proof-of-ownership/registration policy would
require separate protocol work and is not implied by a valid public PQ address.

Template capacity is measured with all admitted identities, including currently
zero or ineligible ones. If the full selected transaction set no longer fits,
new job issuance is refused; existing eligible work stays durable. The service
does not drop transactions while claiming their fees, discard recipients,
create credits, or redistribute their positive amounts. This conservative
availability tradeoff is intentional. Transaction-selection capacity and broader
identity lifecycle policy need review before operating a large pool.

## Pool protocol 1 and the M4 interface

This is a Mercatura-specific newline-delimited JSON protocol over TLS, not
Bitcoin Stratum. UTF-8 JSON has one object per LF frame. Default inbound limit
is 4,096 bytes; nesting is at most eight. No JSON-RPC batches, unknown fields,
duplicate keys, user payout transactions or extra header fields are accepted.
Integers use JSON integer tokens. Receipt sequences and session IDs use decimal
strings; work scores always use decimal strings. Frozen-manifest cutoffs and
amounts remain integer JSON tokens for snapshot-version compatibility. Clients
must parse those tokens exactly, without conversion through floating point.

Requests have `{ "id": positive_int32, "method": string, "params": object }`.
Replies have the same id, `result`, and null `error` on success; errors contain
`{ "code": integer, "message": string }` and null result. Parent notifications
have `method` and `params`, without an id. Do not reuse an outstanding request ID.

| Method | Exact params and response |
|---|---|
| `hello` | `version:1`, `network`, `genesis`, `algorithm:"MercaHash-V1"`, and exactly one of `payout` (native PQ address) or `resume_token` (64 hex characters). Returns protocol/algorithm/network/genesis, decimal session/extranonce namespace, random resume token, heartbeat seconds, pool name and zero fee. |
| `getjob` | Empty params. Returns immutable `job_id`, `snapshot_id`, `header`, complete witness `coinbase`, `merkle_path`, assigned `share_target`, `network_target`, parent, expiry, extranonce namespace, explicit `warmup` and zero `fee_base_units`. |
| `submit` | `job_id`, `nonce` (unsigned 32-bit JSON integer); optional `ntime` must equal the issued timestamp. Returns durable share sequence, receipt/work/job/snapshot IDs, assigned score, accepted=true and network_candidate flag. Acceptance does not stop hashing workers or establish chain inclusion. |
| `manifest` | `job_id`. Returns frozen payout inputs/allocations and per-job coinbase identity plus observed candidate hash/state/confirmations. Public audit data contains no session tokens or RPC credentials. |
| `status` | Empty params. Returns health, connected sessions, share counts, target/score, estimated hashrate, queue/workers/latency/throughput, jobs and candidate state counts. |
| `ping` | Empty params. Returns server Unix time. Send within the negotiated heartbeat timeout. |

`header` is exactly 80 bytes in normal Core wire serialization: version LE32,
parent and Merkle root in internal little-endian hash bytes, time/bits/nonce
LE32. Targets and displayed hashes are 64-character big-endian numeric/display
hex strings. Merkle-path entries use displayed hash hex; reverse bytes before
SHA256d concatenation. Start from the non-witness coinbase txid and combine the
coinbase leaf on the left at each path step. Every extranonce is already fixed
by the server; clients cannot roll it, time or version. After exhausting the
nonce space or receiving a parent notification, obtain another job. The server
reconstructs all work from its persisted block and supplied nonce only.

Parent notification is `{"method":"parent","params":{"parent":hash,"clean_jobs":true}}`.
No automatic reconnect or plaintext downgrade is built into the server. M4 must
verify the TLS server certificate/hostname, negotiate identity, register its
owned PQ payout, store the bearer resume token securely, poll jobs on cadence,
and preserve hashing workers across ordinary acknowledgements. Reconnect with
the token to recover the same durable namespace and unexpired jobs. Only one
connection can own a resumed session at once. Changing address requires a new
session; previous shares keep their original script. Address registration is a
payout choice, not proof of ownership and not a signing/private-key exchange.

| Code | Meaning |
|---|---|
| 100 | Malformed/schema/size/type/header field |
| 101 | Unsupported protocol or algorithm |
| 102 | Wrong network/genesis |
| 103 | Invalid native PQ payout |
| 104 | Unknown/connected session or cross-session job |
| 105 | Recipient/template/assigned-target admission failure |
| 106 | Empty PPLNS window |
| 107 | Unknown, expired or stale-parent job |
| 108 | Duplicate submitted work; an accepted duplicate includes its original receipt |
| 109 | Hash exceeds assigned share target |
| 110 | Session outstanding-request or global verifier overload |
| 111 | Per-session/IP rate limit; transport closes abusive connections |
| 112 | RPC/accounting/internal failure; never acknowledge unpersisted acceptance |
| 113 | Verification cancelled during shutdown; interrupted work can be retried |

Receipt IDs domain-separate genesis, SHA256d header work identity and durable
accepted sequence. They are stable audit references, not a cryptographic
operator signature. Lost ACKs can be retried; uniqueness constraints prevent
double credit and return the prior receipt. Delivery is not exactly once.

## Resource controls and transport security

Defaults: one verifier, queue 16, two in-flight shares per session, 128
connections, 16 per IP, 20 requests per session per second and 80 per IP.
Rates use bounded one-second monotonic windows, shared across connections for
an IP. The reactor caps accept/read work per iteration, input frames, outstanding
IDs and output buffers; slow/idle or oversized clients close.
The recent-IP rate cache has a hard cap of four times `max_sessions` (512 by
default), in addition to pruning idle entries. At capacity, unknown IPs are
temporarily refused; existing IP entries remain usable. This bounds memory
under distributed connection churn and is exercised with distinct loopback IPs.
Outstanding request IDs must be unique; duplicates close the connection instead
of ambiguously consuming the original asynchronous request. Buffered complete
frames drain in bounded batches even when no more socket bytes arrive. TLS
handshakes poll write readiness only when OpenSSL requests it; idle handshakes
do not spin on permanently writable sockets. Write retries preserve the original
plaintext bytes and length when later responses queue.
Default per-client
outbound cap is 8 MiB. Size aggregate socket memory accordingly; connection
limits times output caps are an upper bound, not an allocation at startup.
Only the current job per connected session is cached; disconnect releases it.
Older persisted jobs are loaded transiently for verification or audit requests.
Pending verification keeps the exact block without duplicate manifest data.

Workers preallocate and reuse 128 MiB scratchpads; require a configured budget
of 160 MiB per worker and M2's detected CPU/memory maximum. Allocation or
partial thread startup failures join started threads. Queue overflow refuses
work without creating a new scratchpad. Schema, session, duplicate, expiry and
target assignment checks precede hashing. Network-target claims cannot bypass
verification. Requests and completed shares contain public data only.

TLS 1.2+ is required except explicit `plaintext_regtest=true` on numeric
`127.0.0.1`, regtest only. Invalid/missing TLS material fails startup. There is
no implicit fallback. RPC uses cookie authentication (read fresh each request)
or dedicated user/password, fixed timeouts and a 16 MiB response limit.
Remote RPC requires HTTPS with certificate verification; local HTTP is allowed.
No redirects, debug credential dumps, secret logging or login flow exist.

The simple reactor currently performs bounded synchronous RPC calls. A slow
node can delay all connections. Candidate reconciliation currently visits the
retained candidate set; large long-lived deployments need incremental scanning
and measured throughput. Do not raise worker/connection settings without sizing
Core RPC capacity, scratchpad RAM, output buffers, filesystem and OS limits.

`handshake_seconds` bounds TLS plus initial hello negotiation (default 30,
clamped to heartbeat when omitted). `rpc_connect_timeout_seconds` bounds RPC
connection establishment (default min(10, total RPC timeout)); explicit values
cannot exceed `rpc_timeout_seconds`. The total RPC timeout defaults to 10 and
is configurable up to 60. These are per-call deadlines, not a reactor-cycle
deadline. Startup/refresh makes several calls, and candidate scans make more;
cumulative pauses can exceed a miner heartbeat even when every call is bounded.
For launch, co-locate Core RPC and measure worst-case refresh/reconciliation
latency at the actual candidate-history size. Size handshake, heartbeat and job
lifetime for measured WAN RTT/jitter and reactor pauses; clients should ping well
before the heartbeat deadline, refresh before expiry and replay lost receipts.
Do not increase RPC timeouts without accounting for all calls in a cycle.
Slow-reader and delayed-RPC tests are fault tests on localhost, not evidence of
global WAN suitability. Remote TLS/RPC and long-duration load qualification
remain outstanding; incremental/asynchronous RPC work is deferred.

## SQLite durability and chain observation

Accounting version remains 1; database schema is now 2. Startup transactionally
migrates schema 1 by adding registration history and indexes, preserving all
shares, namespaces, jobs and frozen manifests. Unknown schema versions fail.
Registration IPs are private operational records and are not exported in public
manifests. Tables:

| Table | Durable contents |
|---|---|
| `metadata` | Network/genesis, schema/accounting policy and monotonic job counter |
| `identities` | Lifetime-reserved native PQ scripts |
| `registrations` | Unique newly admitted script, registering IP and time |
| `sessions` | AUTOINCREMENT namespace, script, assigned target, hashed token, creation/connection metadata |
| `session_registrations` | Unique new session, registering IP and time |
| `snapshots` | Content-addressed canonical immutable payout inputs |
| `jobs` | Unique ID/serial, session/snapshot, parent/target, complete exact block, issuance/expiry and manifest |
| `submissions` | Unique header work ID, server job/session/nonce, queued/interrupted/rejected/stale/accepted status |
| `shares` | AUTOINCREMENT accepted sequence, unique work ID, original script, decimal work score and acceptance time |
| `candidates` | Block identity and bytes, snapshot, discovery/RPC/chain state, confirmations, ever-active flag and observation time |
| `events` | Reconciliation state changes |

WAL, `synchronous=FULL`, foreign keys, prepared bindings, transactions and
unique constraints are required. Jobs/snapshots commit before publication.
Accepted shares and their network candidates commit together before ACK and
before submitblock. Process crashes cannot turn acknowledged shares into
uncommitted window inputs. A hash in progress at crash was not acknowledged;
queued rows become interrupted and can be resubmitted. Filesystem durability
still depends on working fsync/storage; this is not a guarantee against hardware
loss or operator rollback. An exclusive advisory lock on the database inode
refuses a second coordinator owner and releases automatically on process exit.
Use one coordinator per database, on local storage with working SQLite/fsync and
advisory locks. External tools must not write behind the live coordinator.

Candidate states separate discovered, submitted, unknown, rejected, active,
orphaned and matured. A null submitblock response records processing only.
Core `getblockheader` confirmations and active `getblockhash(height)` establish
current inclusion. Missing blocks after prior inclusion or negative
confirmations become orphaned. Maturity follows Core's 100-block coinbase rule:
depth must exceed 100. Matured records are revisited for later reorganizations;
maturity is not finality. Discovery is durably retried after crashes, including
duplicate submissions. Orphaned rewards are never counted as confirmed payments.

`manifest` is exportable JSON. To retain independent records, save the returned
frozen manifest, issued coinbase and receipts. Statistics report estimated
share-derived hashrate, not measured miner hardware speed or fictional balances.
There is no unauthenticated HTTP statistics listener; an established public
payout session may request `status` and audit manifests.

## Explorer marker

Canonical coinbase scriptSig is:

```text
<height prefix> 0a 4d43412d504f4f4c2f31 10 <16 extranonce bytes>
```

There are no other scriptSig elements. Preserve the height prefix and require
minimal pushes, exact marker bytes, exactly 16 extranonce bytes and no trailing
elements. `HasPoolMarker` implements these rules and safely refuses malformed
or unknown versions. Typical scriptSig uses under 40 of the 100 allowed bytes.
The marker uses no payout output, reward or witness-reserved bytes.

For the explorer's existing last-144-block chart: inspect each coinbase once,
map a canonical `MCA-POOL/1` to the public label **Mercatura Pool**, and count
one identified block regardless of its recipient count. Pool percentage is
`100 * identified_pool_blocks / sampled_blocks`; preserve the existing solo,
pseudonymous and unidentified categories. Label this attribution as
self-declared/observable, not cryptographically authenticated. Another producer
can imitate the marker. It is never a PPLNS identity or authorization mechanism.
No explorer UI or consensus changes are included in M3.

## Deployment and recovery

Copy `src/pool/pool.testnet.json` or `pool.regtest.json`, set actual RPC port,
genesis/network, database directory and TLS paths, and protect the configuration
and transport key with mode 0600. Use a dedicated unprivileged service account
and a dedicated Core RPC user. Suggested Core allowlist:

```text
rpcwhitelistdefault=1
rpcwhitelist=mercatura-pool:getblockchaininfo,getblockhash,getbestblockhash,getblocktemplate,validateaddress,submitblock,getblockheader
```

Generate `rpcauth` with Core's existing helper. Restrict Core RPC listening to
localhost when co-located; keep the node wallet inaccessible to this RPC user.
Cookie access typically gives broader RPC authority, so reserve it for regtest
or a deliberately constrained local deployment. No new blockchain directory is
created by the pool. The pool account only needs its accounting directory,
configuration and TLS files. If using a separate Core user, grant narrowly scoped
cookie access or prefer a dedicated password.

The sample systemd unit sets one-worker limits and journals only sanitized
lifecycle errors and JSON statistics. `stats_seconds` defaults to 60 (0 disables
periodic logs); stdout/stderr can also be redirected by the process supervisor.
Adjust its paths/MemoryMax/TasksMax together when changing
worker counts; the defaults are not a sizing guarantee. Run in the foreground
first; check network/genesis and health, then install the reviewed unit. Start
pool work only after choosing/documenting the empty-window policy. Stop with
SIGTERM and wait for process exit before moving files.

Use SQLite's online backup API or `sqlite3 pool.sqlite '.backup backup.sqlite'`
for a consistent backup. Never copy only the main file while WAL is live.
For an offline copy, stop cleanly and copy the full database/WAL/SHM set or
checkpoint with a proper SQLite tool. Keep receipts/manifests and incremental
backups outside the host. Test restores separately with no exposed miner port.
Do not run the live and restored copies simultaneously. A backup older than
acknowledged receipts cannot silently replace the ledger: pause and recover all
acknowledged share/job/namespace records before serving miners. If they cannot
be recovered, report the loss and refuse affected shared work; do not invent
deferred balances or reuse rolled-back namespaces. Core's active chain remains
the authority for candidate inclusion after any restore.

WSL2 follows the same Linux build and test commands. Use localhost plaintext
only with regtest. Opening a Windows/WSL port to other hosts requires TLS and a
reviewed firewall/listener configuration. Native Windows support, production
M4 reconnect/worker integration and explorer UI changes remain separate work.

## Optional M3 CI

`.github/workflows/mercatura-pool.yml` is independent of existing Core/M2
workflows. It supports manual dispatch and scoped feature-branch pushes/PRs for
pool, Core source and relevant test changes. Two Ubuntu 24.04 jobs cover the
standalone build, CTest/accounting/persistence/native verification, process and
TLS/RPC fault tests, then a headless optional Core build with live M1/M3 regtest
and existing M2 CPU/wallet/solo regressions. No Qt or non-Linux pool dependencies
are added to normal builds. `BUILD_POOL_COORDINATOR` remains OFF by default.

The workflow pins clang-format 19.1.7 in a local Python venv: the existing style
uses `MainIncludeChar`, which clang-format 18 does not understand. It runs the
dedicated source-hygiene lint (including untracked files locally), existing
include/guard lints and `git diff --check`. Selected public stdout test logs are
retained; node directories, cookies, SQLite files, resume tokens and TLS keys
are excluded from upload. Local syntax/command validation is not a remote
GitHub Actions pass. Check remote results for the exact published commit.
