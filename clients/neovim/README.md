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

Clone and place the plugin directory in your Neovim runtimepath. It is lazily loaded by default on the first invocation of a `:SlangServer` command, so there's no need to rely on a plugin manager for lazy loading.

Alternatively, use your favorite Neovim plugin manager to download and install the plugin.  If you happen to use lazy.nvim you can [install the plugin](https://www.lazyvim.org/configuration/plugins) by adding, e.g., `~/.config/nvim/lua/plugins/slang-server.lua`:
```lua
return {
  {
    "hudson-trading/slang-server.nvim",
  },
}
```

## Configuration

The default configuration is shown below. Override options can be defined in the global `vim.g.slang_server_config`, or passed to `opts = {...}` in the lazy.nvim plugin spec.

```lua
{
   hierarchy = {
      position = "left",
      size = 40,
   },
   kinds = {
      instance = { icon = "", hl = "SlangServerInstance" },
      instancearray = { icon = "", hl = "SlangServerInstanceArray" },
      scope = { icon = "󰅩", hl = "SlangServerScope" },
      scopearray = { icon = "󰅩", hl = "SlangServerScopeArray" },
      package = { icon = "📦", hl = "SlangServerPackage" },
      port = {
         input = { icon = "", hl = "SlangServerPortInput" },
         output = { icon = "", hl = "SlangServerPortOutput" },
         inout = { icon = "", hl = "SlangServerPortInout" },
      },
      param = { icon = "", hl = "SlangServerParam" },
      logic = { icon = "󱒖", hl = "SlangServerLogic" },
      reg = { icon = "", hl = "SlangServerReg" },
   },
   highlights = {
      SlangServerInstance = { fg = "#efbd5d" },
      SlangServerInstanceArray = { fg = "#efbd5d" },
      SlangServerScope = { fg = "#41a7fc" },
      SlangServerScopeArray = { fg = "#41a7fc" },
      SlangServerPackage = { fg = "#f48fb1" },
      SlangServerPortInput = { fg = "#8bcd5b" },
      SlangServerPortOutput = { fg = "#f65866" },
      SlangServerPortInout = { fg = "#34bfd0" },
      SlangServerParam = { fg = "#c75ae8" },
      SlangServerLogic = { fg = "#dd9046" },
      SlangServerReg = { fg = "#dd9046" },
   },
}
```

## GitHub Repos

This plugin lives in two repos:

The code is maintained in [Slang Server](https://github.com/hudson-trading/slang-server).  All issues, PRs, etc. should be directed there.

The [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) repo is synced from the Neovim client code in the Slang Server repo.  It exists solely as a convenience for plugin managers which require a specific directory structure at the root of the repo.
