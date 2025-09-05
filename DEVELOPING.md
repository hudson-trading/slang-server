
# Developing

## Building
Configure your compiler, e.g.
```bash
export PATH="/your/path/to/clang-20-tools/bin/:$PATH"
export CMAKE_CXX_COMPILER=clang++
export CXX=clang++
export CX=clang-20
export CC=clang-20
```

Run `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1  -DCMAKE_BUILD_TYPE=DEBUG` to configure cmake. 
- CMAKE_EXPORT_COMPILE_COMMANDS is to generate compile_commands.json for clangd to pick up.
- CMAKE_BUILD_TYPE=DEBUG is to build with debug symbols

Run `cmake --build build -j --target slang_server` to build `build/bin/slang-server`


## Cpp Testing
```bash
cmake --build build -j --target server_unittests
build/bin/server_unittests
```

The `unittests` target is also buildable and runnable, to ensure any temporary upstream commits are also passing slang's test suite.

## Vscode Testing

Install the Slang Extension on either the [Vscode Marketplace](TODO) or [OpenVSX](TODO)

Add this to `.vscode/settings.json` to use slang-server
```json
"slang.path": "path/to/your/slang-server",
```

Logs are in `Output > slang-server` in the terminal area

To refresh the server, run the command "Verilog: Restart Language Server"

## Vscode Debugging

Configure your build with debug symbols: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=Debug`

Go to the debug panel and press play (Attach to running slang-server)

Type in `slang-server` to select the running server

Now adding cpp breakpoints other vscode features should work.
This method works when running slang-server with vscode or pygls


## LSP Stubs

Server stubs are generated from Microsoft's [stubs generation](https://github.com/microsoft/lsprotocol) repo. These should rarely need to be updated.

The types are made with [reflect-cpp](https://github.com/getml/reflect-cpp) \([docs](https://rfl.getml.com/docs-readme/)\), but most of that stuff is handled at the existing JsonRpc layer.

To update:
```bash
git clone git@github.com:microsoft/lsprotocol.git
cd lsprotocol
python -m generator --plugin cpp
cd ../slang
cp ../../lsprotocol/packages/cpp/* ./include/lsp/
```


## Pygls Testing
These tests were used early during development to ensure compliance with the LSP, but cpp tests are preferred now.

Install uv: https://docs.astral.sh/uv/getting-started/installation/
```
uv venv
source .venv/bin/activate
uv sync
pytest tests/system/
```

### Debugging with pygls

GDB: Run with `--gdb <secs>`, then you'll have that long to attach. Then press 'c' in gdb

RR: Run with `--rr`, then run `rr replay`


## Neovim Testing

(TODO)
