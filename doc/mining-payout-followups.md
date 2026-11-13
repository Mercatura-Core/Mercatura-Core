Mining payout follow-up
======================
MercaWeave coin selection conservatively estimates grouped inputs. Review this
separately; mining payout selection does not change transaction weight limits,
fees, consensus, or coin selection.

Reward address rotation is not group-key rotation. Outputs to one v3 address
share an authorization key, and newly derived mining addresses can share a group
key (the authenticated mining derivation purpose is 2).
