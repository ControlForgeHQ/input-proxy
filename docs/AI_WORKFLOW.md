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
5. Review the resulting draft pull request.
6. Record requested changes as normal GitHub review comments or PR discussion.
7. If changes are requested, launch the agent using the standard PR update prompt.
8. Re-review the updated pull request.
9. Perform manual hardware validation where required.
10. Record significant validation results or limitations in the pull request.
11. Merge after successful review and validation.

Each issue should represent one reviewable capability rather than a complete
feature whenever practical.

GitHub should remain the durable record of implementation, review feedback,
revision responses, and validation results.

---

## Standard implementation prompt

Use the following prompt when launching an implementation agent.

```text
Implement the GitHub issue:

https://github.com/fasteddy516/input-proxy/issues/<issue>

If you cannot access that GitHub issue, stop immediately and explain why rather than guessing its contents.

Before making any changes:

1. Read AGENTS.md.
2. Read docs/ARCHITECTURE.md.
3. Read docs/ENGINEERING.md.
4. Read the GitHub issue linked above in its entirety.

Treat those documents as the authoritative specification.

Your objective is to implement the issue exactly as specified while preserving the documented architecture and engineering principles.

Guidelines:

- Stay strictly within the scope of the issue.
- Do not implement functionality from future roadmap items, even if the implementation would become slightly cleaner.
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

## Standard PR update prompt

Use the following prompt when an implementation agent needs to address review
feedback on an existing pull request.

```text
Read the latest unresolved review comments and discussion on this pull request:

https://github.com/fasteddy516/input-proxy/pull/<pr>

Address only the requested review feedback.

Keep the changes strictly limited to the review comments unless you discover a genuine blocker, in which case stop and explain the blocker rather than broadening the scope automatically.

After making the changes:

- rerun the validation required by the implementation issue;
- update the existing draft PR rather than creating a new PR;
- add a comment to the PR summarizing:
  - what changed;
  - validation performed;
  - any assumptions made;
  - whether any requested review items could not be completed.

Do not merge the PR, modify unrelated files, or open additional issues.
```

The PR comment is part of the required workflow.

It should provide a durable response to the review request so that the complete
request-response path remains visible in GitHub without requiring access to the
agent conversation.

---

## Prompt philosophy

The implementation and PR-update prompts intentionally contain very little
project-specific information.

Project knowledge belongs in the repository and GitHub issue or review history,
not in the launch prompt.

The authoritative sources for initial implementation are:

- GitHub Issue
- AGENTS.md
- docs/ARCHITECTURE.md
- docs/ENGINEERING.md

For revision work, the authoritative sources additionally include:

- the existing pull request;
- unresolved review comments;
- relevant PR discussion.

Keeping implementation details in one place avoids duplicated specifications
and reduces the likelihood of documentation drift.

The launch prompt should direct the agent to authoritative project information,
not duplicate that information.

---

## Pull request review

Review AI-generated pull requests using the same standards applied to human
contributors.

Typical review topics include:

- correctness;
- scope;
- architecture;
- public API;
- ownership;
- cleanup;
- regression tests;
- validation;
- documentation.

Successful review does not imply successful hardware validation.

Review feedback should normally be recorded through GitHub review comments or
PR discussion rather than supplied only through a separate agent conversation.

This preserves the rationale for requested changes and allows both humans and
implementation agents to work from the same review history.

---

## Review and revision cycle

An agent-authored pull request is not expected to bypass normal review simply
because its implementation followed a detailed issue.

The normal revision cycle is:

```text
implementation issue
    |
    v
agent implementation
    |
    v
draft pull request
    |
    v
human review
    |
    +-- no changes requested --> validation
    |
    +-- changes requested
            |
            v
       GitHub review comments
            |
            v
       agent revision
            |
            v
       PR response summary
            |
            v
       human re-review
```

When changes are requested:

- record the requested change and rationale in GitHub;
- ask the agent to read and address the existing review feedback;
- avoid restating detailed technical feedback in the agent prompt unless access
  to the GitHub discussion fails;
- require the agent to update the existing PR rather than creating a replacement;
- require validation to be rerun after the change;
- require the agent to post a summary of its response to the PR discussion.

The PR description should remain the summary of the original implementation.

Revision summaries should normally be added as PR comments so that the evolution
of the implementation remains visible chronologically.

---

## Hardware validation

Hardware validation is considered part of code review, not part of
implementation.

When functionality depends on physical hardware:

- implement hardware-independent regression tests where practical;
- clearly report any validation that could not be performed;
- do not fabricate or infer hardware validation;
- perform manual hardware validation before merging whenever possible;
- record significant platform limitations discovered during validation;
- add useful hardware-validation results to the pull request discussion when
  they materially increase confidence in the implementation.

Temporary validation programs may be created outside the tracked source tree or
inside ignored build directories but should not be committed.

For event-oriented hardware tests, reporting both complete synchronization
frames and raw event counts is preferred when useful. Raw event counts alone
can be misleading for devices such as touchscreens that generate many events
per physical interaction.

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

Review feedback should correct architectural or implementation problems before
merge rather than accepting temporary public interfaces or abstractions that
are known to become obsolete in the next development step.

---

## Continuous improvement

This workflow should evolve as experience is gained.

Changes that consistently improve implementation quality, review quality,
traceability, or validation quality should be incorporated into this document
so future contributors benefit from them.