local _CMD = "SlangServer"

require("slang-server._core.version")

require("slang-server._lsp.clientCommands").register()

local subcommands = require("slang-server._commands")

---@param opts table
local function slang_server(opts)
   local fargs = opts.fargs

   local subcommand_key = fargs[1]

   local args = #fargs > 1 and vim.list_slice(fargs, 2, #fargs) or {}

   local subcommand = subcommands[subcommand_key]
   if not subcommand then
      vim.notify(_CMD, vim.log.levels.ERROR)
      return
   end

   local bufnr = subcommand.context(args)
   local capabilities = require("slang-server._lsp.capabilities")
   if not capabilities.get_client(bufnr) then
      vim.notify(
         string.format("slang-server: '%s' requires a buffer with an attached slang-server LSP client.", subcommand_key),
         vim.log.levels.ERROR
      )
      return
   end
   if not capabilities.check_or_notify(bufnr, subcommand.required_commands) then
      return
   end

   subcommand.impl(args, opts, bufnr)
end

vim.api.nvim_create_user_command(_CMD, slang_server, {
   nargs = "+",
   desc = "SlangServer",
   complete = function(arg_lead, cmdline, _)
      local subcmd_key, subcmd_arg_lead = cmdline:match("^" .. _CMD .. "%s(%S+)%s(.*)$")

      if subcmd_key and subcmd_arg_lead and subcommands[subcmd_key] and subcommands[subcmd_key].complete then
         return subcommands[subcmd_key].complete(subcmd_arg_lead)
      end

      if cmdline:find("^" .. _CMD .. "%s+%w*$") then
         local subcommand_keys = vim.tbl_keys(subcommands)
         return vim.iter(subcommand_keys)
            :filter(function(key)
               return key:find("^" .. arg_lead) ~= nil
            end)
            :totable()
      end
   end,
})

require("slang-server._commands.keymaps").apply()
