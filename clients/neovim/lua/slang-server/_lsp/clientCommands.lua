local M = {}

---@param command string
---@param value any
---@param ctx table
M.executeServerCommand = function(command, value, ctx)
   local client = vim.lsp.get_client_by_id(ctx.client_id)
   if not client then
      vim.notify(
         "slang-server: quick pick LSP client is no longer available",
         vim.log.levels.ERROR
      )
      return
   end
   client:request(
      "workspace/executeCommand",
      { command = command, arguments = { value } },
      function(err)
         if err then
            vim.notify(err.message, vim.log.levels.ERROR)
         end
      end,
      ctx.bufnr
   )
end

---@param command lsp.Command
---@param ctx table
M.quickPick = function(command, ctx)
   local params = command.arguments and command.arguments[1]
   if
      type(params) ~= "table"
      or type(params.items) ~= "table"
      or type(params.onSelectCommand) ~= "string"
   then
      vim.notify("slang-server: invalid quick pick command", vim.log.levels.ERROR)
      return
   end

   vim.ui.select(params.items, {
      prompt = params.placeholder,
      format_item = function(item)
         if item.description then
            return item.label .. " " .. item.description
         end
         return item.label
      end,
   }, function(item)
      if not item then
         return
      end

      M.executeServerCommand(params.onSelectCommand, item.value, ctx)
   end)
end

---@param _ lsp.ResponseError?
---@param params { hierPath: string }
M.activeInstanceChanged = function(_, params)
   local navigation = require("slang-server.navigation")
   if not navigation.state.open then
      return
   end
   require("slang-server.navigation.hierarchy").reveal(params.hierPath)
end

M.register = function()
   vim.lsp.commands = vim.lsp.commands or {}
   vim.lsp.commands["slang.quickPick"] = M.quickPick
   vim.lsp.handlers["slang/activeInstanceChanged"] = M.activeInstanceChanged
end

return M
