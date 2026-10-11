# Native Qt pool mining (M4)

Open the existing Mining tab, select **Pool Mining**, enter the coordinator's
hostname and port, and press **Start Mining**. Solo Mining remains the initial
mode. No mining starts when the application or a wallet opens. Use automatic
workers unless you have measured a better setting; manual selection follows
M2's detected CPU and memory budget. Stop before changing wallets, endpoints,
worker counts or modes. The controls use ordinary Qt widgets and remain visible
below a scrollable configuration area.

TLS 1.2 or later and certificate/hostname verification are mandatory, including
regtest. System roots are used by default. For a private pool, explicitly select
its trusted PEM CA certificate obtained through a trusted channel. Use
**Use system trust store only** to remove the selected additional CA. A certificate
received from an untrusted endpoint is not automatically trusted. There is no
plaintext fallback, coordinator startup, RPC credential exchange or second
chainstate/database. Host, port and optional public CA path use QSettings; no
bearer token is written there.

## Wallet and ownership

M4 reuses `wallet::GetMiningDestination`, the validated M2 normal wallet path.
It accepts only an owned, spendable witness-v2 native PQ destination, reuses the
persisted `Mercatura mining reward` address-book entry and sends only the public
address. Initial address creation may require the ordinary wallet unlock dialog;
preparation finishes and the wallet relocks before mining. Cached destinations
work while locked. No private ownership key, seed or signature enters a job or
pool request. There is no external payout-address UI.

One shared MiningSession still serves all wallet pages. Unloading its selected
wallet cancels that session, while an unrelated unload does not. The existing
WalletController deferred model destruction and application shutdown join barrier
remain in use. Changing the address-book label can require a new destination,
as documented for M2; relabelling is not a pool reconnection operation.

## Provider, workers and wire protocol

`PoolWorkProvider` implements M2's WorkProvider. Its QSslSocket is constructed,
used and destroyed only on M2's coordinator thread. Blocking socket operations
run in 100 ms slices with a ten-second request/connection deadline. GUI Stop
sets atomics; it never waits for network I/O or joins workers. Final shutdown
joins the existing coordinator and all started workers. There is no additional
network thread. Wallet unload has a per-session atomic cancellation token.

The client implements actual M3 v1 `hello`, `getjob`, `manifest`, `submit`,
`status`, `ping`, and clean-parent notifications. Network, genesis, algorithm,
protocol version and the zero-fee marker are checked at negotiation. Share
requests carry the immutable job ID and a uint32 nonce; time, version and
coinbase/extranonce rolling are not performed.

M2 gains an opt-in continuous-share flag, an optional transport revocation token
and provider-supplied deadline. Solo jobs retain the existing single-solution
behavior and 30-second lifetime. Pool acknowledgements leave the work and its
atomic 64-bit nonce cursor intact. Workers share disjoint batches of 256 nonces
over `[0, 2^32)` and reuse their authoritative PoWHashContext/scratchpad. The
solution FIFO contains at most 64 public headers and applies backpressure to
workers. Revocation, expiry, Stop and replacement are checked between complete
hashes, including while the FIFO is full. MercaHash consensus code is unchanged.

The client polls jobs every two seconds, checks incoming notifications each
coordinator iteration and pings before one-third of the heartbeat interval.
Actual expiry is translated from a ping-anchored server clock to a monotonic
local deadline. A parent notification revokes current work even while a reply
is pending. Pool status is polled every ten seconds. Server-returned pool names
and error text are not used as GUI markup or ordinary log messages.

Replies are bounded to 8 MiB including audit/template data, depth 32 and 64
frames per second. At most 524,288 structural JSON items are admitted before
UniValue allocation, preventing dense-array memory amplification. Duplicate object keys, invalid UTF-8/NUL, unexpected IDs,
unknown notifications, unexpected job fields and unsupported versions fail
closed. Requests are bounded to 4,096 bytes, with only one outstanding request
per socket. Coinbase data is capped at 128 KiB, recipients at M3's 1,000 limit,
Merkle paths at 32 hashes and decimal work integers at 1,024 digits. These
client limits can refuse exceptionally large future manifests; they cannot
silently weaken checks to fit them.

## Untrusted coordinator and payout checks

Before releasing a job, the client retrieves its frozen public manifest. It
uses Core serialization, transaction IDs, hashes, Merkle primitives and compact
target decoding to check:

- Exact 80-byte header, nonce zero, legal target within this network's powLimit,
  equality of compact/network/template targets and share target no harder than network.
- Header parent/version/time/bits consistency with the supplied template,
  job expiry, canonical coinbase height/marker and fixed 16-byte namespace.
- Version-2 coinbase, null prevout, final sequence, zero locktime and zero
  reserved witness value; exact persisted session namespace and nonzero serial.
- Coinbase txid and Merkle path; every supplied transaction's bytes, txid and
  wtxid; full transaction Merkle root and mutation flag; witness commitment.
- Canonical snapshot digest and template identifier; network/genesis and zero
  fee; positive native PQ outputs matching the manifest exactly, with no extra
  positive output or unallocated remainder.
- Exact integer reward conservation, weights, work score, supported PPLNS
  window multiple, rounding seed, largest-remainder allocations/ties and byte
  limits. The client uses bounded arbitrary-precision integers, never QJson's
  floating-point representation for money/work.
- Existing Core context-free `CheckBlock`/`CheckTransaction` checks, even when
  local chainstate validation is unavailable: transaction structure, money range,
  duplicate inputs, coinbase placement, Merkle mutation, block size and legacy
  signature-operation bounds. PoW remains omitted until hashing.
- Warm-up jobs paying the registered PQ script the entire reward.

These checks bind the disclosed payout snapshot to the candidate being hashed.
When an in-process local Core node is synchronized and its tip matches the
candidate's parent, M4 additionally invokes the existing `Mining::checkBlock`
proposal path with PoW omitted and all other checks retained. That independently
checks the candidate against local consensus, transactions and emission state.
Concurrent local tip changes pause that job. The UI explicitly distinguishes
this result from consistency-only validation. IBD, an unavailable local mining
interface or a different local tip do not prevent compatible pool mining.

Consistency-only operation **does not independently establish** input/script validity, fees, active parent, DGW difficulty or SP-LT emission state. Neither
path can establish the coordinator's historical shares or cutoff completeness.
A malicious coordinator can supply internally consistent but invalid history
when local proposal validation is unavailable. The M3 job ID includes an issuance time that v1
does not expose, so its digest cannot independently be reconstructed; it is
validated as a bounded identifier and bound to subsequent receipts.

Receipt IDs are checked against Core's SHA256d work/header identity, genesis,
accepted sequence, job/snapshot and assigned work score. They are server audit
references, not signed proofs or evidence of active-chain inclusion. No pending
balance, guaranteed payout or custodial accounting is introduced. Mining
software and coordinator fees remain permanently 0%.

## Reconnection, replay and uniqueness

Disconnect pauses/revokes hashing. Retry delay doubles from one second to at
most 30 seconds; refusal of work uses bounded delays. Authentication/protocol,
certificate and malformed-work failures require correction and a new Start.
Transient service/verifier failures pause work. Lost share replies preserve one
public submission for replay after resume, including a Stop/Start within the
same running application; M3 duplicate receipts recover its
original acceptance without double counting. Stale responses are counted
separately. Hashes above target and other refusals are not counted as accepted.

Bearer tokens use Core's secure allocator, which cleanses released memory and
uses existing locked/no-core pages where supported. Owned temporary JSON/wire
buffers are scrubbed when consumed. Snapshots, normal logs, UI, QSettings and
test output omit tokens. Qt/TLS library-internal buffers and arbitrary privileged
process-memory capture cannot be guaranteed absent from every crash mechanism
on every platform; this needs platform-specific hardening review.

Resume tokens are memory-only, scoped to endpoint/CA path, network/genesis and
payout script. Stop/Start with the same binding can resume within the running
application. Restart loses the token and registers a new durable namespace;
historical shares retain their original script. A rejected resume is retried
rather than automatically churning identities. After stopping, **Forget
disconnected pool session** explicitly permits fresh registration if the
coordinator lost its ledger/token or an old connection cannot resume. M3's
registration quotas still apply. Repeated application restarts with a known
payout avoid the new-identity quota, but still consume the time-windowed
new-session quota; changing to a new payout can consume both and the durable
1,000-recipient admission budget. Stop/Start resumes without new registration.
Core wallet encryption protects wallet material, but there is no existing
general encrypted secret store suitable for persisting this network credential.
No new encrypted secret store is invented.

The most recently issued job and serial are remembered with the in-memory session.
New job identifiers must advance the disclosed coinbase serial; reusing or
rolling it back within that session fails closed. After
reconnect, Stop/Start or nonce exhaustion, the client waits for a different
server job instead of starting the same header at nonce zero. Some remaining
search space can be skipped during recovery. Within one honest coordinator's
durable ledger, session namespace plus monotonic job serial makes finalized
coinbases unique across instances/processes. Resuming one namespace concurrently
is rejected by M3. A malicious coordinator or rolled-back/replaced ledger can
reuse namespaces/history; cross-coordinator global uniqueness is not proven.
Future GPU integration must follow these same server-issued namespaces.

## Statistics and build boundaries

Hashrate, completed hashes, active workers, target hits and submission attempts
are measured locally. Accepted/rejected/stale counts reflect this client's pool
replies since Start, as labelled in the UI; lost acknowledgements are recovered where replay is possible. Last
accepted time is the local receipt time. The separately labelled whole-pool
share/session counts come from `status` and are unauthenticated accounting
claims beyond the authenticated TLS peer. Manifest inspection shows public
audit data and obeys the existing privacy mode.

The client is built only with Qt wallet sources and Qt6::Network, already used
by the wallet. Headless builds acquire no Qt dependency. Coordinator SQLite
storage, curl, POSIX reactor code and coordinator libraries are not linked into
the ordinary GUI; the wallet's existing SQLite dependency remains its own requirement.
`BUILD_POOL_COORDINATOR=OFF` still supports native pool mining. Qt's default
TLS plugins must remain available in static builds as well as packaged dynamic
builds. Static applications explicitly import available operational TLS
backends and fail configuration if a wallet build has none. macOS deployment
includes operational TLS plugins and recognizes current Qt plugin layouts.
The depends recipe uses linked OpenSSL on Linux/FreeBSD, Schannel
on Windows and SecureTransport on macOS. OpenSSL is a Qt-only depends
dependency; headless depends builds with `NO_QT=1` omit it. The portable
authoritative M3 fixture test separately needs OpenSSL headers/libcrypto and
is omitted with an explicit configure message if those are unavailable; this
does not affect the production GUI. Windows CI enables the optional vcpkg
`pool-client-tests` feature to supply that test dependency.
The vcpkg overlay preserves the pinned Boost 1.88 port and backports
Multiprecision's upstream PR 667 literal-macro fix for MSVC's conforming
preprocessor. Multiprecision remains a Qt-only feature dependency.
`pool-native-test-client` is a non-installed test driver, not a new
standalone mining application.

Focused coverage is in `cpu_miner_tests`, `test_mining-qt`, the optional
`test_poolclient-qt`, and `mercatura_pool_native.py`. The latter requires GUI
test support plus the optional M3 coordinator/test hash driver, tests real TLS
and real native MercaHash shares/blocks, and checks three owned PQ coinbase
outputs and zero-fee conservation. Its fault server receipts are explicitly
synthetic. See the implementation report for exact tests run and limitations;
source portability does not establish Windows/macOS runtime validation.

## M4.1 validation boundaries

The dedicated `mercatura-native-pool.yml` workflow prepares Linux live M1–M4,
pool-off and walletless builds, packaged static Linux TLS, Windows MSVC shared
and static Qt, Windows UCRT cross-deployment, and native macOS ARM64/Intel
checks. Preparing a workflow does not mean it has run. Native certificate-store,
packaging/deployment and real desktop checks still need those platforms.

The issuance-time job-ID gap and unauthenticated historical PPLNS claims remain
v1 limitations. A separately versioned protocol could disclose issuance time
and authenticated complete share-history anchors; neither can be inferred from
a coordinator-supplied manifest alone. No protocol extension is introduced here.
