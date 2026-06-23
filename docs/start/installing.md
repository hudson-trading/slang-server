# Installing

### Vscode

Install the extension [here](https://marketplace.visualstudio.com/items?itemName=Hudson-River-Trading.vscode-slang). Then, it will prompt you to allow the extension to autoinstall the server binary from the [releases](https://github.com/hudson-trading/slang-server/releases) page. Alternatively, you can [build slang-server](https://hudson-trading.github.io/slang-server/start/building/) and set `slang.path` to that binary.

### Vscode Forks (Cursor, Antigravity, VSCodium, etc.)

Install from your editor, or download from the [OpenVSX Marketplace](https://open-vsx.org/extension/Hudson-River-Trading/vscode-slang)

### Neovim

`slang-server` is available in [nvim-lspconfig](https://github.com/neovim/nvim-lspconfig) as `slang_server` (note the underscore) and in the [mason.nvim](https://github.com/mason-org/mason.nvim) package registry as `slang-server`, so no additional configuration is required in most cases. The default configuration shipped with nvim-lspconfig can be found [here](https://github.com/neovim/nvim-lspconfig/blob/master/lsp/slang_server.lua).

Install the binary via `:MasonInstall slang-server` (or otherwise place it on `PATH`), then enable the server with `vim.lsp.enable("slang_server")`, or follow your own Neovim configuration's convention for enabling servers.

Restart and run `:LspInfo` to make sure the LSP was correctly installed.

#### Enhanced features

Once the language server is installed, it is recommended to install the [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) plugin; this provides enhanced HDL specific features such as design hierarchy view and waveform integration.

### Other editors

Most modern editors can at least point to a language server binary for specific file types. This will provide standard LSP features, but not HDL specific features.

If the editor also allows for executing LSP commands, HDL features like setting a compilation should be available.
