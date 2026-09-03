local M = {}

-- Identity advertised to the server through the `experimental.slangClient`
-- initialization capability. The server warns when our major/minor version is
-- older than its own; we warn when the server's is older than ours. Patch
-- differences are ignored in both directions. See docs/development/versioning.md.
M.CLIENT_NAME = "neovim-slang"
M.CLIENT_VERSION = "0.2"

local UPGRADE_HINT = "Please upgrade slang-server and possibly also this plugin."
local SOURCE_FILETYPES = { "verilog", "systemverilog" }

---Parse the major and minor components of a semantic version. Tolerates a
---leading "v", a "+githash" build suffix, and surrounding whitespace, e.g.
---the server's "0.2.10+0492171\n".
---@param v string
---@return integer? major, integer? minor
local function parse_major_minor(v)
   v = vim.trim(v):gsub("^v", "")
   local major, minor = v:match("^(%d+)%.(%d+)")
   if not major then
      return nil, nil
   end
   return tonumber(major), tonumber(minor)
end

---@return boolean true when `have` is an older major/minor than `want`
local function is_older_major_minor(have, want)
   local hM, hm = parse_major_minor(have)
   local wM, wm = parse_major_minor(want)
   if not hM or not wM then
      return false
   end
   if hM ~= wM then
      return hM < wM
   end
   return hm < wm
end

---This plugin's name and version, as sent to the server.
---@return { name: string, version: string }
function M.client_info()
   return { name = M.CLIENT_NAME, version = M.CLIENT_VERSION }
end

---Add this plugin's identity to a set of LSP client capabilities.
---@param base lsp.ClientCapabilities? defaults to Neovim's stock capabilities
---@return lsp.ClientCapabilities
function M.make_client_capabilities(base)
   return vim.tbl_deep_extend("force", base or vim.lsp.protocol.make_client_capabilities(), {
      experimental = {
         slangClient = M.client_info(),
      },
   })
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

---@return integer bufnr
function M.get_source_context()
   local bufnr = vim.api.nvim_get_current_buf()
   if M.get_client(bufnr) then
      return bufnr
   end

   local util = require("slang-server.util")
   local source_win = util.last_win({ buflisted = true, filetype = SOURCE_FILETYPES })
   if source_win and M.get_client(source_win.bufnr) then
      return source_win.bufnr
   end

   local source_buf = util.last_buf({ buflisted = true, filetype = SOURCE_FILETYPES })
   if source_buf and M.get_client(source_buf.bufnr) then
      return source_buf.bufnr
   end

   return bufnr
end

-- Per-client cached commands set. Keyed by client.id; populated lazily on the
-- first successful static-check pass for that client and held for the client's
-- lifetime (server_capabilities doesn't change after the LSP initialize
-- handshake). Evicted by the LspDetach autocmd registered below.
---@type table<integer, table<string, true>>
local client_cache = {}

-- Client ids already warned about a version mismatch, so the notification is
-- shown once per server rather than on every command.
---@type table<integer, true>
local version_warned = {}

vim.api.nvim_create_autocmd("LspDetach", {
   group = vim.api.nvim_create_augroup("slang-server.capabilities", { clear = true }),
   callback = function(args)
      client_cache[args.data.client_id] = nil
      version_warned[args.data.client_id] = nil
   end,
})

---Warn if the server predates this plugin's feature set. This is the mirror of
---the server's own check on `experimental.slangClient`: the server warns when we
---are older, we warn when it is. Only advisory -- an older server still runs any
---command it advertises, and unsupported ones are reported individually.
---@param client vim.lsp.Client
local function check_server_version(client)
   if version_warned[client.id] then
      return
   end

   local version = client.server_info and client.server_info.version
   if not version then
      version_warned[client.id] = true
      vim.notify(
         "slang-server: server did not report a version. " .. UPGRADE_HINT,
         vim.log.levels.WARN
      )
      return
   end

   if is_older_major_minor(version, M.CLIENT_VERSION) then
      version_warned[client.id] = true
      vim.notify(
         string.format(
            "slang-server: server v%s is older than this plugin's v%s. Please upgrade slang-server.",
            vim.trim(version),
            M.CLIENT_VERSION
         ),
         vim.log.levels.WARN
      )
   end
end

---Validate a client's static info and return its supported-command set.
---@param client vim.lsp.Client
---@return table<string, true>? commands, string? err_msg
local function get_info(client)
   check_server_version(client)

   local cmds = client_cache[client.id]
   if cmds then
      return cmds, nil
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
