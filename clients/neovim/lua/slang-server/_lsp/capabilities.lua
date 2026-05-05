local M = {}

M.MIN_SERVER_VERSION = "0.2.2"

local UPGRADE_HINT = "Please upgrade slang-server and possibly also this plugin."

---@param v string
---@return integer, integer, integer
local function parse_version(v)
   v = vim.trim(v):match("^[^+]+") or v
   local major, minor, patch = v:match("^(%d+)%.(%d+)%.(%d+)")
   return tonumber(major) or 0, tonumber(minor) or 0, tonumber(patch) or 0
end

---@param have string
---@param want string
---@return boolean
local function version_at_least(have, want)
   local hM, hm, hp = parse_version(have)
   local wM, wm, wp = parse_version(want)
   if hM ~= wM then
      return hM > wM
   end
   if hm ~= wm then
      return hm > wm
   end
   return hp >= wp
end

---@param bufnr integer
---@return vim.lsp.Client?
function M.get_client(bufnr)
   for _, client in pairs(vim.lsp.get_clients({ bufnr = bufnr })) do
      if client.server_info and client.server_info.name == "slang-server" then
         return client
      end
   end
   return nil
end

-- Per-client cached commands set. Keyed by client.id; populated lazily on the
-- first successful static-check pass for that client and held for the client's
-- lifetime (server_info and server_capabilities don't change after the LSP
-- initialize handshake). Evicted by the LspDetach autocmd registered below.
---@type table<integer, table<string, true>>
local client_cache = {}

vim.api.nvim_create_autocmd("LspDetach", {
   group = vim.api.nvim_create_augroup("slang-server.capabilities", { clear = true }),
   callback = function(args)
      client_cache[args.data.client_id] = nil
   end,
})

---Validate a client's static info and return its supported-command set.
---@param client vim.lsp.Client
---@return table<string, true>? commands, string? err_msg
local function get_info(client)
   local cmds = client_cache[client.id]
   if cmds then
      return cmds, nil
   end

   local version = client.server_info and client.server_info.version
   if version and not version_at_least(version, M.MIN_SERVER_VERSION) then
      return nil,
         string.format(
            "slang-server: server version %s is too old (need >= %s). Please upgrade slang-server.",
            vim.trim(version),
            M.MIN_SERVER_VERSION
         )
   end

   local ecp = client.server_capabilities and client.server_capabilities.executeCommandProvider
   if not (ecp and ecp.commands) then
      return nil, "slang-server: server does not advertise executeCommandProvider. " .. UPGRADE_HINT
   end

   cmds = {}
   for _, value in ipairs(ecp.commands) do
      cmds[value] = true
   end
   client_cache[client.id] = cmds
   return cmds, nil
end

---@param bufnr integer
---@param required_commands string[]
---@return boolean ok, string? err_msg
function M.check(bufnr, required_commands)
   local client = M.get_client(bufnr)
   if not client then
      return false, "slang-server: no slang-server LSP client attached. " .. UPGRADE_HINT
   end

   local cmds, err = get_info(client)
   if not cmds then
      return false, err
   end

   for _, command in ipairs(required_commands) do
      if not cmds[command] then
         return false,
            string.format("slang-server: server does not support LSP command '%s'. %s", command, UPGRADE_HINT)
      end
   end

   return true, nil
end

---@param bufnr integer
---@param required_commands string[]
---@return boolean
function M.check_or_notify(bufnr, required_commands)
   local ok, err = M.check(bufnr, required_commands)
   if not ok then
      vim.notify(err, vim.log.levels.ERROR)
      return false
   end
   return true
end

---@param bufnr integer
---@param command string
---@return boolean ok, string? err_msg
function M.command_supported(bufnr, command)
   return M.check(bufnr, { command })
end

return M
