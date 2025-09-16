-- Main module file

local config = require("slang-server._core.config")

---@class SlangModule
local M = {}

config.initialise()

---@param opts slang-server.config.Configuration?
M.setup = function(opts)
   config.update(opts)
end

return M
