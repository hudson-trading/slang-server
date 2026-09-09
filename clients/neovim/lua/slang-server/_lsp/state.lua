local M = {}

---@type table<integer, slang-server.lsp.ClientState>
M.clients = {}

---@param client_id integer
---@param active_path string
---@return slang-server.lsp.ClientState
function M.set_active_path(client_id, active_path)
   local state = M.clients[client_id] or {}
   state.active_path = active_path
   M.clients[client_id] = state
   return state
end

---@param client_id integer
---@return string?
function M.get_active_path(client_id)
   local state = M.clients[client_id]
   return state and state.active_path
end

vim.api.nvim_create_autocmd("LspDetach", {
   group = vim.api.nvim_create_augroup("slang-server.state", { clear = true }),
   callback = function(args)
      local client_id = args.data.client_id
      vim.schedule(function()
         local client = vim.lsp.get_client_by_id(client_id)
         if not client or not next(client.attached_buffers or {}) then
            M.clients[client_id] = nil
         end
      end)
   end,
})

return M
