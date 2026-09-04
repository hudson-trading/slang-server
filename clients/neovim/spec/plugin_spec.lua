-- wait for hierarchy to finish resolving
---@param buf_name string
local function wait_on(buf_name)
   local lines

   local buf = nil
   local win = nil
   for _, candidate_win in ipairs(vim.api.nvim_list_wins()) do
      local this_buf = vim.api.nvim_win_get_buf(candidate_win)
      local this_name = vim.api.nvim_buf_get_name(this_buf)

      if string.find(this_name, buf_name, 1, true) then
         buf = this_buf
         win = candidate_win
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

   return lines, win
end

local function find_line(lines, text)
   for index, line in ipairs(lines) do
      if string.find(line, text, 1, true) then
         return index
      end
   end
   error("Could not find line containing " .. text)
end

local function press_key(win, line, key)
   vim.api.nvim_set_current_win(win)
   vim.api.nvim_win_set_cursor(win, { line, 0 })
   local mapping = vim.fn.maparg(key, "n", false, true)
   assert.is_function(mapping.callback)
   mapping.callback()
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

-- Number of `:messages` lines already accounted for, so each check only looks
-- at what appeared since the previous one.
local seen_message_lines = 0

---@return string[]
local function message_lines()
   local output = vim.api.nvim_exec2("messages", { output = true }).output or ""
   if output == "" then
      return {}
   end
   return vim.split(output, "\n", { trimempty = true })
end

-- The server reports version mismatches and other problems via
-- window/showMessage, which Neovim's default handler prints to `:messages`
-- rather than raising an error. Nothing the tests do should provoke one, so
-- treat any new line as a failure.
local function assert_no_new_messages()
   local lines = message_lines()
   local new = vim.list_slice(lines, seen_message_lines + 1, #lines)
   seen_message_lines = #lines
   assert.are.same(
      {},
      new,
      "slang-server wrote to :messages during this test (most likely a window/showMessage "
         .. "notification from the server, which Neovim prints instead of raising). New :messages lines:\n  "
         .. table.concat(new, "\n  ")
   )
end

describe("SlangServer", function()
   local capabilities = require("slang-server._lsp.capabilities")

   it("Advertises the Neovim client identity", function()
      local client = capabilities.client_info()
      assert.are.same("neovim-slang", client.name)
      assert.matches("^%d+%.%d+$", client.version)

      local version_file = io.open("../../VERSION", "r")
      if version_file then
         local server_version = version_file:read("*l")
         version_file:close()
         assert.are.same(server_version:match("^%d+%.%d+"), client.version)
      end
   end)

   -- load test SV
   vim.cmd("edit tests/foo.sv")
   vim.cmd("set filetype=systemverilog")
   local source_buf = vim.api.nvim_get_current_buf()
   local source_win = vim.api.nvim_get_current_win()
   -- start slang-server
   local server_bin = os.getenv("SLANG_SERVER_BIN") or "../../build/bin/slang-server"
   local client = vim.lsp.start({
      name = "slang-server",
      cmd = { server_bin },
      filetypes = { "systemverilog" },
      root_dir = vim.uv.cwd(),
      capabilities = capabilities.make_client_capabilities(),
   })
   assert(client)
   local function execute_server_command(command, arguments)
      local done = false
      local response
      local request_error
      vim.lsp.get_client_by_id(client):request(
         "workspace/executeCommand",
         { command = command, arguments = arguments },
         function(err, result)
            request_error = err
            response = result
            done = true
         end,
         source_buf
      )
      assert(vim.wait(5000, function()
         return done
      end))
      assert.is_nil(request_error)
      return response
   end

   local function press_key_and_wait_for_activation(win, line)
      local navigation = require("slang-server.navigation")
      local original_set_active_instance = navigation.set_active_instance
      local activated_path
      navigation.set_active_instance = function(hier_path, on_success)
         original_set_active_instance(hier_path, function()
            activated_path = hier_path
            if on_success then
               on_success()
            end
         end)
      end

      local ok, err = pcall(function()
         press_key(win, line, "<CR>")
         assert(vim.wait(5000, function()
            return activated_path ~= nil
         end))
      end)
      navigation.set_active_instance = original_set_active_instance
      assert(ok, err)
      return activated_path
   end

   local function focus_source()
      if vim.api.nvim_win_is_valid(source_win) then
         vim.api.nvim_set_current_win(source_win)
      end
      if vim.api.nvim_buf_is_valid(source_buf) then
         vim.api.nvim_set_current_buf(source_buf)
      end
   end

   -- wait for client to attach to this buffer
   local success, _ = vim.wait(5000, function()
      return #vim.lsp.get_clients() > 0
   end)
   assert(success)
   -- load the plugin, not sure if this is the canonical way to do this from busted
   vim.cmd("luafile ftplugin/systemverilog.lua")
   vim.cmd("luafile lua/slang-server/init.lua")
   -- compile design
   execute_server_command("slang.setTopLevel", { vim.api.nvim_buf_get_name(source_buf) })

   before_each(focus_source)

   after_each(function()
      local navigation = require("slang-server.navigation")
      local ok, err = pcall(function()
         if navigation.state.open then
            navigation.on_close()
         end
      end)
      focus_source()
      assert(ok, err)
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

   it("Single-instance code lenses reveal an open hierarchy", function()
      local client_commands = require("slang-server._lsp.clientCommands")
      local original_reveal = client_commands.revealInHierarchy
      local shown

      local ok, err = pcall(function()
         client_commands.revealInHierarchy = function(params)
            shown = params
         end
         vim.lsp.commands["slang.showInHierarchy"]({
            arguments = { { hierPath = "top.only" } },
         })
      end)
      client_commands.revealInHierarchy = original_reveal

      assert(ok, err)
      assert.are.same({ hierPath = "top.only" }, shown)
   end)

   it("Searches hierarchy members through the server API", function()
      local result
      require("slang-server").search_hierarchy("the_sub", function(resp)
         result = resp
      end)

      assert(vim.wait(5000, function()
         return result ~= nil
      end))
      -- Fuzzy path matches include the four instances and their parameter children.
      assert.are.same(8, result.totalResults)
      assert.is_true(#result.matches <= 100)
      assert.is_true(vim.iter(result.matches):any(function(item)
         return item.path == "foo.gen_loop[2].the_sub"
      end))
   end)

   -- Catches anything the server complained about, including messages emitted
   -- during startup, which land before the first test runs.
   after_each(assert_no_new_messages)

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

   it("Hierarchy renders interface ports and tolerates missing decorations", function()
      local request_buf = vim.api.nvim_get_current_buf()
      local function set_top_level(path)
         local response, request_error = vim.lsp.get_client_by_id(client):request_sync(
            "workspace/executeCommand",
            {
               command = "slang.setTopLevel",
               arguments = { vim.fn.fnamemodify(path, ":p") },
            },
            5000,
            request_buf
         )
         assert(response, request_error)
         assert.is_nil(response.err)
      end

      set_top_level("tests/interface_ports.sv")
      vim.cmd("SlangServer hierarchy interface_top.u")
      local lines = wait_on("Slang-server: Hierarchy")
      find_line(lines, "single_bus")
      find_line(lines, "bus_array")
      vim.api.nvim_buf_delete(0, { force = true })

      local config = require("slang-server._core.config").CONFIG
      local interfaceport = config.kinds.interfaceport
      config.kinds.interfaceport = nil
      local ok, err = pcall(function()
         vim.cmd("SlangServer hierarchy interface_top.u")
         lines = wait_on("Slang-server: Hierarchy")
         find_line(lines, "? single_bus")
         vim.api.nvim_buf_delete(0, { force = true })
      end)
      config.kinds.interfaceport = interfaceport
      set_top_level("tests/foo.sv")
      assert(ok, err)
   end)

   it("Cell selections activate instances", function()
      execute_server_command("slang.setTopLevel", { vim.api.nvim_buf_get_name(source_buf) })
      vim.cmd("SlangServer hierarchy")

      local lines, cells_win = wait_on("Slang-server: Cells")
      press_key(cells_win, find_line(lines, "sub (4)"), "<Space>")
      lines, cells_win = wait_on("Slang-server: Cells")
      local target = "foo.gen_loop[2].the_sub"
      local activated = press_key_and_wait_for_activation(cells_win, find_line(lines, target))
      assert.are.same(target, activated)

      local active = execute_server_command("slang.getActiveInstance", { "sub" })
      assert.are.same(target, active.instPath)

      local _, hierarchy_win = wait_on("Slang-server: Hierarchy")
      vim.api.nvim_set_current_win(hierarchy_win)
      vim.api.nvim_buf_delete(0, { force = true })
   end)

   it("Hierarchy selections activate instances", function()
      execute_server_command("slang.setTopLevel", { vim.api.nvim_buf_get_name(source_buf) })
      local target = "foo.gen_loop[3].the_sub"
      vim.cmd("SlangServer hierarchy " .. target)

      local lines, hierarchy_win = wait_on("Slang-server: Hierarchy")
      local activated = press_key_and_wait_for_activation(
         hierarchy_win,
         find_line(lines, "the_sub sub")
      )
      assert.are.same(target, activated)

      local active = execute_server_command("slang.getActiveInstance", { "sub" })
      assert.are.same(target, active.instPath)
      vim.api.nvim_set_current_win(hierarchy_win)
      vim.api.nvim_buf_delete(0, { force = true })
   end)
end)

-- TODO (tests)
-- * cone tracing
-- * WCP
