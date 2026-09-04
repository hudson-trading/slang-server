local M = {}
local capabilities = require("slang-server._lsp.capabilities")

---@param lens lsp.CodeLens
---@return boolean
local function is_active_selection_lens(lens)
   local command = lens.command
   local params = command and command.arguments and command.arguments[1]
   return command ~= nil
      and command.command == "slang.quickPick"
      and type(params) == "table"
      and params.onSelectCommand == "slang.activateInstance"
      and params.interactionSource == "codeLensSelect"
end

---@type slang-server.ui.Subcommand
M.selectActive = {
   desc = "Select the active instance or generate iteration under the cursor",
   required_commands = { "slang.activateInstance" },
   context = capabilities.get_current_context,
   impl = function(args, opts, bufnr)
      local client = capabilities.get_client(bufnr)
      assert(client)

      local line = vim.api.nvim_win_get_cursor(0)[1] - 1
      local matches = vim.iter(vim.lsp.codelens.get(bufnr)):filter(function(lens)
         return lens.range.start.line == line and is_active_selection_lens(lens)
      end):totable()

      local function run(lens)
         if lens then
            client:exec_cmd(lens.command, { bufnr = bufnr })
         end
      end

      if #matches == 0 then
         vim.notify("No active-selection code lens found on the current line", vim.log.levels.WARN)
      elseif #matches == 1 then
         run(matches[1])
      else
         vim.ui.select(matches, {
            prompt = "Select active-selection code lens",
            format_item = function(lens)
               return lens.command.title
            end,
         }, run)
      end
   end,
}

return M
