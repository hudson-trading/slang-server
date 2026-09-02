local M = {}

---@type slang-server.ui.Subcommand
M.findInstance = {
   impl = function()
      local capabilities = require("slang-server._lsp.capabilities")
      local bufnr = capabilities.get_source_context()
      local required = {
         "slang.getScope",
         "slang.getScopesByModule",
         "slang.getInstancesOfModule",
         "slang.showHierLocation",
      }
      if not capabilities.check_or_notify(bufnr, required) then
         return
      end

      local search = require("slang-server.navigation.findInstance")
      search.start(bufnr, function(inst_path)
         local navigation = require("slang-server.navigation")
         if navigation.state.open then
            local hierarchy = require("slang-server.navigation.hierarchy")
            vim.api.nvim_set_current_win(hierarchy.state.split.winid)
            hierarchy.open_remainder(nil, true, inst_path, true)
         else
            navigation.show(inst_path, true)
         end
      end)
   end,
}

return M
