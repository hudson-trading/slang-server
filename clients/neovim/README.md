# slang-server.nvim

A Neovim plugin to support non-LSP features of [Slang Server](https://github.com/hudson-trading/slang-server).

## Features

Note that it is not necessary to install this plugin in order to use Slang Server.
Neovim supports all standard [LSP](https://microsoft.github.io/language-server-protocol/) commands.
This plugin provides features which extend the standard LSP interface, such as:

* Browse the elaborated design in hierarchy and cell views.
* Search the hierarchy with FzfLua, Telescope, Snacks Picker, or `vim.ui`.
* Open waveforms and add signals to them (experimental).

More information on plugin features can be [found here](https://hudson-trading.github.io/slang-server/features/hdl/neovim/).

## Requirements

* `slang-server` configured as a Neovim language server
* [Nerd Font](https://www.nerdfonts.com/) is recommended

### Plugin dependencies

If installing with lazy.nvim, plugin dependencies are resolved automatically.

* [nui.nvim](https://github.com/MunifTanjim/nui.nvim)

FzfLua, Telescope, or Snacks Picker are optional dependencies. If installed,
the configured picker will be used for the `searchHierarchy` command;
otherwise, a slightly degraded two-step search is provided by native
`vim.ui.input` and `vim.ui.select`.

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

`search.query_delay` debounces requests made through the picker. This delay is
added to any input or query delay applied by the selected picker engine itself;
set it to `0` to rely solely on the picker's behaviour.

A custom `searchHierarchy` picker can be supplied as a function accepting a
`ctx`. Call `ctx.search` whenever its query changes; results arrive
asynchronously and retain the server's fuzzy-match ordering. Call `ctx.select`
with the chosen result item. E.g.:

```lua
require("slang-server").setup({
  search = {
    picker = function(ctx)
      vim.ui.input({ prompt = "Hierarchy query: " }, function(query)
        if not query then
          return
        end

        ctx.search(query, function(result)
          vim.ui.select(result.matches, {
            prompt = ("Select result (%d total)"):format(result.totalResults),
            format_item = function(item)
              return item.path
            end,
          }, function(item)
            if item then
              ctx.select(item)
            end
          end)
        end)
      end)
    end,
  },
})
```

`ctx.search(query, callback)` debounces requests and discards stale responses.
Each result item contains `name`, `path`, `kind`, and optional `description` and
`containerName` fields. The custom picker should pass the original item to
`ctx.select(item)` so the plugin can reveal its path.

## GitHub Repos

This plugin lives in two repos:

The code is maintained in [Slang Server](https://github.com/hudson-trading/slang-server).  All issues, PRs, etc. should be directed there.

The [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) repo is synced from the Neovim client code in the Slang Server repo.  It exists solely as a convenience for plugin managers which require a specific directory structure at the root of the repo.
