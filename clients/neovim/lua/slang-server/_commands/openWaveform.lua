-- Parser for :SlangServer openWaveform

local M = {}

---@type slang-server.ui.Subcommand
M.openWaveform = {
   impl = function(args, opts)
      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")

      local file = args[1]
      client.openWaveform(vim.api.nvim_get_current_buf(), handlers.defaultHandlers, { uri = file })
   end,
   complete = require("slang-server.util").complete_path,
}

return M
