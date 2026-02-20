# Installing


### Vscode

Install the extension [here](https://marketplace.visualstudio.com/items?itemName=Hudson-River-Trading.vscode-slang). Then, it will prompt you to allow the extension to autoinstall the server binary from the [releases](https://github.com/hudson-trading/slang-server/releases) page. Alternatively, you can set `slang.path` to you built slang-server binary.

### Vscode Forks (Cursor, Antigravity, VSCodium, etc.)

Install from your editor, or download from the [OpenVSX Marketplace](https://open-vsx.org/extension/Hudson-River-Trading/vscode-slang)

### Neovim

> `slang-server` will eventually be added to [nvim-lspconfig](https://github.com/neovim/nvim-lspconfig) and [mason.nvim](https://github.com/mason-org/mason.nvim) and no additional configuration will be required. Until then, follow one of the methods below to manually add the server configuration.


For newer versions of Neovim (≥ v0.11), the new [vim.lsp API](https://neovim.io/doc/user/lsp.html#vim.lsp.start()) is the preferred, simpler way to configure language servers:
```lua
vim.lsp.config("slang-server", {
  cmd = { "slang-server" },
  root_markers = { ".git", ".slang" },
  filetypes = {
    "systemverilog",
    "verilog",
  },
})

vim.lsp.enable("slang-server")
```

For older versions of Neovim (< v0.11) with `nvim-lspconfig`, the server can be configured with:
```lua
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
        return util.root_pattern(".git", ".slang")(fname)
      end,
    },
  }
end
```

For users of lazy.nvim, the above could be added to their `nvim-lspconfig` spec at `~/.config/nvim/lua/plugins/nvim-lspconfig.lua` like this:
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
                return util.root_pattern(".git", ".slang")(fname)
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

Pointing at the binary is all you need for standard language features, however a plugin is provided to enable some client-side features which extend the LSP (e.g. the hierarchy view, waveform integration). The plugin can be found in `clients/neovim/` and is also mirrored in [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) for ease of use with Neovim plugin managers.

### Other editors

Most modern editors can at least point to a language server binary for specific file types. This will provide standard LSP features, but not HDL specific  features.

If the editor also allows for executing LSP commands, HDL features like setting a compilation should be available.
