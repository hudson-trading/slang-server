-- Parser for :SlangServer hierarchy

local M = {}

---@type slang-server.ui.Subcommand
M.hierarchy = {
   impl = function(args, opts)
      local capabilities = require("slang-server._lsp.capabilities")
      local bufnr = vim.api.nvim_get_current_buf()
      local required = { "slang.getScope", "slang.getScopesByModule", "slang.getInstancesOfModule" }
      if not capabilities.check_or_notify(bufnr, required) then
         return
      end

      local top = args[1]
      require("slang-server.navigation").show(top or "")
   end,
}

return M
