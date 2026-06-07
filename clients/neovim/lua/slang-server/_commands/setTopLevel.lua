-- Parser for :SlangServer setTopLevel

local M = {}

---@type slang-server.ui.Subcommand
M.setTopLevel = {
   impl = function(args, opts)
      local capabilities = require("slang-server._lsp.capabilities")
      local file = args[1]
      local bufnr
      if file then
         bufnr = capabilities.get_source_context()
      else
         bufnr = vim.api.nvim_get_current_buf()
         if not capabilities.get_client(bufnr) then
            vim.notify(
               "slang-server: setTopLevel without a file must be run from a buffer with an attached slang-server LSP client.",
               vim.log.levels.ERROR
            )
            return
         end
         file = vim.api.nvim_buf_get_name(bufnr)
      end
      if not capabilities.check_or_notify(bufnr, { "slang.setTopLevel" }) then
         return
      end

      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")

      client.setTopLevel(bufnr, handlers.defaultHandlers, { uri = file })
   end,
   complete = require("slang-server.util").complete_path,
}

return M
