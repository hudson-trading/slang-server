local M = {}

---@type slang-server.ui.Subcommand
M.focus = {
   impl = function()
      local capabilities = require("slang-server._lsp.capabilities")
      local bufnr = vim.api.nvim_get_current_buf()
      local lsp_client = capabilities.get_client(bufnr)
      if not lsp_client then
         vim.notify(
            "slang-server: focus must be run from a buffer with an attached slang-server LSP client.",
            vim.log.levels.ERROR
         )
         return
      end

      local required = {
         "slang.getModulesInFile",
         "slang.getActiveInstanceAtPosition",
         "slang.getScope",
         "slang.getScopes",
         "slang.getScopesByModule",
         "slang.getInstancesOfModule",
         "slang.showHierLocation",
      }
      if not capabilities.check_or_notify(bufnr, required) then
         return
      end

      local client = require("slang-server._lsp.client")
      local handlers = require("slang-server.handlers")
      local position = vim.lsp.util.make_position_params(0, lsp_client.offset_encoding or "utf-16")
      local navigation = require("slang-server.navigation")
      if not navigation.state.open then
         navigation.show("", false)
      end

      local function jump(module_name)
         client.getActiveInstanceAtPosition(bufnr, {
            on_success = function(path)
               if not path then
                  vim.notify("No active hierarchy object found at cursor", vim.log.levels.WARN)
                  return
               end
               navigation.show(path, true)
            end,
            on_failure = handlers.defaultOnFailure,
         }, {
            moduleName = module_name,
            textDocument = position.textDocument,
            position = position.position,
         })
      end

      client.getModulesInFile(bufnr, {
         on_success = function(modules)
            if #modules == 0 then
               vim.notify("No modules found in current file", vim.log.levels.WARN)
            elseif #modules == 1 then
               jump(modules[1])
            else
               vim.ui.select(modules, { prompt = "Select source module" }, function(module_name)
                  if module_name then
                     jump(module_name)
                  end
               end)
            end
         end,
         on_failure = handlers.defaultOnFailure,
      }, { path = vim.api.nvim_buf_get_name(bufnr) })
   end,
}

return M
