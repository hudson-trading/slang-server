# AGENTS.md

SystemVerilog LSP server in C++20, built on the [Slang](https://github.com/MikePopoloski/slang) library.
Also includes a VS Code extension (`clients/vscode/`, TypeScript) and Neovim plugin (`clients/neovim/`, Lua).

## Build

```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j8
```

Or use a preset from `CMakePresets.json` (e.g. `cmake --preset clang-debug`, `cmake --preset win64-debug`).
CI uses `--preset <name> -DSLANG_CI_BUILD=ON`.

Key targets:
- `slang_server` — the server binary (`build/bin/slang-server`)
- `server_unittests` — primary C++ tests
- `gen_config_schema` — config JSON schema generator

## Test

**Preferred: build + run server_unittests directly:**
```bash
cmake --build build -j8 --target server_unittests && build/bin/server_unittests
```

**Update golden files** when test output intentionally changes:
```bash
build/bin/server_unittests --update
```
Goldens live in `tests/cpp/golden/*.json` and `tests/cpp/golden/*.out`. They are excluded from formatting hooks.

**CTest** (runs all discovered tests):
```bash
ctest --test-dir build --output-on-failure
```

**Python system tests** (pygls-based, require built server binary):
```bash
uv venv && source .venv/bin/activate && uv sync
pytest tests/system/ --binary build/bin/slang-server
```

**Neovim tests** require luarocks + busted (see `DEVELOPING.md` for setup).

## Architecture

```
src/SlangServer.cpp        Main server class; LSP route handlers + HDL extensions
src/ServerDriver.cpp       Slang driver wrapper; manages syntax trees and open documents
src/ast/ServerCompilation  Wrapper around slang Compilation with incremental updates
src/document/SlangDoc.cpp  File + SyntaxTree pair; token index + shallow compilation
src/document/              Core per-document LSP features (indexer, inlay hints, etc.)
src/completions/           Completion providers (members, instances, macros, keywords)
src/codeactions/           Code actions (expand macro, add define)
src/lsp/                   URI handling
src/util/                  Converters, formatting, markdown, extensions
include/                   Public headers mirroring src/ structure
include/lsp/               LSP type stubs (generated from lsprotocol, rarely updated)
```

## Submodules & External

All in `external/`:
- `slang` — core parser/compiler library (git submodule)
- `reflect-cpp` — JSON serialization (git submodule)
- `ctre` — compile-time regex (git submodule)
- `vscode-system-verilog` — grammar/snippets (git submodule)
- Single-header libs: `BS_thread_pool.hpp`, `expected.hpp`, `boost_concurrent.hpp`, `boost_unordered.hpp`

`external/` is excluded from all formatting and linting hooks.

## Codegen & Schema Sync

When `include/Config.h` changes:
1. Pre-commit hook runs `python3 scripts/genconfig.py`
2. This builds `gen_config_schema`, generates `clients/vscode/resources/config.schema.json`, then converts it to `clients/vscode/src/config.gen.ts`
3. The VS Code extension's `package.json` is updated via the "Extdev: update config" command from the debugging VS Code window

## Code Style

- **C++20**, column limit 100, `#pragma once` (no ifdef guards)
- **lowerCase** for functions, parameters, locals (not LLVM-style UpperCase)
- clang-format v20 config: `.clang-format`
- cmake-format for CMake files (2-space indent)
- Prettier for TS/YAML, Ruff for Python
- `external/` excluded from all formatters

Install pre-commit hooks:
```bash
pip install prek && prek install
```

Run all hooks: `prek run --all-files`

## VS Code Client

Located in `clients/vscode/`. Uses pnpm, Node 20.
```bash
cd clients/vscode && pnpm install && pnpm run compile
```

Debug: copy `.vscode/launch.template.jsonc` to `.vscode/launch.json`, configure build with `cmake -B build/vscode -DCMAKE_BUILD_TYPE=Debug`.

## Key Conventions

- CI builds with `-Werror`; always build locally with the same warning level to avoid CI failures
- Tests use Catch2 (fetched via FetchContent, v3.10.0)
- Test data lives in `tests/data/` (subdirectories like `repo1/`, `basic_config/`, etc.)
- Test harness: `ServerHarness` (C++) and `SlangClient` (Python) wrap the server for in-process/IO testing
- Version lives in the `VERSION` file (currently `0.2.10`)
- License: MIT, REUSE-compliant (see `REUSE.toml`)
