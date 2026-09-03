# Decisions

Non-obvious project decisions are recorded newest first. Earlier technical
rationale remains in [ARCHITECTURE.md](../ARCHITECTURE.md); new or changed
policy belongs here.

## 2026-09-03: Publish the runtime as Apache-2.0 open core

**Decision:** Publish this repository, including the VM, protocol, HAL, reference
board ports, compatibility missions and bundled open-path inference artifacts,
under the Apache License 2.0. Keep the product IDE, AI service and model-training
lifecycle in their separate repositories.

**Context:** Hardware users need to audit what runs on their board, keep saved
programs usable without a service, and port the runtime to hardware we do not
own. The runtime already exposes a small stable contract and has a host test rig,
so it can support outside work without exposing the hosted product stack.

**Why Apache-2.0:** It permits maker, education and commercial use while
providing an explicit patent grant. That lowers friction for board vendors and
downstream firmware distributions.

**Rejected alternatives:** Keeping the runtime private weakens the autonomy and
portability claims. Publishing all four repositories would also expose the
hosted assistant and proprietary training workflow without improving firmware
portability. A strong copyleft licence would create more integration friction
for hardware vendors at this stage.

**Constraints:** Third-party components retain their own licences and must be
documented in `THIRD_PARTY_NOTICES.md`. Nordic's restricted Edge AI components
remain external build-time dependencies. The project name and logos are not
granted as trademarks by the source licence.

**Validation:** Before publication, the tracked tree and all 145 existing
commits were scanned for high-confidence secrets and suspicious credential
filenames, the submodule boundary was checked, and the host test suite and CI
configuration were reviewed.

**Reopen when:** A hardware partner requires a different contribution model, a
third-party component cannot be distributed within this boundary, or the open
runtime no longer produces measurable portability, trust or contributor value.
