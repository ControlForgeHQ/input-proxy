# AGENTS.md

This file defines repository-wide instructions for automated contributors.

## Read the project documentation

Before making substantive changes, read:

1. `docs/ROADMAP.md`
2. `docs/ARCHITECTURE.md`
3. `docs/ENGINEERING.md`

These documents are authoritative for different subjects:

- `ROADMAP.md` — what the project is building and when;
- `ARCHITECTURE.md` — how the system is designed and where behaviour belongs;
- `ENGINEERING.md` — how changes must be implemented, validated, documented, and
  reviewed.

Do not rely on this file as a substitute for those documents.

## Follow documentation ownership

Do not duplicate project-wide architecture, engineering standards, or roadmap
requirements inside implementation-specific work.

If documentation appears inconsistent, identify the conflict rather than
silently choosing one interpretation.

If a task requires an architectural change, update and review the architecture
before treating the new design as established.

## Keep changes scoped

Implement only the behaviour requested by the issue or explicit instruction.

Do not:

- implement future roadmap items early;
- perform unrelated refactoring;
- introduce speculative abstractions;
- expand public APIs without a real external requirement;
- change system configuration during normal runtime operation;
- broaden the project into a general input-remapping or automation system.

Prefer the smallest change that correctly satisfies the requested behaviour.

## Preserve project boundaries

Maintain the documented separation between:

- source-device mechanics;
- proxy-session lifecycle and policy;
- virtual-device mechanics;
- diagnostic and inspection functionality;
- future installation functionality.

Do not bypass session-level policy by wiring source input directly to the
virtual device.

Do not move lifecycle decisions into low-level device modules merely because
doing so is convenient.

## Validate honestly

Add focused regression coverage where practical.

Run the validation required by the issue and by `docs/ENGINEERING.md`.

Do not claim hardware validation unless real hardware was used.

When hardware-dependent behaviour cannot be validated, state that explicitly.

## Pull-request workflow

Create implementation pull requests as drafts.

Keep each pull request focused on one issue.

The pull request description should report:

- what changed;
- validation performed;
- anything not validated;
- assumptions or limitations.

When responding to requested PR changes, add a PR conversation comment
summarizing the update and validation so the request-and-response history remains
documented.

## When scope or design conflicts

If the requested work cannot be implemented correctly without:

- changing documented architecture;
- expanding scope materially;
- violating a documented project invariant;
- introducing a substantially different dependency or design;

stop and explain the conflict rather than silently redesigning the project.