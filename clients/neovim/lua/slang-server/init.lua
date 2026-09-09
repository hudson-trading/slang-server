-- Main module file

require("slang-server._core.version")

local config = require("slang-server._core.config")

---@class SlangModule
local M = {}

---@param opts slang-server.config.Configuration?
M.setup = function(opts)
   config.update(opts)
end

return M
