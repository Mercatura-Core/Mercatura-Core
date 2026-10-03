# Mercatura Security Policy

Security is a critical priority for Mercatura.

Mercatura contains consensus-critical code, proof-of-work functionality, wallet
and key-management code, post-quantum cryptography, peer-to-peer networking,
RPC interfaces, and monetary-policy logic. Vulnerabilities affecting these
areas may have serious consequences for users or the network.

## Supported Versions

Mercatura is currently in pre-release development.

Until the first official public release, security fixes are developed against
the actively maintained Mercatura source branches.

Development snapshots, older commits, experimental builds, and unofficial
binaries should not be assumed to receive security updates.

This section will be updated with an explicit supported-release schedule when
public Mercatura releases are available.

## Reporting a Security Vulnerability

**Do not report an undisclosed security vulnerability through a public GitHub
issue, GitHub discussion, Discord channel, Reddit post, X post, Bitcointalk
post, or other public communication channel.**

The preferred reporting method is **GitHub Private Vulnerability Reporting**
for the Mercatura repository.

To submit a private report:

1. Open the Mercatura repository on GitHub.
2. Select **Security**.
3. Select **Report a vulnerability**.
4. Submit the vulnerability details privately to the Mercatura maintainers.

If private vulnerability reporting is temporarily unavailable, do not publish
the issue publicly. Contact a Mercatura maintainer privately and request an
appropriate private reporting channel.

Please include as much of the following information as possible:

- A clear description of the vulnerability.
- The affected Mercatura version, branch, or commit.
- The affected component or subsystem.
- Steps required to reproduce the issue.
- Expected behavior and observed behavior.
- The potential security impact.
- A proof of concept, test case, or relevant logs when appropriate.
- Any proposed mitigation or fix, if known.

## Critical and High-Impact Issues

Please report privately any issue that may affect the security, integrity, or
availability of Mercatura, including but not limited to:

- Unauthorized creation, destruction, or theft of MCA.
- Consensus divergence or unintended chain splits.
- Incorrect block or transaction validation.
- Subsidy, issuance, or SP-LT adaptive-emission errors.
- Difficulty-adjustment or proof-of-work validation flaws.
- Vulnerabilities in MercaHash.
- Post-quantum authorization or ML-DSA verification vulnerabilities.
- Wallet key generation, derivation, storage, signing, or recovery failures.
- Circumvention of transaction authorization.
- Remote code execution.
- Serious memory-safety vulnerabilities.
- Authentication or authorization bypasses.
- Exposure of wallet secrets or other sensitive information.
- Network-wide denial-of-service vulnerabilities.
- Peer-to-peer vulnerabilities capable of significantly disrupting the network.
- RPC vulnerabilities that could compromise a node or wallet.
- Bugs allowing invalid blocks or transactions to be accepted.

If you are uncertain whether an issue qualifies as a security vulnerability,
report it privately rather than publicly.

## Responsible Testing

Security research should be performed in a way that minimizes risk to users,
infrastructure, and the network.

Whenever possible:

- Use regtest, isolated test environments, or privately controlled nodes.
- Do not access, alter, or destroy funds or data belonging to another person.
- Do not intentionally disrupt public Mercatura infrastructure.
- Do not perform denial-of-service testing against systems you do not control.
- Avoid publishing exploit code or technical details before coordinated
  disclosure.

## Coordinated Disclosure

Mercatura maintainers will investigate security reports and determine the
appropriate remediation and disclosure process based on the severity and scope
of the issue.

Researchers are asked to keep vulnerability details confidential while a fix
is being developed, tested, and distributed.

Where appropriate, Mercatura will coordinate public disclosure after affected
software has been corrected and users have had a reasonable opportunity to
update.

Security fixes may initially be developed privately when premature disclosure
could place users or the network at risk.

## Security Advisories

When appropriate, Mercatura may use GitHub Security Advisories to coordinate
fixes and publish vulnerability information after remediation.

## Bug Bounties

Mercatura does not currently operate a formal bug-bounty program.

Reporting a vulnerability does not create an entitlement to payment or other
compensation. This policy may be updated if a formal security-reward program
is introduced in the future.

## General Support

This policy is for security-sensitive issues.

General support questions, feature requests, documentation issues, and ordinary
bugs should use the normal Mercatura development and community channels.
