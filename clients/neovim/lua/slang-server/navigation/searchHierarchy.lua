-- Coordinates hierarchy search requests and picker selection.
local client = require("slang-server._lsp.client")
local config = require("slang-server._core.config")
local handlers = require("slang-server.handlers")

local M = {}
local generation = 0

local providers = {
   ["fzf-lua"] = "slang-server.navigation.pickers.fzfLua",
   telescope = "slang-server.navigation.pickers.telescope",
   snacks = "slang-server.navigation.pickers.snacks",
   ["vim.ui"] = "slang-server.navigation.pickers.vimUi",
}

local auto_order = { "fzf-lua", "telescope", "snacks" }

---@param name string
---@return slang-server.navigation.SearchPicker?
local function load_provider(name)
   local module = providers[name]
   if not module then
      return nil
   end
   local ok, picker = pcall(require, module)
   if ok and picker.available() then
      return picker.open
   end
end

---@return slang-server.navigation.SearchPicker?
local function picker()
   local configured = config.CONFIG.search and config.CONFIG.search.picker or "auto"
   if type(configured) == "function" then
      return configured
   end
   if configured ~= "auto" then
      return load_provider(configured)
   end
   for _, name in ipairs(auto_order) do
      local found = load_provider(name)
      if found then
         return found
      end
   end
   return load_provider("vim.ui")
end

---@param bufnr integer
function M.start(bufnr)
   generation = generation + 1
   local search_generation = generation
   local open = picker()
   if not open then
      vim.notify("slang-server: configured hierarchy-search picker is unavailable", vim.log.levels.ERROR)
      return
   end

   local request_generation = 0
   local in_flight = false
   local pending

   local function dispatch()
      if in_flight or not pending or generation ~= search_generation then
         return
      end

      local request = pending
      pending = nil
      in_flight = true
      client.searchHierarchy(bufnr, {
         on_success = function(result)
            in_flight = false
            if generation ~= search_generation then
               return
            end
            if request.id == request_generation then
               request.callback(result)
            end
            dispatch()
         end,
         on_failure = function(message)
            in_flight = false
            if generation ~= search_generation then
               return
            end
            if request.id == request_generation then
               handlers.defaultOnFailure(message)
            end
            dispatch()
         end,
      }, { query = request.query })
   end

   open({
      search = function(query, callback)
         request_generation = request_generation + 1
         local current_request = request_generation
         vim.defer_fn(function()
            if generation ~= search_generation or current_request ~= request_generation then
               return
            end
            pending = { id = current_request, query = query, callback = callback }
            dispatch()
         end, math.max(0, config.CONFIG.search and config.CONFIG.search.query_delay or 0))
      end,
      select = function(item)
         generation = generation + 1
         local navigation = require("slang-server.navigation")
         if navigation.state.open then
            local hierarchy = require("slang-server.navigation.hierarchy")
            vim.api.nvim_set_current_win(hierarchy.state.split.winid)
            hierarchy.open_remainder(nil, true, item.path, true)
         else
            navigation.show(item.path, true)
         end
      end,
   })
end

return M
