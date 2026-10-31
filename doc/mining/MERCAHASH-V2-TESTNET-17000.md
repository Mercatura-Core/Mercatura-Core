# Founder-approved public-testnet MercaHash V2 activation

Existing public testnet changes from V1 to V2 at candidate height 17,000. Heights
0,16,998 and 16,999 use V1;17,000,17,001 and every later height use V2. The existing
GetPoWVersion selector consumes the height established from validated ancestry;
neither an unverified peer height nor the active tip substitutes for that height.

There is no difficulty reset, genesis change or block-version vote. DGWv3,
150-second target spacing, chainwork, adaptive emission, transaction ownership
and capacity height 8,000/growth remain unchanged. Historical blocks continue to
be checked under their own algorithm. Reorgs can move candidate work across the
boundary; old work must be canceled/replaced and a stale V1 result cannot be
submitted as a V2 solution. A nonce may satisfy both algorithms at an easy target,
so negative tests must explicitly find proofs valid only for the wrong version.

The existing native CPU, CUDA and OpenCL paths select work through the shared
version-aware mining path. V1 retains a 128 MiB private scratchpad and V2 a 16 MiB
private scratchpad, with existing resource budgets and worker lifetime handling.
No kernel arithmetic, frozen vectors, reward destination,0% software mining fee,
wallet UI or consensus transaction fee behavior changes in this assignment.
Hardware performance claims require measured NVIDIA/AMD device evidence.

Regtest's explicit -mercahashv2height test option and height 20 functional boundary
remain available; no public-testnet command-line consensus override is added.
Mainnet, signet and testnet4 retain their previous PoW settings. Mainnet V2 from
genesis is deferred until several days of real post-activation public-testnet
observation and a later founder decision. No mainnet genesis is generated.

Compulsory PQ networking is independent: an upgraded public-testnet process uses
PQ from startup, including while syncing historical V1 blocks or reindexing.
It has no block-height trigger and never falls back to classical/BIP324 transport.

Release/publication, platform packages, operator coordination and live deployment
remain separate manual gates. See the [qualification report](../validation/MERCATURA-TESTNET-17000-PQ-REQUIRED-PRE-RELEASE.md)
for actual exact-source outcomes and platform/hardware limitations.

## Consensus-aware rollback limit

After block 17,000, a V1-only release cannot follow the upgraded public-testnet
chain. A rollback must retain V2 consensus support or use an explicitly approved
coordinated recovery plan; backups do not make an obsolete consensus implementation
compatible. Before or after that boundary, transport rollback also requires an
agreed compatible peer cohort. No automatic classical fallback is available.

## Offline developer grinder

The existing bitcoin-util grind helper has no validated chain ancestry. Its
existing guard requires explicit -powversion=1 or2 for a transitioned network;
it does not infer a height from an unverified header. That offline diagnostic
option does not override node consensus or replace the native miner's automatic
candidate-height selection.
