# Debug hovers and feature scans

Enable developer features independently of the compiler build type:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSLANG_SERVER_DEBUG=ON
cmake --build build -j8 --target slang_server server_unittests
build/bin/server_unittests '[debug-scan]'
```

`SLANG_SERVER_DEBUG` defaults to `OFF`, including for `Debug` builds. When enabled,
unresolved hovers show token/syntax information, and the server advertises
`slang.debug.scanDocument` in `executeCommandProvider.commands`.

## Scan an open document

In VS Code, run **slang: Debug: Scan Current Document** from the Command Palette.
It appears only when the connected server supports debug scans, and is enabled
for Verilog/SystemVerilog files and headers. The command scans unsaved changes,
supports cancellation, and opens the Problems panel when it finds issues.

Send `workspace/executeCommand` with the active editor's document URI:

```json
{
  "command": "slang.debug.scanDocument",
  "arguments": [{"uri": "file:///path/to/design.sv"}]
}
```

For example, in Neovim with the document open:

```lua
vim.lsp.buf.execute_command({
  command = 'slang.debug.scanDocument',
  arguments = { { uri = vim.uri_from_bufnr(0) } },
})
```

The scan uses the current buffer, including unsaved changes, and its selected
include/design context. It publishes warnings alongside existing compiler
diagnostics with source `slang-server-debug` and codes `missing-hover-goto`,
`definition-name-mismatch`, and `missing-completion`. Missing hover and goto results
share one diagnostic per token; `data.missingFeatures` records which checks failed.
Rerunning replaces the previous
scan findings; edits, reanalysis, and closing the document clear them. Cancelled
scans leave the previous findings intact. The command returns the same findings
plus the URI, negotiated position encoding, and counts of identifiers,
declarations, references, and checks,
so callers need not collect diagnostic notifications.

The scan checks identifiers in active parsed code. It checks real hovers (synthetic
debug hovers do not count), source definition targets, and whether a reference's
expected spelling appears in completions. It also checks every resolved symbol and
declaration name against the source token. Ranged enum declarations can use a
prefix followed by a numeric suffix (for example, `ENTRY[2]` declares `ENTRY0` and
`ENTRY1`). Mismatches include the returned names in `data.targetNames`.
Declarations are not completion sites;
system subroutines and other built-ins without source are not definition sites.
Comments, strings, disabled preprocessor branches, and expansion-only tokens are
excluded. Macro completions use the full name prefix to avoid truncation of the
server's bounded macro list.

These are coverage candidates, not proof of a server bug. The scan recognizes
common reference syntax even when lookup fails, but matching names do not prove
that targets belong to the correct scope. It does not validate hover contents,
completion resolution, or cover
every possible declaration and incomplete-edit context.

## Scan a repository or folder for an agent

The debug-enabled C++ server has a native batch mode. It loads the workspace once,
opens each source file internally, and calls the same C++ scanner used by the
editor command. No Python process or LSP transport is involved:

```sh
build/bin/slang-server --debug-scan /path/to/repo/rtl --workspace /path/to/repo \
  --output /tmp/scan.json
```

The scanner walks each document's shallow token index and uses its syntax and
resolved declaration locations to distinguish declarations from references.
Only references are completion sites. Diagnostic data and groups include a
`role` field (`declaration` or `reference`). The native runner preserves source
bytes, reports progress on stderr, and continues after individual file errors.

The Python 3.9+ runner remains available for exercising the LSP command, with
workspace-relative exclusion globs and per-request timeouts:

```sh
scripts/debug-scan.py /path/to/repo/rtl --workspace /path/to/repo \
  --server build/bin/slang-server --exclude 'rtl/generated' --output /tmp/scan.json
```

The target can be a single file or a folder. The workspace defaults to the target
folder (or the file's parent); specify the repository root to use its usual
`.slang/server.json`, include paths, defines, indexing, and build configuration.
Neither runner changes that configuration. Directory scans include `.sv`, `.svh`,
`.v`, and `.vh` files and skip `.git` and symlinks. Scan file selection is independent
of index exclusions. The Python runner additionally accepts repeatable
workspace-relative `--exclude` globs and a `--timeout` per-request limit in seconds.

Both runners produce JSON containing:

- `documents`: every finding, compiler diagnostics, URI, path, and check counts.
- `groups`: issues grouped by code, syntax kind, completion query, trigger, and
  whether definition lookup resolved. Each occurrence points into `documents`.
- `summary`: file/issue/group counts and whether the scan completed.
- `errors` and `messages`: runner failures and server configuration messages.

LSP ranges are zero-based UTF-8 byte positions. Group occurrence line/column
numbers are one-based. A compact group summary goes to stderr. Exit status is
`0` for no findings, `1` for findings, or `2` for an incomplete scan or runner error.
On failure, already completed files remain in the report together with the failing
path. The LSP runner also includes a tail of the server log. Reports may contain
project identifiers and paths.

An agent working on slang-server should:

1. Read `summary`, `errors`, `messages`, and compiler diagnostics first. Correct
   missing configuration before treating unresolved references as server defects.
2. Pick a group and inspect a representative occurrence in the source. Use its
   `diagnostic.data` (`token`, `syntaxKind`, `resolved`, `completionKind`, `trigger`,
   `completionCount`) to reproduce the failed request.
3. Reduce it to a minimal SystemVerilog example with generic names before adding a
   regression test to slang-server. Do not copy proprietary source into this repo.
4. Fix the corresponding lookup, hover, or completion path, run the regression and
   server tests, rebuild `slang_server`, then rerun the same scan to check the group.

The runner creates a triage report; it does not modify source or file remote issues.
