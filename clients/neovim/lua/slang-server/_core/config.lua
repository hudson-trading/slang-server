local M = {}

---@type slang-server.config.Configuration
M.CONFIG = {}

vim.g.loaded_slang_server = false

---@type slang-server.config.Configuration
local default_config = {
   hierarchy = {
      position = "left",
      size = 40,
   },
   kinds = {
      instance = { icon = "", hl = "SlangServerInstance" },
      instancearray = { icon = "", hl = "SlangServerInstanceArray" },
      scope = { icon = "󰅩", hl = "SlangServerScope" },
      scopearray = { icon = "󰅩", hl = "SlangServerScopeArray" },
      package = { icon = "📦", hl = "SlangServerPackage" },
      port = {
         input = { icon = "", hl = "SlangServerPortInput" },
         output = { icon = "", hl = "SlangServerPortOutput" },
         inout = { icon = "", hl = "SlangServerPortInout" },
      },
      param = { icon = "", hl = "SlangServerParam" },
      logic = { icon = "󱒖", hl = "SlangServerLogic" },
      reg = { icon = "", hl = "SlangServerReg" },
   },
   highlights = {
      SlangServerInstance = { fg = "#efbd5d" },
      SlangServerInstanceArray = { fg = "#efbd5d" },
      SlangServerScope = { fg = "#41a7fc" },
      SlangServerScopeArray = { fg = "#41a7fc" },
      SlangServerPackage = { fg = "#f48fb1" },
      SlangServerPortInput = { fg = "#8bcd5b" },
      SlangServerPortOutput = { fg = "#f65866" },
      SlangServerPortInout = { fg = "#34bfd0" },
      SlangServerParam = { fg = "#c75ae8" },
      SlangServerLogic = { fg = "#dd9046" },
      SlangServerReg = { fg = "#dd9046" },
   },
}

M.initialise = function()
   if not vim.g.loaded_slang_server then
      M.update(default_config)
      M.update(vim.g.slang_server_config)

      vim.g.loaded_slang_server = true
   end
end

---@param opts slang-server.config.Configuration?
M.update = function(opts)
   M.CONFIG = vim.tbl_deep_extend("force", M.CONFIG, opts or {})
end

return M
