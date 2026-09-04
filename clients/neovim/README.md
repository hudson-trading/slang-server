# slang-server.nvim

A Neovim plugin to support non-LSP features of [Slang Server](https://github.com/hudson-trading/slang-server).

## Features

Note that it is not necessary to install this plugin in order to use Slang Server.
Neovim supports all standard [LSP](https://microsoft.github.io/language-server-protocol/) commands.
This plugin is for the following features which extend the standard LSP interface.
More information on plugin features can be [found here](https://hudson-trading.github.io/slang-server/features/hdl/neovim/).

The hierarchy and Cells views can select an active elaborated instance for each module or interface. That selection drives instance-specific hovers and inlay hints, and stays synchronized with CodeLens and Go to Definition navigation.

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

`:SlangServer searchHierarchy` provides an interactive search over the compiled
design and reveals the selected object in the hierarchy view. It uses FzfLua,
Telescope, or Snacks Picker when available, in that order. These integrations
are optional and require no picker-specific registration. Without one of those
plugins, the command falls back to `vim.ui.input` followed by `vim.ui.select`.

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
    searchHierarchy = { key = "<leader>vs" },
  },
  search = {
    -- "auto", "fzf-lua", "telescope", "snacks", "vim.ui", or a custom function
    picker = "auto",
    query_delay = 150, -- milliseconds
  },
})
```

`search.query_delay` debounces requests made through the picker. This delay is
added to any input or query delay applied by the selected picker engine itself;
set it to `0` to rely solely on the picker's behavior. Only one server request
is kept in flight; changes made while it runs are coalesced into a single request
for the latest query.

A custom picker can be supplied as a function. Call `ctx.search` whenever its
query changes; results arrive asynchronously and retain the server's fuzzy-match
ordering. Call `ctx.select` with the chosen result item:

```lua
require("slang-server").setup({
  search = {
    picker = function(ctx)
      my_picker({
        on_query = function(query, update_items)
          ctx.search(query, function(result)
            update_items(result.matches, result.totalResults)
          end)
        end,
        on_select = function(item)
          ctx.select(item)
        end,
      })
    end,
  },
})
```

`ctx.search(query, callback)` debounces requests and discards stale responses.
Each result item contains `name`, `path`, `kind`, and optional `description` and
`containerName` fields. The custom picker should pass the original item to
`ctx.select(item)` so the plugin can reveal its path.

`selectActive` runs an active-instance or active-generate-iteration code lens on
the current source line directly, normally skipping Neovim's code-lens picker.
It requires code lenses to be enabled and refreshed as described above.

## GitHub Repos

This plugin lives in two repos:

The code is maintained in [Slang Server](https://github.com/hudson-trading/slang-server).  All issues, PRs, etc. should be directed there.

The [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) repo is synced from the Neovim client code in the Slang Server repo.  It exists solely as a convenience for plugin managers which require a specific directory structure at the root of the repo.
