-- Parser for :SlangServer hierarchy

local M = {}

---@type slang-server.ui.Subcommand
M.hierarchy = {
   impl = function(args, opts)
      local capabilities = require("slang-server._lsp.capabilities")
      local bufnr = capabilities.get_source_context()
      local required = {
         "slang.getScope",
         "slang.getScopes",
         "slang.getScopesByModule",
         "slang.getInstancesOfModule",
         "slang.showHierLocation",
      }
      if not capabilities.check_or_notify(bufnr, required) then
         return
      end

      local top = args[1]
      if not top then
         local client = capabilities.get_client(bufnr)
         assert(client)
         top = require("slang-server._lsp.state").get_active_path(client.id)
      end
      require("slang-server.navigation").show(top or "", top ~= nil)
   end,
}

return M
