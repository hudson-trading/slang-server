-- Main module file

require("slang-server._core.version")

local config = require("slang-server._core.config")

---@class SlangModule
local M = {}

---@param opts slang-server.config.Configuration?
M.setup = function(opts)
   config.update(opts)
end

---Create an LSP command that loads server.json environment overrides on every launch.
---@param command string[]? Executable and arguments; defaults to { "slang-server" }
---@param resolved_config table? Resolved client config, required on Neovim 0.10
---@return function
M.server_cmd = function(command, resolved_config)
   command = command or { "slang-server" }
   return function(dispatchers, client_config)
      client_config = assert(
         client_config or resolved_config,
         "server_cmd requires a resolved client config on Neovim 0.10"
      )
      local root = client_config.root_dir
      if client_config.workspace_folders and client_config.workspace_folders[1] then
         root = vim.uri_to_fname(client_config.workspace_folders[1].uri)
      end
      return vim.lsp.rpc.start(command, dispatchers, {
         cwd = client_config.cmd_cwd,
         detached = client_config.detached,
         env = vim.tbl_extend(
            "force",
            client_config.cmd_env or {},
            require("slang-server.environment").load(root)
         ),
      })
   end
end

return M
