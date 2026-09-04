-- Parser for :SlangServer setBuildFile

local M = {}
local capabilities = require("slang-server._lsp.capabilities")

---@type slang-server.ui.Subcommand
M.setBuildFile = {
   desc = "Set the Slang Server build file",
   required_commands = { "slang.setBuildFile" },
   context = capabilities.get_source_context,
   impl = function(args, opts, bufnr)
      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")

      local file = args[1]
      client.setBuildFile(bufnr, handlers.defaultHandlers, { uri = file })
   end,
   complete = require("slang-server.util").complete_path,
}

return M
