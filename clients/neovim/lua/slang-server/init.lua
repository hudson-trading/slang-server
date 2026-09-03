-- Main module file

require("slang-server._core.version")

local config = require("slang-server._core.config")

---@class SlangModule
local M = {}

---@param opts slang-server.config.Configuration?
M.setup = function(opts)
   config.update(opts)
end

---@param query string
---@param callback fun(result: slang-server.lsp.HierarchySearchResult)
---@param opts slang-server.SearchHierarchyOptions?
M.search_hierarchy = function(query, callback, opts)
   opts = opts or {}
   local capabilities = require("slang-server._lsp.capabilities")
   local client = require("slang-server._lsp.client")
   local handlers = require("slang-server.handlers")
   client.searchHierarchy(opts.bufnr or capabilities.get_source_context(), {
      on_success = callback,
      on_failure = opts.on_error or handlers.defaultOnFailure,
   }, { query = query })
end

return M
