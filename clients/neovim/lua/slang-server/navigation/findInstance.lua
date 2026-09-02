local client = require("slang-server._lsp.client")
local handlers = require("slang-server.handlers")

local M = {}
local generation = 0

---@param bufnr integer
---@param on_choice fun(inst_path: string)
function M.start(bufnr, on_choice)
   generation = generation + 1
   local search_generation = generation

   local function active()
      return generation == search_generation
   end

   client.getScopesByModule(bufnr, {
      on_success = function(instance_sets)
         if not active() then
            return
         end

         local instances = {}
         local pending = 0

         local function show_picker()
            if not active() then
               return
            end
            table.sort(instances)
            if #instances == 0 then
               vim.notify("No instances found", vim.log.levels.WARN)
               return
            end
            vim.ui.select(instances, { prompt = "Select instance" }, function(inst_path)
               if active() and inst_path then
                  on_choice(inst_path)
               end
            end)
         end

         local function complete_one()
            if not active() then
               return
            end
            pending = pending - 1
            if pending == 0 then
               show_picker()
            end
         end

         for _, cell in ipairs(instance_sets) do
            if cell.instCount == 1 and cell.inst then
               instances[#instances + 1] = cell.inst.instPath
            else
               pending = pending + 1
            end
         end

         if pending == 0 then
            show_picker()
            return
         end

         for _, cell in ipairs(instance_sets) do
            if cell.instCount ~= 1 or not cell.inst then
               client.getInstancesOfModule(bufnr, {
                  on_success = function(resp)
                     if not active() then
                        return
                     end
                     for _, inst in ipairs(resp) do
                        instances[#instances + 1] = inst.instPath
                     end
                     complete_one()
                  end,
                  on_failure = function(message)
                     if not active() then
                        return
                     end
                     handlers.defaultOnFailure(message)
                     complete_one()
                  end,
               }, { moduleName = cell.declName })
            end
         end
      end,
      on_failure = function(message)
         if active() then
            handlers.defaultOnFailure(message)
         end
      end,
   })
end

return M
