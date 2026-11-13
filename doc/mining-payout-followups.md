Mining payout follow-up
======================
MercaWeave coin selection conservatively estimates grouped inputs. Review this
separately; mining payout selection does not change transaction weight limits,
fees, consensus, or coin selection.

Reward address rotation is not group-key rotation. Outputs to one v3 address
share an authorization key, and newly derived mining addresses can share a group
key (the authenticated mining derivation purpose is 2).

Qt's reward selector offers exactly Standard PQ (v2) and MercaWeave (v3).
With no saved preference, the default is v3 when activated and wallet-supported,
and v2 otherwise; a saved address keeps its own type and must still pass wallet
ownership and activation checks. Explicit selections persist as stable `v2`/`v3`
identifiers in the wallet/network-scoped QSettings `payoutType` key. Old integer
`payout` values 1 and 2 retain their meanings. Old 0 (Automatic) is migrated once
to a concrete type, preferring a selected address's type, then supported
activation. Incompatible or inactive selected addresses block Start and require
an appropriate address; migration never replaces them. Runtime address resolution
accepts only concrete types 1 and 2.
