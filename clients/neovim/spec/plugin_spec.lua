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

   it("Merges partial navigation keymap configuration", function()
      local config = require("slang-server._core.config")
      local original = config.CONFIG

      config.update({
         navigation = {
            hierarchy = {
               keymaps = {
                  jump = "g<cr>",
                  toggle = false,
               },
            },
         },
      })

      assert.are.same("g<cr>", config.CONFIG.navigation.hierarchy.keymaps.jump)
      assert.is_false(config.CONFIG.navigation.hierarchy.keymaps.toggle)
      assert.are.same("q", config.CONFIG.navigation.hierarchy.keymaps.close)
      assert.are.same("<cr>", config.CONFIG.navigation.cells.keymaps.jump)
      assert.are.same("left", config.CONFIG.navigation.position)
      assert.are.same(50, config.CONFIG.navigation.width)
      assert.is_false(config.CONFIG.navigation.wrap)
      assert.is_true(config.CONFIG.navigation.cells.show)
      assert.are.same(25, config.CONFIG.navigation.cells.height)

      config.CONFIG = original
   end)

   it("Adds configured mappings and skips disabled mappings", function()
      local navigation = require("slang-server.navigation")
      local mappings = {}
      local spec = {
         impl = function() end,
         desc = "Test mapping",
      }

      navigation.add_mapping(mappings, "g<cr>", spec)
      navigation.add_mapping(mappings, false, spec)

      assert.are.same({ ["g<cr>"] = spec }, mappings)
   end)

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
            title = "Pick an item",
            command = "slang.quickPick",
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

   it("Opens navigation and searches instances from the standalone command", function()
      local navigation = require("slang-server.navigation")
      local original_select = vim.ui.select
      local selected_items
      local selected_path
      local was_open_when_picker_shown
      local focused_line

      local ok, err = pcall(function()
         vim.ui.select = function(items, opts, on_choice)
            selected_items = items
            was_open_when_picker_shown = navigation.state.open
            assert.are.same("Select instance", opts.prompt)
            selected_path = items[3]
            on_choice(selected_path)
         end

         vim.cmd("SlangServer findInstance")
         assert(vim.wait(5000, function()
            return selected_items ~= nil
         end))
         wait_on("Slang-server: Hierarchy")
         local hierarchy = require("slang-server.navigation.hierarchy")
         local cursor = vim.api.nvim_win_get_cursor(hierarchy.state.split.winid)
         focused_line = vim.api.nvim_buf_get_lines(
            hierarchy.state.split.bufnr,
            cursor[1] - 1,
            cursor[1],
            false
         )[1]
      end)

      vim.ui.select = original_select
      local opened_after_selection = navigation.state.open
      if navigation.state.open then
         navigation.on_close()
      end

      assert(ok, err)
      assert.are.same({
         "foo",
         "foo.gen_loop[0].the_sub",
         "foo.gen_loop[1].the_sub",
         "foo.gen_loop[2].the_sub",
         "foo.gen_loop[3].the_sub",
      }, selected_items)
      assert.are.same("foo.gen_loop[1].the_sub", selected_path)
      assert.is_false(was_open_when_picker_shown)
      assert.is_true(opened_after_selection)
      assert.is_not_nil(string.find(focused_line, "the_sub", 1, true))
   end)

   it("Leaves navigation closed when instance selection is cancelled", function()
      local navigation = require("slang-server.navigation")
      local original_select = vim.ui.select
      local picker_shown = false

      local ok, err = pcall(function()
         vim.ui.select = function(_, _, on_choice)
            picker_shown = true
            on_choice(nil)
         end
         vim.cmd("SlangServer findInstance")
         assert(vim.wait(5000, function()
            return picker_shown
         end))
      end)
      vim.ui.select = original_select

      assert(ok, err)
      assert.is_false(navigation.state.open)
   end)

   it("Ignores stale standalone instance search responses", function()
      local command = require("slang-server._commands.findInstance").findInstance
      local capabilities = require("slang-server._lsp.capabilities")
      local lsp = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")
      local original_check = capabilities.check_or_notify
      local original_get_scopes = lsp.getScopesByModule
      local original_get_instances = lsp.getInstancesOfModule
      local original_select = vim.ui.select
      local original_failure = handlers.defaultOnFailure
      local responses = {}
      local instance_responses = {}
      local selections = {}

      local ok, err = pcall(function()
         capabilities.check_or_notify = function()
            return true
         end
         lsp.getScopesByModule = function(_, response_handlers)
            responses[#responses + 1] = response_handlers
         end
         lsp.getInstancesOfModule = function(_, response_handlers)
            instance_responses[#instance_responses + 1] = response_handlers
         end
         vim.ui.select = function(items)
            selections[#selections + 1] = items
         end
         handlers.defaultOnFailure = function() end

         command.impl()
         command.impl()
         responses[2].on_success({
            { declName = "new", instCount = 1, inst = { instPath = "top.new" } },
         })
         responses[1].on_success({
            { declName = "old", instCount = 1, inst = { instPath = "top.old" } },
         })
         assert.are.same(1, #selections)
         assert.are.same({ "top.new" }, selections[1])

         selections = {}
         command.impl()
         command.impl()
         responses[4].on_failure("new search failed")
         responses[3].on_success({
            { declName = "stale", instCount = 1, inst = { instPath = "top.stale" } },
         })

         command.impl()
         responses[5].on_success({ { declName = "old", instCount = 2 } })
         command.impl()
         responses[6].on_success({ { declName = "new", instCount = 2 } })
         instance_responses[2].on_success({ { instPath = "top.new" } })
         instance_responses[1].on_success({ { instPath = "top.old" } })
         assert.are.same({ { "top.new" } }, selections)
      end)

      capabilities.check_or_notify = original_check
      lsp.getScopesByModule = original_get_scopes
      lsp.getInstancesOfModule = original_get_instances
      vim.ui.select = original_select
      handlers.defaultOnFailure = original_failure

      assert(ok, err)
   end)

   it("Discards hierarchy scope results from an older session", function()
      local hierarchy = require("slang-server.navigation.hierarchy")
      local navigation = require("slang-server.navigation")
      local lsp = require("slang-server._lsp.client")
      local original_tree = hierarchy.state.tree
      local original_generation = hierarchy.state.generation
      local original_open_state = navigation.state.open
      local original_source_buf = rawget(navigation.state, "sv_buf")
      local original_get_scope = lsp.getScope
      local deferred
      local new_tree_mutations = 0
      local old_tree = {
         get_node = function()
            return nil
         end,
         set_nodes = function() end,
         render = function() end,
      }
      local new_tree = {
         set_nodes = function()
            new_tree_mutations = new_tree_mutations + 1
         end,
         render = function() end,
      }

      local ok, err = pcall(function()
         hierarchy.state.tree = old_tree
         hierarchy.state.generation = 20
         navigation.state.open = true
         navigation.state.sv_buf = { bufnr = 7 }
         lsp.getScope = function(_, handlers)
            deferred = handlers.on_success
         end

         hierarchy._lazy_open("top", false, nil, false)
         hierarchy.state.tree = new_tree
         hierarchy.state.generation = 21
         deferred({})
      end)

      hierarchy.state.tree = original_tree
      hierarchy.state.generation = original_generation
      navigation.state.open = original_open_state
      navigation.state.sv_buf = original_source_buf
      lsp.getScope = original_get_scope

      assert(ok, err)
      assert.are.same(0, new_tree_mutations)
   end)

   it("Active instance notifications reveal an open hierarchy", function()
      local client_commands = require("slang-server._lsp.clientCommands")
      local navigation = require("slang-server.navigation")
      local hierarchy = require("slang-server.navigation.hierarchy")
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
      local hierarchy = require("slang-server.navigation.hierarchy")
      local cells = require("slang-server.navigation.cells")
      assert.is_false(vim.api.nvim_get_option_value("wrap", { win = hierarchy.state.split.winid }))
      assert.is_false(vim.api.nvim_get_option_value("wrap", { win = cells.state.split.winid }))
      local expected = [=[
   foo foo]=]
      assert.are.same(expected, table.concat(lines, "\n"))
      lines = wait_on("Slang-server: Cells")
      expected = [=[
  foo (1)
   └╴foo
  sub (4)]=]
      assert.are.same(expected, table.concat(lines, "\n"))

      local source_winid = require("slang-server.navigation").state.source_winid
      local source_bufnr = vim.api.nvim_win_get_buf(source_winid)
      for _, state in ipairs({ hierarchy.state, cells.state }) do
         local target_bufnr = vim.api.nvim_create_buf(true, false)
         vim.api.nvim_set_current_win(state.split.winid)
         vim.api.nvim_win_set_buf(state.split.winid, target_bufnr)

         assert.are.same(state.split.bufnr, vim.api.nvim_win_get_buf(state.split.winid))
         assert.are.same(target_bufnr, vim.api.nvim_win_get_buf(source_winid))

         vim.api.nvim_win_set_buf(source_winid, source_bufnr)
         vim.api.nvim_buf_delete(target_bufnr, { force = true })
      end
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

   it("Renders and expands interface ports", function()
      local ok, err = pcall(function()
         local interface_file = vim.fn.fnamemodify("tests/interface_ports.sv", ":p")
         vim.cmd("SlangServer setTopLevel " .. vim.fn.fnameescape(interface_file))
         vim.cmd("SlangServer hierarchy interface_port_top.dut.bus.valid")

         local lines = wait_on("Slang-server: Hierarchy")
         local rendered = table.concat(lines, "\n")
         assert.is_not_nil(string.find(rendered, "󰈀 bus test_bus", 1, true))
         assert.is_not_nil(string.find(rendered, "valid logic", 1, true))

         require("slang-server.navigation").on_close()
         vim.cmd("SlangServer hierarchy interface_port_top.dut.buses[-1].valid")
         lines = wait_on("Slang-server: Hierarchy")
         rendered = table.concat(lines, "\n")
         assert.is_not_nil(string.find(rendered, "󰈀 buses test_bus", 1, true))
         assert.is_not_nil(string.find(rendered, "󰈀 [-1] test_bus", 1, true))
         assert.is_not_nil(string.find(rendered, "valid logic", 1, true))
      end)

      local navigation = require("slang-server.navigation")
      if navigation.state.open then
         navigation.on_close()
      end
      local foo_file = vim.fn.fnamemodify("tests/foo.sv", ":p")
      vim.cmd("SlangServer setTopLevel " .. vim.fn.fnameescape(foo_file))
      assert(ok, err)
   end)
end)

-- TODO (tests)
-- * cone tracing
-- * WCP
