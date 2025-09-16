-- Parser for :SlangServer hierarchy

local M = {}

---@type slang-server.ui.Subcommand
M.hierarchy = {
   impl = function(args, opts)
      local top = args[1]
      require("slang-server.hierarchy").show(top or "")
   end,
}

return M
