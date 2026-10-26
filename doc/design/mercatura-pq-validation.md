# PQ transport checkpoint: validation and remaining gates

Status: P1 source audit complete with stated limitations; P2 candidate 1 prepared
for maintainer review. P3–P11 are not complete. The runtime transport implementation
is 0%. Counting completed numbered phases equally gives 1/11, approximately 9%;
this is not an estimate of engineering effort or time remaining.

## Reproducible environment and commands

Audited base: `631886dd5159614b628ced3fe4fa1e289ce19451`, local branch `v0.1`
created from initially clean `work`. See `git log` for the checkpoint commit;
this file deliberately does not embed its own containing commit hash.

Observed environment: Debian GNU/Linux 13, Linux x86-64, GCC/G++ 14.2.0,
OpenSSL 3.5.7, Intel Xeon Platinum 8573C, three CPUs exposed, approximately
9.7 GiB RAM, no swap. This is a cloud VM, not Ubuntu 22.04 WSL2 or Ryzen 4600G.
No Windows/macOS node or primitive build was run.

Only the standalone portable primitive driver was compiled. Production CMake
sources and flags were not changed. A full node configure/build was unavailable:
`cmake --version` returned command not found, no active build directory existed,
and Boost/Libevent/SQLite development headers were absent. A read-only
`git ls-remote` failed to connect to the configured proxy at port 8080.
GitHub connector reads succeeded. No public P2P connection was attempted.

The evaluated ML-KEM source lives outside the production tree. To reproduce in
an environment with Git HTTPS access:

```sh
git clone --branch v2.0.0 https://github.com/pq-code-package/mlkem-native.git work/mlkem-native
git -C work/mlkem-native rev-parse HEAD
# Must be d1b2fe782888bdb761a50336012923180be7f502.
python3 contrib/devtools/pq-primitive-check.py \
  --work-dir work/pq-primitives-release --mlkem-source work/mlkem-native
```

The driver checks the expected Git blob hash of all 43 evaluation files before
compiling. Its source pin is not inferred from the mutable tag name. Compiler
flags are C99/C++20, `-O2`, ML-DSA parameter set 65, `MLD_CONFIG_CORE_API_ONLY`
for the existing upstream translation unit, ML-KEM parameter set 768, and
`MLK_CONFIG_NO_RANDOMIZED_API`. Native arithmetic and FIPS202 backend flags are
unset. ML-KEM is compiled for this executable only; node libraries are unchanged.
No upstream code was modified. Existing ML-DSA is reused at its recorded v2.0.0
commit `834a90d5e846ffa1e1611bd24e160bb2e9b86d35`; its tag was verified, not
every existing vendored blob independently downloaded.

The actual successful command used external source and build directories:

```sh
python3 contrib/devtools/pq-primitive-check.py \
  --work-dir /workspace/work/pq-primitives-release \
  --mlkem-source /workspace/work/mlkem-native
ASAN_OPTIONS=detect_leaks=0 python3 contrib/devtools/pq-primitive-check.py \
  --work-dir /workspace/work/pq-primitives-sanitized \
  --mlkem-source /workspace/work/mlkem-native --sanitize address,undefined
```

Sanitizer compilation uses `-O1 -g -fno-omit-frame-pointer`,
`-fsanitize=address,undefined` and `-fno-sanitize-recover=all`. Normal and sanitized
builds usually take tens of seconds to a few minutes on this environment;
future full node builds may take much longer and need fresh estimates.
No long-running node benchmarks or functional test suite were started.

## Executed checks

| Check | Cases | Result and scope |
| --- | ---: | --- |
| SHA3-384/HMAC-SHA3-384/existing L32 HKDF | 101 | PASS, repository C++ vs Python hashlib/hmac; sponge/HMAC block boundaries, empty/binary input, oversized HMAC key, and existing wallet golden result. |
| Published NIST ACVP ML-KEM-768 | 80 | PASS: 25 keygen, 25 encapsulation, 10 decapsulation including implicit-rejection outputs, 10 secret-key checks, 10 public-key checks. |
| Published NIST ACVP ML-DSA-65 keygen | 25 | PASS, seeded public and expanded secret-key outputs. |
| ML-DSA signature/domain/size checks | 12 | PASS, deterministic same-randomness signing, verification, wrong message/key/context/signature, transaction vs P2P context, NUL context difference, truncated signature/key, oversized context, existing transaction golden signature fingerprint. |
| KEM rejection behavior | 5 | PASS, valid roundtrip, altered ciphertext returns success with a different secret, stable implicit rejection for same input, invalid modulus rejected, corrupted secret-key hash rejected. |
| Upstream v2.0.0 ML-KEM KAT | 3 | PASS, exact full public/private key, ciphertext and shared-secret bytes from the pinned expected-vector header. |
| Candidate fixed primitive inputs | 4 | PASS, replay of two fixed signing API inputs and two confirmation HMAC inputs; no handshake state machine. |
| Independent OpenSSL RFC 5869 | 12 | PASS, candidate Extract and seven labeled Expands, plus empty info/salt and 1/48/80/96-byte cases. The new production general HKDF is not implemented. |

Total: **242 counted cases** per full run, with individual assertions inside
some cases. This is a case count, not 242 independently implemented test suites.
The NIST fixtures are pinned to `usnistgov/ACVP-Server` commit
`975de31eb83d87039ec88934fdc47d8c312b892d`, source directories
`ML-KEM-keyGen-FIPS203`, `ML-KEM-encapDecap-FIPS203`, and
`ML-DSA-keyGen-FIPS204`. Input bytes and shared-secret outputs are exact.
Large keygen/ciphertext expected outputs are retained as SHA3-384 digests of
the full official bytes; key-check/decapsulation inputs remain complete.
This is offline vector compatibility, not a NIST validation certificate.

The same script also runs without `--mlkem-source`, checking the existing
Mercatura primitives and fixed signing inputs without downloading any dependency.
That reduced run passed 154 cases; it does not claim ML-KEM coverage.

Existing wallet golden child output remains
`0be624be1e03a6fd1e078d25352115a74aca5696e332ab7e23e5dd019b784484`.
The existing fixed transaction signature's SHA-256 fingerprint remains
`12b1e37ac7df6e2b58601d5e2b9bd2f53cf6e25b89e1cb1d13dbd3644806bea4`.
Those checks preserve byte behavior; they do not run complete wallet/consensus
regression suites.

An initial address/undefined sanitizer run reached process exit, where
LeakSanitizer failed with `LeakSanitizer does not work under ptrace` and a fatal
runtime diagnostic. That run is recorded as failed, not a sanitizer pass.
The successful subsequent run explicitly disabled **only leak detection**,
leaving address and undefined-behavior instrumentation enabled. No real memory
or undefined-behavior finding was suppressed. Leak checking remains unavailable
in this runtime and must run in a suitable independent environment.

The complete candidate fixture was calculated offline using fixed primitive
calls and direct canonical byte layouts. Repository SHA3/HMAC outputs were
cross-checked with hashlib/hmac, and HKDF was cross-checked with OpenSSL.
This is not an independently implemented AKE or a formal model.

Candidate fixture SHA-256:
`6222abf5163ee34a987fe383a8e10b07e50abf1a3ab93f2a0e031a50dd9415f5`.
Full candidate records have lengths 3,265 / 4,353 / 4,409 / 3,369 / 60.
With two 16-byte prefaces the wire count is 15,488 (I sends 7,750; R 7,738).
These are computed serialization sizes, **not performance benchmarks**.

## Mandatory transport failure matrix after approval

Every rejection below must additionally assert: zero application messages
delivered or emitted, no completed assurance flags, no classical fallback request,
bounded resources, secret cleanup and a clean disconnect. An observed disconnect
alone is insufficient. All rows are currently **NOT RUN against a transport**.

| Input / scenario | Exact required outcome |
| --- | --- |
| Valid unpinned session | Both roles complete all five records; same session ID and reciprocal keys; `(encrypted,authenticated,identity_verified)=(true,true,false)`. |
| Valid session with expected key on one side | Only that side sets identity_verified; both still verify signatures and confirmation. |
| Wrong pin / silently replaced persistent key | Reject; no substitute identity, pin update or fallback. |
| Wrong chain ID/magic/genesis/signet challenge | Reject before expensive work when detectable; no cross-network replay or local network switch. |
| Wrong version/suite/flags/role/type | Reject exact header before body allocation or KEM/signature work. |
| Length unequal to expected fixed size, including huge integer | Reject after header; no allocation proportional to peer length. |
| Modified KEM public key with invalid modulus | Reject library public-key check, no successful session. |
| Different valid KEM public key inserted by attacker | Authentication/transcript/confirmation fails; malformed-byte tests alone do not cover this case. |
| Modified ciphertext with valid size | Honor implicit rejection; authentication or secret confirmation fails; no validity oracle. |
| Wrong signing key, modified signature/context or signing mode | Signature verification fails; Pure vs HashML-DSA is not reinterpreted. |
| Altered nonce/identity/header or transcript boundary | Authentication fails, including wrong byte order, added NUL or omitted domain length. |
| Incorrect R confirmation | I sends no M5 or application traffic; disconnects and clears secrets. |
| Incorrect I confirmation | R releases no application traffic or completed status; disconnects. |
| M3 valid but M4 suppressed | Neither side enables application processing. |
| M4 valid but M5 suppressed | I remains gated until its own M5 was fully sent; R never enables its gate without M5. |
| Replay from another connection | Fresh keys/nonces cause rejection; no replay cache is a substitute for freshness. |
| Reflected role/messages, duplicate or reordered record | Wrong expected state/type/role rejects; no repeated expensive operation. |
| Unknown-key-share attempt across simultaneous connections | Verify ordered identity/role/network binding and session/key disagreement; no accepted unintended pinned endpoint. |
| Empty/truncated record, disconnect at each byte/stage | Bounded wait until absolute deadline, then cleanup; no partly authenticated session. |
| Single-byte fragmentation / delayed trickle | Accept valid bounded fragments; deadlines never reset per byte; crypto runs once per stage. |
| Partial sends at every boundary | Stable queued bytes and exact accounting; no reencryption, duplicated record or cipher counter advance. |
| Unsupported extension/trailing body field | Length/state failure; no ignored handshake extension. |
| Early application/version data, including appended to M1–M4 | Reject before parsing/releasing application data. |
| M5 plus first valid packet in same TCP buffer | Verify M5 first; only then process packet bytes once. |
| V1/V2 peer, PQ-required connection, timeout or network failure | Required attempt fails; no legacy reconnect, policy mutation, or pin loss in manual/proxy paths. |
| Preface collision and prefix fragmentation | Correct discriminator buffering, at most 16-byte lookahead, no duplicated bytes; exact PQ marker commits to fail-closed PQ parsing. |
| Wrong AEAD tag/encrypted length/control bits | Reject with bounded allocation and zero unauthenticated plaintext. |
| Packet reordering/duplication, rekey packets 223/224/225 | Correct directional counters and fail-closed ordering; compare independent packet vectors. |
| Packet limit at 2^32 / nonce epoch boundaries | Close before prohibited counter use; no key/nonce reuse. |
| RNG error, signing failure, allocation failure | No success or weak deterministic substitute; cleanup and fail closed. |
| Worker completes after disconnect/replacement | Stale generation result discarded; no use-after-free, secret leak or revived connection. |
| Flood/queue saturation/source churn | Enforce global, source, anonymous, byte, lifetime and worker bounds; no unbounded threads/accounting map. |
| NAT, shared IP and Tor users under load | Measure fairness, no proxy-IP misidentification or simplistic lasting ban from resource rejection. |
| Corrupt/missing/symlink/wrong-network key file | Explicit startup/generation failure; no implicit identity replacement; OS-appropriate permissions. |

This matrix must become unit/functional/fuzz coverage after approval. Add fuzz
targets for bounded parser/state/fragment streams, KEM/identity bytes and version
selection. Run ASan/UBSan and supported leak/TSan checks as appropriate; do not
classify this standalone primitive run as transport fuzzing.

## Unrun regression and benchmark qualification

Full node unit suites, BIP324/V1 networking functions, PQ wallet/native
transactions, consensus/PoW/block validation, Qt, native mining, proxies/Tor,
IPv6, node interoperability, and end-to-end benchmarks are unrun here.
No Windows/macOS support claim is made. The mining branch was inspected through
read-only remote comparison, not merged or built. Source in this checkpoint
does not change these behaviors, but that is not a substitute for regression
evidence on the later implementation.

After design approval and full dependencies are available, establish the
unchanged-node baseline first. Use an isolated datadir per regtest node,
nonconflicting ports and no live seed/public endpoint. Preserve BIP324 tests,
native authorization and wallet vectors, and realistic 150-second block-arrival
qualification. Report a runtime estimate before full builds or long tests.

P8 must measure real node-to-node PQ and BIP324 handshakes on the same hardware:
latency/CPU/bytes/peak RAM, establishment/reconnection, throughput/steady state,
single/multiple peers, normal and bounded heavy churn, blocks/native PQ relay,
bootstrap, partition recovery and mining coexistence. Publish raw samples and
mean/median/high percentiles with compiler/OS details. No such measurements
exist at this checkpoint, including on the requested Ryzen/WSL2 machine.

## Security and production gates

- [x] Actual source/branch/remotes/worktree inspected; unrelated work preserved.
- [x] ML-KEM and existing ML-DSA release commits resolved; evaluation files pinned.
- [x] Pure ML-DSA semantics, exact contexts/signed bytes and candidate fixtures defined.
- [x] Wallet HKDF and transaction signature golden behavior checked independently of node build.
- [x] Presented-key proof distinguished from a trusted expected identity.
- [x] No custom runtime handshake, no deployment and no consensus change.
- [ ] Maintainer explicitly approves candidate document/fixture revision.
- [ ] Complete advisory/FIPS errata/SP 800-227 review through accessible authoritative sources.
- [ ] Independent AKE/combiner/forward-secrecy/UKS/replay analysis; formal model if feasible.
- [ ] Second independent complete protocol/packet fixture derivation and frozen interoperability vectors.
- [ ] P3 production ML-KEM integration, general HKDF, identity subsystem and RNG failure handling.
- [ ] P4/P5 actual authenticated transport, keys/storage/pins, application gates and accurate RPC/Qt status.
- [ ] P6 enforced resource bounds with measured CPU/stack/RSS and lifecycle cancellation safety.
- [ ] P7 complete negative matrix, fuzz/sanitizer, node and Mercatura regression tests.
- [ ] P8 real end-to-end performance/resource benchmarks; no invented figures.
- [ ] P9 Linux node build and Windows/macOS release qualification.
- [ ] P10 existing RPC/Qt assurance status validation.
- [ ] P11 authorized public-testnet optional/required qualification and independent-community bootstrap.
- [ ] Approved mining branch integration in a combined candidate, without weakening consensus or extreme-test opt-in policy.
- [ ] Independent security findings resolved; maintainer authorization for production enforcement/deployment.

Until those gates pass, engineering is incomplete and mainnet readiness is not
established. The next action is maintainer review of the concrete candidate;
safe independent primitive/library preparation can continue while it is pending.
Do not waive the design gate because the primitives pass tests.
