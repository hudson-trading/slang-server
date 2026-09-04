-- Parser for :SlangServer hierarchy

local M = {}
local capabilities = require("slang-server._lsp.capabilities")

---@type slang-server.ui.Subcommand
M.hierarchy = {
   desc = "Open Slang Server hierarchy",
   required_commands = {
      "slang.getScopes",
      "slang.getScopesByModule",
   },
   context = capabilities.get_source_context,
   impl = function(args, opts, bufnr)
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
