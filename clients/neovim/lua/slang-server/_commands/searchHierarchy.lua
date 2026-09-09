local M = {}

---@type slang-server.ui.Subcommand
M.searchHierarchy = {
   impl = function()
      local capabilities = require("slang-server._lsp.capabilities")
      local bufnr = capabilities.get_source_context()
      if not capabilities.check_or_notify(bufnr, {
         "slang.getScope",
         "slang.getScopesByModule",
         "slang.searchHierarchy",
      }) then
         return
      end
      require("slang-server.navigation.searchHierarchy").start(bufnr)
   end,
}

return M
