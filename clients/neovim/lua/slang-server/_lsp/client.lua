local capabilities = require("slang-server._lsp.capabilities")

local M = {}

-- LSP commands
---@param bufnr integer
---@param params lsp.ExecuteCommandParams
---@param handlers RespHandlers
local lsp_execute = function(bufnr, params, handlers)
   local command = params.command

   local on_failure = handlers.on_failure or function() end

   local ok, err = capabilities.command_supported(bufnr, command)
   if not ok then
      on_failure(err)
      return
   end

   local lsp_client = capabilities.get_client(bufnr)
   if not lsp_client then
      on_failure("slang-server: no slang-server LSP client attached")
      return
   end

   local completed = false
   local sent = lsp_client:request("workspace/executeCommand", params, function(request_err, result)
      if completed then
         return
      end
      completed = true
      if request_err then
         on_failure(request_err.message)
      else
         handlers.on_success(result)
      end
   end, bufnr)
   if sent == false and not completed then
      completed = true
      on_failure("slang-server: failed to send workspace/executeCommand request")
   end
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
---@param params { hierPath: string, takeFocus: boolean? }
M.showHierLocation = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.showHierLocation",
      arguments = { {
         hierPath = params.hierPath,
         takeFocus = params.takeFocus or false,
      } },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { query: string }
M.searchHierarchy = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.searchHierarchy",
      arguments = { params.query },
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
---@param params { path: string }
M.getModulesInFile = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.getModulesInFile",
      arguments = { params.path },
   }, handlers)
end

---@param bufnr integer
---@param handlers RespHandlers
---@param params { moduleName: string, textDocument: { uri: string }, position: slang-server.SourceLoc }
M.getActiveInstanceAtPosition = function(bufnr, handlers, params)
   lsp_execute(bufnr, {
      command = "slang.getActiveInstanceAtPosition",
      arguments = { params },
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
