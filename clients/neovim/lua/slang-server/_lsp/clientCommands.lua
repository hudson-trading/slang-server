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
   local completed = false
   local sent = client:request(
      "workspace/executeCommand",
      { command = command, arguments = { value } },
      function(err)
         if completed then
            return
         end
         completed = true
         if err then
            vim.notify(err.message, vim.log.levels.ERROR)
         end
      end,
      ctx.bufnr
   )
   if sent == false and not completed then
      completed = true
      vim.notify("slang-server: failed to send workspace/executeCommand request", vim.log.levels.ERROR)
   end
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

---@param client vim.lsp.Client
local function refresh_codelenses(client)
   for bufnr in pairs(client.attached_buffers or {}) do
      if
         vim.api.nvim_buf_is_loaded(bufnr)
         and client:supports_method(vim.lsp.protocol.Methods.textDocument_codeLens, bufnr)
      then
         vim.lsp.codelens.refresh({ bufnr = bufnr })
      end
   end
end

---@param _ lsp.ResponseError?
---@param params slang-server.lsp.ActivateInstanceParams
---@param ctx lsp.HandlerContext
M.activeInstanceChanged = function(_, params, ctx)
   require("slang-server._lsp.state").set_active_path(ctx.client_id, params.hierPath)

   local lsp_client = vim.lsp.get_client_by_id(ctx.client_id)
   if lsp_client then
      refresh_codelenses(lsp_client)
   end

   local navigation = require("slang-server.navigation")
   if not navigation.state.open then
      return
   end
   require("slang-server.navigation.hierarchy").reveal(params.hierPath, { select = true })
end

M.register = function()
   vim.lsp.commands = vim.lsp.commands or {}
   vim.lsp.commands["slang.quickPick"] = M.quickPick
   vim.lsp.handlers["slang/activeInstanceChanged"] = M.activeInstanceChanged
end

return M
