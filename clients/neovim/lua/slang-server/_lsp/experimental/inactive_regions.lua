-- lua/slang-server/_lsp/inactive_regions.lua

local M = {}

local ns = vim.api.nvim_create_namespace("SlangServerInactiveRegions")

local files = {}

vim.api.nvim_set_hl(0, "SlangServerInactiveRegion", {
   link = "Comment",
   default = true,
})

local function apply_highlights(uri)
   local ranges = files[uri]
   if not ranges then
      return
   end

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

   files[params.uri] = params.regions
   apply_highlights(params.uri)
end

return M
