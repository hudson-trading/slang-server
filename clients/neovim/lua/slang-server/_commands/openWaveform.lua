-- Parser for :SlangServer openWaveform

local M = {}
local capabilities = require("slang-server._lsp.capabilities")

---@type slang-server.ui.Subcommand
M.openWaveform = {
   desc = "Open a waveform file",
   required_commands = { "slang.openWaveform" },
   context = capabilities.get_source_context,
   impl = function(args, opts, bufnr)
      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")

      local file = args[1]
      client.openWaveform(bufnr, handlers.defaultHandlers, { uri = file })
   end,
   complete = require("slang-server.util").complete_path,
}

return M
