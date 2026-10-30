# Mercatura final security backport qualification

**Overall: PARTIAL. Founder fast-forward and packaging are not technically ready.**

Integrated base: `61f1f4b9e24d07ea56fcfc1952aaa73aac498394`. Security-patched code: `0a66162c465898b132e31d7574b0b8fb89ff09cc`.

Dedicated branch: `security/bitcoin-core-2026-10-backports`. [GitHub branch](https://github.com/Mercatura-Core/Mercatura-Core/tree/security/bitcoin-core-2026-10-backports).

Official `v0.1`: `631886dd5159614b628ced3fe4fa1e289ce19451` (read-only observation; no modification).

Evidence documentation commit: recorded after this initial evidence snapshot is committed.

Publication and CI have not yet occurred at this documentation snapshot.

The final receipt is a documentation-only successor when publication details are added. Its literal SHA is recorded in the external publication receipt and final response; it cannot be embedded in its own content. Production and test blobs must remain identical to the reviewed code SHA.

## Security and local qualification

| Issue | Security commit | Result |
|---|---|---|
| CVE-2026-117350 | `77a4de7a10730544f728fd809f710a9a8fec5e46` | PASS — focused evidence preserved |
| CVE-2026-117349 | `2f25a22f4fd6522b451f7cdad599b4cb2e79f9a3` | PASS — focused evidence preserved |
| CVE-2026-117346 | `0a66162c465898b132e31d7574b0b8fb89ff09cc` | PASS — focused evidence preserved |

Prior SHA256 manifests, source/tree metadata, result JSON, JUnit XML and functional CSV were authenticated. Registered unit/Qt: **193 passed / 0 failed / 1 existing asset skip**. Default native functional: **56 passed / 0 failed / 0 skipped**. Clean Release build, PQ networking/enforcing profile and MercaHash V1/V2/native Qt mining: **PASS**. Only documentation changed; those lengthy suites were not rerun. The code-review inventory identifies all 13 production and 11 test/build-registration files, replaced conditionals, integer widths, concurrency and file-position APIs. No verified defect, removed security check, unrelated modification or new dependency was found.

Partial ASan/UBSan: 8 socket, 1 pause and 15 storage cases passed; only selected translation units were instrumented. Seven PQ smoke targets passed 1,000 fixed-seed inputs each. Whole-program sanitizer and coverage-guided fuzz qualification remain unavailable; no new defect justified repeating unrelated cryptographic campaigns.

## Separate compatibility verdicts

| Category | Status | Evidence |
|---|---|---|
| Existing on-disk serialization | PASS | 60 explicit byte-identical old/new comparisons across three persistent schemas, 100 field round trips, three old-width overflow rejections; prior 512 samples and golden encoding retained. |
| Old-database direct open | PARTIAL | Generated pre-storage-fix database opens without reindex on patched executable; original height-12332 public-testnet snapshot is unavailable. |
| Reindex behavior | PASS | Authenticated final-upgrade plus default reindex/storage-recovery functional tests at exact security-code SHA. |
| Chainstate recovery | PASS | Authenticated final-upgrade chainstate reindex and full native recovery tests. |
| Rollback within old range | PASS | Checksum-pinned v0.1.2-testnet release and pre-C binary directly opened new-written low-range metadata, verified all blocks, old/new undo reorg, restart; patched reopen passed. |
| Large-file behavior | PASS | Linux sparse seek/allocation/truncation and persistent undo above 4 GiB; native Windows/macOS remains pending. |
| Original historical snapshot on patched code | NOT TESTED | No authorized original height-12332 fixture present. Requires a disposable copy, matching expected hash/chainwork/UTXO, verifychain(4,0) and restart; no live explorer access. |

The rollback probe uses generated regtest history with a native PQ spend, not a real public-testnet database. The published old release archive hash is `858f087d41defba5d2db718fc2c1bc78e5d865ac98df41ba59408f87b2f118f0`. The original generated fixture was unchanged. Old readers are not qualified beyond UINT32_MAX; no unconditional rollback guarantee applies. In-memory struct widening does not alter disk VARINT field order. Regular database flush/compaction is expected; no application migration marker, format rewrite or forced reindex was introduced.

## Native CI and exact source

CI state: **NOT TRIGGERED**. Dispatch: **NOT REQUESTED**. Initial job coverage: NOT OBSERVED.


The standard `ci.yml` push workflow has Linux/cross-build, native macOS ARM64 and Windows MSVC full-unit/functional jobs plus lint. Branch-push jobs depend on `vars.SKIP_BRANCH_PUSH != true`; actual initial scheduling must be checked, not inferred. These native jobs are headless. The GUI-capable `pq-integration.yml` matrix covers Linux x86_64/ARM64, macOS ARM64 and Windows MSVC, Qt/PQ policy/transport/identity, controlled networking, enforced profile and local package smoke. Its filtered CTest command excludes the new net/flatfile suites, so it complements the standard full-unit workflow rather than substituting for it. Neither workflow publishes releases.

A final documentation-only receipt must not cause duplicate lengthy CI: its commit may use GitHub’s documented `[skip ci]` marker. The recorded CI SHA then differs only in documentation; production/test/workflow blobs are compared explicitly, and commit-level status on the receipt is not claimed as passed. Native checks are PENDING/NOT TESTED until completed evidence exists. No repeated Actions queries, watch command, waiting loop or redispatch is used.

### Platform coverage at this handoff

| Platform | Compile | Unit/security | Functional | Qt | Enforcing profile | Package smoke |
|---|---|---|---|---|---|---|
| Linux x86_64 local | PASS (prior exact code) | PASS (prior exact code) | PASS (prior exact code) | PASS (prior exact code) | PASS (prior exact code) | NOT TESTED locally |
| Windows x64 MSVC CI | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED |
| macOS ARM64 CI | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED |
| macOS x86_64 cross CI | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED |
| Linux ARM64 CI | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED | PENDING / NOT TESTED |

Linux direct select execution establishes that the affected backend actually ran; it does not establish native Windows/macOS success. Cross-build coverage is compilation only unless runtime tests explicitly execute. Read exact scheduled job and step coverage from the initial snapshot; queued/running jobs are not passes.

## Independent gates

| Gate | Status |
|---|---|
| A — Security patches | PASS |
| B — Local regression | PASS |
| C — Storage compatibility | PARTIAL |
| D — Native Windows | NOT TESTED |
| E — Native macOS | NOT TESTED |
| F — PQ enforcing profile | PASS |
| G — Git provenance | PENDING |
| H — CI | NOT TESTED |
| I — Founder approval | PENDING |

## Founder prerequisites and safety

Before any fast-forward, obtain completed native Windows/macOS and applicable ARM results for the reviewed production/test blobs, full security regression coverage (including the standard unit workflow), and the enforcing profile. Resolve the original-database gap with a disposable authorized snapshot if original-history qualification is required. Recheck that official v0.1 is unchanged and an ancestor; a divergent remote branch requires stopping. Founder approval is a separate manual decision and is not presumed. Packaging is not authorized by this incomplete technical evidence. No merge commands are enabled while these gates remain open; see the separate founder handoff file.

Production consensus, genesis/network identity, DGW/emission, hashing, MercaHash V1/V2, PQ handshake/derivation/authentication, native mining rewards and capacity heights are unchanged. No live endpoint, operator datadir, release, tag or deployment was accessed/modified. All local test processes exited.

## Read-only next-decision appendix

Public-testnet MercaHash V2 activation remains pending (`nMercaHashV2Height` is unset for public testnet); genesis and V1 history are unchanged. Fixed 4 MiB capacity activation remains at height 8,000. Existing V2 boundary tests passed. Required-PQ transport policy is separate from PoW activation. No height is selected here, and no current chain height is inferred from the historical snapshot.

After security qualification and founder incorporation, collect: actual observed public-testnet height with timestamp/source; recent block rate; mining participation/variability; package schedule; explorer/bootstrap upgrade readiness; founder Qt wallet readiness; other miner readiness; desired lead time; activation-boundary test and rollback plan. The intended sequence remains security-hardened release, coordinated PQ networking upgrade, stable PQ-connected network, then a separately chosen future MercaHash V2 block-height activation. No live observation was made.

## Evidence and report locations

- Prior evidence: `/workspace/work/security-backports/evidence`.
- New evidence and publication/CI receipts: `/workspace/work/security-final/evidence`.
- `doc/security/MERCATURA-2026-10-UPSTREAM-SECURITY-BACKPORTS.md`
- `doc/security/mercatura-2026-10-upstream-security-backports.json`
- `doc/security/MERCATURA-2026-10-SECURITY-BACKPORT-FINAL-QUALIFICATION.md`
- `doc/security/MERCATURA-2026-10-FOUNDER-FAST-FORWARD-HANDOFF.md`

Only sanitized reports are committed. Complete raw build/test logs, generated databases, wallets/identity material, downloaded binaries and archives are excluded. Final clean Git status, exact branch SHA and receipt checksums are captured after committing/pushing the documentation.
