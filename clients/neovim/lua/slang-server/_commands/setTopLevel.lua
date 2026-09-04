-- Parser for :SlangServer setTopLevel

local M = {}
local capabilities = require("slang-server._lsp.capabilities")

---@type slang-server.ui.Subcommand
M.setTopLevel = {
   desc = "Set the current design top level",
   required_commands = { "slang.setTopLevel" },
   context = function(args)
      if args[1] then
         return capabilities.get_source_context()
      end
      return capabilities.get_current_context()
   end,
   impl = function(args, opts, bufnr)
      local file = args[1]
      if not file then
         file = vim.api.nvim_buf_get_name(bufnr)
      end

      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")

      client.setTopLevel(bufnr, handlers.defaultHandlers, { uri = file })
   end,
   complete = require("slang-server.util").complete_path,
}

return M
