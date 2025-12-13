-- Parser for :SlangServer addToWaves

local M = {}

---@type slang-server.ui.Subcommand
M.addToWaves = {
   impl = function(args)
      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")
      local ui = require("slang-server._core.ui")

      local recursive = args[1] == "true"

      local bufnr = vim.api.nvim_get_current_buf()

      local first_client = vim.lsp.get_clients({ bufnr = vim.api.nvim_get_current_buf() })[1]
      local position_encoding = first_client and first_client.offset_encoding or 'utf-16'

      client.getInstances(bufnr, {
         on_success = function(resp)
            if resp == nil or next(resp) == nil then
               vim.notify("No instances to add", vim.log.levels.WARN)
               return
            end
            if #resp == 1 then
               client.addToWaveform(bufnr, handlers.defaultHandlers, { path = resp[1], recursive = recursive })
            else
               local lines = {}
               for _, path in ipairs(resp) do
                  table.insert(lines, ui.NuiMenu.item(path))
               end
               local menu = ui.components.menu("Add instance to waves", {
                  lines = lines,
                  on_submit = function(item)
                     client.addToWaveform(bufnr, handlers.defaultHandlers, { path = item.text, recursive = recursive })
                  end,
               })
               menu:mount()
            end
         end,
         on_failure = handlers.defaultOnFailure,
      }, { position = vim.lsp.util.make_position_params(0, position_encoding) })
   end,
}

return M
