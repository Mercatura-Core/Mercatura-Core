# MercaWeave v0.1 — WO05 wallet and PSBT integration

Date: 2026-11-02, America/Los_Angeles. Research only. No network activation.

## Architecture recorded before implementation

Starting clean research HEAD: `93b356af34bd1ea60fd7c58a99533f3d0e93bc31`.
Branch `research/mercaweave-v0.1`, `/workspace/Mercatura-MercaWeave-v0.1`.
Original `/workspace/Mercatura-Core` remains on `work` at
`4b88b619fb1b08c7f30112a38eb471880b2a6af4`; `project/v0.1` remains at that SHA.
The WO04 Release baseline was rebuilt and reproduced: 17/17 pass.

The architecture recorded in the first WO05 commit was a separate research
wallet facade using the existing protected 32-byte PQ ownership seed and native
seed-envelope helpers; native coin selection and PSBT storage/parser/merge;
complete public group reconstruction; and authoritative WO04 validation. No
CWallet database, GUI, RPC, node or existing wallet was to be instantiated.
Only a compile-time test guard in wallet.cpp was permitted, with normal source
erasure proving the deployed source path unchanged. This remains the implemented
boundary. The results below describe actual code and tests, not live usability.

## A. Repository

Tested code HEAD: `e15f461cce3e5f5afd2cdb27d343ac276657077a`. Implementation
commits since the starting HEAD:

```text
4b05aadc5888009b7fcbed294fff7a6ebc68a28c Reuse protected native PQ seed storage for isolated grouped wallet keys
55b7f1711f71548db21b453b4d4742b5a03917ab Add authoritative partial grouped PSBT signing and fail-closed combining
0d6df049f3627624cefaa46a388433d28903a55e Exercise wallet derivation version and missing issued-range rejection
a201f5c089f12e52a67b66c4aac72206a0647ef1 Rank native coin-selection candidates by complete grouped transaction costs
e58834c7d03447c8a37de493b16bb46040bc3df1 Measure twelve realistic wallet workflows against native v2 counterparts
9c1a6f6f4ed17f967519564a6acc0170e86076c6 Use native two-unit dust policy in the isolated current-policy check
2cdfb6e3bba56084039afeadb2b2b3c643885f15 Bound PSBT map allocation and exercise recovered signers and mutation counts
92d3ef2bb77c8504c2a6fc2f6901cc404db54496 Report per-transaction limits and align mixed-workload key ownership
6c7bc17adfe5119f3a14a6a3657fbfc9144d77b7 Link native PSBT signing RTTI and miniscript dependencies under UBSan
c92cdf21c61b4c8d14924ab94e5e030e1485932b Bound wallet cursor creation so every issued snapshot stays restorable
e15f461cce3e5f5afd2cdb27d343ac276657077a Reject symlink redirection before creating research snapshot files
```

The final containing commit adds the qualification runner, this report, README
instructions and new text evidence. It is the final WO05 HEAD; resolve it with
`git rev-parse HEAD` or the SHA in the final response. Its own SHA is not embedded
in its content, avoiding recursive commit hashing. Obtain the complete list with
`git log --format='%H %s' 93b356a..HEAD`.

The source checkpoint was clean except for the new qualification runner, whose
exact bytes are hashed in qualification.json. Both worktrees are clean after the
final local commit; the original HEAD/branch and project/v0.1 are unchanged.
No push, merge, reset, rebase or new branch occurred. All earlier design reports,
vectors and evidence are checked byte-for-byte against 93b356a: 37
files unchanged. No private wallet data, build products or binary artifacts are
committed. All ownership seeds in tests are explicitly public fixtures.

## B. Wallet

`research_wallet.{h,cpp}` implements encrypted creation from an existing PQ seed,
unlock/lock, protected-seed callbacks, native v2 and grouped v3 address issuance,
explicit rotation, recovery, explicit-public-record watch-only wallets and
confined snapshot save/load. The facade never obtains ownership from classical
CKey/CPubKey/BIP32/xpub metadata. Native generic PSBT code still contains its
existing classical data types; the research signer does not interpret those as
PQ ownership and rejects alternate input signing metadata.

Both schemes use the same authoritative 32-byte PQ seed. V2 calls the unchanged
native DeriveMercaturaPQChildSeed. V3 calls the locked WO01 Derive function:
PRK is HMAC-SHA3-384 keyed by ASCII `Mercatura/PQGroupWalletMaster/v1` over the
master seed. Expand32 uses HMAC over info followed by 0x01, first 32 bytes.
Group info is ASCII `Mercatura/PQGroupMLDSA65Key/v1`, 0x00, genesis32, account LE32,
purpose U8, group LE32. Nonce info uses ASCII `Mercatura/PQGroupAddrNonce/v1`
and appends address LE32. The existing commitment/Output function is unchanged.
All serialization, hash domains, manifest rules, pure ML-DSA-65, context strings
and consensus limits remain locked. No new protocol version or amendment exists.

Purposes 0/1/2 are receive/change/mining. Group keys separate network, account,
purpose and group; nonce addresses separate address indices. The wallet rotates
after 64 newly issued v3 addresses per group, with checked u32 exhaustion.
Repeated deposits/rewards to one address do not issue addresses or consume that
counter. Explicit rotation can advance early. These are wallet policies, not
consensus limits. Address text uses actual Bech32m/ConvertBits with existing HRPs
mca/tmca/tb/mcrt through a research-only API, with no Qt or chainparams changes.

Native CMasterKey/CCrypter and the unchanged wallet.cpp v1 seed encryption,
seed-check and IV helpers protect the ownership seed. A fresh native encryption
master key is an envelope key, not a second PQ ownership seed. The two guarded
wallet.cpp sections expose the existing helper bodies only in standalone builds.
No native encryption algorithm or ordinary wallet storage schema is changed.
Expanded ML-DSA keys are derived temporarily and cleansed by their destructors;
seed/derived-seed temporary buffers and unlocked storage are cleansed. Signing
holds the protected-seed callback lock throughout private-key use. Lock and
concurrent signing/issuance are serialized by the facade's recursive mutex.

Research snapshots contain a native encrypted envelope, public address records,
scheme/derivation version, network, account/purpose/type cursors, issued indices,
and a lookahead value of 64. Owned metadata has HMAC-SHA3-384 under the PQ seed;
restored owned records are unavailable until successful unlock authenticates the
metadata and rederives every record. Watch-only snapshots contain no encrypted
seed and use a public checksum for corruption detection, not secret ownership.
They track only explicit public records. No public child derivation is invented.

Backup/restore preserves exact unused issued records, early-rotation cursors and
next address. RecoverIssued rederives explicit known ranges from the existing
seed and rebuilds high-water indices. Wrong seed/location/network/version,
missing ranges, damaged MAC, truncation and watch checksum damage fail closed.
A snapshot supports at most 4,096 public records and 4,096 cursors; creation and
restoration enforce the same bounds. Testing found and fixed empty rotations
exceeding the restore cursor limit, before qualification.

The stored lookahead value is metadata; no chain rescan, automatic unused-range
discovery or background CWallet lookahead engine is claimed. Seed-only recovery
needs the issued ranges; seed plus only observed payments cannot reconstruct
unobserved issuance or an empty advanced cursor. A native CWallet database
migration/import and real rescan remain future work.

Storage is restricted to ignored work/wo05-wallets, with an owner-only directory,
0600 encrypted files and atomic rename. The configured root must resolve to its
literal directory; root/child symlink escapes are rejected before opening files.
The root-redirection regression exercises only temporary research directories.
This is not WalletBatch/Berkeley/SQLite integration, journaling or fsync-backed
crash durability. Cross-process locking, hostile concurrent filesystem changes,
backup rollback protection and real database recovery are unqualified.

## C. Transactions

`wallet_funding.{h,cpp}` uses actual wallet::COutput, OutputGroup,
CoinEligibilityFilter, SelectCoinsBnB, CoinGrinder and KnapsackSolver. It tries
disjoint individual/shared-key chunk partitions, deduplicates candidate outpoints,
then ranks actual complete candidate fees, bytes and weight. It does not equate
fewest UTXOs with cheapest authorization or always spend the entire group. The
regression chooses a cheaper useful grouped subset over fewer native inputs.
This bounded heuristic is not a proof of globally optimal selection.

Automatic admits eligible v2 and v3 inputs; StandardPQ admits only v2;
MercaWeave admits only v3. Ownership follows the actual spent script, never a
chosen mode label. Ineligible modes fail without falling back to another mode.
Values/scripts come from the authoritative synthetic view; ownership records
come from verified wallet metadata. Duplicate, foreign, missing, unsafe,
unconfirmed and immature coinbase inputs fail or are filtered. Recorded depth
is bounded by synthetic coin height. Unconfirmed ancestors are excluded rather
than estimating packages from caller-supplied mempool metadata.

Inputs are ordered by outpoint; v3 groups use exact public key, lowest input
anchor and absolute LE32 follower references. Only native v2/v3 spent outputs
are supported. Mixed transactions preserve native PQAuth digests, public keys,
witnesses and signatures. A native input cannot silently become grouped
ownership. Change must be an explicitly issued owned change address. When any
v2 input is selected, change is restricted to v2 in this research facade.
Grouped-only funding can use explicit grouped change or a native destination.

Fixed witness sizes are v2 5,268, anchor 5,301 and follower 39 bytes, plus actual
41-byte unsigned input bodies, CompactSize counts, marker/flag, outputs and
change. Estimated signed transactions are serialized using native transaction
classes. All workload estimates equal completed bytes, weight and actual fee.

1 MCA is 100 units. Fees use integer CFeeRate rounding on full serialized bytes,
including witness: max(1, explicit minimum, ceil(per-k * full bytes / 1,000)).
Higher user rates/minima and explicit maxima are honored. Dust uses native
GetDustThreshold with DUST_RELAY_TX_FEE, with a two-unit floor. The WO04 research
CurrentPolicy helper had a hardcoded 1,000-unit dust rate; it now uses the actual
native constant. A two-unit native v2 output passes that structural policy check.
This corrects a fixture diagnostic; production policy is unchanged.

Every assembled candidate checks real IsStandardTx, 400,000-WU standard weight,
existing signature-operation policy, native transaction/input/witness-resource
checks and WO03 checked block resource accounting. Member/group/input limits
remain 1,024/256/4,096. Sweeps partition at actual standard limits; a protocol
maximum is never presented as a relayable transaction. The planner additionally
caps all candidate inputs at 4,096, including native inputs.

Full CurrentPolicy structural checks continue to reject grouped inputs with
inputs-not-standard. A Funded.current_policy success is a structural preflight,
not mempool admission or confirmation. No ancestor/descendant, RBF, package,
fee-estimator or relay code is changed. Those live paths are not exercised;
this facade excludes unconfirmed inputs and does not offer replacement/package
funding. All v3 workflow acceptance is through isolated consensus-fixture mode.

Multi-output payments preserve recipient order and values, independently of
signature grouping. G/H/I pay 100 distinct native PQ recipients. K preserves
20 distinct payments and one explicit change output. Output batching benefits
both schemes and is not attributed exclusively to MercaWeave.

## D. PSBT

`wallet_psbt.{h,cpp}` uses the real PartiallySignedTransaction/PSBTInput containers,
production parser/serialization and Merge, unchanged. Proprietary input keys are
CompactSize(0xfc), CompactSize(10), ASCII MercaWeave, CompactSize(subtype), with
empty keydata. Subtypes 1/2/3/4 hold nonce32/follower LE32/anchor public-key1952/
anchor signature3309. There is no supplied global manifest. V2 retains the
existing Mercatura proprietary namespace and public-key/signature helpers.

Create records all public metadata; Fund creates such PSBTs; Inspect preflights
the entire transaction through Engine::AcquireInputs, which performs the same
native coin/transaction checks as Prepare but returns no authorization or jobs.
All witness_utxo values/scripts must match authoritative coins. Optional complete
non-witness transactions must match hash/index/output too. Inspect reconstructs
the canonical manifest and ownership commitments, then verifies every existing
signature under the complete body and actual spent outputs before signing.

Sign can add just owned groups and owned native inputs without another
participant's private key. It stages the entire result; entropy failure, lock,
wrong ownership or view mutation publishes nothing. Completed foreign groups
are verified, not trusted. FinalizeAvailable writes every complete group's
anchor/follower witnesses without requiring keys, leaving others partial.
Repeated signing/finalization is idempotent for existing valid records.
Combine verifies both inputs, requires the same unsigned body, rejects conflicting
values and only then calls native Merge. Distinct valid randomized signatures
for one field are an explicit fail-closed local combine policy; they do not prove
different transaction bodies and do not amend the protocol. Exact duplicates
merge idempotently. Production signing defaults to native strong randomness;
zero randomness is explicitly named and limited to public test fixtures.

Extract requires every input complete, materializes the transaction and runs
actual Engine::Validate/real Script dispatch against the current authoritative
view. Caller booleans, public metadata and script-finalization fields cannot
mint a WO04 proof. Final fields must match verified proprietary records.

Offline transport is real encoded/decoded PSBT, with no network. The signer
still needs an authenticated prevout snapshot; this research implementation uses
the owned synthetic view. It does not prove SPV/chain membership from PSBT data.
No real offline device or air-gap workflow is claimed. The normal global-Params
PSBT finalizer is not used for experimental groups, since that path has no
activated MercaWeave authorization. The research adapter implements its own
strict orchestration over native containers and actual validation.

Import preflights counts and lengths before native parsing: 16 MiB overall,
4 MiB per value, 4,096 bytes per key, 256 fields per map, 32,768 fields total.
It rejects duplicate keys, noncanonical CompactSize, trailing maps, unsupported
MercaWeave locations/subtypes/keydata, wrong lengths/roles, unsupported sighashes,
classical/alternate PQ ownership metadata and unsafe unknown-key aliases.
Raw-key identifier/subtype caches must agree. Native v2 fields retain their
existing length rules. Safe unrelated proprietary/unknown fields are preserved
without authorization meaning. Canonical reserialization order is an additional
research-local import restriction; some otherwise legal noncanonical PSBT map
orders are deliberately not accepted. All these restrictions remain outside
normal PSBT parser behavior.

## E. Security and adversarial coverage

The new suites report 247 wallet assertions/27
rejections and 67 PSBT assertions/41 explicit rejections.
All 1000 completed-PSBT byte mutations were rejected. Mutation sampling is
not coverage-guided fuzzing or proof against arbitrary hostile input.

Tests exercise protected native seed envelopes, purpose/account/network key
separation, nonce/address determinism, the 64-address boundary, index overflow,
encrypted restart, exact next-address recovery, metadata corruption/version and
missing ranges, seed/location mismatch, watch-only restrictions, 16 concurrent
issuances, maximum cursor-table restore and symlink-root rejection. Restored
signers rediscover their explicit issued coins and sign correctly.

PSBT tests include interleaved groups, followers without foreign keys, native v2
mixing, parsed external partials, changed version/inputs/order/sequence/outputs/
output order/amount/fee/locktime, wrong network/spent script/nonce/key/role,
wrong signing context, tampered signature/final witness, duplicate raw fields,
unsupported subtype/keydata/location/sighash, unsafe unknown aliases, excess
inputs/fields, truncated/trailing bytes, conflicting valid randomized signatures,
failed entropy after an earlier staged signature, locked/watch-only signing,
four concurrent deterministic signers, repeated finalization and stale view
changes/spent coins. Prior suites retain excessive group/member/input limits,
missing/duplicate/future/cyclic anchors, hostile witnesses and context/cache
binding regressions. WO04 again reports 2,153 assertions, 2,000 mutations and
2,800 concurrent script reads.

Only complete, verified signatures enter witnesses. The acquired snapshot is
diagnostic data, not a proof. View epoch checks surround expensive work and
publication; final extraction reacquires and validates current coins. The
facade is not a live chainstate trust source. Protocol-wide scriptSig/txid
malleability concerns documented in WO04 remain; this wallet funds only native
v2/v3 inputs with empty scriptSig and does not change the locked digest.

## F. Qualification and measurements

Exact commands, from the research worktree (CMake 4.4.4 already under work/tools):

```bash
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-release -DCMAKE_BUILD_TYPE=Release
work/tools/cmake/data/bin/cmake --build work/build-release -j2
work/tools/cmake/data/bin/ctest --test-dir work/build-release --output-on-failure
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-debug -DCMAKE_BUILD_TYPE=Debug
work/tools/cmake/data/bin/cmake --build work/build-debug -j2
work/tools/cmake/data/bin/ctest --test-dir work/build-debug --output-on-failure
work/tools/cmake/data/bin/cmake -S src/test/mercaweave -B work/build-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMERCAWEAVE_SANITIZERS=ON
work/tools/cmake/data/bin/cmake --build work/build-sanitize -j2
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 work/tools/cmake/data/bin/ctest --test-dir work/build-sanitize --output-on-failure
python3 src/test/mercaweave/qualify_wallet.py --release work/build-release --debug work/build-debug --sanitize work/build-sanitize
git diff --check
```

Final CTest: Release 22/22, Debug 22/22, ASan+UBSan 22/22: 66 passed, zero failed,
zero CTest skips. Assertions remain enabled. All WO01–04 targets are retained.
ASan leak detection is disabled because the already-recorded ptrace probe cannot
run LeakSanitizer; leak detection is not called passed. Clang/alternate compiler,
libFuzzer, AFL/honggfuzz and Valgrind were unavailable; TSan, cross-platform tests
and an independent audit were not run. No toolchain installation was needed.

All new research code and native wallet/PSBT objects retain full ASan/UBSan
checks. Sanitizer linking initially exposed native MutableTransactionSignatureCreator
RTTI and miniscript static-initializer dependencies. Linking their actual,
unmodified sign.cpp/miniscript.cpp resolved it; no fake RTTI or new sanitizer
exclusion was added. WO03's pre-existing vptr-only exception remains restricted
to unmodified policy.cpp in the older malleability executable; isolated policy
and every new research target remain fully instrumented. GCC 14 emits existing
optimized std::vector comparison -Wstringop-overread diagnostics in native PSBT
templates; no new suppression was added and no runtime sanitizer error remains.

Evidence is under `src/test/mercaweave/wallet-artifacts/`: complete CTest logs,
wallet/PSBT/integration results, normal/isolated controls, compile/source/symbol
isolation JSON per build, three 24-row workload CSVs and qualification.json with
commands, build flags, environment, source/binary-output hashes and preserved
prior hashes. The runner verifies 48 cross-build workload comparisons, equal
sizes/fees/signature counts/public fixture hashes and identical deterministic
vector/test/control stdout. Original vectors retain SHA-256
`ea4978206307ac21d4f544b05e6da3543e094c9a1c4bd97c44f90fad4420c49c`.
Fresh randomized signatures and encrypted ciphertexts intentionally differ;
those bytes are not compared or retained. Benchmark fixture signatures use
explicit zero entropy, not the production default.

The following Release aggregates use base units for fees. Each row's transaction
count/outputs reflect real standard-limit splitting; sum weight may exceed the
per-transaction limit. Every individual transaction is at most 400,000 WU. Native
comparisons use the same input counts, value distribution and number of distinct
owner keys. F has three keys in each scheme; the grouped case has ten v3 and ten
v2 inputs. C/J remain 100/200 independent keys and require 100/200 signatures.

G means grouped/mixed, V2 the native comparison; key counts match in each pair.

| Case | Inputs | Outputs G/V2 | Keys | Tx G/V2 | Groups G | Signatures G/V2 | Bytes G/V2 | Max WU G/V2 | Fee G/V2 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A_mining_rewards_reused_address | 100 | 1 / 2 | 1 | 1 / 2 | 1 | 1 / 100 | 13,317 / 531,010 | 25,776 / 396,750 | 14 / 532 |
| B_deposits_reused_address | 100 | 1 / 2 | 1 | 1 / 2 | 1 | 1 / 100 | 13,317 / 531,010 | 25,776 / 396,750 | 14 / 532 |
| C_independent_deposit_keys | 100 | 2 / 2 | 100 | 2 / 2 | 100 | 100 / 100 | 534,310 / 531,010 | 399,159 / 396,750 | 536 / 532 |
| D_ten_group_keys | 100 | 1 / 2 | 10 | 1 / 2 | 10 | 10 / 100 | 60,675 / 531,010 | 73,134 / 396,750 | 61 / 532 |
| E_two_group_keys | 100 | 1 / 2 | 2 | 1 / 2 | 2 | 2 / 100 | 18,579 / 531,010 | 31,038 / 396,750 | 19 / 532 |
| F_mixed_funding | 20 | 2 / 2 | 3 | 1 / 1 | 2 | 12 / 20 | 64,512 / 106,278 | 67,260 / 109,026 | 65 / 107 |
| G_one_input_100_recipients | 1 | 100 / 100 | 1 | 1 / 1 | 1 | 1 / 1 | 9,654 / 9,621 | 22,707 / 22,674 | 10 / 10 |
| H_ten_inputs_100_recipients | 10 | 100 / 100 | 1 | 1 / 1 | 1 | 1 / 10 | 10,374 / 57,402 | 24,534 / 71,562 | 11 / 58 |
| I_fifty_grouped_100_recipients | 50 | 100 / 100 | 1 | 1 / 1 | 1 | 1 / 50 | 13,574 / 269,762 | 32,654 / 288,842 | 14 / 270 |
| J_independent_exchange_sweep | 200 | 3 / 3 | 200 | 3 / 3 | 200 | 200 / 200 | 1,068,565 / 1,061,965 | 399,159 / 396,750 | 1,071 / 1,063 |
| K_withdrawals_with_change | 20 | 21 / 21 | 2 | 1 / 1 | 2 | 2 / 20 | 13,039 / 107,095 | 18,238 / 112,294 | 14 / 108 |
| L_grouped_mining_to_native_savings | 100 | 1 / 2 | 1 | 1 / 2 | 1 | 1 / 100 | 13,317 / 531,010 | 25,776 / 396,750 | 14 / 532 |

All actual fees equal the estimates above; every completed transaction verified
through WO04. G/H/I have 100 distinct recipients in both schemes. K has 20
withdrawals plus explicit native change. A/B/L reuse one public record, without
issuing 100 addresses. Mining cases install mature synthetic coinbase coins;
they do not run a miner, create blocks or alter reward/mining code. L's savings
destination is a distinct native v2 key, with no Secure Vault withdrawal,
recovery, delay or freeze features.

A shared-key 100-input sweep costs 13,317 bytes/one signature/14 units versus
531,010 bytes/100 signatures/532 units across two native transactions. A
100-independent-key sweep instead costs 534,310 bytes/100 signatures/536 units
versus 531,010/100/532 native; grouping cannot compress independent keys. A
one-input/100-output payment is 33 bytes larger in v3 and has the same ten-unit
fee. Ordinary output batching is common to both schemes.

CSV timing fields are one wall sample for each complete workflow on this single
Debian/GCC/x86_64 EPYC cloud host, not robust microbenchmarks or deployment
capacity figures. Construction includes repeated exact sweep preflight, with an
O(n²) current assembly path; signing/finalization includes repeated complete
inspection; extraction/verification deliberately validates twice. Native prepare
and dispatch counters describe the last explicit WO04 Validate, not every
verification in the timed APIs. Group count and native-input count determine
generated authorizations, not total verification effort. Key/address generation
and encryption setup occur before timed construction. No new live-memory,
low-end-device or production-wallet performance claim is made.

## G. Production isolation

The only WO05 production-source edit is the wallet.cpp compile-time bridge.
Removing its two test-only branches yields exactly the pre-WO05 file. Existing
WO04 interpreter hooks are unchanged and erase exactly to their normal baseline.
The standalone CMake project alone defines the two research symbols. Compile
commands, negative header compilation, source erasure and nm checks pass in all
three builds; no normal control exposes research wallet symbols or group hooks,
no normal build includes the standalone targets, checker ABIs remain separate,
and no node signature/transaction-cache symbol is introduced.

Normal wallet receiving/signing code therefore has no experimental grouped API.
The broader claim that normal nodes cannot spend raw unactivated v3 outputs
would be false: existing mandatory consensus treats unknown witness versions
permissively. The normal control still accepts contextless unknown v3 under
mandatory flags; the isolated control rejects it without a bound group proof.
Normal software can also construct generic raw scripts/unknown-version address
text. That does not generate or enforce protected MercaWeave custody. Current
unknown-witness input policy discourages relay but is not consensus ownership
protection. Never fund unactivated v3 outputs as protected custody on any network.

No activation height/schedule, network setting, chainparams, runtime flag, Qt,
RPC, normal CMake target, mining, ordinary PSBT parser or wallet schema changes
exist. CWallet/WalletBatch lifecycle, normal PSBT finalizer dispatch, wallet
transaction tracking, live policy/fee estimation and node chainstate orchestration
remain unimplemented integration boundaries. The fixture is not a deployed
wallet integration hidden behind a runtime switch.

## H. Remaining risks and required gates

- Real CScriptCheck/CheckInputScripts queues, all cache fast paths, cs_main,
  authenticated persistent chainstate and proof lifecycle under concurrency.
- Real ConnectBlock/DisconnectBlock, reorgs, mempool reconciliation, finality,
  sequence locks, block headers/merkle/PoW and package/RBF/ancestor handling.
- Actual CWallet protected-seed import, database migrations, crash durability,
  concurrent storage, rollback handling, rescan/lookahead and recovery across
  real issued-but-unused ranges. The metadata format is research-local.
- Native PSBT finalizer/coordinator interoperability, broader safe unknown
  fields/map ordering, offline prevout authentication and hardware signer tests.
- Selection optimization/failure diagnostics, privacy/linkability and economic
  policy for reused/grouped addresses; bounded algorithms are not global solvers.
- Independent cryptographic/security audit, coverage-guided fuzzing, supported
  leak tests, TSan, alternative compilers/architectures and low-end performance.
- WO04 transaction identity/scriptSig limitations remain outside the native-only
  wallet subset. No consensus or relay workaround was introduced.

This agent performed code self-review and adversarial tests; no independent
audit is claimed. WO06 work must retain compile-time isolation until these gates
are separately qualified. Qt and native-mining integration are not implemented
here and no Secure Vault is implemented.

## I. Final verdict

**READY WITH EXPLICIT RESTRICTIONS**

The research facade demonstrates protected-seed derivation/recovery, exact
cost-aware funding, mixed native/grouped signing, real PSBT transport/partial
signing/combine/finalization and complete isolated validation, with three green
builds and unchanged earlier evidence. It is suitable for further explicitly
isolated WO06 research. It is not ready for production custody, ordinary-wallet
deployment, network activation or claims of full-node consensus readiness.
