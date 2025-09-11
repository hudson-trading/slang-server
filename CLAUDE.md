# Slang Server Development Guide

The Slang Server is a SystemVerilog language server written in C++20, and heavily uses the Slang library for parsing and analysis, located at `external/slang`.

## Build Commands

```bash
# Build the project
cmake -B build/claude
cmake --build build/claude -j8

# Run tests
ctest --test-dir build/claude --output-on-failure

# Server tests only- this is the primary testing
cmake --build build/claude -j8 --target server_unittests && build/bin/server_unittests
# Add --update if updating the golden outputs which get stored in tests/cpp/golden

# Server tests only- this is the primary testing
cmake --build build/claude -j8 --target slang_server
```

## Testing Framework

- **Unit Tests**: Uses Catch2 framework, located in `tests/cpp/`
- **Test Command**: `ctest --test-dir build/claude --output-on-failure`

## Architecture Overview

`src/SlangServer.cpp`: The single instance server class, that has methods that directly map to the language server routes and hardware language extensions. It holds the indexer, which gathers all of the symbols in the workspace on start up.
`src/ServerDriver.cpp`: This is a wrapper around the slang driver, and is recreated every time flags need to be parsed. It manages the syntax trees and open documents
`src/ast/ServerCompilation.cpp`: This is a wrapper around a slang Compilation, and it knows how to update the compilation when 
`src/document/SlangDoc.cpp` This represents a file/SyntaxTree pair, and also manages analysis features for that document, like a token index and a shallow compilation.
`src/document`: These files have features core LSP features for a document.



## Code Style and Standards

- Follow existing C++ code style (enforced by pre-commit hooks)
- Use modern C++20 features and idioms
- Write unit tests for new functionality
- Maintain high performance and correctness standards

## Development Workflow

1. Build: `cmake -B build/claude && cmake --build build/claude -j8`
2. Test: `ctest --test-dir build/claude --output-on-failure`
3. Format: Automatic via pre-commit hooks
