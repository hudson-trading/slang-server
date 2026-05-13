-- Parser for :SlangServer setTopLevel

local M = {}

---@type slang-server.ui.Subcommand
M.setTopLevel = {
   impl = function(args, opts)
      local capabilities = require("slang-server._lsp.capabilities")
      local bufnr = vim.api.nvim_get_current_buf()
      if not capabilities.check_or_notify(bufnr, { "slang.setTopLevel" }) then
         return
      end

      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")

      local file = args[1] or vim.api.nvim_buf_get_name(bufnr)
      client.setTopLevel(bufnr, handlers.defaultHandlers, { uri = file })
   end,
   complete = require("slang-server.util").complete_path,
}

return M
