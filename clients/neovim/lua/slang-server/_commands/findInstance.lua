local M = {}
local capabilities = require("slang-server._lsp.capabilities")

---@type slang-server.ui.Subcommand
M.findInstance = {
   desc = "Find an instance in the compiled design",
   required_commands = {
      "slang.getScopes",
      "slang.getScopesByModule",
      "slang.getInstancesOfModule",
   },
   context = capabilities.get_source_context,
   impl = function(args, opts, bufnr)
      local search = require("slang-server.navigation.findInstance")
      search.start(bufnr, function(inst_path)
         local navigation = require("slang-server.navigation")
         if navigation.state.open then
            local hierarchy = require("slang-server.navigation.hierarchy")
            hierarchy.reveal(inst_path, { focus = true })
         else
            navigation.show(inst_path, true)
         end
      end)
   end,
}

return M
