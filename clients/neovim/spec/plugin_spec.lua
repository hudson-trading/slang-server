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
---@param cleanup fun()
local function with_cleanup(fn, cleanup)
   local ok, err = xpcall(fn, debug.traceback)
   cleanup()
   assert(ok, err)
end

---@param fn fun()
---@return string[]
local function capture_notifications(fn)
   local old_notify = vim.notify
   local messages = {}
   vim.notify = function(msg, ...)
      messages[#messages + 1] = msg
   end

   with_cleanup(fn, function()
      vim.notify = old_notify
   end)
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

      with_cleanup(function()
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
      end, function()
         config.CONFIG = original
      end)
   end)

   it("Keeps global command mappings disabled by default", function()
      local mappings = require("slang-server._core.config").CONFIG.keymaps

      assert.is_false(mappings.enable_defaults)
      assert.is_nil(mappings.hierarchy.enabled)
      assert.are.same("<leader>vh", mappings.hierarchy.key)
      assert.is_nil(mappings.findInstance.enabled)
      assert.are.same("<leader>vi", mappings.findInstance.key)
      assert.is_nil(mappings.focus.enabled)
      assert.are.same("<leader>vf", mappings.focus.key)
      assert.is_nil(mappings.selectActive.enabled)
      assert.are.same("<leader>va", mappings.selectActive.key)
   end)

   it("Declares metadata for every command", function()
      local commands = require("slang-server._commands")
      for name, command in pairs(commands) do
         assert.are.same("string", type(command.desc), name)
         assert.is_true(command.desc ~= "", name)
         assert.are.same("table", type(command.required_commands), name)
         assert.are.same("function", type(command.context), name)
      end

      assert.are.same({
         "slang.getScopes",
         "slang.getScopesByModule",
      }, commands.hierarchy.required_commands)
      assert.are.same({
         "slang.getScopes",
         "slang.getScopesByModule",
         "slang.getInstancesOfModule",
      }, commands.findInstance.required_commands)
      assert.are.same({
         "slang.getModulesInFile",
         "slang.getActiveInstanceAtPosition",
         "slang.getScopes",
         "slang.getScopesByModule",
      }, commands.focus.required_commands)
   end)

   it("Checks declarative command requirements against the resolved context", function()
      local commands = require("slang-server._commands")
      local capabilities = require("slang-server._lsp.capabilities")
      local original_get_client = capabilities.get_client
      local original_check = capabilities.check_or_notify
      local invoked

      local ok, err = pcall(function()
         commands.testMetadata = {
            desc = "Test metadata",
            required_commands = { "slang.test" },
            context = function(args)
               assert.are.same({ "one", "two" }, args)
               return 42
            end,
            impl = function(args, _, bufnr)
               invoked = { args, bufnr }
            end,
         }
         capabilities.get_client = function(bufnr)
            assert.are.same(42, bufnr)
            return {}
         end
         capabilities.check_or_notify = function(bufnr, required)
            assert.are.same(42, bufnr)
            assert.are.same({ "slang.test" }, required)
            return true
         end

         vim.cmd("SlangServer testMetadata one two")
      end)

      commands.testMetadata = nil
      capabilities.get_client = original_get_client
      capabilities.check_or_notify = original_check
      assert(ok, err)
      assert.are.same({ { "one", "two" }, 42 }, invoked)
   end)

   it("Opens the hierarchy at the active path unless a path is provided", function()
      local command = require("slang-server._commands.hierarchy").hierarchy
      local capabilities = require("slang-server._lsp.capabilities")
      local client_state = require("slang-server._lsp.state")
      local navigation = require("slang-server.navigation")
      local original_get_client = capabilities.get_client
      local original_show = navigation.show
      local client_id = 12345
      local original_client_state = client_state.clients[client_id]
      local shown = {}

      local ok, err = pcall(function()
         capabilities.get_client = function()
            return { id = client_id }
         end
         navigation.show = function(path, focus)
            shown[#shown + 1] = { path, focus }
         end
         client_state.set_active_path(client_id, "top.active")

         command.impl({}, {}, 1)
         command.impl({ "top.explicit" }, {}, 1)
      end)

      capabilities.get_client = original_get_client
      navigation.show = original_show
      client_state.clients[client_id] = original_client_state
      assert(ok, err)
      assert.are.same({
         { "top.active", true },
         { "top.explicit", true },
      }, shown)
   end)

   it("Uses the slang-server position encoding when adding to waves", function()
      local command = require("slang-server._commands.addToWaves").addToWaves
      local capabilities = require("slang-server._lsp.capabilities")
      local client = require("slang-server._lsp.client")
      local original_get_client = capabilities.get_client
      local original_get_instances = client.getInstances
      local original_make_params = vim.lsp.util.make_position_params
      local encoding

      local ok, err = pcall(function()
         capabilities.get_client = function()
            return { offset_encoding = "utf-8" }
         end
         vim.lsp.util.make_position_params = function(_, position_encoding)
            encoding = position_encoding
            return {}
         end
         client.getInstances = function() end

         command.impl({}, {}, vim.api.nvim_get_current_buf())
      end)

      capabilities.get_client = original_get_client
      client.getInstances = original_get_instances
      vim.lsp.util.make_position_params = original_make_params
      assert(ok, err)
      assert.are.same("utf-8", encoding)
   end)

   it("Does not expose the utility module as a global", function()
      local module_name = "slang-server.util"
      local original_module = package.loaded[module_name]
      local original_global = _G.M
      local sentinel = {}

      _G.M = sentinel
      package.loaded[module_name] = nil
      require(module_name)

      local exposed = _G.M
      package.loaded[module_name] = original_module
      _G.M = original_global
      assert.are.same(sentinel, exposed)
   end)

   it("Defers mappings configured through setup until ftplugin initialization", function()
      local plugin = require("slang-server")
      local config = require("slang-server._core.config")
      local original_config = config.CONFIG
      local original_set = vim.keymap.set
      local mappings = {}

      local ok, err = pcall(function()
         vim.keymap.set = function(mode, lhs, rhs, opts)
            mappings[#mappings + 1] = { mode = mode, lhs = lhs, rhs = rhs, opts = opts }
         end
         plugin.setup({
            keymaps = {
               focus = { key = "gj", enabled = true },
            },
         })
         assert.are.same(0, #mappings)
         require("slang-server._commands.keymaps").apply()
      end)

      vim.keymap.set = original_set
      config.CONFIG = original_config
      assert(ok, err)
      assert.are.same(1, #mappings)
      assert.are.same("n", mappings[1].mode)
      assert.are.same("gj", mappings[1].lhs)
   end)

   it("Configures global mappings through vim.g", function()
      local module_name = "slang-server._core.config"
      local original_module = package.loaded[module_name]
      local original_global = vim.g.slang_server_config
      local original_set = vim.keymap.set
      local mappings = {}

      local ok, err = pcall(function()
         vim.g.slang_server_config = {
            keymaps = {
               findInstance = { key = "gi", enabled = true },
            },
         }
         package.loaded[module_name] = nil
         require(module_name)
         vim.keymap.set = function(mode, lhs, rhs, opts)
            mappings[#mappings + 1] = { mode = mode, lhs = lhs, rhs = rhs, opts = opts }
         end
         require("slang-server._commands.keymaps").apply()
      end)

      vim.keymap.set = original_set
      vim.g.slang_server_config = original_global
      package.loaded[module_name] = original_module
      assert(ok, err)
      assert.are.same(1, #mappings)
      assert.are.same("n", mappings[1].mode)
      assert.are.same("gi", mappings[1].lhs)
   end)

   it("Adds only enabled global command mappings", function()
      local config = require("slang-server._core.config")
      local original_config = config.CONFIG
      local original_set = vim.keymap.set
      local mappings = {}

      local ok, err = pcall(function()
         config.CONFIG = vim.tbl_deep_extend("force", {}, config.CONFIG, {
            keymaps = {
               hierarchy = { enabled = true },
               findInstance = { enabled = true },
               focus = { enabled = true },
               selectActive = { enabled = true },
            },
         })
         vim.keymap.set = function(mode, lhs, rhs, opts)
            mappings[#mappings + 1] = { mode = mode, lhs = lhs, rhs = rhs, opts = opts }
         end

         require("slang-server._commands.keymaps").apply()
      end)

      vim.keymap.set = original_set
      config.CONFIG = original_config
      assert(ok, err)
      assert.are.same(4, #mappings)

      local invoked = {}
      local by_key = {}
      local original_cmd = vim.cmd
      with_cleanup(function()
         vim.cmd = function(command)
            invoked[command] = true
         end
         for _, mapping in ipairs(mappings) do
            assert.are.same("n", mapping.mode)
            by_key[mapping.lhs] = mapping
            mapping.rhs()
         end
      end, function()
         vim.cmd = original_cmd
      end)
      assert.are.same("Open Slang Server hierarchy", by_key["<leader>vh"].opts.desc)
      assert.are.same("Find an instance in the compiled design", by_key["<leader>vi"].opts.desc)
      assert.are.same("Reveal object in Slang Server hierarchy", by_key["<leader>vf"].opts.desc)
      assert.are.same("Select the active instance or generate iteration under the cursor", by_key["<leader>va"].opts.desc)
      assert.is_true(invoked["SlangServer hierarchy"])
      assert.is_true(invoked["SlangServer findInstance"])
      assert.is_true(invoked["SlangServer focus"])
      assert.is_true(invoked["SlangServer selectActive"])
   end)

   it("Enables default global mappings while allowing individual overrides", function()
      local config = require("slang-server._core.config")
      local original_config = config.CONFIG
      local original_set = vim.keymap.set
      local mappings = {}

      local ok, err = pcall(function()
         config.CONFIG = vim.tbl_deep_extend("force", {}, config.CONFIG, {
            keymaps = {
               enable_defaults = true,
               focus = { enabled = false },
               findInstance = { key = "gi" },
            },
         })
         vim.keymap.set = function(_, lhs)
            mappings[#mappings + 1] = lhs
         end
         require("slang-server._commands.keymaps").apply()
      end)

      vim.keymap.set = original_set
      config.CONFIG = original_config
      assert(ok, err)
      table.sort(mappings)
      assert.are.same({ "<leader>va", "<leader>vh", "gi" }, mappings)
   end)

   it("Runs active-selection code lenses without the code-lens picker", function()
      local command = require("slang-server._commands.selectActive").selectActive
      local capabilities = require("slang-server._lsp.capabilities")
      local original_get_client = capabilities.get_client
      local original_get_lenses = vim.lsp.codelens.get
      local original_select = vim.ui.select
      local selected = {}

      assert.are.same({ "slang.activateInstance" }, command.required_commands)

      local active_lens = {
         range = { start = { line = vim.api.nvim_win_get_cursor(0)[1] - 1 } },
         command = {
            title = "top.cpu (2)",
            command = "slang.quickPick",
            arguments = {
               {
                  placeholder = "Select active instance for cpu",
                  items = {},
                  onSelectCommand = "slang.activateInstance",
                  interactionSource = "codeLensSelect",
               },
            },
         },
      }

      local ok, err = pcall(function()
         capabilities.get_client = function()
            return {
               exec_cmd = function(_, lens_command, ctx)
                  selected[#selected + 1] = { lens_command.title, ctx }
               end,
            }
         end
         vim.lsp.codelens.get = function()
            return {
               active_lens,
               {
                  range = active_lens.range,
                  command = { title = "Go to Instantiation", command = "slang.activateInstance" },
               },
            }
         end
         command.impl({}, {}, vim.api.nvim_get_current_buf())

         active_lens.command.title = "top.cpu.lanes[1]"
         active_lens.command.arguments[1].placeholder = "Select active generate iteration"
         vim.lsp.codelens.get = function()
            return { active_lens }
         end
         command.impl({}, {}, vim.api.nvim_get_current_buf())

         local module_lens = vim.deepcopy(active_lens)
         module_lens.command.title = "top.cpu (2)"
         local generate_lens = vim.deepcopy(active_lens)
         vim.lsp.codelens.get = function()
            return { module_lens, generate_lens }
         end
         vim.ui.select = function(items, opts, on_choice)
            assert.are.same("Select active-selection code lens", opts.prompt)
            assert.are.same("top.cpu.lanes[1]", opts.format_item(items[2]))
            on_choice(items[2])
         end
         command.impl({}, {}, vim.api.nvim_get_current_buf())
      end)

      capabilities.get_client = original_get_client
      vim.lsp.codelens.get = original_get_lenses
      vim.ui.select = original_select
      assert(ok, err)
      assert.are.same(3, #selected)
      assert.are.same("top.cpu (2)", selected[1][1])
      assert.are.same("top.cpu.lanes[1]", selected[2][1])
      assert.are.same({ bufnr = vim.api.nvim_get_current_buf() }, selected[1][2])
      assert.are.same({ bufnr = vim.api.nvim_get_current_buf() }, selected[2][2])
      assert.are.same("top.cpu.lanes[1]", selected[3][1])
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

   it("Ignores a more recent source buffer without an attached server", function()
      local navigation = require("slang-server.navigation")
      local attached_bufnr = vim.api.nvim_get_current_buf()

      vim.cmd("vnew")
      local unattached_bufnr = vim.api.nvim_get_current_buf()
      vim.bo[unattached_bufnr].filetype = "systemverilog"

      local source = navigation.state.sv_buf
      vim.cmd("bwipeout!")

      assert.are.same(attached_bufnr, source.bufnr)
   end)

   it("Reveals the active hierarchy object at the source cursor", function()
      local command = require("slang-server._commands.focus").focus
      local lsp = require("slang-server._lsp.client")
      local navigation = require("slang-server.navigation")
      local original_get_modules = lsp.getModulesInFile
      local original_get_active = lsp.getActiveInstanceAtPosition
      local original_show = navigation.show
      local module_request
      local active_request
      local reveals = {}

      local ok, err = pcall(function()
         lsp.getModulesInFile = function(_, handlers, params)
            module_request = params
            handlers.on_success({ "foo" })
         end
         lsp.getActiveInstanceAtPosition = function(_, handlers, params)
            active_request = params
            handlers.on_success("foo.signal")
         end
         navigation.show = function(path, focus)
            reveals[#reveals + 1] = { path, focus }
         end

         command.impl({}, {}, vim.api.nvim_get_current_buf())
      end)

      lsp.getModulesInFile = original_get_modules
      lsp.getActiveInstanceAtPosition = original_get_active
      navigation.show = original_show

      assert(ok, err)
      assert.are.same(vim.api.nvim_buf_get_name(0), module_request.path)
      assert.are.same("foo", active_request.moduleName)
      assert.are.same(vim.uri_from_bufnr(0), active_request.textDocument.uri)
      assert.are.same({ { "", false }, { "foo.signal", true } }, reveals)
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

      local messages
      with_cleanup(function()
         navigation.state.sv_win = false
         lsp.showHierLocation = function()
            requested = true
         end
         messages = capture_notifications(function()
            navigation.show_hier_location("top.child")
         end)
      end, function()
         lsp.showHierLocation = original_show
         navigation.state.sv_win = original_source_win
      end)

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

   it("Reports when a quick-pick command cannot be sent", function()
      local client_commands = require("slang-server._lsp.clientCommands")
      local original_get_client = vim.lsp.get_client_by_id

      local messages
      with_cleanup(function()
         messages = capture_notifications(function()
            vim.lsp.get_client_by_id = function()
               return {
                  request = function()
                     return false
                  end,
               }
            end
            client_commands.executeServerCommand("slang.activateInstance", {}, {
               bufnr = 0,
               client_id = 42,
            })
         end)
      end, function()
         vim.lsp.get_client_by_id = original_get_client
      end)

      assert.are.same({ "slang-server: failed to send workspace/executeCommand request" }, messages)
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

         command.impl({}, {}, vim.api.nvim_get_current_buf())
         command.impl({}, {}, vim.api.nvim_get_current_buf())
         responses[2].on_success({
            { declName = "new", instCount = 1, inst = { instPath = "top.new" } },
         })
         responses[1].on_success({
            { declName = "old", instCount = 1, inst = { instPath = "top.old" } },
         })
         assert.are.same(1, #selections)
         assert.are.same({ "top.new" }, selections[1])

         selections = {}
         command.impl({}, {}, vim.api.nvim_get_current_buf())
         command.impl({}, {}, vim.api.nvim_get_current_buf())
         responses[4].on_failure("new search failed")
         responses[3].on_success({
            { declName = "stale", instCount = 1, inst = { instPath = "top.stale" } },
         })

         command.impl({}, {}, vim.api.nvim_get_current_buf())
         responses[5].on_success({ { declName = "old", instCount = 2 } })
         command.impl({}, {}, vim.api.nvim_get_current_buf())
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

   it("Discards hierarchy reveal results from an older session", function()
      local hierarchy = require("slang-server.navigation.hierarchy")
      local navigation = require("slang-server.navigation")
      local lsp = require("slang-server._lsp.client")
      local original_tree = hierarchy.state.tree
      local original_split = hierarchy.state.split
      local original_generation = hierarchy.state.generation
      local original_open_state = navigation.state.open
      local original_source_buf = rawget(navigation.state, "sv_buf")
      local original_get_scopes = lsp.getScopes
      local deferred
      local old_tree = {
         get_nodes = function()
            return { {} }
         end,
         render = function() end,
      }

      local ok, err = pcall(function()
         hierarchy.state.tree = old_tree
         hierarchy.state.split = { winid = vim.api.nvim_get_current_win() }
         hierarchy.state.generation = 20
         navigation.state.open = true
         navigation.state.sv_buf = { bufnr = 7 }
         lsp.getScopes = function(_, handlers)
            deferred = handlers.on_success
         end

         hierarchy.reveal("top", { focus = true })
         hierarchy.state.generation = 21
         deferred({ { path = "", children = {} } })
      end)

      hierarchy.state.tree = original_tree
      hierarchy.state.split = original_split
      hierarchy.state.generation = original_generation
      navigation.state.open = original_open_state
      navigation.state.sv_buf = original_source_buf
      lsp.getScopes = original_get_scopes

      assert(ok, err)
   end)

   it("Discards older reveal results in the same hierarchy session", function()
      local hierarchy = require("slang-server.navigation.hierarchy")
      local navigation = require("slang-server.navigation")
      local lsp = require("slang-server._lsp.client")
      local original_tree = hierarchy.state.tree
      local original_split = hierarchy.state.split
      local original_generation = hierarchy.state.generation
      local original_open_state = navigation.state.open
      local original_source_buf = rawget(navigation.state, "sv_buf")
      local original_get_scopes = lsp.getScopes
      local responses = {}
      local nodes = { {} }
      local mutations = 0
      local tree = {
         get_nodes = function()
            return nodes
         end,
         set_nodes = function(_, refreshed)
            nodes = refreshed
            mutations = mutations + 1
         end,
         render = function() end,
      }

      local ok, err = pcall(function()
         hierarchy.state.tree = tree
         hierarchy.state.split = { winid = vim.api.nvim_get_current_win() }
         hierarchy.state.generation = 20
         navigation.state.open = true
         navigation.state.sv_buf = { bufnr = 7 }
         lsp.getScopes = function(_, handlers)
            responses[#responses + 1] = handlers
         end

         hierarchy.reveal("old")
         hierarchy.reveal("new")
         responses[2].on_success({
            { path = "", children = { { instName = "new", kind = "Instance", children = {} } } },
         })
         responses[1].on_success({
            { path = "", children = { { instName = "old", kind = "Instance", children = {} } } },
         })

         assert.are.same(1, mutations)
         assert.are.same("new", nodes[1].instName)
      end)

      hierarchy.state.tree = original_tree
      hierarchy.state.split = original_split
      hierarchy.state.generation = original_generation
      navigation.state.open = original_open_state
      navigation.state.sv_buf = original_source_buf
      lsp.getScopes = original_get_scopes

      assert(ok, err)
   end)

   it("Reveals paths without reopening an active hierarchy", function()
      local hierarchy = require("slang-server.navigation.hierarchy")
      local navigation = require("slang-server.navigation")
      local original_reveal = hierarchy.reveal
      local original_show = hierarchy.show
      local original_state = navigation.state.open
      local revealed
      local reopened = false

      local ok, err = pcall(function()
         hierarchy.reveal = function(path, opts)
            revealed = { path, opts }
         end
         hierarchy.show = function()
            reopened = true
         end
         navigation.state.open = true

         navigation.show("top.child", true)

         assert.are.same({ "top.child", { focus = true } }, revealed)
         assert.is_false(reopened)
      end)

      hierarchy.reveal = original_reveal
      hierarchy.show = original_show
      navigation.state.open = original_state

      assert(ok, err)
   end)

   it("Active instance notifications reveal an open hierarchy", function()
      local client_commands = require("slang-server._lsp.clientCommands")
      local client_state = require("slang-server._lsp.state")
      local navigation = require("slang-server.navigation")
      local hierarchy = require("slang-server.navigation.hierarchy")
      local original_reveal = hierarchy.reveal
      local original_state = navigation.state.open
      local original_get_client = vim.lsp.get_client_by_id
      local original_refresh = vim.lsp.codelens.refresh
      local client_id = 12345
      local original_client_state = client_state.clients[client_id]
      local bufnr = vim.api.nvim_get_current_buf()
      local refreshes = {}
      local revealed

      local ok, err = pcall(function()
         client_state.clients[client_id] = nil
         hierarchy.reveal = function(path, opts)
            revealed = { path, opts }
         end
         vim.lsp.get_client_by_id = function(id)
            assert.are.same(client_id, id)
            return {
               attached_buffers = { [bufnr] = true },
               supports_method = function(_, method, method_bufnr)
                  assert.are.same(vim.lsp.protocol.Methods.textDocument_codeLens, method)
                  assert.are.same(bufnr, method_bufnr)
                  return true
               end,
            }
         end
         vim.lsp.codelens.refresh = function(opts)
            refreshes[#refreshes + 1] = opts.bufnr
         end

         navigation.state.open = false
         client_commands.activeInstanceChanged(
            nil,
            { hierPath = "top.hidden", interactionSource = "codeLensSelect" },
            { client_id = client_id }
         )
         assert.is_nil(revealed)

         navigation.state.open = true
         client_commands.activeInstanceChanged(
            nil,
            { hierPath = "top.visible", interactionSource = "codeLensSelect" },
            { client_id = client_id }
         )
         assert.are.same({ "top.visible", { select = true } }, revealed)
         assert.are.same({ active_path = "top.visible" }, client_state.clients[client_id])
         assert.are.same({ bufnr, bufnr }, refreshes)
      end)
      hierarchy.reveal = original_reveal
      navigation.state.open = original_state
      vim.lsp.get_client_by_id = original_get_client
      vim.lsp.codelens.refresh = original_refresh
      client_state.clients[client_id] = original_client_state

      assert(ok, err)
   end)

   it("Parses and resolves hierarchy path segments", function()
      local path = require("slang-server.navigation.path")
      assert.are.same(
         { "pkg", "top", "gen", "[2]", "child" },
         path.split("pkg::top.gen[2].child")
      )
      assert.are.same("pkg::member", path.join("pkg", "member", "Package"))
      assert.are.same("top.array[2]", path.join("top.array", "[2]", "InstanceArray"))
      assert.are.same("top.child", path.join("top", "child", "Instance"))

      local combined = { { instName = "gen[2]", path = "top.gen[2]" } }
      local child, index = path.resolve_child(combined, { "gen", "[2]" }, 1)
      assert.are.same(combined[1], child)
      assert.are.same(2, index)

      local separate = { { instName = "[2]", path = "top.gen[2]" } }
      child, index = path.resolve_child(separate, { "[2]" }, 1, "top.gen")
      assert.are.same(separate[1], child)
      assert.are.same(1, index)
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

   it("Can hide cells and wrap the hierarchy", function()
      local config = require("slang-server._core.config").CONFIG.navigation
      local navigation = require("slang-server.navigation")
      local hierarchy = require("slang-server.navigation.hierarchy")
      local cells = require("slang-server.navigation.cells")
      local original_show = config.cells.show
      local original_wrap = config.wrap

      with_cleanup(function()
         config.cells.show = false
         config.wrap = true
         vim.cmd("SlangServer hierarchy")
         wait_on("Slang-server: Hierarchy")

         assert.is_nil(cells.state.split)
         assert.is_true(vim.api.nvim_get_option_value("wrap", { win = hierarchy.state.split.winid }))
      end, function()
         if navigation.state.open then
            navigation.on_close()
         end
         config.cells.show = original_show
         config.wrap = original_wrap
      end)
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
         "slang-server: 'setTopLevel' requires a buffer with an attached slang-server LSP client.",
         "slang-server: 'addToWaves' requires a buffer with an attached slang-server LSP client.",
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
