# slang-server.nvim

A Neovim plugin to support non-LSP features of [Slang Server](https://github.com/hudson-trading/slang-server).

## Features

Note that it is not necessary to install this plugin in order to use Slang Server.
Neovim supports all standard [LSP](https://microsoft.github.io/language-server-protocol/) commands.
This plugin is for the following features which extend the standard LSP interface.
More information on plugin features can be [found here](https://hudson-trading.github.io/slang-server/hdl/neovim/).

Hierarchy search is also available to Neovim integrations without transferring the full design:

```lua
require("slang-server").search_hierarchy("fifo.data", function(result)
  vim.print(result.totalResults, result.matches)
end)
```

The server performs the fuzzy match and returns at most 100 entries per query.

Active-instance selection uses standard LSP code lenses. To display and run
these lenses, configure Neovim as described in the
[Code lenses documentation](https://hudson-trading.github.io/slang-server/start/installing/#code-lenses).

## Requirements

* Neovim 0.10.0 or newer
* `slang-server` configured as a Neovim language server
* [Nerd Font](https://www.nerdfonts.com/) is recommended

### Plugin dependencies

If installing with lazy.nvim, plugin dependencies are resolved automatically.

* [nui.nvim](https://github.com/MunifTanjim/nui.nvim)

## Installation

You can use your favorite Neovim plugin manager to download and install the plugin. If you happen to use lazy.nvim you can install the plugin by adding, e.g., `~/.config/nvim/lua/plugins/slang-server.lua`:

```lua
return {
  {
    "hudson-trading/slang-server.nvim",
  },
}
```

The plugin defers command and mapping initialization until a Verilog or
SystemVerilog ftplugin is loaded. Its lazy.nvim package specification therefore
sets `lazy = false`; adding another plugin-manager lazy-loading trigger is neither
required nor recommended. To install without a plugin manager, simply clone and
place the plugin directory in your Neovim runtimepath.

## Configuration

The default configuration can be found in [config.lua](./lua/slang-server/_core/config.lua). Override options can be defined in the global `vim.g.slang_server_config`, or passed to `opts = {...}` in the lazy.nvim plugin spec.

Global mappings for plugin commands are disabled by default. Set
`keymaps.enable_defaults = true` to enable them all; individual mappings
can still override `enabled` or `key`:

```lua
require("slang-server").setup({
  navigation = {
    position = "left",
    width = 50,
    wrap = false,
    hierarchy = {
      keymaps = {
        jump = "<cr>",
      },
    },
    cells = {
      show = true,
      height = 25, -- rows
      keymaps = {
        jump = "<cr>",
      },
    },
  },
  keymaps = {
    enable_defaults = true,
    focus = { enabled = false },
    findInstance = { key = "<leader>vf" },
  },
})
```

`selectActive` runs an active-instance or active-generate-iteration code lens on
the current source line directly, normally skipping Neovim's code-lens picker.
It requires code lenses to be enabled and refreshed as described above.

## GitHub Repos

This plugin lives in two repos:

The code is maintained in [Slang Server](https://github.com/hudson-trading/slang-server).  All issues, PRs, etc. should be directed there.

The [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) repo is synced from the Neovim client code in the Slang Server repo.  It exists solely as a convenience for plugin managers which require a specific directory structure at the root of the repo.
