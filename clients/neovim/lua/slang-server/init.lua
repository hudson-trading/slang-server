-- Main module file

local config = require("slang-server._core.config")
local inactive_regions = require("slang-server._lsp.experimental.inactive_regions")

---@class SlangModule
local M = {}

config.initialise()

---@param opts slang-server.config.Configuration?
M.setup = function(opts)
   config.update(opts)

   -- note: this config requires the slang_server to be explicitly named slang_server
   vim.lsp.config("slang_server", {
      capabilities = {
         experimental = {
            inactiveRegions = {
               inactiveRegions = true,
            },
         },
      },

      handlers = {
         ["textDocument/inactiveRegions"] = inactive_regions.handler,
      },
   })
end

return M
