-- Parser for :SlangServer openWaveform

local M = {}

---@type slang-server.ui.Subcommand
M.openWaveform = {
   impl = function(args, opts)
      local capabilities = require("slang-server._lsp.capabilities")
      local bufnr = vim.api.nvim_get_current_buf()
      if not capabilities.check_or_notify(bufnr, { "slang.openWaveform" }) then
         return
      end

      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")

      local file = args[1]
      client.openWaveform(bufnr, handlers.defaultHandlers, { uri = file })
   end,
   complete = require("slang-server.util").complete_path,
}

return M
