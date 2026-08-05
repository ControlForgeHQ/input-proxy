# Engineering Principles

This document describes the engineering principles that guide the implementation of `input-proxy`.

These principles apply to all contributions, regardless of whether they are written by a human or generated with development tools.

When a tradeoff must be made, preserving these principles is generally more important than minimizing code size or implementation effort.


## Preserve architecture

The documented architecture is the primary design authority for the project.

Implementations should conform to the documented architecture rather than evolving it implicitly.

If an implementation appears to require architectural changes, those changes should be proposed and reviewed before modifying the implementation.

Avoid introducing new abstractions, responsibilities, or dependencies without a clear architectural justification.

When in doubt:

- preserve the existing architecture;
- prefer consistency over cleverness;
- make the smallest change that correctly solves the problem.


## Make ownership explicit

Ownership of every resource must be clear.

Resources include, but are not limited to:

- dynamically allocated memory;
- file descriptors;
- libevdev objects;
- uinput objects;
- strings;
- buffers.

For every owned resource, it should be obvious:

- who creates it;
- who owns it;
- who may use it;
- who destroys it.

Avoid shared ownership whenever practical.

Prefer transferring ownership explicitly over relying on undocumented lifetime assumptions.

A reader should be able to determine the lifetime of a resource without tracing the entire program.


## Keep modules focused

Each module should have one primary responsibility.

A module should be understandable in isolation without requiring knowledge of unrelated parts of the system.

When functionality begins to span multiple unrelated concerns, consider introducing a new module rather than expanding an existing one.

Modules should communicate through well-defined public interfaces.

Avoid exposing implementation details through public headers.

Keep third-party libraries, platform-specific APIs, and implementation-specific data structures behind module boundaries whenever practical.


## Optimize for readability

Code is read far more often than it is written.

Prefer code that is immediately understandable over code that is merely concise.

Favour explicit control flow over clever implementations.

Avoid surprising behaviour, hidden side effects, and unnecessary abstraction.

A small amount of duplication is often preferable to introducing an abstraction before its value has been demonstrated.

Comments should explain intent rather than restating the code.

When reviewing a change, ask:

- Is the code easier to understand?
- Is the ownership obvious?
- Is the control flow obvious?
- Would a contributor unfamiliar with this module understand it after a single read?


## Commit messages

This project follows the spirit of the Conventional Commits specification.

Commit messages should describe the primary purpose of the change rather than every individual modification contained within it.

Preferred prefixes include:

| Prefix | Purpose |
|--------|---------|
| `feat:` | New functionality |
| `fix:` | Bug fixes |
| `refactor:` | Internal restructuring without changing behaviour |
| `perf:` | Performance improvements |
| `test:` | Tests and test infrastructure |
| `docs:` | Documentation changes |
| `build:` | Build system, compiler, dependencies, packaging |
| `ci:` | Continuous integration and GitHub Actions workflows |
| `chore:` | Repository maintenance, tooling, templates, and other non-functional changes |

When a change reasonably fits more than one category, choose the prefix that best describes the primary purpose of the commit.
