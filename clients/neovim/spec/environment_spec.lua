describe("Server environment", function()
   local environment = require("slang-server.environment")
   local root, user_home, notifications, original_notify

   local function write(base, file, text)
      local path = base .. "/.slang/" .. file
      vim.fn.mkdir(vim.fs.dirname(path), "p")
      vim.fn.writefile(vim.split(text, "\n", { plain = true }), path)
   end

   before_each(function()
      root = vim.fn.tempname()
      user_home = root .. "/user"
      notifications = {}
      original_notify = vim.notify
      vim.notify = function(message, level)
         notifications[#notifications + 1] = { message = message, level = level }
      end
   end)

   after_each(function()
      vim.notify = original_notify
      vim.fn.delete(root, "rf")
   end)

   it("merges JSONC layers and reloads literal values", function()
      assert.are.same({}, environment.load(root, user_home))
      write(root, "server.json", [[{
         // Workspace defaults
         "env": {"SHARED": "workspace", "WORKSPACE_ONLY": "kept", "EMPTY": "",},
      }]])
      write(user_home, "server.json", [[{"env":{"SHARED":"user","USER_ONLY":"kept"}}]])
      write(root, "local/server.json", [[{
         "env": {"SHARED": "local", "LITERAL": "https://example.test/* \" ,} $VAR \u263a"}, /* comment */
      }]])
      assert.are.same({
         SHARED = "local",
         WORKSPACE_ONLY = "kept",
         USER_ONLY = "kept",
         EMPTY = "",
         LITERAL = 'https://example.test/* " ,} $VAR ☺',
      }, environment.load(root, user_home))
      write(root, "local/server.json", [[{"env":{}}]])
      assert.are.same("user", environment.load(root, user_home).SHARED)
      vim.fn.delete(user_home .. "/.slang/server.json")
      assert.are.same("workspace", environment.load(root, user_home).SHARED)
      assert.are.same({}, environment.load(nil, user_home))
      assert.are.same({}, notifications)
   end)

   it("reports invalid files without applying partial overrides", function()
      for _, invalid in ipairs({
         "{",
         "{,}",
         [[{"env":{"A":"ok",,}}]],
         "[]",
         [[{"env":null}]],
         [[{"env":[]}]],
         [[{"env":{"OK":"x","BAD":42}}]],
         [[{"env":{"BAD=NAME":"x"}}]],
         [[{"env":{"BAD":"\u0000"}}]],
         [[{"env":{}} /* unfinished]],
      }) do
         write(root, "server.json", invalid)
         notifications = {}
         assert.are.same({}, environment.load(root, user_home))
         assert.are.same(1, #notifications)
         assert.is_truthy(notifications[1].message:find(root .. "/.slang/server.json", 1, true))
         assert.are.same(vim.log.levels.ERROR, notifications[1].level)
      end
   end)

   it("loads each workspace at process creation and preserves spawn options", function()
      local original_start = vim.lsp.rpc.start
      local original_load = environment.load
      local captured
      local dispatchers = {}
      local command = { "custom-server", "--debug" }
      local cfg = {
         root_dir = root,
         cmd_cwd = root,
         cmd_env = { SHARED = "inherited", EXTRA = "kept" },
         detached = false,
      }
      local ok, err = pcall(function()
         environment.load = function(directory)
            return original_load(directory, user_home)
         end
         vim.lsp.rpc.start = function(cmd, handlers, options)
            captured = { cmd = cmd, handlers = handlers, options = options }
            return "rpc"
         end
         local launch = require("slang-server").server_cmd(command, cfg)
         write(root, "server.json", [[{"env":{"SHARED":"workspace"}}]])
         assert.are.same("rpc", launch(dispatchers))
         assert.are.same({
            cmd = command,
            handlers = dispatchers,
            options = {
               cwd = root,
               detached = false,
               env = { SHARED = "workspace", EXTRA = "kept" },
            },
         }, captured)
         write(root, "server.json", [[{"env":{"SHARED":"updated"}}]])
         launch(dispatchers)
         assert.are.same("updated", captured.options.env.SHARED)
         local other = root .. "/other"
         write(other, "server.json", [[{"env":{"SHARED":"other"}}]])
         launch(dispatchers, {
            root_dir = root,
            workspace_folders = { { uri = vim.uri_from_fname(other) } },
         })
         assert.are.same({ SHARED = "other" }, captured.options.env)
         assert.are.same({ SHARED = "inherited", EXTRA = "kept" }, cfg.cmd_env)
      end)
      vim.lsp.rpc.start = original_start
      environment.load = original_load
      assert(ok, err)
   end)

   it("sets overrides in the child process without changing Neovim's environment", function()
      write(root, "server.json", [[{"env":{"SLANG_ENV_PROBE":"from config"}}]])
      local output = root .. "/output.json"
      local probe = root .. "/probe.lua"
      vim.fn.writefile({
         "local values = {",
         "  value = vim.env.SLANG_ENV_PROBE,",
         "  inherited = vim.env.PATH,",
         "  cwd = vim.uv.cwd(),",
         "}",
         "vim.fn.writefile({vim.json.encode(values)}, arg[1])",
      }, probe)
      local previous = vim.env.SLANG_ENV_PROBE
      local exit_code
      local cfg = { root_dir = root, cmd_cwd = root }
      local launch = require("slang-server").server_cmd({
         vim.v.progpath, "--headless", "-u", "NONE", "-i", "NONE", "-l", probe, output,
      }, cfg)
      launch({
         on_exit = function(code)
            exit_code = code
         end,
      })
      assert.is_true(vim.wait(5000, function()
         return exit_code ~= nil
      end))
      assert.are.same(0, exit_code)
      local values = vim.json.decode(table.concat(vim.fn.readfile(output), "\n"))
      assert.are.same("from config", values.value)
      assert.are.same(vim.env.PATH, values.inherited)
      assert.are.same(vim.uv.fs_realpath(root), values.cwd)
      assert.are.same(previous, vim.env.SLANG_ENV_PROBE)
      assert.are.same({}, notifications)
   end)
end)
