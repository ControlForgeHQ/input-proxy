# Engineering

This document defines how `input-proxy` is developed and maintained.

It is authoritative for engineering principles, implementation standards,
testing and validation expectations, documentation practices, logging
conventions, versioning, commits, reviews, and release practices.

Runtime design belongs in `docs/ARCHITECTURE.md`.

Product direction belongs in `docs/ROADMAP.md`.

## Engineering principles

### Prefer correctness over convenience

`input-proxy` sits between physical input hardware and operating-system input
consumers.

Incorrect behaviour can produce stuck keys, stuck touch contacts, duplicate
input, lost events, or unstable device identity.

Prefer behaviour that is demonstrably correct over behaviour that is merely
convenient to implement.

When assumptions can be tested directly, test them before encoding them into
the architecture.

### Keep the runtime simple

Prefer small, explicit, understandable implementations.

Avoid speculative abstraction, unnecessary frameworks, hidden control flow, and
features without a demonstrated requirement.

A small amount of duplication is often preferable to introducing an abstraction
before its value has been demonstrated.

### Preserve architectural boundaries

The architecture is the authority for module responsibilities and lifecycle
behaviour.

Implementations should conform to the documented architecture rather than
changing it implicitly.

If a correct implementation appears to require an architectural change, update
and review the architecture before treating the new behaviour as established.

### Make ownership explicit

Ownership of resources must be clear.

Resources include:

- dynamically allocated memory;
- file descriptors;
- libevdev objects;
- uinput objects;
- strings;
- buffers;
- operating-system handles.

For every owned resource, it should be apparent:

- who creates it;
- who owns it;
- who may use it;
- who destroys it.

Avoid shared ownership where practical.

Cleanup must remain safe after partial initialization.

### Keep modules focused

Each module should have one primary responsibility.

Modules should communicate through narrow interfaces and should not reach
through architectural boundaries merely because doing so is convenient.

Avoid exposing implementation-specific structures through public headers unless
they are genuinely part of the public API.

Keep third-party libraries and platform-specific details behind appropriate
module boundaries where practical.

### Prefer standard platform interfaces

Prefer ordinary Linux, Unix, and POSIX facilities over dependencies on a
specific deployment environment.

For example, runtime logging uses standard output and standard error rather than
depending directly on systemd or journald.

Add a platform-specific dependency only when it provides functionality that
cannot reasonably be achieved using the project's existing platform interfaces.

### Optimize for readability

Code is read more often than it is written.

Prefer:

- explicit control flow;
- descriptive names;
- narrow functions;
- obvious ownership;
- straightforward error handling.

Avoid cleverness that saves a few lines while increasing the amount of context
required to understand the code.

Comments should explain intent, constraints, or non-obvious behaviour rather
than restating the implementation.

### Diagnose before automating

When investigating operating-system or hardware behaviour, prefer observing the
real system before adding compensating code.

Small temporary probes, `strace`, `evtest`, libevdev queries, and focused
hardware experiments are appropriate tools for validating assumptions.

Do not permanently add complexity to compensate for behaviour that has not been
demonstrated.

### Validate on real hardware when practical

Hardware-independent regression tests are required where practical, but they do
not replace hardware validation for behaviour that depends on Linux input,
uinput, udev, hotplug, or real device semantics.

When hardware validation cannot be performed:

- report that limitation explicitly;
- do not imply or fabricate validation;
- keep the change testable so hardware validation can be completed separately.

## Documentation model

Project documentation is divided by responsibility so that each piece of
information has one authoritative home.

Avoid maintaining the same requirement, design decision, or process rule in
multiple documents.

When duplication is discovered, retain the information in the document that
owns the subject and remove or replace the duplicate with a reference.

### README.md

`README.md` is the user-facing entry point.

It should answer:

- what `input-proxy` is;
- what it does;
- how to build it;
- how to run it;
- how runtime permissions are configured;
- where to find deeper project documentation.

It may summarize capabilities, but it does not define runtime architecture,
engineering standards, or future roadmap requirements.

### docs/ROADMAP.md

`docs/ROADMAP.md` defines product direction.

It is authoritative for:

- release goals;
- planned feature sequencing;
- milestone scope;
- version-level product intent;
- future non-goals.

The roadmap describes what a release is intended to accomplish and why.

It should not prescribe implementation details.

### docs/ARCHITECTURE.md

`docs/ARCHITECTURE.md` defines system design.

It is authoritative for:

- runtime architecture;
- module responsibilities;
- resource and lifecycle ownership;
- state-machine semantics;
- CLI structure and operating modes;
- event and control flow;
- persistent-device behaviour;
- architectural invariants;
- design rationale.

It should answer:

> Where does this behaviour belong, and how should the system behave?

It should not define contributor workflow, coding style, commit conventions, or
release planning.

### docs/ENGINEERING.md

This document defines how the project is developed and maintained.

It is authoritative for:

- engineering principles;
- coding and design standards;
- testing and validation expectations;
- documentation practices;
- logging conventions;
- commit and review practices;
- versioning and release practices;
- expectations for human and automated contributors.

It should answer:

> What standards must an implementation meet?

### AGENTS.md

`AGENTS.md` is a concise routing and guardrail document for automated
contributors.

It should:

- require agents to read the authoritative project documentation;
- identify which document owns which kind of information;
- define agent-specific behavioural constraints;
- require narrowly scoped work;
- prohibit unrelated refactoring and premature roadmap implementation.

It should not restate architecture, CLI behaviour, lifecycle details, coding
standards, or roadmap features.

### Issue templates and issues

Issues define specific requested work.

They should contain only task-specific information such as:

- objective;
- background or observed behaviour when useful;
- in-scope work;
- explicit non-goals;
- acceptance criteria;
- task-specific validation requirements.

Issues should assume that project-wide contributor instructions and standards
are already supplied through `AGENTS.md` and the authoritative documentation.

Do not repeat generic instructions such as:

- preserve the architecture;
- avoid unrelated refactoring;
- follow coding standards;
- read every project document.

Those are project-wide rules.

### Maintaining documentation

When a change affects documentation:

1. identify which document owns the changed information;
2. update that authoritative document;
3. check other documents for stale duplicated statements;
4. remove unnecessary duplication or replace it with a reference.

A normal change should require edits to as few authoritative documents as
possible.

If one fact repeatedly requires synchronized edits in several documents,
reconsider where that fact is documented.

## Coding standards

Use C17.

Compile without relying on non-standard language extensions unless a specific
platform requirement justifies them.

Prefer:

- explicit types;
- narrow function responsibilities;
- early validation of arguments;
- clear result propagation;
- deterministic cleanup;
- private implementation headers for non-public seams.

Public APIs should remain deliberately small.

Do not expose an API publicly merely to make testing convenient. Prefer a
private internal seam when the functionality is not intended for external
callers.

Warnings should be treated seriously.

New code should compile cleanly with the project's configured warning settings.

## Error handling

Errors should be classified at the lowest layer that can accurately describe
what happened.

Lifecycle or policy decisions belong at the layer that has sufficient context
to interpret that result.

For example:

- a source-device module may report permission denial;
- the proxy session decides whether that denial is fatal or temporarily
  recoverable based on lifecycle state.

Do not hide materially different failures behind a generic result when the
distinction is required for correct policy.

Conversely, do not create result variants for distinctions that have no useful
caller-visible meaning.

## Logging

Runtime logging uses ordinary process streams.

- Normal lifecycle and status messages go to standard output.
- Warnings and errors go to standard error.
- The runtime does not write its own log files.
- Logging must not require systemd, journald, syslog, or a third-party logging
  framework.

Messages should be concise and human-readable.

Do not add timestamps, PIDs, host names, service names, or other metadata
normally supplied by the execution environment.

Log meaningful state changes, not loop iterations.

Retry loops must not repeatedly log the same unchanged condition.

Normal operation and `--verbose` must not dump raw evdev event traffic.

Verbose mode is for additional lifecycle, state-transition, identity, and
diagnostic context.

Raw input inspection belongs in dedicated diagnostic functionality or external
tools.

## Testing

Every behaviour change should have focused regression coverage where practical.

Tests should verify observable contracts rather than internal implementation
details unless the internal detail itself is the contract under test.

A change is not considered validated merely because it builds.

Normal validation should include:

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

Additional validation should be specified by the issue when required.

Tests must not claim to validate hardware behaviour that they simulate through
mocks or seams.

Temporary hardware probes and experimental programs should not become permanent
production targets unless they provide enduring diagnostic value.

## Incremental development

Implement functionality in small, reviewable increments.

Each pull request should:

- address one coherent issue;
- leave the project buildable;
- preserve existing behaviour outside its scope;
- contain appropriate tests;
- avoid unrelated cleanup.

A pull request should remain releasable whenever practical.

Do not implement future roadmap work opportunistically as part of an unrelated
change.

## Commits

The project follows the spirit of Conventional Commits.

Preferred prefixes are:

| Prefix | Purpose |
| --- | --- |
| `feat:` | New functionality |
| `fix:` | Bug fixes |
| `docs:` | Documentation changes |
| `test:` | Tests and test infrastructure |
| `refactor:` | Internal restructuring without intended behaviour change |
| `perf:` | Performance improvements |
| `build:` | Build system, compiler, dependencies, packaging |
| `ci:` | Continuous integration workflows |
| `chore:` | Release preparation and repository maintenance |

Commit messages should describe the primary purpose of the change.

Prefer focused commits that tell a useful project history.

Version-only release preparation should normally use:

```text
chore: bump version to X.Y.Z
```

## Pull requests and review

Pull requests should be created as drafts until implementation and validation
are ready for review.

A pull request description should summarize:

- what changed;
- why;
- validation performed;
- anything explicitly not validated;
- assumptions or limitations when relevant.

When follow-up changes are requested on a pull request, the resulting update
should be documented in the PR conversation so the request-and-response history
remains visible.

Review should focus on:

- correctness;
- architecture;
- lifecycle behaviour;
- resource ownership;
- error semantics;
- scope discipline;
- test adequacy;
- hardware validation where relevant.

Formatting or abstraction preferences should not outweigh a clear and correct
implementation without a concrete maintainability benefit.

## Versioning

The project uses semantic versioning.

Before Version 1.0, minor releases may contain intentional interface changes
when they are clearly documented.

Patch releases contain bug fixes, documentation corrections, validation
improvements, and narrowly scoped behavioural corrections. They should not
intentionally introduce new feature scope.

The Git release tag uses a `v` prefix:

```text
v0.2.0
```

The application version does not:

```text
input-proxy 0.2.0
```

The build should maintain one authoritative version source.

Release preparation should not require manually synchronizing multiple version
definitions.

## Releases

Before creating a release:

- ensure the intended version is set;
- perform a clean configure and build;
- run the full regression suite;
- verify `--version`;
- verify relevant CLI help;
- perform hardware validation required by the release;
- confirm the repository working tree is clean;
- verify documentation reflects the released behaviour.

Release notes should describe meaningful user-visible changes rather than
reproduce the commit log.

Do not delay a release for unrelated future improvements.

## Automated contributions

Automated contributors are held to the same engineering standards as human
contributors.

Automation should be used to accelerate implementation, not to bypass design
review.

Automated contributors must:

- follow `AGENTS.md`;
- respect documented architecture and roadmap scope;
- report assumptions;
- report incomplete validation honestly;
- avoid unrelated refactoring;
- stop rather than silently broaden scope when a task conflicts with documented
  architecture or requires a materially different design.

The project should remain tooling-agnostic. Contributor documentation should
describe expected behaviour rather than depend unnecessarily on one particular
AI development product.