# Slang Server

`slang-server` is a [Language Server](https://microsoft.github.io/language-server-protocol/) for SystemVerilog based on the [Slang](https://github.com/MikePopoloski/slang) library, providing useful language features for editors like VS Code and Neovim.

Install on the [VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=Hudson-River-Trading.vscode-slang), the [OpenVSX Marketplace](https://open-vsx.org/extension/Hudson-River-Trading/vscode-slang), or with [Neovim](https://hudson-trading.github.io/slang-server/start/installing/#neovim).

[Other editors](https://hudson-trading.github.io/slang-server/start/installing/#other-editors) don't have an accompanying client at the moment, but core language features will still work.

Contributions are welcome! See [DEVELOPING.md](DEVELOPING.md)

## Features

Quick, high quality lint messages on every keystroke, with links to the [Slang warning reference](https://sv-lang.com/warning-ref.html).

![](docs/assets/images/lints.gif)

Informative hovers and gotos on nearly every symbol across your workspace and libraries.

![](docs/assets/images/hovers.gif)

Find references across your entire workspace.

![](docs/assets/images/gotorefs.gif)

Configurable inlay hints that provide useful information.

![](docs/assets/images/all_inlays.png)

Intuitive completions for module instances and macros, as well as scope members of packages, modules, structs, and more.

![](docs/assets/images/completions.gif)

HDL-specific features that allow you to easily set a filelist or top level for a design, browse the elaborated hierarchy, and go back and forth with a waveform viewer.

![](docs/assets/images/hdl.gif)

For more detailed feature info, see [the docs](https://hudson-trading.github.io/slang-server/features/features/).
