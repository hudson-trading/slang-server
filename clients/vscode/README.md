# Slang Server: SystemVerilog Language Server

*Slang Server is a new SystemVerilog language server implmented in C++*

See [the docs](https://hudson-trading.github.io/slang-server) for instructions on [installing](https://hudson-trading.github.io/slang-server/start/installing/) and [configuring](https://hudson-trading.github.io/slang-server/start/config/).


## Quick Start

Build from source at [https://github.com/hudson-trading/slang-server](https://github.com/hudson-trading/slang-server)

Then point to your build in the `slang.path` vscode setting.

## Features

Quick, high quality lint messages on from [Slang](https://github.com/MikePopoloski/slang) on every keystroke, with links to the [Slang warning reference](https://sv-lang.com/warning-ref.html).

![](images/lints.gif)

Informative hovers and gotos on nearly every symbol across your workspace and libraries.

![](images/hovers.gif)

Intuitive completions for module instances and macros, as well as scope members of packages, modules, structs, and more.

![](images/completions.gif)

HDL-specific features that allow you to easily set a filelist or top level for a design, browse the elaborated hierarchy, and interact with waveform viewers.

![](images/hdl.gif)

For more detailed feature info, see [the docs](https://hudson-trading.github.io/slang-server/features/features/).
