# slang-server.nvim

A Neovim plugin to support non-LSP features of [Slang Server](https://github.com/hudson-trading/slang-server).

## Features

Note that it is not necessary to install this plugin in order to use Slang Server.  Neovim supports all standard [LSP](https://microsoft.github.io/language-server-protocol/) commands.  This plugin is for the following features which extend the standard LSP interface:

| Feature | Description |
|---------|-------------|
| setTopLevel | Marks a file as containing the top-level module and compiles the design |
| setBuildFile | Given an arguments file (see `slang -f`) compile the design |
| hierarchy | Opens a hierarchy navigator |
| openWaveform | Opens a waveform file (e.g. VCD, FST) with a [WCP](https://gitlab.com/waveform-control-protocol/wcp) capable waveform viewer |
| addToWaves | Adds the variable or scope currently under cursor to the waveform viewer |

## Requirements

Some [Nerd Font](https://www.nerdfonts.com/) characters are used.  Installing and using one of them is recommended.

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
