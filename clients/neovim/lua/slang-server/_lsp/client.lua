local M = {}

-- LSP commands
---@param bufnr integer
---@param params lsp.ExecuteCommandParams
---@param handlers RespHandlers
local lsp_execute = function(bufnr, params, handlers)
   local command = params.command

   local on_failure = handlers.on_failure or function() end

   local client_found = false
   for _, client in pairs(vim.lsp.get_clients({ bufnr = bufnr })) do
      if client.server_capabilities.executeCommandProvider then
         for _, value in ipairs(client.server_capabilities.executeCommandProvider.commands) do
            if command == value then
               client_found = true
               break
            end
         end
      end
   end

   if not client_found then
      on_failure("No client found")
      return
   end

   local handle = function(resp)
      for _, client_resp in pairs(resp) do
         if client_resp.error then
            on_failure(client_resp.error.message)
            return
         else
            handlers.on_success(client_resp.result)
         end
      end
   end

   vim.lsp.buf_request_all(bufnr, "workspace/executeCommand", params, handle)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { uri: string }
M.setTopLevel = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.setTopLevel",
      arguments = { params.uri },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { uri: string }
M.setBuildFile = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.setBuildFile",
      arguments = { params.uri },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { hierPath: string? }
M.getScope = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.getScope",
      arguments = { params.hierPath or "" },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { hierPath: string }
M.getScopes = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.getScopes",
      arguments = { params.hierPath },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
M.getScopesByModule = function(bufnr, handlers)
   lsp_execute(bufnr, {
      command = "slang.getScopesByModule",
      arguments = {},
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { moduleName: string }
M.getInstancesOfModule = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.getInstancesOfModule",
      arguments = { params.moduleName },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { uri: string }
M.openWaveform = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.openWaveform",
      arguments = { params.uri },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { position: lsp.TextDocumentPositionParams }
M.getInstances = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.getInstances",
      arguments = { params.position },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { path: string, recursive: boolean }
M.addToWaveform = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.addToWaveform",
      arguments = { params },
   }, handlers)
end

return M
