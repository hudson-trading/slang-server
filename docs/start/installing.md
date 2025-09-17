# Installing

### Build from Source

```bash
# Clone the repository
git clone https://github.com/hudson-trading/slang-server.git
cd slang-server

# Pull dependencies (slang and reflect-cpp)
git submodule update --init --recursive

# Build with cmake using a C++20 compliant compiler
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j --target slang_server
```

In the future there will be pre-built binaries released for common platforms. The editor clients will auto-install these, similar to what clangd and others do.

### Vscode

Install the extension [here](https://marketplace.visualstudio.com/items?itemName=Hudson-River-Trading.vscode-slang), then set `slang.path` to the slang-server binary. (at `build/bin/slang-server`)

See [VSCode Options](https://github.com/hudson-trading/slang-server/blob/main/clients/vscode/CONFIG.md)

An OpenVSX release is coming soon, but in the meantime it's possible to run `cd clients/vscode && pnpm install && pnpm run prepublishOnly` to create an installable vsix. OpenVSX is used by the Vscode forks.

### Neovim

There are many ways to configure a language server in Neovim. One can use the [nvim API](https://neovim.io/doc/user/lsp.html#vim.lsp.start()) directly. Plugins like nvim-lspconfig and mason make managing language servers easier but slang-server has not yet been added to those projects. For users of lazy.nvim, the language server can be configured by adding or amending `~/.config/nvim/lua/plugins/lsp/nvim-lspconfig.lua` with this:
```lua
return {
  "neovim/nvim-lspconfig",
  opts = {
    setup = {
      slang_server = function(_, opts)
        local configs = require("lspconfig.configs")
        local util = require("lspconfig.util")

        if not configs.slang_server then
          configs.slang_server = {
            default_config = {
              cmd = {
                "slang-server",
              },
              filetypes = {
                "systemverilog",
                "verilog",
              },
              single_file_support = true,
              root_dir = function(fname)
                return util.root_pattern(".slang-server.json")(fname)
                  or vim.fs.dirname(vim.fs.find(".git", { path = fname, upward = true })[1])
              end,
            },
          }
        end
      end,
    },
    servers = {
      slang_server = {
        enabled = true,
        mason = false,
      },
    },
  },
}
```

Neovim natively handles the LSP. No additional plugin is required to use Slang Server for standard LSP actions (e.g. [Go to Definition](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_definition), however the [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) plugin is provided to enable use of the features which extend the LSP (e.g. hierachical compilation).

### Other editors

Most modern editors can at least point to a language server binary for specific file types, which provides standard LSP features, but not HDL specific frontend features like the hierarchy view.

If the editor also allows for executing LSP commands, HDL features like setting a compilation should be available, although the process may not be as smooth.
