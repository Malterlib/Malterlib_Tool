# Source validation

EditorConfig parsing, pattern matching, and property resolution are provided by
`Malterlib/Develop` through `<Mib/Develop/EditorConfig>`. MTool supplies repository
boundaries and Git snapshot contents, and implements validation and diagnostics.

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

The initial validator enforces `max_line_length`. Diagnostics include the absolute file path,
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
a missing limit disables this validator. Invalid limits fail validation.
Other properties are retained for future validators but are not enforced.

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

To audit configured source repositories from the workspace root, `repo-run`
makes the current mib's tool binaries available to the commands it launches:

```bash
./mib repo-run -n "Malterlib/*" -- MTool Validate
./mib repo-run -n "Malterlib/S*" -- MTool Validate --staged
./mib repo-run -n "Malterlib/*" -- MTool Validate --base origin/master
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

## Integration tests

Validation tests use the Malterlib test framework in
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
