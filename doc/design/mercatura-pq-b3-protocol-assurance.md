# Round B3: authenticated PQ protocol assurance

**Complete with documented limitations.** The requested B3 source, deterministic
regression, sanitizer/fuzz, real-node, native-mining and required source CI gates
have passed. This is an implementing-agent assurance record; independent
cryptographic approval is not claimed.

## Source and preserved boundaries

Starting B2 branch `hardening/pq-p2p-b2-ci-resilience`, exact checkpoint
`94bd925c95477f44ee5a427e3a7995e70e1abe20`; qualified B2 source
`61d7009edc1950a8294ac0133349fd7e75bf2fef`. B2 publication workflow
`40438554454` was freshly verified completed/success before B3.
B3 uses branch `hardening/pq-p2p-b3-protocol-assurance` in a separate worktree.
The original checkout, branches, builds, evidence and history are preserved.
Publication uses `project` only, without reset, squash or force-push.
The qualified B3 source is
`db84cc3ac3bcd71b7f6cf913c7eef695e407873d`, published to the new B3 branch.
Its actual [CI run 40452670558](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40452670558)
completed successfully with all seven required jobs. Final documentation is a
separate descendant; its exact publication SHA and observed CI are bound by the
public handoff receipt and Git history.

The frozen Candidate 1 specification, all permanent vectors, production crypto,
protocol, adapter, identity, resource policy, mining and consensus sources are
preserved. PQ remains default-off and explicitly regtest-only. No public peers,
public PQ listeners or deployment configuration are test subjects.
Exact frozen hashes, source references and raw-log hashes are in
[machine-readable evidence](mercatura-pq-b3-evidence.json).

## Verified source CI

[Workflow 40452670558](https://github.com/Mercatura-Core/Mercatura-Core/actions/runs/40452670558)
completed successfully on exact source `db84cc3ac3bcd71b7f6cf913c7eef695e407873d`.
All seven required jobs and the runner-selection meta job passed. Native decoded
logs were retrieved and checked, rather than inferring execution from a green
build alone. Their checksums and case summaries are in the machine evidence.

| Job | Job ID | Actual result / executed scope |
| --- | --- | --- |
| Windows native x86_64 MSVC | `122018777604` | Passed: 185 CTest catalog entries (optional script assets skipped), all 50 selected functional cases; assurance/transport suites and new B3 functional case confirmed in decoded logs |
| macOS native ARM64 | `122018777859` | Passed: 185 CTest catalog entries (optional script assets skipped), all 50 selected functional cases; assurance/transport suites and new B3 functional case confirmed in decoded logs |
| lint | `122018841816` | Passed repository lint |
| Linux native x86_64, headless | `122018841913` | Passed: 185 CTest catalog entries (optional script assets skipped), all 50 selected functional cases; assurance/transport suites and new B3 functional case confirmed in decoded logs |
| Windows x86_64 UCRT cross-compile | `122018841919` | Passed cross-compile; no native runtime qualification inferred |
| macOS Intel x86_64 cross-compile | `122018841924` | Passed cross-compile; no native runtime qualification inferred |
| macOS ARM64 cross-compile | `122018842001` | Passed cross-compile; no native runtime qualification inferred |

The native CTest catalog adds the portable assurance suite; the functional
selection adds the B3 containment case. The extra Linux-only fault/barrier,
late-cancellation and opt-in capacity runs are the explicitly recorded local
executions, not additional Windows/macOS CI qualification. Each native functional
job uses peers built for that platform; heterogeneous multi-host or independent
external-peer interoperability is not claimed. Optional script assets remain
skipped as in the baseline. Final documentation-only CI is observed separately
in the publication receipt; source CI was green before that publication.

## Specification-to-implementation map

References are to sections of the [frozen specification](mercatura-pq-transport-v1.md).
`P` below means existing `pq_p2p_tests`, `T` means `pq_transport_tests`,
`A` means new `pq_assurance_tests`, and `L` means excluded Linux
`pq_lifecycle_tests`. Tests are permanent registrations, not invented targets.
The executed matrix and outcomes are recorded separately in the evidence;
source inspection alone establishes no proof.

| Property / requirement | Production enforcement | Existing / added regression | Vector; failure and qualification boundary | Classification |
| --- | --- | --- | --- | --- |
| Network separation, sections 3/4 | `RegtestNetwork`, `Handshake::Advance`, adapter's incremental N comparison | P `wrong_network_rejected_before_randomness`; A both hellos / all 85 bytes and live transcript fields; functional startup restrictions | Candidate `N`/M1/M2; mismatch before crypto; non-regtest derivation/activation intentionally unavailable | Implemented and tested |
| Canonical record framing, sections 2/4 | `Header/ValidHeader`, `Handshake::Advance`, `PQTransport::ReceivedBytes` | A `canonical_headers_all_bits_and_exact_record_sizes`; T trailing bytes | Candidate five headers/records; reject exact type/role/version/suite/flags/length before avoidable work | Implemented and tested |
| Role/order/duplicate separation, sections 4/9 | `m_initiator`, `m_next`, `m_expected`, send buffer checks | A `order_roles_duplicates_and_terminal_states`; T every outbound record fully sent | Candidate role labels; terminal failure, no alternate state/role interpretation | Implemented and tested |
| Fresh nonces and ephemeral KEM, sections 4/5 | `SecureRandom`, `KEMKey::Generate`, `Handshake::Start/Advance` | A four interleaved sessions; existing real-node fresh reconnect | Candidate public coins; differing controlled seeds prove association in tested cases, not RNG quality | Implemented and tested |
| Exact signature context, section 6 | `Identity::Sign`, `Verify`, `mercatura_mldsa65_*` | A `pure_mldsa_context_and_role_separation`; primitive ACVP checks | Candidate signatures; reject wrong context, NUL, role or signer; Pure ML-DSA unchanged | Implemented and tested |
| Full transcript binding, section 6 | `Handshake::Add/Transcript/SignMessage/Advance` | A `in_transit_transcript_fields_cannot_complete_authentication`; exact P fixture | Candidate A/h3/T3/T4/h5 and msg_I/msg_R; live modified exchange cannot complete; no universal AKE theorem | Implemented and tested |
| Presented identity / expected pin, sections 1/11 | `CheckPeer`, `Fingerprint`, `ParseFingerprint/ParsePinBinding`, `MakePQTransport` | P/T pin tests; T exact destination factory; B1 rotation functional | Candidate public keys/fingerprints; mismatch aborts, aliases/ports do not broaden trust, no TOFU | Implemented and tested |
| HKDF and directional labels, section 7 | `Schedule`, `Derive`, `Extract/Expand`, `Finish` | P exact schedule; A all four directional keys and independent sessions; primitive checker | Candidate IKM/salt/PRK/7 outputs; fixed role order and RFC5869; primitive assumptions remain | Implemented and tested |
| Mutual full confirmation, section 7 | `Advance` M4/M5, `MAC/Equal` | A `all_confirmation_bytes_required_after_valid_signature` | Candidate M4/M5 tags; each of 48 positions required, valid signature alone insufficient; comparison timing is source-reviewed, not measured proof | Implemented and tested |
| Authentication-state publication, sections 9/13 | `Poll`, `InstallKeys`, `GetInfo`, `MarkBytesSent` | T M5 partial sends and failed sibling; L all five job cancellation boundaries | Candidate M5 gate; all assurance false until local completion, terminal failure revokes public status | Implemented and tested |
| Application data gate, section 9 | `SetMessageToSend`, `ReceivedMessageComplete`, `GetReceivedMessage`, `ReceivedBytes` | T all pending send stages, exact M5 bytes, existing coalesced M5+packet; functional no premature version | Candidate records plus packet fixture; queued message retained unmodified; no plaintext delivery on failure | Implemented and tested |
| Session IDs / ownership, sections 7/9 | Handshake-owned secrets, unique Work transfer, `TakeKeys`, `m_generation` checks | A 4 interleaved deterministic sessions, 36 foreign M3/M4/M5 cases; L stale generation and cancellation | Candidate session/id; fresh per-session association; generation rejection uses explicit test-only aging/increment, not ID-wrap exhaustion | Implemented and tested |
| Directional packet authentication, section 8 | `PacketCipher::AAD/Encrypt/DecryptLength/Decrypt`, unique send/receive ciphers | Existing reference/tag/AAD/role negatives; A boundary and replay tests | Frozen 1348 packets; tag/length/role/h5/order errors are terminal, no released plaintext | Implemented and tested |
| Counters and rekey, section 8 | Existing FS cipher classes; PacketCipher counters and cached length | A independent directions at 0/222/223/224/225/447/448/449/671/672; existing multiple epochs | Packet fixture all-stream digest; fresh/stale/replayed state fails; 2^32 cap source-reviewed, not loop-exhausted | Implemented and tested |
| Partial reads/writes, section 8/9 | Adapter fixed buffers, `m_send_pos`, cached `m_packet_length` | T fragmentation 1/7/4096/65536; outbound final-byte tests; existing coalesced M5 | Candidate budgets and packet bytes; no re-encryption or repeated length decode per fragment | Implemented and tested |
| Error cleanup / no resumption, sections 9/12 | `Fail/Disconnect`, `Work::Discard`, RAII Secret, cipher destruction | A terminal retries and zero traffic outputs; L erasure/cancellation; T failed sibling | Candidate negatives; public metadata cleared, ownership retained only while legitimate owners exist | Implemented and tested |
| Resource/source independence, section 12 | `Context::Admit/Submit/Run/Stop`, `SourceGroup` | Existing B2 capacity/fairness; T unrelated healthy pair; B3 real-node sibling | No permanent traffic vector proves availability; bounded-source tradeoffs retained, no pin privilege | Implemented and tested |
| Required-PQ / public activation, section 10 | `AppInitParameterInteraction`, context setup, `MakeTransport`, `ShouldReconnectV1=false` | Existing functional V1/V2 rejection and startup failures | Default off, regtest-only; no fallback after started PQ; bootstrap/proxy rollout deferred | Implemented and tested |
| RPC and copied assurance state, section 13 | `GetInfo`, `CNode::CopyStats`, `getpeerinfo` | T false pending/failed bits, exact pin factory, B3 node reconnect/removal | Only public identity/session fields; unpinned possession is not independently verified; P10 Qt presentation deferred | Implemented and tested |
The classification in each row refers to the named completed Linux fixture
executions. Platform CI conclusions remain a separate gate.
Randomness quality, cryptographic security, forward secrecy/erasure adequacy,
unknown-key-share/reflection resistance beyond tested traces and side-channel
properties remain design assumptions requiring independent analysis.
Counter exhaustion at 2^32 packets and 64-bit generation wrap are implemented
but insufficiently tested: their guards/ownership were inspected, while B3
exercises bounded rekey epochs and explicit stale generations. Reviewed
near-exhaustion injection remains a test gap. Full discovery/bootstrap, public
network operation, Windows persistent storage and the full Qt assurance
presentation remain intentionally deferred.

## State, transcript and trust verification

The transport enforces a send-progress state in addition to the cryptographic
Handshake state: the latter can derive final keys while M5 is queued, but the
initiator's public/application gate opens only after complete `MarkBytesSent`.
The responder's gate opens only after successful M5 verification. A coalesced
packet following valid M5 is permitted; trailing bytes after M1..M4 are not.
In `CNode::ReceiveMsgBytes`, only a complete authenticated transport message
reaches the processing queue. Worker-pending input is rejected rather than
returning success with unconsumed bytes into that loop.

The status triple below is `pq_encrypted / pq_authenticated /
pq_identity_verified`.

| Local phase | Permitted progress | Application / public assurance |
| --- | --- | --- |
| I initial worker / sending M1 | One Start result; finish P+M1 before accepting P+M2 | Closed / false |
| R detecting / processing M1 / sending M2 | Exact preface/header/N; admitted M1 job; finish P+M2 before M3 | Closed / false |
| I processing M2 / sending M3 | Validate peer/N/KEM; decapsulate and sign; finish M3 before M4 | Closed / false |
| R processing M3 / sending M4 | Verify expected identity and sig_I before decapsulation; finish M4 before M5 | Closed / false |
| I processing M4 / sending M5 | Verify sig_R and confirm_R; internal keys ready while M5 is queued | Closed / false until all 60 M5 bytes marked sent |
| R waiting for M5 | Verify complete header and all 48 confirmation bytes inline | Opens only after valid M5 |
| Established | Directional packets, cached partial I/O, authenticated message decode | Unpinned true/true/false; pinned true/true/true |
| Failed, disconnected or stopped | Cancel/reject work, release owned state, no retry/resumption/fallback | Closed / false; no session ID/fingerprint |

New live alteration tests route the actual generated suffix after mutating N,
nonce, KEM key, identity, ciphertext, signature or confirmation. Valid public-key
substitution separately prevents modulus rejection from being mistaken for a
transcript-binding result. Header tests flip all 480 header bits across five
canonical headers, enter actual handshake states, vary sizes and reject wrong
records. The full successful frozen wire exchange remains exactly 15488 bytes.

Four deterministic sessions share valid identities and pins while all supplied
KEM/nonce/signing coins differ. Tests compare peer keys/IDs and exchange every
foreign M3/M4/M5 across 12 ordered session pairs. They also preserve the source
session on target rejection. B1 synchronization hooks hold completion after
calculation and before publication at all five crypto stages; disconnect cannot
publish assurance even at this late boundary. These hooks are excluded from
ordinary node binaries. Finite interleavings are not a complete concurrency proof.

The connection identity in worker publication is implicit in exclusive ownership
of `PQTransport::m_work`, rather than a global result map keyed by NodeId.
`Work::generation` is additionally compared to the owning transport generation.
Different connections can both start at generation 1 without sharing a result
slot. This implementation interpretation of the specification's connection-plus-
generation binding is explicit for reviewer evaluation; the tests do not exhaust
64-bit generation wrap or every possible scheduler interleaving.

Exact factory pin tests exercise IPv4, bracketed IPv6, configured names, case,
different ports and alternate IPv6 spelling, then fresh reconnect. No alias
normalization or automatic pin update is introduced. Existing real-node wrong
pin, persistent restart and explicit identity rotation regressions remain gates.

The current Qt peer panel displays the transport name, but
`src/qt/rpcconsole.cpp` shows its session-ID widget only for classical V2.
It does not yet provide the three separate PQ assurance labels proposed in
specification section 13. RPC and copied node stats expose the PQ state; the
Qt presentation remains an explicit P10 gap. No misleading new Qt assurance
indicator or GUI redesign is introduced in B3.

## Packet and real-node containment matrix

New packet regressions test both directions before/at/after three rekeys,
authentication failure at each selected index, old packet replay, stale/fresh
counter mismatch and terminal API-order errors. Existing independent packet
fixtures stay unchanged. Empty/oversized/control/command cases remain covered
by their permanent reference and application tests.

The B3 functional fixture uses three fresh loopback regtest nodes and the
existing bounded transparent relay. It establishes two successive ordinary
unpinned sessions, requires different IDs, then checks six cases: invalid M3
signature, invalid M4 confirmation, invalid M5 confirmation, old M5, invalid
first application ciphertext and disconnect after M3. Each must remove only
the affected connection, release no unauthenticated version at the receiving
node, and preserve the sibling session ID and successful ping/pong delivery.
Real-node cancellation of a specific worker uses the deterministic in-memory
barriers; the wire disconnect case alone makes no worker-scheduling claim.

Executed results, commands, durations, case exits, source hashes and
introducing commits are recorded in the tables and machine-readable ledger.

## Executed local evidence

The final test/tooling source is `db84cc3ac3bcd71b7f6cf913c7eef695e407873d`.
All production sources remain byte-identical to the B2 checkpoint. The fresh
Release build uses GCC 14.2, CMake 3.31.6, wallet/Qt/OpenCL enabled, CUDA/IPC/
DBus/QR/ZMQ disabled; extracted dependencies use a relocated pkg-config sysroot.
The host is Debian 13 x86_64 with a two-CPU quota and eight-GiB memory ceiling.
Log/receipt times are UTC; the validation session began October 26, 2026 in
America/Los_Angeles. Correctness runs may overlap; native mining runs separately
from compilation and other runtime workloads. Timings are not performance SLOs.

| Run | Executed cases / assertions | Wall seconds | Outcome |
| --- | --- | ---: | --- |
| Final focused protocol, Clang ASan/UBSan/LSan including native crypto C | 20 / 84599 | 5.719 | Passed |
| Final focused protocol/adapter/lifecycle, normal | 56 / 188414 | 15.080 | Passed |
| Same focused boundary, ASan/UBSan/LSan | 56 / 188459 | 17.713 | Passed |
| Same focused boundary, TSan | 56 / 188519 | 16.928 | Passed |
| Explicit B2 capacity, normal | 2 / 843 | 1.227 | Passed |
| Explicit B2 capacity, ASan/UBSan/LSan | 2 / 843 | 1.285 | Passed |
| Explicit B2 capacity, TSan | 2 / 849 | 1.255 | Passed |
| Frozen packet reference | 1348 packets, three rekeys/direction | 1.392 | Passed |
| Primitive reference | 101 symmetric, 105 ACVP, 45 ML-DSA signature, 12 signature, 12 OpenSSL HKDF, 5 negative KEM, 3 upstream KAT, 4 candidate checks | 14.597 | Passed |
| Final source lint | Repository runner, all checks | 20.377 | Passed |

The 56-case selection skips the existing benchmark and two opt-in capacity
cases; those two capacity cases run explicitly afterward. Assertion counts vary
with asynchronous polling; overlapping suites are not added into a security
coverage percentage. Leak checking stays enabled; no sanitizer suppressions are
used. Compatible non-ptrace execution was explicitly approved for LSan/TSan.
The focused adapter boundary instruments first-party protocol/crypto adapters,
network context, storage and lifecycle hooks; other linked Core libraries and
native C in that normal build remain uninstrumented. The separate Clang
protocol/fuzzer build instruments the native crypto C as well. Neither result
is full-node TSan or sustained full-node sanitizer qualification.

| Guided target | Executions | Edge / feature counters | Corpus files | Outcome |
| --- | ---: | --- | ---: | --- |
| `pq_session` (new continued exchanges) | 1637 | 1965 / 4675 | 83 | Passed |
| `pq_handshake` | 1711 | 2062 / 5163 | 56 | Passed |
| `pq_packet_stream` | 5414 | 497 / 1697 | 36 | Passed |

Each campaign requested 60 seconds; combined wall time was 184.807 seconds,
8762 executions, zero crash artifacts and zero sanitizer findings. Edge/feature
counters are libFuzzer observations for these targets, not percentages of the
whole node or a claim of complete state coverage. Corpus files/checksums and
fixed-seed commands are retained. The earlier 51-million-execution campaign is
historical and is not counted as B3 work.

| Ordinary node regression | Wall seconds | Observed result |
| --- | ---: | --- |
| New B3 three-node containment | 18.393 | Two good sessions with fresh reconnect; six failures preserved sibling session and ping/pong; peer removal and shutdown passed |
| Existing wire negative matrix | 66.960 | 24 scenarios; absolute preface/record deadlines 5.072/30.125 seconds; no premature version or classical retry |
| B1 lifecycle plus V2-from-genesis | 33.043 | Local configuration rejections, native PQ payment/block relay, wrong pin, persistent rotation, reconnect/restart; height 103, full verification 4.401/4.258 seconds |
| V1-to-V2 at height 20 | 63.153 | Native PQ payment/block relay and the same lifecycle recovery; height 103, full verification 10.696/10.248 seconds |

Both positive node runs retained the exact 15488-byte handshake accounting,
changed ephemeral identities/session on reconnect, preserved explicit persistent
identity/rotation semantics, and matched the post-restart chain tip. Public
transaction/block/session observations are in the JSON; no wallets or seed files
are published. RPC-generated blocks in these fixtures are not native mining.

## Native CPU mining compatibility

Both runs use one native CPU worker, explicit `-par=1` on both endpoints,
normal regtest difficulty, PQ required and a 3072-MiB sampled aggregate RSS
watchdog. The V2 run mines for 60 seconds before Stop and also requires an
actual mature native-funded wallet payment; the transition run mines for ten
seconds with activation at height 20. Neither uses RPC block generation as a
substitute for native mining.

| Schedule | Accepted before Stop | Final verified height | Stop / Close ms | Worst Qt heartbeat ms | Total command seconds |
| --- | ---: | ---: | --- | ---: | ---: |
| V2 from genesis, native-funded payment and mining reconnect | 533 | 548 | 48 / 108 | 303 | 124.511 |
| V1-to-V2 at height 20 | 19 | 34 | 145 / 62 | 87 | 41.351 |

V2 payment `3ac941b111ad643bd4d5da0ff75ebe3a15b82b4769c71187b7cd2f486490edfe`
confirmed in `7740586563212f0879eb7b363d1d107752682ae06ae8f7a6b2e1b8a579f6a5e0`.
A fresh PQ reconnect completed in 4.305 seconds while mining remained busy and
hashes advanced. This includes reconnect scheduling and RPC polling, not only
cryptographic work. Both runs reopened the stopped producer without mining,
reconnected with a fresh PQ session, matched exact tips, passed full chain
verification and confirmed every child exited. Resource sampling covers the
mining phase; post-restart verification is protected by the total watchdog.
No broader default-thread GUI or representative-hardware performance claim is
made. B2's automatic-validation GUI heartbeat failure remains a limitation.

## Sanitizer, fuzz and integrated gates

Focused ASan/UBSan/LSan and TSan runs target the new assurance, transport and
lifecycle paths using the existing excluded targets. Their other linked Core
dependencies are uninstrumented. A separate Clang libFuzzer build instruments
the protocol and crypto library, including compiled native crypto C; assembly
and system libraries remain outside that boundary. There is no full-node TSan
or new sustained instrumented endurance claim.

The new `pq_session` fuzz target continues generated live exchanges after a
bit change, truncation, extension or old-session record substitution. It asserts
terminal failure cannot revive and no altered exchange becomes mutually ready.
The existing handshake and packet-stream targets supply adjacent coverage.
Campaigns use fixed seeds, finite duration, 16-KiB inputs, 1-GiB RSS guard and
10-second input timeout. Corpus hashes, edge/feature counters and actual
executions belong in the final evidence; a high count is not a security proof.

Ordinary node gates include V2-from-genesis, V1-to-V2 transition, identity/pin
lifecycle, transaction/block relay, restart and full chain verification. Native
CPU mining uses the existing one-worker controlled `-par=1` fixture. B2's
three-hour endurance result is preserved; B3 does not repeat it for test-only
changes. B2's automatic-validation GUI heartbeat and extended latency/sampling
outliers remain unresolved limitations, not new B3 successes.

## Review, defects and deployment recommendation

No production defect was confirmed in the completed B3 local checks. B3 adds
15 permanent unit cases, one permanent three-node functional case and one new
continued-exchange fuzz target. Six coherent source commits preserve production
protocol, cryptographic, resource, mining and consensus behavior.

| Development issue | Disposition |
| --- | --- |
| Initial default IPC configuration lacked Cap'n Proto | Configuration did not complete; explicitly disabled IPC consistently with the existing qualification configuration |
| Initial normal CLI build could not find libevent headers | Initial pkg-config cache used system paths; refreshed discovery with the extracted dependency sysroot. The unchanged source then built successfully. Initial exit-1 build log is retained. |
| Native payment requested with a 20-second mining phase | Helper rejected it before starting any node because coinbase-maturity qualification requires at least 60 seconds. Retained exit-2 command receipt; supported 60-second run passed without changing checks. |

No B3 action remains blocked by an approval/security restriction. Compatible
sanitizer execution was approved; no restriction was bypassed. Historical B2
approval or runtime failures are not reclassified as new B3 results.
The [independent review package](mercatura-pq-b3-independent-review-package.md)
contains exact construction inputs and questions about malicious KEM
contributions, AKE authentication, transcript/role/session binding, confirmation,
forward secrecy, downgrade/pin models, packet ratchets and formal analysis.

Residual gates include Windows fail-closed persistence; POSIX unencrypted
storage and trusted-user/ancestor assumptions; discovery and decentralized
bootstrap; external independent peers; real Tor/proxy/IPv6/NAT behavior;
representative hardware/platform release testing; full Qt presentation; and
separately authorized public-testnet qualification. No symbolic proof or
independent cryptographic audit has been performed by this implementing agent.
Completion classification: **Complete with documented limitations**. The
implementation is ready for independent cryptographic review and the next
separately authorized controlled qualification stage. No fundamental protocol
concern or production defect was confirmed in these bounded checks. No public
activation, mainnet enforcement or independent cryptographic certification is
granted; the explicit assumptions and deployment gates above remain mandatory.

| B3 checkpoint | Completion evidence |
| --- | --- |
| B3.0 baseline | Exact B2 checkpoint/green CI and original refs preserved; separate worktree |
| B3.1 security-property mapping | Nineteen source/test/vector rows with classifications; explicit review assumptions and gaps |
| B3.2 handshake/transcript | Canonical records, roles/order, live transcript mutations, exact Pure context, all confirmation bytes and send gates |
| B3.3 session/trust | Four interleaved sessions, 36 foreign authentication/confirmation substitutions, exact pins, fresh reconnect and five cancellation boundaries |
| B3.4 packets | Directional authentication, terminal errors, stale counters and selected boundaries across three rekeys; frozen 1348-packet reference |
| B3.5 bounded qualification | Three guided targets, focused ASan/UBSan/LSan/TSan and opt-in capacity; scope and limits retained |
| B3.6 integration | Real-node payment/block/identity/restart cases, two native CPU schedules and all seven required source CI jobs |
| B3.7 review preparation/publication | Independent reviewer package, machine evidence, unresolved-risk inventory and a public-only review archive with publication receipt |

## Reproduction and publication checks

Build the repository normally for your platform. The fault/barrier targets below
are excluded Linux GCC/Clang developer targets; ordinary native platform CI
builds/runs the portable assurance and transport cases through `test_bitcoin`.
Use a separate Clang build for the real libFuzzer target with
`BUILD_FUZZ_BINARY=ON` and `SANITIZERS=fuzzer,address,undefined`. Keep sanitizer
configurations separate and leak detection enabled in a compatible environment.

```sh
build=/path/to/normal-build
runs=$(mktemp -d)
cmake --build "$build" --target mercaturad mercatura-cli pq_b1_tests \
    pq_b1_sanitizer_tests pq_b1_tsan_tests test_mining_app-qt -j2
"$build/bin/pq_b1_tests" --log_level=test_suite --report_level=detailed
"$build/bin/pq_b1_tests" --run_test=pq_lifecycle_tests_capacity
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$build/bin/pq_b1_sanitizer_tests" --report_level=detailed
TSAN_OPTIONS=halt_on_error=1 "$build/bin/pq_b1_tsan_tests" --report_level=detailed
python3 test/functional/feature_pq_b3_assurance.py \
    --configfile="$build/test/config.ini" --tmpdir="$runs/containment" --portseed=972
python3 contrib/devtools/qualify-mercahash.py --build-dir="$build" \
    --mode=cpu --schedule=v2 --workers=1 --seconds=60 --deadline=360 \
    --offscreen --pq --relay-transaction --validation-threads=1 \
    --memory-budget-mib=3072 --output-parent="$runs/native"
python3 contrib/devtools/pq-fuzz-campaign.py --binary=/path/to/clang-build/bin/pq_fuzz \
    --output="$runs/fuzz" --seconds-per-target=60 \
    --targets pq_session pq_handshake pq_packet_stream
```

The evidence lists every actual command, sanitizer environment, duration, exit,
source checkpoint/dirty-file digest and retained raw-log checksum. Earlier
checks performed before source commits are bound by their exact source-file
hashes; final normal/adapter/node/mining runs use the committed source SHA.
Public summaries/corpora are review inputs; raw node datadirs, wallets, identity
files and RPC cookies are excluded from publication. Final documentation is a
separate descendant; the publication receipt binds its exact SHA and observed
CI without an impossible self-referential committed hash.
