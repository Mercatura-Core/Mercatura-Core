# Mercatura Core — Three-Issue Security Hardening

**Overall result: PARTIAL.** The three defensive fixes are implemented as independent commits. Local qualification results below apply to the exact patched source; native platform gates remain open.

**Base:** `61f1f4b9e24d07ea56fcfc1952aaa73aac498394`
**Patched source:** `0a66162c465898b132e31d7574b0b8fb89ff09cc`
**Branch:** `security/bitcoin-core-2026-10-backports`

The original checkout remains at 631886dd5159614b628ced3fe4fa1e289ce19451. The qualified candidate was fetched through the authorized project remote into an isolated worktree. No original changes were discarded and no integration history was rewritten.

| Issue | Result | Security commit | Upstream |
|---|---|---|---|
| CVE-2026-117350 — TCP urgent-data receive-backpressure | PASS | `77a4de7a10730544f728fd809f710a9a8fec5e46` | [PR](https://github.com/bitcoin/bitcoin/pull/36665) / `2186232955fa70f47c584fb4973e5d849dcfe0f1` |
| CVE-2026-117349 — Persistently paused send buffers | PASS | `2f25a22f4fd6522b451f7cdad599b4cb2e79f9a3` | [PR](https://github.com/bitcoin/bitcoin/pull/36667) / `5da09030f7402e90a4fd73fced576c44d05cf1d3` |
| CVE-2026-117346 — Block/undo absolute file positions | PASS | `0a66162c465898b132e31d7574b0b8fb89ff09cc` | [PR](https://github.com/bitcoin/bitcoin/pull/36669) / `2b5b6fe231ff45a09d9e24411e4420d51ed45cab` |

**Fix A: TCP urgent-data receive-backpressure**

Exceptions were monitored even when receive monitoring was paused, allowing urgent data to trigger more reads. select exception monitoring requires a receive request; send-only/paused sockets cannot get urgent-data ERR readiness. Official advisory: https://bitcoincore.org/en/2026/10/28/disclose-tcp-oom/

Files: `src/test/CMakeLists.txt`, `src/test/sock_tests.cpp`, `src/test/util/sock_select.cpp`, `src/util/sock.cpp`.

| Focused check | Result | Seconds |
|---|---|---|
| `A-select` | PASS | 0.164 |
| `A-unit` | PASS | 23.095 |
| `A-pq-select-functional` | PASS | 160.87 |
| `A-classical-select-functional` | PASS | 138.806 |
| `A-select-asan-ubsan` | PASS | 0.229 |
| `A-pq-fuzz-smoke` | PASS | 2.539 |
| `A-pq-protocol` | PASS | 0.565 |

**Fix B: Persistently paused send buffers**

Pre-BIP31 peers could remain send-paused while additional announcements accumulated, without an independent pause deadline. Both pause writers record only false-to-true transitions atomically; continued pause beyond the existing twenty-minute interval triggers disconnect, independent of protocol version or transport. Official advisory: https://bitcoincore.org/en/2026/10/28/disclose-fpausesend-oom/

Files: `src/net.cpp`, `src/net.h`, `src/test/CMakeLists.txt`, `src/test/net_tests.cpp`, `src/test/util/net.h`.

| Focused check | Result | Seconds |
|---|---|---|
| `B-unit` | PASS | 24.211 |
| `B-final-pause-unit` | PASS | 0.366 |
| `B-pause-asan-ubsan` | PASS | 0.417 |
| `B-peer-timeouts-functional` | PASS | 2.209 |
| `B-pq-lifecycle-functional` | PASS | 161.661 |
| `B-pq-lifecycle-unit` | PASS | 16.958 |

**Fix C: Block/undo absolute file positions**

32-bit absolute offsets and undo accounting could wrap; Windows long-based seek/truncate operations could not represent large positions. Absolute positions/accounting are uint64_t; seeks, truncation and allocation use checked wide operations; existing VARINT bytes remain identical for old valid values. Official advisory: https://bitcoincore.org/en/2026/10/28/disclose-undo-overflow/

Files: `src/chain.h`, `src/flatfile.cpp`, `src/flatfile.h`, `src/kernel/chain.h`, `src/node/blockstorage.cpp`, `src/node/blockstorage.h`, `src/streams.cpp`, `src/streams.h`, `src/test/CMakeLists.txt`, `src/test/blockmanager_tests.cpp`, `src/test/blockstorage_durability_tests.cpp`, `src/test/flatfile_tests.cpp`, `src/test/util/sparse.h`, `src/util/fs_helpers.cpp`, `src/util/fs_helpers.h`, `test/functional/feature_security_storage_compat.py`, `test/functional/test_runner.py`.

| Focused check | Result | Seconds |
|---|---|---|
| `C-storage-unit` | PASS | 156.938 |
| `C-storage-asan-ubsan` | PASS | 11.301 |
| `C-old-to-new-functional` | PASS | 135.479 |

**Adaptation and compatibility findings**

A follows the complete upstream select patch through Mercatura’s shared socket layer. Direct select socket tests and real classical/PQ loopback functional tests passed on Linux. The original select implementation failed 128 assertions in the same urgent-data regression.

B adapts the upstream clock type to Mercatura’s existing atomic chrono::seconds convention and preserves the twenty-minute timeout. Both send-pause writers and inactivity checks are covered. Tests use authenticated PQ and classical transports, legacy/current protocol versions, exact boundaries, recovery/reentry and clean teardown. The original-behavior control failed 392 assertions. PQ keeps a borrowed send span stable until CNode destruction; tests preserve that existing security rule.

C ports all nine upstream files and audits the additional Mercatura accounting, range rounding and record-size conversions. Windows uses _fseeki64/_ftelli64/_chsize_s; POSIX uses fseeko/ftello with representability checks. Allocation reserves its requested range and never an entire sparse prefix. Aggregate usage/pruning sums saturate, and fatal overflow prevents subsequent persistence. Existing serializers and four-byte record headers remain in place. Normal 128 MiB block targets, the existing capacity schedule and every consensus rule remain unchanged.

Old-width metadata bytes were compared for 256 representative fixed-seed values across file positions, file metadata and disk block indices; 512 total samples, explicit 32/64-bit boundaries, a fixed golden encoding and malformed VARINT input were tested. A real sparse undo position above 4 GiB survived index persistence, restart, readback, disconnect and reconnection. Its logical file size was 4,296,015,872 bytes with 2,101,248 allocated bytes.

The controlled upgrade test copied only stopped, generated blocks/chainstate from a source-pinned pre-storage-fix writer. It verified all old block hashes, chainwork, UTXO hash/count/amount, old undo reorg, new block/undo writes, both reindex modes, verification and restart. The fixture stayed byte-for-byte unchanged. Wallets and networking identities were excluded. Real historical replay/original-snapshot tests were not repeated; earlier source-pinned evidence is distinguished from this new result.

**Exact-source integration qualification**

Fresh out-of-source Release build (initial compilation, executable-mode-only correction and final SHA metadata refresh recorded separately): **PASS**; daemon, CLI, Qt wallet, unit executable, native Qt mining targets and PQ helpers built. Configuration/build logs record exact flags and source.

Registered CTest unit/Qt suite: **193 passed, 0 failed, 1 skipped; 194 total**. The existing script_assets_tests asset-data skip is retained. See final-unit-qt.xml and final-unit-qt.log.

Default MERCATURA_SCRIPTS suite: **56 passed, 0 failed, 0 skipped; 56 total**. The default selection is unchanged; the inherited extended suite was not run. See final-native-functional.csv and log.

Required-PQ public-testnet profile: **PASS build / PASS functional**. A separate headless CMake profile built daemon/CLI and tested default required policy, forbidden off, no classical fallback, real PQ/pinned links and clean shutdown. The enforced-profile public-testnet nodes stayed at genesis on loopback.

PQ networking and MercaHash/native mining regressions: **PASS** in the registered unit/Qt and native functional suites; no consensus or mining implementation changes.

Exact-source controlled database upgrade: **PASS**. Full-history/original-database compatibility remains **PARTIAL**, with the scope described above.

**Bounded adversarial and sanitizer evidence**

A exercised 128 pending-urgent-data event masks. B exercised 256 fixed-seed timing samples plus exact timeout boundaries and repeated pause recovery/reentry. C exercised 512 metadata samples, invalid/large ranges, sparse physical I/O and malformed serialization. Seven repository PQ smoke targets each passed 1,000 fixed-seed inputs. This is bounded deterministic smoke/property testing, not coverage-guided fuzzing.

ASan/UBSan actually passed 8 socket cases, 1 paused-send case (1,158 assertions), and 15 selected storage cases, with the relevant production/test translation units instrumented. The other linked libraries were uninstrumented. Full details and commands are in A-select-asan-ubsan.json, B-pause-asan-ubsan.json and C-storage-asan-ubsan.json. No whole-program sanitizer claim is made.

**Platform status**

| Platform | Exact-source status |
|---|---|
| Linux x86_64 | PASS — Native fresh Release daemon/CLI/Qt, registered unit/Qt suite, default native functional suite, direct select backend, controlled storage upgrade and enforced-PQ profile. |
| Windows x64 MSVC | NOT RUN — No native MSVC/Windows executor attached. Existing CI definition available; no new run triggered. |
| macOS ARM64 | NOT RUN — No native macOS executor attached. Existing native CI definition only. |
| macOS x86_64 | NOT RUN — No native macOS executor or usable cross SDK/toolchain available; existing cross-build workflow definition only. |
| Linux ARM64 | NOT RUN — No ARM executor/toolchain attached; existing native CI definition only. |

**CI and publication**

Publication is authorized and pending the documentation commit. No merge, tag, release, deployment or live-testnet access is authorized. CI results remain untested until an actual run is identified.

**Final audit and remaining concerns**

git diff --check passes. Protected consensus, cryptographic, PQ implementation, mining and wallet paths match the original candidate. No test or security check was removed/disabled, no production dependency was added, and no public activation height or network identity changed. Focused precommit logs record the then-current HEAD and staged tree separately. A focused trees match A. Earlier B focused/sanitizer trees differ only by the later test-only unused move-assignment override and stdexcept include; the final 1,158-assertion pause test matches committed B. C final focused/sanitizer trees differ only by the later executable-mode correction. The earlier C upgrade run precedes remaining unit-fixture corrections; the final exact-source upgrade passed independently. All final integration gates pin the final SHA. Per-check staged-tree deltas are included in the JSON. See final-source-audit.md, final-static-audit.json, C-width-audit.md and the reviewed patches.

- Native Windows/macOS and ARM qualification on this exact source remains open; Linux select execution does not establish native platform success.
- CI was not triggered or polled under the assignment.
- Sanitizers instrumented the selected real production/test translation units; other linked libraries were uninstrumented. Whole-program sanitizer and coverage-guided libFuzzer qualification were not run.
- Filesystems must support the required large files. FAT32 file-size limits and unsupported narrow off_t platforms are not expanded by this patch. Unrepresentable operations fail closed.
- Existing four-byte record-length headers remain unchanged; unrepresentable record sizes fail closed before narrowing/writing. This work expands absolute positions and accounting, not the record-header format.
- Controlled old-to-new database and metadata compatibility passed. Real historical public-testnet replay/original-database qualification on the patched source was not repeated; no legitimate real historical snapshot was present.

Earlier failed fixture/invocation attempts and their corrections are retained in attempt-ledger.md; they are not counted as passes. Expected original-code negative controls are explicitly identified.

**Handoff**

**Production code changed:** YES — authorized security fixes only.
**Consensus / network identity / public MercaHash V2 activation height changed:** NO.
**Live infrastructure / v0.1 modified:** NO.
**CI polled:** NO.
**All owned processes exited:** YES.
**Ready for founder review:** YES.
**Ready for release:** NO — remaining qualification gates apply.

**Evidence preservation:** Prior complete logs remain in `/workspace/work/security-backports/evidence`; their original manifest and report hashes were verified before updates. New sanitized assessment evidence is in `/workspace/work/security-final/evidence`. Reports are now preserved as documentation-only commits. The final commit/remote SHA and clean Git status are recorded in the post-commit publication receipt to avoid self-referencing a commit hash.

**Additional compatibility qualification**

The new explicit-boundary probe passed 60 old/new byte comparisons covering 20 values in all three changed persistent schemas, including zero, VARINT length boundaries and UINT32_MAX; 100 widened-field round trips and three old-reader overflow rejections passed. Six flat-file cases passed. A separate targeted generated-chain probe passed new-writer rollback to checksum-verified v0.1.2-testnet and to the pre-storage-fix binary, checking direct open without reindex, full hashes/chainwork/UTXO, verifychain level 4, old/new undo reorg, clean restart and patched reopen. No production source changed and the prior lengthy suites were not repeated.

Rollback is limited to values representable in the old 32-bit fields. Old software must not be used on a database that has crossed those bounds; old readers reject oversized VARINTs, and downgrade is not guaranteed. Normal database flush/LevelDB maintenance is not an application-format migration; no new migration/version marker or forced reindex was introduced. No original public-testnet fixture was available, so no original-history PASS is inferred from this generated chain. See the final qualification report for separate compatibility verdicts and remaining CI/native gates.
