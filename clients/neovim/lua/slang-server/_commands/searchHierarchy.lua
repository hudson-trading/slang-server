local M = {}
local capabilities = require("slang-server._lsp.capabilities")

---@type slang-server.ui.Subcommand
M.searchHierarchy = {
   desc = "Search the compiled design hierarchy",
   required_commands = { "slang.searchHierarchy" },
   context = capabilities.get_source_context,
   impl = function(_, _, bufnr)
      require("slang-server.navigation.searchHierarchy").start(bufnr)
   end,
}

return M
