# Native Send spending modes

## Source audit

The targeted work starts on `integration/mercaweave-v0.1-upgrade` at
`a9867dbafaba683476cf9aea56c02b5c6b449de1`. Five uncommitted files from the
ongoing native Qt fee-display regression are retained: `interfaces/wallet.h`,
`wallet/interfaces.cpp`, `qt/coincontroldialog.cpp`, and the Qt wallet test header
and implementation (all under `src/`). No push is authorized for this task.

The production Send dialog owns `CCoinControl` and opens the existing Inputs
dialog. Its advanced frame follows the existing options setting. The research
panel's three choices are disconnected from production sending. Native wallet
funding already handles mixed inputs and grouped signing, but no input-type
selection policy exists. Automatic discovery enters `AvailableCoins`; manually
selected inputs enter `FetchSelectedInputs`. Both must enforce the same policy.

The existing fee-label code treats v3 as an unsupported future witness. The
retained regression exposed that crash. Initial byte-count and signing assertions
passed, while immediate transaction-table observation raced Qt event delivery;
the regression now waits for that asynchronous update. Fee preview preserves
manual choices when an estimate is unavailable.

Implementation reuses the Send form, Inputs dialog, tree/list model, wallet
funding and existing grouped estimator. Consensus, cryptography, public network
parameters, mining and research capabilities are outside this change.

## Using Send

The compact **Spending mode** selector is visible above the normal Send form.
It chooses the authorization type of the coins paying for the transaction.
The recipient address remains your separate choice.

| Mode | Eligible funding inputs |
|---|---|
| Automatic (Recommended), the default | Existing native selection; eligible v2 and v3 inputs may be mixed after activation. |
| Standard PQ — Witness v2 only | Independent-key native witness-v2 coins. Grouped coins are excluded. |
| MercaWeave — Witness v3 only | Authenticated wallet-owned grouped witness-v3 coins on an activated network. Standard v2 coins are excluded. |

Public-network MercaWeave activation remains **undecided and inactive**. The
grouped choice is visibly unavailable until the actual native network and wallet
permit it. Testing uses only the existing explicit isolated-regtest mechanism.
No setting in this selector activates a network or issues a grouped address.

Use **Inputs…** beside the selector to open the familiar Coin Control dialog,
including when advanced statistics/change controls are hidden. The **Input type**
column identifies each coin from its actual script and authenticated wallet
ownership. Its filter shows all supported inputs, v2, or v3. List/tree views,
confirmations, amounts, locking and individual selection remain available.

The view filter does not change the spending policy. Changing modes or filters
keeps selected inputs. Conflicting selections remain visible for explicit
deselection; funding rejects them with a clear error. Hiding advanced controls
also preserves the policy and explicit choices.

A wallet holding v2 funds can report insufficient eligible funds in v3-only
mode: those v2 coins are deliberately excluded. Choose Automatic or Standard PQ
if you intend to spend them. Grouped signing still uses the wallet's normal
unlock prompt and requires authenticated ownership. Wallet switching/unloading
and activation-boundary changes invalidate prepared Send transactions.

A v2 input paying a v3 recipient remains v2-authorized. A v3 input paying a v2
recipient remains grouped-authorized. Default change remains native v2, unless
you explicitly request another supported change address through existing controls.
Selecting a mode alone neither migrates funds nor converts existing balances.

## Fee previews and confirmation

Coin Control uses the native transaction-wide grouped estimator, accounting for
one anchor signature and its followers rather than one signature per grouped
input. All accounting uses full serialized bytes, without Bitcoin's witness
discount. Preview labels retain their approximation marker because recipients
and change may still change during funding. An unavailable aggregate preview is
shown as unavailable and leaves selected inputs intact. The normal Send
confirmation shows the actual funded transaction's authoritative fee.

## Funding interface

`CCoinControl::m_spending_mode` is the typed input policy. `AvailableCoins`
filters automatically discovered inputs; `FetchSelectedInputs` rejects conflicting
manual inputs, including inputs supplied through the funding interface. Explicit grouped-mode
manual inputs require native activation and authenticated wallet ownership.
Automatic retains the existing external-input coordinator behavior.
Alternative selection attempts retain the same policy. Recipient and change
selection do not derive from this field.

The shared RPC funding options accept optional `spending_mode`:
`"automatic"`, `"v2"`, or `"v3"`. This applies to `fundrawtransaction`,
`walletcreatefundedpsbt`, and `send`. Omission preserves existing RPC defaults.
An invalid string fails validation. Existing signing, PSBT, fee settings,
subtract-fee, RBF, confirmation and broadcast paths are reused.

The UI preview applies the existing grouped coordinator's 4096-input/output
budget before allocating dummy witnesses. This is a local preview bound, not
a consensus rule. No cryptographic format, signature domain, public activation
parameter or research capability changes are part of this task.

## Targeted local checkpoint

Started on `integration/mercaweave-v0.1-upgrade` at
`a9867dbafaba683476cf9aea56c02b5c6b449de1`, retaining the five prior Qt fee/interface/test
working files described above. All new work remains on this integration branch.
The final local commit SHA is reported with delivery; no push is authorized.

### Exact changed paths

- `doc/design/mercaweave-send-spending-modes.md`
- `src/interfaces/wallet.h`
- `src/qt/coincontroldialog.cpp`
- `src/qt/coincontroldialog.h`
- `src/qt/forms/coincontroldialog.ui`
- `src/qt/forms/sendcoinsdialog.ui`
- `src/qt/sendcoinsdialog.cpp`
- `src/qt/sendcoinsdialog.h`
- `src/qt/test/wallettests.cpp`
- `src/qt/test/wallettests.h`
- `src/qt/walletmodel.cpp`
- `src/qt/walletmodel.h`
- `src/wallet/coincontrol.h`
- `src/wallet/interfaces.cpp`
- `src/wallet/rpc/spend.cpp`
- `src/wallet/spend.cpp`
- `src/wallet/test/spend_tests.cpp`
- `test/functional/mercatura_spending_modes.py`
- `test/functional/test_runner.py`

### Implementation and coverage

The original Send form, `CCoinControl`, Inputs button, `CoinControlDialog`,
checkbox selection, list/tree views, amounts, confirmations, coin locking, fee
controls and change controls are reused. The button moves above the advanced
frame so manual input selection remains discoverable with the mode selector.
No independent Send backend or research-panel integration is introduced.

Explicit policies filter `AvailableCoins` and reject conflicting inputs in
`FetchSelectedInputs`. Explicit v3 also requires native availability and
authenticated wallet-owned records. Selection retries retain the policy.
Automatic leaves existing coordinator behavior and RPC defaults intact.

The native wallet's aggregate estimator replaces per-input v3 assumptions in
Coin Control. Preview still assumes native PQ recipient/change outputs and is
marked approximate; unavailable preview preserves selection. The confirmation
fee comes from the actual funded transaction.

The focused additions exercise all three mode labels/defaults; actual v2/v3
funding restrictions; automatic and manual mixed signing; clear conflicts and
insufficient eligible funds; independent recipient/change types; native Coin
Control type display/filter/selection; aggregate fee bytes equal to the actual
three-input signed transaction with one anchor and one follower; confirmation,
broadcast and block inclusion; real encrypted-wallet unlock; unsupported-wallet
switching; wallet destruction during and after unlock; safe recipient editing
after unload; and real activation/reorg availability changes. Existing native
Qt v2 Send and watch-only PSBT tests run in the same Qt executable. The real
regtest test funds, signs, mempool-checks, broadcasts and confirms a grouped
transaction. Existing activation unit coverage verifies public schedules remain
unset and rejects explicit activation arguments on public networks.

### Validation

All results below are from the final targeted runtime/test source, using native
Linux Release binaries (GCC 14.2, Qt 6.8.2, Boost 1.83, SQLite 3.46.1) on a
2-CPU / 8-GiB container. GUI tests use the offscreen Qt platform. Build parallelism
is `-j4`; native functional parallelism is `--jobs=4`. No complete inherited
Bitcoin functional suite runs.

| Check | Estimate supplied before execution | Actual wall time | Result |
|---|---|---|---|
| Initial targeted incremental build | 4–8 min | 189.760 s | PASS |
| Backend/unlock refinement build | 2–5 min | 150.398 s | PASS |
| Final recipient-unload refinement build | 1–3 min | 32.774 s | PASS |
| Final focused wallet suites, four cases | 2–4 min wallet; 6–9 min combined focused round | 88.296 s | 4/4 PASS |
| Final focused Qt executable | About 4 min; included in combined estimate | 218.459 s | PASS |
| Final real funded regtest case | 1–3 min; included in combined estimate | 49.695 s | PASS |
| Complete native CTest checkpoint | 35–50 min | 2712.465 s (45 min 12 s) | 194 PASS, 1 intentional skip, 0 FAIL |
| Complete Mercatura-native functional checkpoint | 20–35 min | 1755.698 s (29 min 16 s), including setup | 58/58 PASS, 0 skips/failures |
| Repository lint on staged implementation | 1–3 min | 19.974 s | PASS after executable-bit correction |

The full native functional runner reports 1672 seconds of active execution
(6590 accumulated case-seconds). Its driver duration above also includes setup.
The new spending-mode case passes in that four-job checkpoint in 117 seconds.
The full CTest run also repeats the Qt executable successfully in 398.48 seconds.
`script_assets_tests` is the existing intentional skip because Mercatura disables
inherited classical-signature semantics; it is not a missing-assets failure.

Full unit execution: **2026-11-07 07:55:28–08:40:41 UTC**.
Full functional execution: **2026-11-07 08:40:41–09:09:56 UTC**.

Commands, with `BUILD=work/build-native-release` and `cmake`/`ctest` resolved to
the local CMake 4.4.4 tool installation:

```sh
cmake -S . -B "$BUILD"
cmake --build "$BUILD" -j4
ctest --test-dir "$BUILD" --output-on-failure -j4 \
  -R '^(spend_tests|coinselection_tests|psbt_wallet_tests|mercaweave_activation_tests)$'
QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD" --output-on-failure \
  -j1 -R '^test_bitcoin-qt$'
python3 "$BUILD/test/functional/test_runner.py" --jobs=4 --timeout-factor=3 \
  --tmpdirprefix=work/send-spending-modes/runtimes mercatura_spending_modes.py
QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD" --output-on-failure -j4
python3 "$BUILD/test/functional/test_runner.py" --jobs=4 --timeout-factor=3 \
  --tmpdirprefix=work/send-spending-modes/runtimes
cargo run --manifest-path test/lint/test_runner/Cargo.toml
git diff --check
git diff --cached --check
```

The lint invocation uses the existing local CI tool environment and
`COMMIT_RANGE=664d491..HEAD`. Final documentation lint and whitespace checks
must also pass before the local checkpoint commit; their final receipt is
retained with the local evidence.

Two early functional attempts exposed fixture assumptions: native PQ RPC
scripts have script hex rather than a `witness_version` field, and the native
fee-rate convention is `Decimal("0.001")` with `COIN=100`. Both were corrected
without loosening signature, mempool, transaction, fee-cap or input-type
assertions. An initial lint run caught the new shebang script's missing
executable bit; its mode is now 755. The first full-unit attempt was deliberately
interrupted after 139.439 seconds to fix the recipient model left behind after
wallet destruction. It is not counted as a passing checkpoint. The final
focused and complete runs above follow that fix.

Logs and exact commands/start/end times are retained under the ignored local
`work/send-spending-modes/` directory, including `journal.jsonl`, final focused
and regression logs, the hash-verified complete unit log, resolved failed and
interrupted attempts, source audit, and tested runtime/binary hash manifest.
The final runtime/test files are checked against that manifest before commit;
report completion does not change compiled runtime/test files.

All applicable local checkpoints pass. There are **no remaining blockers for
this targeted local change**. New remote CI has not run because this task does
not authorize a push. Earlier remote CI results predate this dropdown and are
not used as qualification for it.

### Boundaries

This task changes none of the 190 protected paths in the earlier integration
baseline. Of that historical manifest, 189 still match; `src/mining/solo.cpp`
retains its previously reviewed integration payout gate. It is unchanged in
this task. Consensus, signature formats, public activation parameters,
MercaHash/DGW/emissions/limits/fee policy/PQ networking/genesis and research
execution guards are unchanged. Grouped activation tests use new disposable isolated regtest directories.
The native public-testnet identity controls also use fresh loopback-only test
nodes, never public endpoints or existing public-testnet datadirs.
No public activation height is chosen, and no push, merge, tag, release,
packaging or deployment is performed.

This checkpoint qualifies the targeted native Linux Release UI/funding change.
It is not a public-network activation or a new claim about unrelated platform,
GPU or broader production-upgrade qualification.
