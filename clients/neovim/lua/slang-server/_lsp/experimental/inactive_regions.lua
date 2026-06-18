local M = {}

local ns = vim.api.nvim_create_namespace("SlangServerInactiveRegions")

vim.api.nvim_set_hl(0, "SlangServerInactiveRegion", {
   link = "Comment",
   default = true,
})

---@class slang-server.InactiveRegionsParams
---@field uri string
---@field regions lsp.Range[]

---@param uri string
---@param ranges lsp.Range[]
local function apply_highlights(uri, ranges)
   local bufnr = vim.fn.bufnr(vim.uri_to_fname(uri), false)
   if bufnr == -1 then
      return
   end

   vim.api.nvim_buf_clear_namespace(bufnr, ns, 0, -1)

   for _, range in ipairs(ranges) do
      vim.api.nvim_buf_set_extmark(bufnr, ns, range.start.line, range.start.character, {
         end_row = range["end"].line,
         end_col = range["end"].character,
         hl_group = "SlangServerInactiveRegion",
         hl_eol = true,
      })
   end
end

---@param err lsp.ResponseError?
---@param params slang-server.InactiveRegionsParams?
function M.handler(err, params, _, _)
   if err then
      return
   end

   if not params then
      return
   end

   if not params.uri or not params.regions then
      return
   end

   apply_highlights(params.uri, params.regions)
end

return M
