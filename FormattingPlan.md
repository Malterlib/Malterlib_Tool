# MTool Malterlib formatting implementation plan

## Objective

Add `MTool Format` for whole files, selected ranges, and wildcard-selected files. Resolve `.editorconfig` before processing each file, and use an explicit custom property to opt files into the Malterlib code standard. Share the formatting engine with the existing `MTool Validate` command so automatic fixes and formatting diagnostics cannot develop separate interpretations of the rules.

Implement Format as a `CDistributedTool`, following Validate's registration and asynchronous command structure. Process independent files concurrently on the local machine with bounded resource usage. Remote services are outside the current scope. Use `/opt/Source/Malterlib8` for all real-source reformatting experiments during implementation; develop the implementation in the current checkout. This document is a plan, not authorization to mass-format either checkout now.

## Existing implementation and integration points

- [Validate implementation](Apps/MTool/Source/Malterlib_Tool_App_MTool_Validate.cpp): `CTool_Validate`, `fg_ValidateRepository`, and `fg_ValidateChanges` provide command registration, repository selection, configuration resolution, and diagnostics. Currently only `max_line_length` is enforced, and file loops are sequential.
- [MTool documentation](Apps/MTool/README.md): records existing audit, staged, and base-comparison behavior, exclusions, column counting, and test setup. Preserve these contracts.
- [EditorConfig API](../Develop/Source/Malterlib_Develop_EditorConfig.h) and [documentation](../Develop/Documentation/EditorConfig.md): reusable parser and actor resolver already support custom properties, inheritance, `unset`, boundaries, immutable snapshot loaders, and coalesced configuration reads. No second configuration parser is needed.
- [Distributed tool implementation](../Concurrency/Source/DistributedApp/Malterlib_Concurrency_DistributedTool.cpp): discovers tools through runtime class registration. Deriving from `CDistributedTool` supplies command integration; it does not itself parallelize the command's work.
- [Validation tests](Test/Test_Malterlib_Tool_Validation.cpp) and [fixtures](Test/Malterlib_Tool_TestHelpers.h): reuse the deployed test MTool, isolated repositories, temporary state, and Git configuration.
- [Core coding instructions](../Core/CLAUDE.md) define the current standard. [Older formatting documentation](../Core/Documentation/Malterlib_Core_CodeStandard_Formatting.dox) and [formatting examples](../Core/Test/Test_Malterlib_Core_CodeFormatting.cpp) provide additional cases, but are not automatically authoritative golden output. For example, older documentation requires removing single-statement braces, while current instructions say they are not required and require them for split conditions.

## Proposed user interface

The following syntax is proposed and must be implemented using the existing command specification API:

```bash
MTool Format --file Source/Example.cpp
MTool Format --file Source/Example.cpp --lines 40:75
MTool Format --file Source/Example.cpp --offset 120 --length 48
MTool Format -C /opt/Source/Malterlib8 --pattern 'Malterlib/Concurrency/Source/*.cpp' --recursive
MTool Format -C /opt/Source/Malterlib8 --pattern 'Malterlib/Concurrency/Source/*.cpp' --recursive --check
MTool Format --file Source/Example.cpp --lines 40:75 --diff
MTool Format --file Source/Example.cpp --lines 40:75 --strict-range
```

- `--file` selects an explicit file; permit repeated file and pattern options for whole-file batches. Relative paths resolve against `-C`, defaulting to the caller's current directory.
- `--pattern` enumerates regular files with the existing File module wildcard facilities; `--recursive` enables descendant traversal. Document the actual file-enumeration glob semantics separately from EditorConfig section patterns. Quote patterns so MTool receives them unchanged.
- Require an input selector; an empty invocation must not recursively rewrite the current directory. No matches is an input error; matching files that are all excluded is a successful no-op with an explicit summary.
- Format writes in place by default. `--check` checks without writing; `--diff` prints the proposed patch without writing. Reject combining these two modes.
- `--jobs N` sets the maximum concurrent file jobs; default to a bounded host-capacity value. `--jobs 1` uses the same code path for deterministic comparisons.
- Range options require exactly one explicit file and cannot be combined with patterns or multiple files. `--lines` uses one-based inclusive source lines. `--offset` and `--length` use zero-based byte offsets in the original on-disk contents and must occur together. Reject combining line and byte ranges, overflow, reversed ranges, out-of-bounds offsets, and boundaries inside encoded code points or CRLF pairs.
- A zero-length byte range formats the syntactic unit at the cursor; EOF selects the preceding unit, and an empty file has an empty selection. Publish this behavior for editor integrations.
- Exit 0 means successful formatting or no violations in check mode. Exit 1 means check-mode violations or unresolved formatting violations after a write. Use a distinct nonzero operational-error status, proposed as 2, for Format input, configuration, parse, I/O, or worker failures. Preserve Validate's existing externally visible exit behavior.
- Summaries distinguish selected, excluded, unchanged, changed/would-change, unresolved, and failed files. Diagnostics use original file coordinates and stable rule identifiers.

## EditorConfig extension and file selection

Introduce the consumer-owned property `malterlib_format = malterlib`. Accept `off` to disable formatting; generic `unset` removes the inherited property. A missing property also disables Malterlib formatting. Normalize this property's values in the formatting settings consumer, leaving the generic resolver's preservation of custom values unchanged. Reject unknown nonempty values rather than silently treating a typo as disabled.

```editorconfig
root = true

[*]
indent_style = tab
indent_size = 4
tab_width = 4
max_line_length = 190

[*.{c,cc,cpp,cxx,h,hh,hpp,hxx}]
malterlib_format = malterlib

[**/{ImportCache,IC}/**]
malterlib_format = unset
indent_style = unset
indent_size = unset
tab_width = unset
max_line_length = unset

[Vendor/**]
malterlib_format = off
```

The extension selects a formatter profile, not a language. Initially support C and C++ source/header files, including `.imp.h` and extensionless public include wrappers under configured `Include/Mib/**` sections. Add explicit language selection for ambiguous extensions only if required by the corpus. Other languages, including the MTool build DSL, need their own syntax backend before they can be opted in; do not apply C++ rewriting to `.MHeader`, Markdown, JSON, scripts, or arbitrary text. Existing generic line-length validation continues to work for those files.

Profile defaults are tabs, four-column tab stops, and 190 columns. Explicit resolved standard properties take precedence for supported per-file exceptions: indentation style/size, tab width, and `max_line_length` (including `off`). Honor `end_of_line`, `insert_final_newline`, and `trim_trailing_whitespace` when present and safe; otherwise preserve the file's representation. Validate values in one settings constructor. Treat `charset` as an encoding constraint and report unsupported/mismatched encodings rather than silently transcoding. Specify the resulting profile plus overrides in documentation.

Selection sequence:

1. Enumerate requested paths and deduplicate by normalized filesystem identity, including overlapping wildcard results. Skip symlinks and avoid directory-symlink traversal; reject ambiguous hard-linked write targets or implement an explicit identity policy.
2. Resolve effective properties before reading source contents. Both explicit files and wildcard files must obey the opt-in. An explicitly excluded file produces a clear skipped result.
3. Use the containing repository root as the discovery boundary, matching Validate. For files outside Git, use the explicit invocation root; document and reject selections outside that root unless a suitable root is supplied. A workspace-spanning pattern resolves each nested repository independently.
4. Exclude binaries, generated/vendor paths disabled by configuration, and unsupported file kinds. A supported-language file opted in but unsafe to parse is a reported failure, not an invisible exclusion.

Audit every existing exclusion section when introducing the property: clearing `max_line_length` does not disable a separate formatter property. Update templates and selected module configurations only after corpus trials pass, preserving intentional source-specific exceptions. Enabling the property also enables formatting checks in managed Validate pre-commit hooks, so configuration rollout must follow code cleanup or introduce only clean scopes.

## Shared engine and module boundaries

Place the reusable engine in `Malterlib/Develop`, alongside EditorConfig, exposed through `<Mib/Develop/CodeFormatting>` in `NMib::NDevelop`. MTool already depends on `Lib_Malterlib_Develop`. Keep Git, command-line output, scheduling, and writes in Tool. Keep EditorConfig parsing independent of formatter rules.

Proposed public concepts:

| Type | Responsibility |
| --- | --- |
| `CCodeFormattingSettings` | Validated profile and resolved property overrides. |
| `CCodeFormattingRequest` | Immutable source bytes, language, original-coordinate selections, range policy, and settings. |
| `CCodeFormattingEdit` | Original byte interval, replacement bytes, rule ID, and diagnostic anchor. |
| `CCodeFormattingDiagnostic` | Rule, severity, original span, explanation, and automatic-fix availability. |
| `CCodeFormattingResult` | Ordered non-overlapping edits, requested/effective ranges, diagnostics, and explicit completion status. |

Use a pure analysis operation such as `fg_AnalyzeCodeFormatting(request)` to produce a canonical edit plan and residual diagnostics. A separate shared helper validates and applies that plan to an in-memory buffer. Format applies it; Validate reports it without writing. Do not implement a parallel set of regular-expression checks for the same layout rules.

Extract current column measurement into a reusable text-layout utility before extending it. Preserve Validate's tab stops, Unicode accounting, leading BOM handling, LF/CRLF/CR mapping, malformed-byte accounting, and embedded-NUL behavior. Formatting must never use Validate's NUL-to-space normalization on data it will write; unsupported/malformed source bytes remain untouched with a diagnostic.

Separate independent generic text rules from Malterlib syntax rules. Generic line-length checks remain active when only `max_line_length` is configured. Prevent duplicate reports when the syntax engine and generic validator describe the same overlong line. After a format write, unresolved constraints, such as an indivisible overlong string literal, must remain visible.

Follow framework declarations in `.h`, included implementations in `.hpp`, other implementations in `.cpp`, tabs, ownership conventions, and actor lifetime requirements. Introduce no filesystem operations inside the pure engine.

## Syntax and rule implementation

Build a lossless token/trivia representation with structural context and a deterministic layout pass. First implement a bounded feasibility prototype for representative C++ constructs; confirm it can classify declarations, expressions, templates, lambdas, and preprocessor boundaries without requiring a successful project build. If the required structural accuracy cannot be achieved, evaluate the repository's existing parser/toolchain facilities before committing to a parser dependency. Freeze this choice before bulk editing; do not assume a stock formatter configuration can express every Malterlib rule.

Tokenization must preserve comments, raw and escaped literals, user-defined literals, preprocessor directives and continuations, conditional branches, and source offsets. Support modern constructs used here: concepts/requires clauses, attributes, coroutines, template packs/folds, overloaded operators, and Malterlib's formatting/actor DSL expressions. Formatting a header must work without a compilation database. Analyze all conditional branches conservatively; an uncertain construct must produce an explicit unsupported result instead of guessed edits.

Implement a documented rule matrix with golden before/after cases:

| Rule group | Required behavior |
| --- | --- |
| Indentation and width | Tabs/default width, scope depth, continuation depth, configurable column limit, deterministic wrapping. |
| Operators and commas | Distinguish binary operators from unary dereference and declarators; preserve token meaning; use standard operator/member-access spacing and leading commas in split lists. |
| Blocks and conditions | Opening braces on separate aligned lines, keyword spacing, braces required for multiline conditions; retain otherwise-legal single-statement braces. |
| Split statements | Logical units on separate lines, matching parentheses aligned, standalone terminal semicolon, exception for folds ending in `, ...);`. |
| Continuations | Extra indentation only when the expression may continue; initial `(` stays at statement indentation; nested split calls avoid redundant continuation indentation. |
| Declarations | Full parameter lists kept together or wholly split; convert to trailing return syntax when required to split an overlong function declaration/definition. |
| Lambdas | Assigned lambda braces at continuation depth and terminal semicolon at statement depth. |
| Logical spacing | Blank lines around recognizable setup/check/operation/assert/cleanup/return groups; spacing after local `using namespace`; compact return-only switch cases and separation before multi-statement case `break`. |
| Comments | Preserve text, attachment, license banners, and protected formatting; adjust safe surrounding layout without rewriting prose. |

Some rules change tokens, not just whitespace: adding required braces and converting return-type syntax need structural transformations. Isolate those operations, prove their preconditions, and validate the result; preserve dangling-else binding, variable scope, attributes, constraints, qualifiers, and return-type name lookup. Ambiguous macro-expanded declarations and unsafe transformations get diagnostic-only results. Do not split literal contents merely to satisfy the column limit.

Recognizing semantic logical groups is not always deterministic. Automate structural cases such as terminal returns and case breaks, preserve meaningful existing group boundaries, and document cases requiring human judgment. Naming, replacing `class` with `struct`, ownership changes, moving implementations between files, and evaluating comment usefulness are separate refactoring/review concerns. Do not claim full semantic code-standard enforcement from a formatter.

Define protected macro regions and optional paired `// malterlib-format off` / `// malterlib-format on` directives. Unmatched directives are errors. Verify token-stream equivalence for whitespace-only changes, and use dedicated structural equivalence tests for approved token-changing rules. Run layout to a stable canonical result with a bounded convergence check; instability is a formatter failure.

## Range formatting contract

Always inspect the complete file for lexical and structural context. Never format an isolated substring that might begin inside a comment, literal, template, or nested statement.

- Map every requested range to original byte coordinates before any edit. Normalize and merge multiple internal ranges.
- By default expand a selection only to the smallest complete formatting units required for a stable result: statement, declaration, enclosing construct, or associated whitespace. Report the effective ranges in the result and CLI output whenever expansion occurs.
- `--strict-range` forbids modifications outside the requested range. If a necessary edit crosses its boundary, return an unresolved range diagnostic and apply no edits to that unit. Do not truncate replacements or silently format the whole file.
- Limit file-wide normalization, such as line-ending conversion or final-newline insertion, to whole-file requests or selections that legitimately cover the affected boundary. Bytes outside effective ranges must be identical after application.
- Treat directives, multiline comments/literals, and macro bodies as indivisible/protected where necessary. Reject unsafe selection expansion across them with a useful diagnostic.
- Anchor every edit and diagnostic to original source positions. Coalesce related edits by formatting unit, validate non-overlap, then apply from highest byte offset downward or through a single reconstruction pass.

Changed-line Validate uses a separate reporting mask: parse full snapshot contents and compute canonical edits with context, but report only violations whose original affected span intersects added/modified lines. For whitespace insertions use a documented adjacent-token anchor, not an entire enclosing function span. Unchanged surrounding violations remain suppressed. If the fix would extend outside changed lines, identify its complete suggested range; Validate still performs no edits. Deletion-only hunks retain the existing no-added-lines behavior.

## Distributed command and local parallel execution

Add `CTool_Format : CDistributedTool` in `Apps/MTool/Source/Malterlib_Tool_App_MTool_Format.cpp`, mirroring Validate's registration guards, `Validation` category, coroutine error capture, and `DMibRuntimeClass` registration. Do not route it through the synchronous legacy `CTool` wrapper.

Use a coordinator plus a bounded pool of worker actors. Each work item contains an immutable file snapshot, settings, language, ranges, and source identity. Workers compute edits without writing or emitting interleaved console output. The coordinator owns result ordering and applies each file's validated result once. Files run concurrently; formatting passes within a file run in a defined order. Bound both concurrent jobs and bytes in flight, with cancellation and backpressure.

Dispatch reads, snapshot materialization, and writes through `fg_BlockingActor`. Run CPU formatting across worker actors so one actor does not serialize the workload. Pass coroutine arguments by value, observe futures, and use `fg_AsyncDestroy` for owned actors. Keep snapshots and temporary resources alive until all dependent jobs finish or cancel.

Share this coordinator/job model with Validate. Snapshot preparation and the existing repository lock remain coordinated. The current changed-line implementation reuses one temporary `patch` filename: give each parallel job a unique patch/blob path or capture immutable buffers before dispatch. Avoid mutable shared per-file maps across suspensions.

## Safe file application

Read bytes once and record source identity/digest, encoding/BOM, line endings, and permissions. Initially support validated UTF-8 with optional BOM, including ASCII; report other source encodings as unsupported without modifying them. Keep Validate's broader existing text validation behavior intact.

Before replacing a file, verify it still matches the analyzed snapshot and remains an eligible regular file. Stage output beside the destination and use platform file abstractions for an atomic per-file replacement, preserving permissions and cleaning up temporary files on failure. An atomic rename alone does not close concurrent-editor races: investigate available locking/identity primitives and document any remaining optimistic-concurrency limitation. Never overwrite a detected intervening edit. Do not rewrite unchanged files or alter their timestamps.

A batch is not a transaction across all files. Report per-file outcomes and partial completion accurately; cancellation stops new jobs and prevents unfinished results from being written. Never let retried worker analysis cause duplicate writes.

## Validate integration

1. Refactor file selection, immutable source acquisition, resolved settings, and reporting out of line-only loops without changing existing behavior.
2. Select a file when any applicable validator is enabled. Remove the assumption that missing `max_line_length` means every validation rule is disabled.
3. In audit mode, analyze tracked working-tree text with working-tree configuration and the existing repository boundary. Preserve untracked/configuration behavior and binary exclusions.
4. In staged mode, acquire complete source blobs from the selected index snapshot and configuration from that same snapshot. Preserve alternate indexes, initial commits, split indexes, renames, attributes, and unmerged-index rejection.
5. In base mode, use complete HEAD blobs, HEAD configuration/attributes, and the existing merge-base comparison. Do not consult working-tree contents for formatting context.
6. Preserve exact blob bytes during capture; use binary/file-backed process output so leading BOMs, CR-only lines, and NUL boundaries are not lost. Reuse the existing source-line mapping logic with regression coverage.
7. Feed full snapshots to the shared engine with the changed-line reporting mask. Retain generic line-length diagnostics when Malterlib formatting is disabled, and distinguish syntax-analysis failures from style violations.
8. Extend summaries and help text for rule categories while retaining compatible existing output for line-length-only cases where practical. Validate remains read-only with respect to source, index, and tracked configuration.

## Implementation sequence and acceptance gates

1. **Behavior specification and corpus.** Turn the current standard into golden cases, settle syntax classifications and range anchors, and document automated versus diagnostic-only rules. Record clean/dirty state of each relevant Malterlib8 sub-repository. Begin with Concurrency, which has already been reviewed for formatting correctness, and select representative C/C++ files plus exclusion cases. Use its existing formatting to establish expected unchanged output and investigate proposed differences before accepting golden cases. No corpus rewrites yet.
2. **Shared settings and text foundations.** Add the custom property consumer, coordinate/line map, edit/result types, and reusable column checks. Prove that existing Validate tests still pass with formatting disabled. Test configuration inheritance, nested roots, overrides, and exclusions.
3. **Syntax feasibility and engine.** Complete the parser prototype, then implement the rule matrix in focused increments. Require golden output, token/structural safety checks, explicit unsupported cases, and idempotence before enabling writes.
4. **Range support.** Implement expansion, strict ranges, cursor handling, original-coordinate edits, and changed-line masks. Require byte-for-byte preservation outside effective ranges and range idempotence with updated coordinates.
5. **Format command and local workers.** Add selectors, dry-run modes, bounded scheduling, deterministic diagnostics, safe writes, and failure/cancellation handling. Compare `--jobs 1` with multiple jobs for identical output and prove actual overlap with controlled worker fixtures.
6. **Validate integration.** Add syntax checks in all three existing modes, immutable full-file blob acquisition, and parallel jobs with unique temporary paths. Require compatibility tests for partially staged source/configuration and unchanged-context suppression.
7. **Malterlib8 trials and rollout.** Exercise read-only diffs first, then selected file writes and wildcard batches. Run compiler/test checks, inspect changes, repeat formatting to prove zero further edits, and request user review of the stable Concurrency changes. Incorporate feedback before expanding trials to other modules, then introduce opt-in sections and document usage. Expand supported languages only through their own parser/rule milestones.

Likely files: new Develop public wrapper and `Malterlib_Develop_CodeFormatting*.h/.cpp` files; new Format command and shared MTool processing helpers; changes to Validate; Develop unit tests and Tool integration tests; documentation in both modules. Existing source globs include direct `Source/*` files; update MHeader groups if implementation moves into subdirectories. No new dependency direction from Develop to Tool or Git.

## Verification and Malterlib8 trial procedure

Read `Malterlib/Test/CLAUDE.md` before implementing tests. Use native tests with isolated fixtures for:

- Every rule and its exceptions: templates, nested calls, multiline declarations, trailing returns, lambdas, folds, switch cases, coroutines, comments, macros, raw literals, and conditional compilation.
- Idempotence, deterministic results, unchanged compliant input, stable diagnostics, and no token changes outside approved structural transformations.
- Range endpoints, empty input, cursor/EOF, UTF-8 and BOM offsets, tabs, CRLF/CR/LF, multiple internal ranges, expansion and strict behavior, and exact preservation outside effective ranges.
- EditorConfig inheritance, `off`/`unset`, invalid values, wildcard intersections, duplicates, repository boundaries, unsupported languages, and generated/vendor exclusions.
- All existing staged/base edge cases, plus formatting enabled while line length is unset, full context differing from the working tree, and changes inside multiline constructs.
- Parallel result equivalence, bounded resources, overlapping requests, read/write failures, changed source, cancellation, worker failures, and temporary-file cleanup.

Build the relevant tests together from the implementation workspace, using one build invocation:

```bash
set -o pipefail
MalterlibBuildShowProgress=false ./mib build-target Tests Com_Test_Malterlib_Develop,Com_Test_Malterlib_Tool
/opt/Deploy/Tests8/RunAllTests --paths '["Malterlib/Develop/*", "Malterlib/Tool/*"]'
```

The test deployment directory is `/opt/Deploy/Tests8`. Rebuild `Com_RunAllTests` if its generated list needs updating. Judge success by exit code. Do not invoke Ninja directly or overlap builds of the same configuration.

Building the `MTool` workspace deploys to `/opt/Source/Malterlib7/Binaries/Malterlib/macOS/arm64`, separately from the Tests deployment:

```bash
set -o pipefail
MalterlibBuildShowProgress=false ./mib build MTool
/opt/Source/Malterlib7/Binaries/Malterlib/macOS/arm64/MTool Format -C /opt/Source/Malterlib8 --pattern 'Malterlib/Concurrency/Source/*.cpp' --recursive --diff
```

For real-source trials, invoke the newly built MTool by its explicit deployment path (the MTool workspace destination above, or the test MTool under `/opt/Deploy/Tests8`) so a bootstrap binary cannot be mistaken for the implementation. Point every writable formatting command at `/opt/Source/Malterlib8`; preserve the configured deployment destinations. Keep the opt-in trial configurations inside Malterlib8.

Start with selected files in `/opt/Source/Malterlib8/Malterlib/Concurrency`, then a Concurrency-wide wildcard trial. This module has already been reviewed for formatting correctness and should have fewer issues, making it the initial baseline for detecting false positives and unnecessary rewrites. Review each proposed difference against the current standard; prior review does not imply every file is guaranteed compliant. Once Concurrency results are understood and stable, request user review as described below. After incorporating that feedback, expand to Core, String, and Container files covering the remaining rule matrix, followed by module-sized wildcard selections. Record per-sub-repository status and diffs before and after, preserving existing user changes. Build affected targets from Malterlib8 with its `./mib`, then run relevant tests; schedule builds sequentially if the checkouts share artifact paths. Repeat Format and require zero edits, and run Validate against the same complete files to verify no remaining fixable formatting violations. Record unsupported/unfixable cases explicitly rather than reporting full conformance.

**Concurrency user-review checkpoint.** When the Concurrency trial has a reviewed diff, passing relevant build/tests, repeat formatting produces no further edits, and Validate reports no remaining fixable formatting violations, ask the user to review the changes and provide feedback. Present the concrete diff in `/opt/Source/Malterlib8/Malterlib/Concurrency`, summarize the formatting decisions and verification results, and identify any unresolved cases. Wait for the user's feedback before expanding formatting trials to other modules; incorporate it into the rules and regression cases, then rerun the affected checks.

Completion requires working single-file/range/wildcard formatting, configuration filtering, shared Validate results in every mode, demonstrated local parallel execution, safe failure behavior, documented limitations, and reviewed Malterlib8 trial results. Bulk formatting of other checkouts is outside this implementation plan.
