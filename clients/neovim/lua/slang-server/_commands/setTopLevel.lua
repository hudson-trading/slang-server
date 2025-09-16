-- Parser for :SlangServer setTopLevel

local M = {}

---@type slang-server.ui.Subcommand
M.setTopLevel = {
   impl = function(args, opts)
      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")

      local bufnr = vim.api.nvim_get_current_buf()
      local file = args[1] or vim.api.nvim_buf_get_name(bufnr)
      client.setTopLevel(bufnr, handlers.defaultHandlers, { uri = file })
   end,
   complete = require("slang-server.util").complete_path,
}

return M
