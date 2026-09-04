local M = {}

---@type slang-server.config.Configuration
M.CONFIG = {}

---@type slang-server.config.Configuration
local default_config = {
   -- Navigation split window layout and buffer-local mappings
   navigation = {
      position = "left",
      width = 50,
      wrap = false,
      hierarchy = {
         keymaps = {
            yank_path = "yn", -- Yank the selected node's full hierarchical path
            yank_value = "yv", -- Yank the selected signal or parameter value
            yank_file = "yf", -- Yank the path of the file containing the selected node
            jump = "<cr>", -- Reveal the selected node in its source buffer
            jump_to_declaration = "gd", -- Reveal the selected node's declaration
            toggle = "<space>", -- Expand or collapse the selected hierarchy node
            close = "q",
            help = "?",
         },
      },
      cells = {
         show = true,
         height = 25,
         keymaps = {
            jump = "<cr>", -- Reveal the selected module or instance in the hierarchy
            toggle = "<space>", -- Expand or collapse a module's instance list
            find_instance = "/", -- Search all instances and reveal the selection in hierarchy
            close = "q",
            help = "?",
         },
      },
   },
   -- Global command mappings; disabled by default
   keymaps = {
      -- Each mapping inherits enable_defaults unless it sets enabled explicitly.
      -- For example, enable_defaults = true with focus = { enabled = false }
      -- enables every default mapping except focus.
      enable_defaults = false,
      hierarchy = { key = "<leader>vh" }, -- Open the design hierarchy
      findInstance = { key = "<leader>vi" }, -- Find an instance and reveal it in the hierarchy
      focus = { key = "<leader>vf" }, -- Reveal the source object under the cursor in the hierarchy
      selectActive = { key = "<leader>va" }, -- Select the active instance or generate iteration under the cursor
   },
   -- Icon and highlight group for each element kind
   kinds = {
      instance = { icon = "", hl = "SlangServerInstance" },
      instancearray = { icon = "", hl = "SlangServerInstanceArray" },
      scope = { icon = "󰅩", hl = "SlangServerScope" },
      scopearray = { icon = "󰅩", hl = "SlangServerScopeArray" },
      interfaceport = { icon = "󰈀", hl = "SlangServerInterfacePort" },
      interfaceportarray = { icon = "󰈀", hl = "SlangServerInterfacePortArray" },
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
   -- Default colors for the highlight groups; can be overridden by your colorscheme
   highlights = {
      SlangServerInstance = { fg = "#efbd5d" },
      SlangServerInstanceArray = { fg = "#efbd5d" },
      SlangServerScope = { fg = "#41a7fc" },
      SlangServerScopeArray = { fg = "#41a7fc" },
      SlangServerInterfacePort = { fg = "#34bfd0" },
      SlangServerInterfacePortArray = { fg = "#34bfd0" },
      SlangServerPackage = { fg = "#f48fb1" },
      SlangServerPortInput = { fg = "#8bcd5b" },
      SlangServerPortOutput = { fg = "#f65866" },
      SlangServerPortInout = { fg = "#34bfd0" },
      SlangServerParam = { fg = "#c75ae8" },
      SlangServerLogic = { fg = "#dd9046" },
      SlangServerReg = { fg = "#dd9046" },
   },
}

---@param opts slang-server.config.Configuration?
M.update = function(opts)
   M.CONFIG = vim.tbl_deep_extend("force", M.CONFIG, opts or {})
end

M.update(default_config)

-- User config can be provided as a vim global, rather than an argument to setup()
M.update(vim.g.slang_server_config)

return M
