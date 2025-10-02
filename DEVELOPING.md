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

## Vscode Client Testing
Install the extension dependencies by following the instructions [here](clients/vscode/DEVELOPING.md)

Copy the launch template:

`cp .vscode/launch.template.jsonc .vscode/launch.json`

Configure a build for vscode to use

`cmake -B build/vscode -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=DEBUG`

Go to Debug > Run slang-vscode and attach gdb

This will spin up the extension and automatically attach gdb to the slang-server process.

## Vscode Debugging

Configure your build with debug symbols: `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_BUILD_TYPE=Debug`

Go to the debug panel and press play (Attach to running slang-server)

Type in `slang-server` to select the running server

Now adding cpp breakpoints other vscode features should work.
This method works when running slang-server with vscode or pygls

You can use the auto-attach debug config by pointing your repo to the build/vscode server build, then doing the following after changes:
- rebuild your changes if any for the build/vscode build
- run the `Slang: Restart Lanugage Server` command
- run the `Attach to slang-server (auto-attach)` debug option to reattach.

## Neovim Testing

Neovim tests require nlua and busted as [described here](https://mrcjkb.dev/posts/2023-06-06-luarocks-test.html).
They can be configured and run via luarocks as follows:
```bash
cd clients/neovim/
luarocks --tree .luarocks install nlua
luarocks --tree .luarocks --lua-version 5.1 install busted
eval $(luarocks --tree .luarocks --lua-version 5.1 path)
PATH=$PATH:../../build/bin luarocks --tree .luarocks --lua-version 5.1 test
```

## lsp-rr-wrapper.py

This wrapper script can be used to aid in debugging `slang-server`.
It will record the underlying `slang-server` using the [rr debugger](https://rr-project.org/) and
make a copy of stdin, stdout and stderr in `/tmp/slang-server.std*`.
To use it, simply wrap the call to `slang-server` with this script in a given editor's LSP config.
Note that currently the wrapper overwrites temp every time it is run.

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

<!--
## Neovim Testing

(TODO) -->
