-- wait for hierarchy to finish resolving
local function wait_on_hier()
   local lines

   local success, _ = vim.wait(5000, function()
      lines = vim.api.nvim_buf_get_lines(0, 0, -1, false)
      if #lines < 1 then
         return false
      end
      for _, line in ipairs(lines) do
         if string.find(line, "Loading scope") then
            return false
         end
      end
      return true
   end)
   assert(success)

   return lines
end

describe("SlangServer", function()
   -- load test SV
   vim.cmd("edit tests/foo.sv")
   vim.cmd("set filetype=systemverilog")
   -- start slang-server
   local client = vim.lsp.start({
      name = "slang-server",
      -- NOCOMMIT -- pick this up from slang-server release
      cmd = {
         "slang-server",
      },
      filetypes = { "systemverilog" },
      root_dir = vim.uv.cwd(),
   })
   assert(client)
   -- wait for client to attach to this buffer
   -- TODO -- something not right here, sometimes get:
   -- vim/_editor.lua:0: nvim_exec2(): Vim:No client found
   --
   -- stack traceback:
   --         vim/_editor.lua: in function 'cmd'
   --         spec/example_spec.lua:60: in function <spec/example_spec.lua:57>
   --
   vim.wait(5000, function()
      return #vim.lsp.get_clients() > 0
   end)
   -- load the plugin, not sure if this is the canonical way to do this from busted
   vim.cmd("luafile ftplugin/systemverilog.lua")
   vim.cmd("luafile lua/slang-server/init.lua")
   -- compile design
   vim.cmd("SlangServer setTopLevel")

   it("Hierarchy no args", function()
      vim.cmd("SlangServer hierarchy")
      local lines = wait_on_hier()
      local expected = [=[
   foo foo]=]
      assert.are.same(expected, table.concat(lines, "\n"))
      vim.api.nvim_buf_delete(0, { force = true })
   end)

   it("Hierarchy with scope arg", function()
      vim.cmd("SlangServer hierarchy foo.gen_loop[2].the_sub")
      local lines = wait_on_hier()
      local expected = [=[
   foo foo
   └╴ 󰅩 gen_loop
     ├╴ 󰅩 [0]
     ├╴ 󰅩 [1]
     ├╴ 󰅩 [2]
       ├╴   i integer
       └╴  the_sub sub
         └╴   param int
     └╴ 󰅩 [3]]=]
      assert.are.same(expected, table.concat(lines, "\n"))
      vim.api.nvim_buf_delete(0, { force = true })
   end)
end)

-- TODO (tests)
-- * cone tracing
-- * WCP
