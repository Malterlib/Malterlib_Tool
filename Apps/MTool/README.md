# Source validation

EditorConfig parsing, pattern matching, and property resolution are provided by
`Malterlib/Develop` through `<Mib/Develop/EditorConfig>`. The formatting engine
is provided by the same module through `<Mib/Develop/CodeFormatting>`. MTool
supplies repository boundaries and Git snapshot contents, schedules the work,
and implements diagnostics, patches, and safe writes. `MTool Format` and
`MTool Validate` share one engine, so an automatic fix and a reported violation
cannot develop separate interpretations of the rules.

`MTool Validate`, in the Validation command group, audits all tracked text files
in the current Git repository,
using working-tree `.editorconfig` files. Use `-C <repository>` to select a
different repository. Untracked files are excluded, but untracked configuration
files are applied, so rules can be audited before staging them.

```bash
MTool Validate
MTool Validate -C Malterlib/Core
MTool Validate --staged
MTool Validate --base origin/master
```

`--staged` checks only added or modified lines that will be committed, using
`.editorconfig` blobs from the index. It preserves `GIT_INDEX_FILE`, supports
initial commits, and ignores unchanged lines in renamed files. Unstaged changes
to either content or configuration do not affect this check.

`--base <reference>` checks added or modified lines in committed changes from
the merge base of that reference and `HEAD` to `HEAD`. It uses `.editorconfig`
files and Git attributes committed at `HEAD`, so staged and working-tree edits do not affect the
result. Changes made only on the base branch after divergence are not counted.
Attribute files are materialized under `MToolValidate` in the repository's Git
directory so this also works with older Git versions that ignore `GIT_ATTR_SOURCE`.
That directory is removed during coroutine cleanup; `MToolValidate.lock` serializes
concurrent checks in the same repository. The committing index is preserved.
Both changed-line modes use private Git metadata and snapshot attributes, ignoring
unstaged attributes, `.git/info/attributes`, and global/system attribute files.
Staged mode copies the selected index, including split-index data; temporary tree
objects stay in the private object directory and are removed with the snapshot.
The reference can be a local or remote-tracking branch, tag, or commit.
References are resolved locally; this command does not fetch from a remote.
An invalid reference, missing `HEAD`, or absent/ambiguous merge base reports an
error. `--base` and `--staged` cannot be combined.

`MTool Validate` enforces `max_line_length` for every selected file, and the
Malterlib formatting rules for files that opt in with `malterlib_format`. A file
is selected when any validator applies to it, so formatting can be checked in a
scope that has no line-length limit. Validate never writes; it reports the edit
plan the shared engine would apply.

Formatting diagnostics are written as `path:line:column: rule: explanation`.
Line-length-only files keep the existing `path:line: line length ...` message
and the original summary line, so output for scopes that have not opted in is
unchanged. Opted-in files report line length through the engine instead, which
avoids describing the same overlong line twice.

Changed-line modes reassemble each file's complete snapshot from its
full-context patch, analyze the whole file, and then report only violations on
added or modified lines. A file whose snapshot cannot be reassembled, including
one containing NUL bytes, is reported as unanalyzable rather than analyzed from
normalized content. Deletion-only hunks keep the existing no-added-lines
behavior. Each file gets its own patch file so concurrent jobs cannot collide.

The line-length validator enforces `max_line_length`. Diagnostics include the absolute file path,
line number, measured columns, and limit. Tabs advance to the next `tab_width`
tab stop (falling back to `indent_size`, then 4). Other Unicode code points count
as one column, including embedded NULs in files explicitly marked as text.
The file-leading BOM and line endings do not count. LF, CRLF, and CR source
lines have consistent lengths and diagnostic line numbers in every mode.
Changed-line checks use full patch context to map Git's LF-based hunks back to
source lines; unchanged context is never validated. Patches stay in the existing
temporary validation directory so process text buffering cannot discard NULs.
Binary files, symlinks,
submodules, and deleted files are excluded.

Audit, staged, and base-comparison summaries include elapsed seconds, the number of files
checked, and the number excluded by configuration. Audit mode resolves rules
before reading file contents, then asks Git to select text files only from the
enabled paths. Changed-line modes also avoid generating patches for excluded files.

Settings are resolved from the repository root down to each file. Later matching
sections override earlier sections; nested configurations override parents;
`root = true` resets inherited settings. Configuration outside the repository
is excluded so validation is reproducible. Property names and relevant values
are case insensitive, while path matching is case sensitive. Custom property
values retain their original spelling; `unset` removes any inherited property.

Supported section patterns include `*`, `**`, `?`, character sets and ranges
(including negation, such as `[!0-9]`), escaped characters, and brace alternatives
such as `*.{cpp,h}`. Numeric brace ranges
are currently rejected. Use `max_line_length = off` or `unset` for an exception;
a missing limit disables the line-length validator, unless `malterlib_format`
selects the Malterlib profile, whose default limit is 190. Invalid limits fail
validation. Other properties are retained for future validators but are not
enforced.

No repository configuration enables `malterlib_format` yet, so the templates and
module configurations above are unchanged and every existing scope keeps its
current behavior.

Patterns are compiled once per configuration section and matched as Unicode
characters. Brace alternatives retain their token boundaries; repeated
wildcards do not cause exponential backtracking. Section patterns may contain
up to 1024 Unicode characters.

```editorconfig
root = true

[*]
indent_style = tab
indent_size = 4
tab_width = 4
max_line_length = 190

[*.md]
max_line_length = unset

[**/{ImportCache,IC}/**]
indent_style = unset
indent_size = unset
tab_width = unset
max_line_length = unset

[**/{ImportCache,IC}/**.MHeader]
indent_style = tab
indent_size = 4
tab_width = 4
```

Malterlib installs the `--staged` check as a managed pre-commit hook in the root
and source-module repositories when `MalterlibInstallGitHookHelpers` is enabled.
Binary distribution and external repositories are excluded. The hook runs the
workspace's local MTool binary, which is updated by building the MTool workspace.
New project initialization also copies the root `.editorconfig` template.
The source configurations unset every project formatting property in
`ImportCache` and `IC` directories at any depth, including the shortened Windows
cache paths. EditorConfig has no blanket property reset for a matching section;
if another project default is added, also unset it in the exclusion sections.
Cache `.MHeader` files restore the project indentation settings in a later
section, while inheriting the unset line-length limit.

All source repositories also unset project formatting for canonical license
texts, generated `REUSE.toml` metadata, patch/diff files, and npm lockfiles.
Targeted repository settings cover
generated `AGENTS.md`, BuildSystem parser fixtures, vendored NASM/LLVM/SQLite
files, Mach stubs, gperftools headers, and exported design assets. The embedded
font CSS only disables the line-length check; its other settings still apply.
Markdown files have no line-length limit, allowing wide tables and long prose;
their other formatting settings remain active. Doxygen documentation and
Malterlib-owned source remain checked.
YAML files, `.clangd`, and `.clang-format` use two-space indentation while
retaining the line-length limit. These format-specific defaults precede the
exclusions so generated/vendor files still have all project settings unset.

Pre-commit hooks are ordered by configurable properties, like
`MalterlibRootPostCheckoutHooks`:

| Property | Repositories | Default |
| --- | --- | --- |
| `MalterlibRootPreCommitHooks` | Workspace root | Source validation |
| `MalterlibSourcePreCommitHooks` | Core and source modules | Source validation |
| `MalterlibBinaryPreCommitHooks` | Binary distributions, including SDK and Qt | LFS content check |

Enabling `malterlib_format` also enables formatting checks in these hooks, so
roll the property out only after the affected sources are already clean.

Set a complete list in the project's root `.MBuildSystem` to control the order:

```text
Property
{
	MalterlibRootPreCommitHooks [
		"Tools/GitHooks/before-validation.sh"->MakeAbsolute()
		, "Malterlib/Core/Scripts/GitHooks/pre-commit-validate.sh"->MakeAbsolute()
		, "Tools/GitHooks/after-validation.sh"->MakeAbsolute()
	]
	MalterlibSourcePreCommitHooks MalterlibRootPreCommitHooks
}
```

An explicit empty list disables that category's pre-commit helpers. Defaults
apply only when the property is undefined. `MalterlibInstallGitHookHelpers`
continues to control installation of all managed helpers.

From the workspace root, `./mib validate` validates every repository whose
build-system entry sets `Repository.Format` in one run: the repositories are
validated at once on one pool of formatting workers, each repository's
diagnostics are written in repository order, and one summary covers them all.
It takes `--staged`, `--base`, and the repository filters, and exits like
`MTool Validate` does.

```bash
./mib validate
./mib validate --staged
./mib validate --base origin/master -n "Malterlib/S*"
```

`repo-run` still launches the single-repository tool per repository, with the
current mib's tool binaries on the path:

```bash
./mib repo-run -n "Malterlib/*" -- MTool Validate
```

To include local source checkouts that are not currently configured in mib:

```bash
set -o pipefail
Result=0
for Repo in . Malterlib/*; do
	[ -e "$Repo/.git" ] || continue
	printf '\nRepository: %s\n' "$Repo"
	MTool Validate -C "$Repo" || Result=1
done
exit "$Result"
```

The implementation separates file selection, configuration resolution, and line
validation. Additional rules can use the same input selection and resolved
properties in audit, staged, and base-comparison modes.

# Source formatting

`MTool Format`, in the Validation command group, formats sources that opt in
with `malterlib_format = malterlib` in `.editorconfig`. The rules, line
structure, protected regions, and range contract are documented in
[Malterlib/Develop](../../../Develop/Documentation/CodeFormatting.md). A split
construct that fits is brought back to one line; splitting an overlong construct
is not implemented yet, so an overlong line is reported and left alone.

```bash
MTool Format --file Source/Example.cpp
MTool Format --file Source/Example.cpp --lines 40:75
MTool Format --file Source/Example.cpp --offset 120 --length 48
MTool Format -C /path/to/project --pattern 'Malterlib/Concurrency/Source/*.cpp' --recursive
MTool Format -C /path/to/project --pattern 'Malterlib/Concurrency/Source/*.cpp' --recursive --check
MTool Format --file Source/Example.cpp --lines 40:75 --diff
MTool Format --file Source/Example.cpp --lines 40:75 --strict-range
```

From a workspace root, `./mib format` runs the same engine once over every
repository whose build-system entry sets `Repository.Format`: the repositories
are walked in parallel, a repository's files are queued for formatting as soon
as its walk is done, and the diagnostics and one summary cover them all, in
path order. It takes `--check`, `--diff`, `--jobs`, and the repository filters,
and exits like `MTool Format` does; its reported time includes finding the
repositories. The Malterlib root and module repositories
set the property; binary and external repositories do not.

```bash
./mib format --check
./mib format -n "Malterlib/Concurrency"
```

`--file` selects explicit files and `--pattern` enumerates regular files with
the File module's wildcard search, where only the last path component may
contain wildcards; `--recursive` enables descendant traversal. Both options take
one value or a comma-separated list, following the shared command-line
convention for list options; repeating an option replaces its earlier value.
Quote patterns so MTool receives them unchanged. Relative paths resolve against
`--working-directory` (`-C`), which defaults to the caller's current directory.
These file-search globs are not EditorConfig section patterns and do not share
their syntax.

An input selector is required: an empty invocation never rewrites the current
directory. No matches is an input error. Matching files that are all excluded
are a successful no-op with an explicit summary. Symbolic links are skipped,
directory symbolic links are not traversed, and overlapping selections are
deduplicated by normalized path.

A recursive pattern does not enter a directory git ignores, nor one below which
an EditorConfig section that covers every file sets `malterlib_format` to a
disabling value or `unset`, so a dependency's build output or an import cache is
never listed. The ignore rules are the repository's `.gitignore` files, its
`info/exclude`, and the file `core.excludesFile` names in the git configuration,
read from the system, global, repository, and worktree files with the precedence
git gives them and honoring `GIT_CONFIG_GLOBAL`, `GIT_CONFIG_SYSTEM`,
`GIT_CONFIG_NOSYSTEM`, and `XDG_CONFIG_HOME`; without a value the default
`~/.config/git/ignore` applies. Configuration `include` directives are not
followed. A directory the rules above it ignore is not entered even when it is
a repository of its own.

Format writes in place by default. `--check` reports without writing and
`--diff` prints the proposed unified patch to standard output without writing;
combining them is rejected. `--jobs N` bounds how many files are formatted
concurrently and defaults to a bounded host-capacity value; `--jobs 1` uses the
same code path. Selected files are processed in sorted path order and results
are reported in that order, so a parallel run and a single-job run produce
identical output.

Range options require exactly one selected file and cannot be combined with
patterns or several files. `--lines` uses one-based inclusive source lines.
`--offset` and `--length` use zero-based byte offsets in the original on-disk
contents and must occur together. Combining line and byte ranges, reversed or
out-of-bounds ranges, and boundaries inside an encoded code point or a CRLF pair
are all rejected. A zero-length byte range formats the unit at the cursor, and
at end of file the preceding unit. By default a selection expands to the whole
lines it touches; `--strict-range` never modifies bytes outside the request and
reports a `range-boundary` violation instead.

Each file's configuration is resolved from its own containing repository, so a
selection spanning nested repositories resolves every file independently. A file
outside the resolved root is rejected with a message suggesting `-C`.

Before replacing a file, Format verifies that it is still the regular file whose
bytes were analyzed, writes the result beside the destination, restores the
original attributes, and renames it into place. A file that changed on disk
during analysis is never overwritten. Unchanged files are not rewritten. A batch
is not a transaction: per-file outcomes are reported individually and partial
completion is reported accurately.

Exit status 0 means the files were formatted, or that check mode found no
violations. Exit status 1 means check-mode violations, or violations that
remain unresolved after a write, such as an indivisible overlong line. Exit
status 2 is reserved for operational errors: input, configuration, parse, I/O,
and worker failures. Validate's existing exit behavior is unchanged.

Summaries distinguish selected, unchanged, changed or would-change, unresolved,
and failed files, along with the number excluded by configuration.

## License checks

`./mib check-license` normalizes CRLF and CR line endings to LF when comparing
license files and generated `REUSE.toml` metadata. Line-ending differences alone
do not require a fix. Real text and whitespace differences are still reported.

`--fix` writes LF when creating or updating license files and generated metadata.
Matching files keep their existing line endings, and upstream source files are
not rewritten.

## Integration tests

Validation, formatting, license-check, and agent-generation tests use the Malterlib test framework in
`Malterlib/Tool/Test`. The test target builds MTool as a runtime dependency and
deploys it using the existing test-app layout at `Tests/TestApps/MTool`.
The tests locate it relative to their executable. Git must be available on
`PATH`. Each test uses a directory named after its test path, such as
`MToolTests/Validation/CLI/BaseHeadSnapshot`, beside the test executable, so the
suites support both direct execution in one process and parallel execution by
RunAllTests.

The fixture directory is reset before each test and registered with
`fg_TestAddCleanupPath`. Files remain available for local debugging; CI and
`MalterlibCleanupTestFiles=true` remove them using the normal test cleanup mechanism.

Git defaults are read from a fixture-local `gitconfig` through `GIT_CONFIG_GLOBAL`,
avoiding repeated configuration processes. Repository-specific settings still override these defaults.

Each fixture also directs child-process temporary files and `MToolRootDirectory`
into its own directory, keeping command-line trust databases and sockets separate
between parallel suites. MTool normally stores that state in `~/.Malterlib/MTool`;
the environment variable overrides this location on all platforms.

Build the tests from the workspace root:

```bash
MalterlibBuildShowProgress=false ./mib build-target Tests Com_Test_Malterlib_Tool
```

Run the deployed test executable directly, or select the suites in RunAllTests
(Windows deployment paths shown; use your configured Tests destination):

```bash
/c/Deploy/Tests/Test_Malterlib_Tool --test
/c/Deploy/Tests/RunAllTests --paths '["Malterlib/Tool/*"]'
```

Rebuild `Com_RunAllTests` after adding this target to an existing Tests workspace
so its generated test list includes the new executable.

The test dependency sets `MToolEmbedCMake false` and
`MToolBuildHelperTools false`. This omits embedded CMake and the Ninja, zstd,
and bsdtar helper executables. The normal MTool workspace keeps both settings
enabled. The shared libraries required by MTool remain linked.

When iterating locally, configure the Tests destination in
`BuildSystem/Default/PostCopy.MConfig`. Building the test target updates both
the test executable and its MTool dependency there. For example:

```text
Projects
{
	Tests
	{
		Destination "C:/Deploy/Tests"
	}
}
```

## Agent instructions

`./mib update-agents` expands Markdown includes from `CLAUDE.md` into `AGENTS.md`
in the current directory. It does not require a Git repository or load a build
system. Project initialization runs it after fetching the repositories.

```bash
./mib update-agents
./mib update-agents -C /path/to/project
./mib update-agents --input docs/CLAUDE.md --output docs/AGENTS.md
```

Paths in `--input` and `--output` are relative to `--current-directory` (`-C`).
Include paths are relative to the Markdown file containing them. Standalone
`@path` lines may have up to three backticks around them; inline bare and
single-backtick mentions are supported too.

References remain in the text with a “see below” annotation. Included content
follows the containing file, with standalone includes first, followed by inline
includes. Within a line, code-form mentions are collected before bare mentions.
Each file is expanded once; duplicates, cycles, and missing includes receive
HTML comment markers. A missing input file fails without replacing the output.
Generated files use UTF-8 without a BOM and LF line endings.
