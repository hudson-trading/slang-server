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

   -- Catches anything the server complained about, including messages emitted
   -- during startup, which land before the first test runs.
   after_each(assert_no_new_messages)

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

   it("Renders and expands interface ports", function()
      local ok, err = pcall(function()
         local interface_file = vim.fn.fnamemodify("tests/interface_port_arrays.sv", ":p")
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
