-- wait for hierarchy to finish resolving
---@param buf_name string
local function wait_on(buf_name)
   local lines

   local buf = nil
   for _, win in ipairs(vim.api.nvim_list_wins()) do
      local this_buf = vim.api.nvim_win_get_buf(win)
      local this_name = vim.api.nvim_buf_get_name(this_buf)

      if string.find(this_name, buf_name, 1, true) then
         buf = this_buf
         break
      end
   end
   assert(buf)

   local success, _ = vim.wait(5000, function()
      lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
      if #lines < 1 then
         return false
      end
      for _, line in ipairs(lines) do
         if string.find(line, "Loading ") then
            return false
         end
      end
      return true
   end)
   assert(success, lines)

   return lines
end

---@param fn fun()
---@return string[]
local function capture_notifications(fn)
   local old_notify = vim.notify
   local messages = {}
   vim.notify = function(msg, ...)
      messages[#messages + 1] = msg
   end

   local ok, err = pcall(fn)
   vim.notify = old_notify
   assert(ok, err)
   return messages
end

describe("SlangServer", function()
   -- load test SV
   vim.cmd("edit tests/foo.sv")
   vim.cmd("set filetype=systemverilog")
   -- start slang-server
   local server_bin = os.getenv("SLANG_SERVER_BIN") or "../../build/bin/slang-server"
   local client = vim.lsp.start({
      name = "slang-server",
      cmd = { server_bin },
      filetypes = { "systemverilog" },
      root_dir = vim.uv.cwd(),
   })
   assert(client)
   -- wait for client to attach to this buffer
   local success, _ = vim.wait(5000, function()
      return #vim.lsp.get_clients() > 0
   end)
   assert(success)
   -- load the plugin, not sure if this is the canonical way to do this from busted
   vim.cmd("luafile ftplugin/systemverilog.lua")
   vim.cmd("luafile lua/slang-server/init.lua")
   -- compile design
   vim.cmd("SlangServer setTopLevel")

   it("Routes hierarchy navigation through server commands", function()
      local lsp = require("slang-server._lsp.client")
      local capabilities = require("slang-server._lsp.capabilities")
      local original_supported = capabilities.command_supported
      local original_get_client = capabilities.get_client
      local requests = {}

      local ok, err = pcall(function()
         capabilities.command_supported = function()
            return true
         end
         capabilities.get_client = function()
            return {
               request = function(_, method, params, callback, bufnr)
                  requests[#requests + 1] = { bufnr = bufnr, method = method, params = params }
                  callback(nil, nil)
                  return true
               end,
            }
         end

         local handlers = { on_success = function() end }
         lsp.showHierLocation(7, handlers, { hierPath = "top.child", takeFocus = true })
      end)

      capabilities.command_supported = original_supported
      capabilities.get_client = original_get_client
      assert(ok, err)

      assert.are.same({
         bufnr = 7,
         method = "workspace/executeCommand",
         params = {
            command = "slang.showHierLocation",
            arguments = { { hierPath = "top.child", takeFocus = true } },
         },
      }, requests[1])
   end)

   it("Targets the same source window and buffer for hierarchy navigation", function()
      local navigation = require("slang-server.navigation")
      local lsp = require("slang-server._lsp.client")
      local capabilities = require("slang-server._lsp.capabilities")
      local original_source_win = rawget(navigation.state, "sv_win")
      local original_get_client = capabilities.get_client
      local source_win = {
         bufnr = vim.api.nvim_get_current_buf(),
         winid = vim.api.nvim_get_current_win(),
         winnr = vim.api.nvim_get_current_win(),
      }
      navigation.state.sv_win = source_win

      local original_show = lsp.showHierLocation
      local request
      local ok, err = pcall(function()
         capabilities.get_client = function(bufnr)
            if bufnr == source_win.bufnr then
               return {}
            end
         end
         lsp.showHierLocation = function(bufnr, _, params)
            request = {
               bufnr = bufnr,
               current_win = vim.api.nvim_get_current_win(),
               params = params,
            }
         end

         navigation.show_hier_location("top.child")
      end)
      lsp.showHierLocation = original_show
      capabilities.get_client = original_get_client
      navigation.state.sv_win = original_source_win

      assert(ok, err)
      assert.are.same(source_win.bufnr, request.bufnr)
      assert.are.same(source_win.winid, request.current_win)
      assert.are.same({ hierPath = "top.child", takeFocus = true }, request.params)
   end)

   it("Does not request hierarchy navigation without a valid source window", function()
      local navigation = require("slang-server.navigation")
      local lsp = require("slang-server._lsp.client")
      local original_source_win = rawget(navigation.state, "sv_win")
      local original_show = lsp.showHierLocation
      local requested = false

      navigation.state.sv_win = false
      lsp.showHierLocation = function()
         requested = true
      end
      local messages = capture_notifications(function()
         navigation.show_hier_location("top.child")
      end)
      lsp.showHierLocation = original_show
      navigation.state.sv_win = original_source_win

      assert.is_false(requested)
      assert.are.same({ "Cannot jump to location: invalid target window" }, messages)
   end)

   it("Generic quick pick dispatches its selected value", function()
      local client_commands = require("slang-server._lsp.clientCommands")
      local original_select = vim.ui.select
      local original_execute = client_commands.executeServerCommand
      local command
      local selected

      local ok, err = pcall(function()
         client_commands.executeServerCommand = function(selected_command, value)
            command = selected_command
            selected = value
         end
         vim.ui.select = function(items, options, on_choice)
            assert.are.same("Pick one", options.prompt)
            assert.are.same("second (current)", options.format_item(items[2]))
            on_choice(items[2])
         end
         vim.lsp.commands["slang.quickPick"]({
            arguments = {
               {
                  placeholder = "Pick one",
                  items = {
                     { label = "first", value = 1 },
                     { label = "second", description = "(current)", value = 2 },
                  },
                  onSelectCommand = "test.callback",
               },
            },
         }, { bufnr = 0 })
      end)
      vim.ui.select = original_select
      client_commands.executeServerCommand = original_execute

      assert(ok, err)
      assert.are.same("test.callback", command)
      assert.are.same(2, selected)
   end)

   it("Active instance notifications reveal an open hierarchy", function()
      local client_commands = require("slang-server._lsp.clientCommands")
      local navigation = require("slang-server.navigation")
      local hierarchy = require("slang-server.navigation/hierarchy")
      local original_open = hierarchy.open_remainder
      local original_state = navigation.state.open
      local revealed

      local ok, err = pcall(function()
         hierarchy.open_remainder = function(parent, root, path, from_cell)
            revealed = { parent, root, path, from_cell }
         end

         navigation.state.open = false
         client_commands.activeInstanceChanged(nil, { hierPath = "top.hidden" })
         assert.is_nil(revealed)

         navigation.state.open = true
         client_commands.activeInstanceChanged(nil, { hierPath = "top.visible" })
         assert.are.same({ nil, true, "top.visible", false }, revealed)
      end)
      hierarchy.open_remainder = original_open
      navigation.state.open = original_state

      assert(ok, err)
   end)

   it("Searches hierarchy members through the server API", function()
      local result
      require("slang-server").search_hierarchy("the_sub", function(resp)
         result = resp
      end)

      assert(vim.wait(5000, function()
         return result ~= nil
      end))
      assert.is_true(result.totalResults >= 4)
      assert.is_true(#result.matches <= 100)
      assert.is_true(vim.iter(result.matches):any(function(item)
         return item.path == "foo.gen_loop[2].the_sub"
      end))
   end)

   it("Hierarchy no args", function()
      vim.cmd("SlangServer hierarchy")
      local lines = wait_on("Slang-server: Hierarchy")
      local expected = [=[
   foo foo]=]
      assert.are.same(expected, table.concat(lines, "\n"))
      lines = wait_on("Slang-server: Cells")
      expected = [=[
  foo (1)
   └╴foo
  sub (4)]=]
      assert.are.same(expected, table.concat(lines, "\n"))
      vim.api.nvim_buf_delete(0, { force = true })
   end)

   it("Focuses an existing hierarchy window", function()
      local navigation = require("slang-server.navigation")
      local hierarchy = package.loaded["slang-server.navigation/hierarchy"]
         or package.loaded["slang-server.navigation.hierarchy"]
         or require("slang-server.navigation.hierarchy")
      local original_open = navigation.state.open
      local original_split = hierarchy.state.split
      local original_reveal = hierarchy.reveal
      local original_is_valid = vim.api.nvim_win_is_valid
      local original_set_current = vim.api.nvim_set_current_win
      local focused

      local ok, err = pcall(function()
         navigation.state.open = true
         hierarchy.state.split = { winid = 42 }
         hierarchy.reveal = function() end
         vim.api.nvim_win_is_valid = function(winid)
            return winid == 42
         end
         vim.api.nvim_set_current_win = function(winid)
            focused = winid
         end

         navigation.show("")
         assert.are.same(42, focused)
      end)
      navigation.state.open = original_open
      hierarchy.state.split = original_split
      hierarchy.reveal = original_reveal
      vim.api.nvim_win_is_valid = original_is_valid
      vim.api.nvim_set_current_win = original_set_current
      assert(ok, err)
   end)

   it("Explicit commands use the source buffer when focus is in the hierarchy panel", function()
      vim.cmd("SlangServer hierarchy")
      wait_on("Slang-server: Hierarchy")

      local messages = capture_notifications(function()
         vim.cmd("SlangServer setTopLevel tests/foo.sv")
      end)
      for _, msg in ipairs(messages) do
         assert.is_nil(string.find(msg, "no slang-server LSP client attached", 1, true))
      end

      vim.api.nvim_buf_delete(0, { force = true })
   end)

   it("Context-sensitive commands require source buffer focus", function()
      vim.cmd("SlangServer hierarchy")
      wait_on("Slang-server: Hierarchy")

      local messages = capture_notifications(function()
         vim.cmd("SlangServer setTopLevel")
         vim.cmd("SlangServer addToWaves")
      end)

      assert.are.same({
         "slang-server: setTopLevel without a file must be run from a buffer with an attached slang-server LSP client.",
         "slang-server: addToWaves must be run from a buffer with an attached slang-server LSP client.",
      }, messages)

      for _, msg in ipairs(messages) do
         assert.is_nil(string.find(msg, "Please upgrade slang-server", 1, true))
      end

      vim.api.nvim_buf_delete(0, { force = true })
   end)

   it("Hierarchy with scope arg", function()
      vim.cmd("SlangServer hierarchy foo.gen_loop[2].the_sub")
      local lines = wait_on("Slang-server: Hierarchy")
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
      lines = wait_on("Slang-server: Cells")
      expected = [=[
  foo (1)
   └╴foo
  sub (4)]=]
      assert.are.same(expected, table.concat(lines, "\n"))
      vim.api.nvim_buf_delete(0, { force = true })
   end)
end)

-- TODO (tests)
-- * cone tracing
-- * WCP
