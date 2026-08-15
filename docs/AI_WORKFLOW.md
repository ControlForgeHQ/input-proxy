# AI-Assisted Development Workflow

## Purpose

`input-proxy` uses AI-assisted implementation as part of its normal development
workflow.

AI implementation agents are treated as contributors whose work is specified,
reviewed, validated, and merged through GitHub Issues and pull requests.

Project-wide contributor instructions are defined in `AGENTS.md`.

Architecture, engineering standards, and product direction are defined in the
repository documentation referenced by `AGENTS.md`.

This document defines only the workflow used to launch, review, and revise
AI-assisted implementation work.

## Source of truth

GitHub is the durable record for implementation work.

The authoritative sources for an implementation are:

- the GitHub issue;
- `AGENTS.md`;
- the project documentation referenced by `AGENTS.md`.

For revision work, the authoritative sources additionally include:

- the existing pull request;
- review comments;
- PR discussion.

Launch prompts should point the implementation agent to those sources rather
than duplicating their contents.

## Normal workflow

The normal implementation cycle is:

1. Define one reviewable capability or bug fix.
2. Create a GitHub issue using the appropriate issue template.
3. Launch the implementation agent using the standard implementation prompt.
4. Review the resulting draft pull request.
5. Record requested changes in the GitHub pull-request discussion or review.
6. If changes are required, launch the agent using the standard PR update
   prompt.
7. Re-review the updated pull request.
8. Perform manual or hardware validation where required.
9. Record significant validation results or limitations in the PR discussion.
10. Merge after review and validation succeed.

Issues should remain narrowly scoped.

Pull requests should normally correspond to one issue.

## Standard implementation prompt

Use the following prompt for initial implementation work.

Only the GitHub issue URL should normally need to be changed.

```text
Implement the GitHub issue:

https://github.com/ControlForgeHQ/input-proxy/issues/<issue>

Read and follow AGENTS.md before making changes.

Read the linked issue in its entirety and implement it exactly as specified.

If you cannot access the issue or required repository documentation, stop and explain why rather than guessing.

Create a draft pull request when the implementation is complete.

Do not merge the pull request or open additional issues.
```

The implementation agent is expected to obtain all project-wide architecture,
engineering, validation, reporting, and scope guidance through `AGENTS.md` and
the documentation it references.

Do not duplicate those instructions in the launch prompt.

## Standard PR update prompt

Use the following prompt when an existing pull request requires changes.

Only the pull-request URL should normally need to be changed.

```text
Address the latest unresolved review feedback and discussion on this pull request:

https://github.com/ControlForgeHQ/input-proxy/pull/<pr>

Read and follow AGENTS.md before making changes.

Use the existing pull request, its linked implementation issue, and the recorded review discussion as the authoritative specification for this revision.

Address only the requested feedback.

If the requested changes require a material scope or architecture change, stop and explain the conflict rather than broadening the implementation automatically.

Update the existing draft pull request rather than creating a new one.

After completing the revision, add a PR conversation comment summarizing the changes and validation performed.

Do not merge the pull request or open additional issues.
```

Revision summaries belong in PR comments rather than replacing the original PR
description.

This preserves the chronological request-response history.

## Pull-request review

AI-generated pull requests are reviewed using the same standards as other
contributions.

Review should determine whether:

- the issue was implemented correctly;
- scope was respected;
- documented architecture was preserved;
- error and lifecycle behaviour are correct;
- resource ownership remains clear;
- regression coverage is adequate;
- required validation was performed;
- anything requiring hardware validation remains unverified.

Review feedback should be recorded in GitHub rather than existing only in a
separate AI conversation.

This allows both humans and implementation agents to work from the same durable
history.

## Revision cycle

The normal revision cycle is:

```text
GitHub issue
    |
    v
implementation agent
    |
    v
draft pull request
    |
    v
review
    |
    +-- accepted --> validation / merge
    |
    +-- changes requested
            |
            v
       GitHub review feedback
            |
            v
       implementation agent
            |
            v
       updated PR
            |
            v
       PR response summary
            |
            v
       re-review
```

Avoid reproducing detailed review instructions in the PR update prompt.

The review discussion itself should contain the required technical context.

## Validation

Automated regression validation is part of implementation.

Manual and hardware validation may be completed during review when the
implementation environment cannot perform it.

When hardware validation materially increases confidence in a change, record the
result in the PR discussion.

Do not infer hardware validation from mocked or simulated tests.

## Workflow principles

The AI workflow should remain intentionally thin.

Project knowledge belongs in:

- issues;
- repository documentation;
- pull-request history.

The launch prompt exists only to direct the agent to those authoritative
sources and define the immediate GitHub workflow action.

If the standard prompt begins accumulating architecture, coding standards,
validation rules, or issue-specific implementation guidance, that information
probably belongs elsewhere.

The workflow should evolve when repeated experience shows that a change improves
implementation quality, review quality, traceability, or efficiency.
