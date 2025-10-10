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

## Installation
Use your favorite Neovim plugin manager to download and install the plugin.  If you happen to use lazy.nvim you can [install the plugin](https://www.lazyvim.org/configuration/plugins) by adding `~/.config/nvim/lua/plugins/slang-server.lua`:
```lua
return {
  {
    "hudson-trading/slang-server.nvim",
    dependencies = {
      "MunifTanjim/nui.nvim",
    },
    opts = {},
  },
}
```
