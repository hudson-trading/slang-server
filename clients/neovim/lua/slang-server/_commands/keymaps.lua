local M = {}

M.apply = function()
   local config = require("slang-server._core.config").CONFIG
   local commands = require("slang-server._commands")
   local keymaps = config.keymaps or {}

   for command_name, mapping in pairs(keymaps) do
      local command = commands[command_name]
      if command then
         local enabled = mapping.enabled
         if enabled == nil then
            enabled = keymaps.enable_defaults
         end
         if enabled and mapping.key and mapping.key ~= "" then
            local mapped_command = command_name
            vim.keymap.set("n", mapping.key, function()
               vim.cmd("SlangServer " .. mapped_command)
            end, { desc = command.desc })
         end
      end
   end
end

return M
