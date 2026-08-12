# slang-server.nvim

A Neovim plugin to support non-LSP features of [Slang Server](https://github.com/hudson-trading/slang-server).

## Features

Note that it is not necessary to install this plugin in order to use Slang Server.
Neovim supports all standard [LSP](https://microsoft.github.io/language-server-protocol/) commands.
This plugin is for the following features which extend the standard LSP interface.
More information on plugin features can be [found here](https://hudson-trading.github.io/slang-server/hdl/neovim/).

## Requirements

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

The plugin is lazily loaded by default on the first invocation of a `:SlangServer` command, so there's no need to rely on a plugin manager for lazy loading. To install without a plugin manager, simply clone and place the plugin directory in your Neovim runtimepath.

## Configuration

The default configuration can be found in [config.lua](./lua/slang-server/_core/config.lua). Override options can be defined in the global `vim.g.slang_server_config`, or passed to `opts = {...}` in the lazy.nvim plugin spec.

### Inactive regions

To enable inactive-region highlighting, define a notification handler and add it along with the experimental capability to your `slang_server` LSP configuration:

```lua
local inactive_regions_ns = vim.api.nvim_create_namespace("SlangServerInactiveRegions")
local inactive_region_hl = "SlangServerInactiveRegion"

vim.api.nvim_set_hl(0, inactive_region_hl, {
  link = "Comment",
  default = true,
})

local function apply_inactive_region_highlights(uri, ranges)
  local bufnr = vim.fn.bufnr(vim.uri_to_fname(uri), false)
  if bufnr == -1 then
    return
  end

  vim.api.nvim_buf_clear_namespace(bufnr, inactive_regions_ns, 0, -1)

  for _, range in ipairs(ranges) do
    vim.api.nvim_buf_set_extmark(
      bufnr,
      inactive_regions_ns,
      range.start.line,
      range.start.character,
      {
        end_row = range["end"].line,
        end_col = range["end"].character,
        hl_group = inactive_region_hl,
        hl_eol = true,
      }
    )
  end
end

local function inactive_regions_handler(err, params, _, _)
  if err or not params or not params.uri or not params.regions then
    return
  end

  apply_inactive_region_highlights(params.uri, params.regions)
end

vim.lsp.config("slang_server", {
  capabilities = {
    experimental = {
      inactiveRegions = {
        inactiveRegions = true,
      },
    },
  },
  handlers = {
    ["textDocument/inactiveRegions"] = inactive_regions_handler,
  },
})
```

## GitHub Repos

This plugin lives in two repos:

The code is maintained in [Slang Server](https://github.com/hudson-trading/slang-server).  All issues, PRs, etc. should be directed there.

The [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) repo is synced from the Neovim client code in the Slang Server repo.  It exists solely as a convenience for plugin managers which require a specific directory structure at the root of the repo.
