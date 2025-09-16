local _CMD = "SlangServer"

local subcommands = {}
subcommands = vim.tbl_deep_extend("error", subcommands, require("slang-server._commands.setTopLevel"))
subcommands = vim.tbl_deep_extend("error", subcommands, require("slang-server._commands.setBuildFile"))
subcommands = vim.tbl_deep_extend("error", subcommands, require("slang-server._commands.hierarchy"))
subcommands = vim.tbl_deep_extend("error", subcommands, require("slang-server._commands.openWaveform"))
subcommands = vim.tbl_deep_extend("error", subcommands, require("slang-server._commands.addToWaves"))

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

   subcommand.impl(args, opts)
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
