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

On certain systems (Arch, etc), you may need to have the project use its vendored copy of the `fmt` library,
rather than the one on your system. This can be achieved by appending `-DCMAKE_DISABLE_FIND_PACKAGE_fmt=TRUE`
to the CMake configuration step, e.g.:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_fmt=TRUE
```

In the future there will be pre-built binaries released for common platforms. The editor clients will auto-install these, similar to what clangd and others do.

### Vscode

Install the extension [here](https://marketplace.visualstudio.com/items?itemName=Hudson-River-Trading.vscode-slang), then set `slang.path` to the slang-server binary. (at `build/bin/slang-server`)

See [VSCode Options](https://github.com/hudson-trading/slang-server/blob/main/clients/vscode/CONFIG.md)

An OpenVSX release is coming soon, but in the meantime it's possible to run `cd clients/vscode && pnpm install && pnpm run prepublishOnly` to create an installable vsix. OpenVSX is used by the Vscode forks.

### Neovim

> [!NOTE]
> Once Slang Server is more actively used (you can help by starring [the project](https://github.com/hudson-trading/slang-server)!), it will be added to [nvim-lspconfig](https://github.com/neovim/nvim-lspconfig) and [mason.nvim](https://github.com/mason-org/mason.nvim) and no additional configuration will be required. Until then, follow one of the methods below to manually add the server configuration.

There are many ways to configure a language server in Neovim.

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

Neovim natively handles the LSP. No additional plugin is required to use Slang Server for standard LSP actions (e.g. [Go to Definition](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_definition).

However, a plugin is provided to enable use of some enhanced client-side features which extend the LSP (e.g. hierarchical compilation). The plugin can be found in `clients/neovim/` and is also mirrored in [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) for ease of use with Neovim plugin managers.

### Other editors

Most modern editors can at least point to a language server binary for specific file types, which provides standard LSP features, but not HDL specific frontend features like the hierarchy view.

If the editor also allows for executing LSP commands, HDL features like setting a compilation should be available, although the process may not be as smooth.
