# Installing

### Build from Source

```bash
# Clone the repository
git clone https://github.com/hudson-trading/slang-server.git
cd slang-server

# Pull dependencies
git submodule update --init --recursive

# Build with cmake using a C++20 compliant compiler
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Pre-built binaries

In the future there will be pre-built binaries released for common platforms. It's also planned to have the editor clients auto-install these, similar to what clangd and others do.

### Neovim

There are many ways to configure a language server in Neovim. One can use the [nvim API](<https://neovim.io/doc/user/lsp.html#vim.lsp.start()>) directly. Plugins like nvim-lspconfig and mason make managing language servers easier but slang-server has not yet been added to those projects. If you happen to use lazy.nvim you can configure the language server by adding or ammending `~/.config/nvim/lua/plugins/lsp/nvim-lspconfig.lua` with this:

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

### Vscode

Extension will be released soon

<!-- Install the extension [here](https://marketplace.visualstudio.com/items?itemName=HudsonRiverTrading.slang-vscode), then set `slang.path` to your server binary -->

<!-- See [Vscode Options](../clients/vscode/CONFIG.md) -->
