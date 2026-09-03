# Installing

### Vscode

Install the extension [here](https://marketplace.visualstudio.com/items?itemName=Hudson-River-Trading.vscode-slang). Then, it will prompt you to allow the extension to autoinstall the server binary from the [releases](https://github.com/hudson-trading/slang-server/releases) page. Alternatively, you can [build slang-server](https://hudson-trading.github.io/slang-server/start/building/) and set `slang.path` to that binary.

### Vscode Forks (Cursor, Antigravity, VSCodium, etc.)

Install from your editor, or download from the [OpenVSX Marketplace](https://open-vsx.org/extension/Hudson-River-Trading/vscode-slang)

### Neovim

`slang-server` is available in [nvim-lspconfig](https://github.com/neovim/nvim-lspconfig) as `slang_server` (note the underscore) and in the [mason.nvim](https://github.com/mason-org/mason.nvim) package registry as `slang-server`, so no additional configuration is required in most cases. The default configuration shipped with nvim-lspconfig can be found [here](https://github.com/neovim/nvim-lspconfig/blob/master/lsp/slang_server.lua).

Install the binary via `:MasonInstall slang-server` (or otherwise place it on `PATH`), then enable the server with `vim.lsp.enable("slang_server")`, or follow your own Neovim configuration's convention for enabling servers.

Restart and run `:LspInfo` to make sure the LSP was correctly installed.

#### Code lenses

On Neovim 0.10 and 0.11, code lenses must be refreshed explicitly. It is also
recommended to map `vim.lsp.codelens.run()` so that the lenses on the current
line can be selected and executed. The following configuration keeps lenses
current and maps `<leader>cl` to run them:

```lua
vim.api.nvim_create_autocmd("LspAttach", {
  callback = function(args)
    local client = vim.lsp.get_client_by_id(args.data.client_id)
    if not client or client.name ~= "slang_server"
        or not client:supports_method("textDocument/codeLens") then
      return
    end

    local group = vim.api.nvim_create_augroup("slang-server-codelens", { clear = false })
    vim.api.nvim_clear_autocmds({ group = group, buffer = args.buf })
    vim.api.nvim_create_autocmd({ "BufEnter", "CursorHold", "InsertLeave" }, {
      group = group,
      buffer = args.buf,
      callback = function()
        vim.lsp.codelens.refresh({ bufnr = args.buf })
      end,
    })

    vim.lsp.codelens.refresh({ bufnr = args.buf })
    vim.keymap.set("n", "<leader>cl", vim.lsp.codelens.run, {
      buffer = args.buf,
      desc = "Run code lens",
    })
  end,
})
```

Neovim 0.12 and newer can use `vim.lsp.codelens.enable()` instead of the
refresh autocmds. `vim.lsp.codelens.run()` is still used to execute the lens on
the current line.

#### Enhanced features

Once the language server is installed, it is recommended to install the [slang-server.nvim](https://github.com/hudson-trading/slang-server.nvim) plugin; this provides enhanced HDL specific features such as design hierarchy view and waveform integration.

### Other editors

Most modern editors can at least point to a language server binary for specific file types. This will provide standard LSP features, but not HDL specific features.

If the editor also allows for executing LSP commands, HDL features like setting a compilation should be available.
