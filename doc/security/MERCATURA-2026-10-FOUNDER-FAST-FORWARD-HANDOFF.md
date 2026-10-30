# Founder-controlled fast-forward handoff

**STOP: technical gates remain incomplete. No merge/push commands are authorized by this handoff yet.**

Official last observed v0.1: `631886dd5159614b628ced3fe4fa1e289ce19451`.
Security code: `0a66162c465898b132e31d7574b0b8fb89ff09cc`.
Evidence documentation commit: `None`.
Branch: `security/bitcoin-core-2026-10-backports` on authorized `project` remote (`https://github.com/Mercatura-Core/Mercatura-Core.git`).
Final literal branch HEAD and initial CI run URLs are in the post-commit publication receipt `/workspace/work/security-final/evidence/final-publication.json` and final founder response.

Required before fast-forward: completed native/security/PQ enforcing qualification, review of original historical database gap, clean checkout, unchanged official branch ancestry, and explicit founder approval. Never infer approval from this assignment. No tags/releases/live deployment or V2 activation change is authorized.

Read-only review commands (small independent blocks):

```sh
git status --porcelain=v1
git remote get-url project
```

Stop unless status is empty and project is the official repository.

```sh
git fetch --no-tags project
git rev-parse project/v0.1 project/security/bitcoin-core-2026-10-backports
```

Compare the security tip to the exact final publication receipt; stop on any unexpected branch change.

```sh
git merge-base --is-ancestor project/v0.1 project/security/bitcoin-core-2026-10-backports
git log --oneline project/v0.1..project/security/bitcoin-core-2026-10-backports
```

Stop if ancestry fails. After all technical gates and founder approval pass, a new exact-SHA handoff must check local v0.1/worktrees, switch to v0.1, fast-forward it to project/v0.1 and then the exact approved security SHA, verify HEAD, and push only project/v0.1 with a normal non-force push. Those write commands are intentionally withheld while required evidence is pending.

Existing PQ workflow action if dispatch is unavailable here:

```sh
gh workflow run pq-integration.yml --repo Mercatura-Core/Mercatura-Core --ref security/bitcoin-core-2026-10-backports
```

Run exactly once only if the final receipt says no dispatch succeeded. In GitHub UI: Actions → “PQ P9-P11 native qualification (no publication)” → Run workflow → select `security/bitcoin-core-2026-10-backports`. Confirm the resulting run head SHA against the published tip. Record its URL/source once and do not poll. This workflow creates local disposable smoke packages; it does not publish releases.

The next separate founder development decision, after security qualification and manual incorporation, is the public-testnet MercaHash V2 activation height. This handoff selects no height.
