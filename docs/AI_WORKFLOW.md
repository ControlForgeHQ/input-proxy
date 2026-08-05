# AI-Assisted Development Workflow

## Purpose

This project is designed to support AI-assisted software development while
maintaining the same engineering standards expected of human contributors.

GitHub Issues define the implementation specification.

Repository documentation defines the architecture, engineering principles, and
coding standards.

AI implementation agents are treated as contributors whose work is reviewed,
validated, and merged through the normal pull request process.

---

## Workflow

The recommended implementation workflow is:

1. Define a single, well-bounded capability.
2. Create a GitHub Issue describing that capability.
3. Include validation requirements in the issue.
4. Launch an AI implementation agent using the standard implementation prompt.
5. Review the resulting pull request.
6. Perform manual hardware validation where required.
7. Merge after successful review.

Each issue should represent one reviewable capability rather than a complete
feature whenever practical.

---

## Standard implementation prompt

Use the following prompt when launching an implementation agent.

```text
You are implementing GitHub issue #<issue number> for this repository.

Issue:
https://github.com/fasteddy516/input-proxy/issues/<issue number>

If you cannot access the GitHub issue, stop immediately and explain why rather than guessing its contents.

Before making any changes:

1. Read AGENTS.md.
2. Read docs/ARCHITECTURE.md.
3. Read docs/ENGINEERING.md.
4. Read GitHub issue #<issue number> in its entirety.

Treat those documents as the authoritative specification.

Your objective is to implement the issue exactly as specified while preserving the documented architecture and engineering principles.

Guidelines:

- Stay strictly within the scope of the issue.
- Preserve the existing public API unless a genuine technical blocker requires a change.
- If you believe an architectural or public API change is necessary, stop and explain the issue rather than redesigning the project.
- Avoid unrelated refactoring.
- Prefer straightforward, explicit C over clever abstractions.
- Keep ownership and cleanup paths obvious.
- Build the project and perform the validation described in the issue.
- Report exactly what was and was not validated.
- Summarize the implementation, any assumptions made, and any recommended follow-up work.
- Create a draft pull request.
- Do not merge or open additional issues.

If any requirement appears ambiguous, ask for clarification before making architectural assumptions.
```

---

## Prompt philosophy

The implementation prompt intentionally contains very little project-specific
information.

Project knowledge belongs in the repository, not in the prompt.

The authoritative sources are:

- GitHub Issue
- AGENTS.md
- docs/ARCHITECTURE.md
- docs/ENGINEERING.md

Keeping implementation details in one place avoids duplicated specifications and
reduces the likelihood of documentation drift.

---

## Pull request review

Review AI-generated pull requests using the same standards applied to human
contributors.

Typical review topics include:

- correctness
- scope
- architecture
- public API
- ownership
- cleanup
- regression tests
- validation
- documentation

Successful review does not imply successful hardware validation.

---

## Hardware validation

Hardware validation is considered part of code review, not part of implementation.

When functionality depends on physical hardware:

- Implement hardware-independent regression tests where practical.
- Clearly report any validation that could not be performed.
- Do not fabricate or infer hardware validation.
- Perform manual hardware validation before merging whenever possible.
- Record significant platform limitations discovered during validation.

Temporary validation programs may be created outside the tracked source tree or
inside ignored build directories but should not be committed.

---

## Engineering philosophy

Small, reviewable pull requests are preferred over large feature branches.

Repository documentation should answer architectural questions before an
implementation agent begins writing code.

Implementation agents should not redesign the project while implementing an
issue.

If an issue cannot be completed without changing the documented architecture,
the preferred behaviour is to stop and explain the blocker rather than making
architectural assumptions.

---

## Continuous improvement

This workflow should evolve as experience is gained.

Changes that consistently improve implementation quality, review quality, or
validation quality should be incorporated into this document so future
contributors benefit from them.