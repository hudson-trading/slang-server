M = {}

---@param arg_lead string
---@param opts table
---@return string[]
function M.complete_path(arg_lead, opts)
   local cdir = vim.fs.normalize(vim.fs.dirname(arg_lead))
   local fstub = vim.fs.basename(arg_lead)

   if not vim.fn.isdirectory(cdir) then
      return {}
   end

   local function mkpath(item, itype)
      local path
      if cdir == "." and not string.match(arg_lead, "^%./") then
         path = item
      else
         path = vim.fs.joinpath(cdir, item)
      end

      if itype == "directory" then
         path = vim.fs.joinpath(path, "")
      end
      return path
   end

   local completions = {}
   for item, itype in vim.fs.dir(cdir, {}) do
      if string.match(item, "^" .. fstub) then
         completions[#completions + 1] = mkpath(item, itype)
      end
   end

   return completions
end

---@generic T
---@generic U
---@param xs T[]
---@param map fun(a: T): U
---@param reduce fun(a: U, b: U): U
---@return U
function M.map_reduce(xs, map, reduce)
   local acc = map(xs[1])
   for i = 2, #xs do
      acc = reduce(acc, map(xs[i]))
   end
   return acc
end

local function buf_match(bufnr, buf_filters)
   local match = true
   for opt, vals in pairs(buf_filters) do
      vals = type(vals) == "table" and vals or { vals }

      match = M.map_reduce(vals, function(v)
         return vim.api.nvim_get_option_value(opt, { buf = bufnr }) == v
      end, vim.fn["or"])

      if not match then
         break
      end
   end
   return match
end

---@param buf_filters table<string, any>
---@return vim.fn.getbufinfo.ret.item?
function M.last_buf(buf_filters)
   ---@type vim.fn.getbufinfo.ret.item?
   local last_bufinfo

   for _, bufnr in ipairs(vim.api.nvim_list_bufs()) do
      if buf_match(bufnr, buf_filters) then
         local bufinfo = vim.fn.getbufinfo(bufnr)[1]
         if last_bufinfo == nil or (bufinfo.lastused > last_bufinfo.lastused) then
            last_bufinfo = bufinfo
         end
      end
   end

   return last_bufinfo
end

---@param buf_filters table<string, any>
---@return vim.fn.getwininfo.ret.item?
function M.last_win(buf_filters)
   -- NOTE: We can't directly choose windows by most recently accessed, except
   -- for the last one "#".
   -- In general, we find the most recently accessed buffer and return *some*
   -- window in the current tabpage that contains it.

   -- Make sure we're only checking for visible windows
   buf_filters = vim.tbl_extend("force", buf_filters, { bufhidden = "" })

   -- See if we get lucky with "#" first
   local last_winid = vim.fn.win_getid(vim.fn.winnr("#"))
   local last_bufnr = vim.fn.winbufnr(last_winid)
   if last_bufnr ~= -1 and buf_match(last_bufnr, buf_filters) then
      return vim.fn.getwininfo(last_winid)[1]
   end

   local last_bufinfo = M.last_buf(buf_filters)
   if last_bufinfo then
      -- Get *some* winid containing this buffer in the current tabpage
      last_winid = vim.fn.bufwinid(last_bufinfo.bufnr)
      if last_winid ~= -1 then
         return vim.fn.getwininfo(last_winid)[1]
      end
   end
end

---@param str string?
---@param reg string?
function M.yank_and_notify(str, reg)
   if str then
      vim.fn.setreg(reg or "+", str)
      vim.notify("Yanked " .. str, vim.log.levels.INFO)
   end
end

---@param loc slang-server.ScopedRange
---@param winnr integer?
function M.jump_loc(loc, winnr)
   if not winnr or winnr == -1 then
      vim.notify("Cannot jump to location: invalid target window", vim.log.levels.ERROR)
      return
   end

   local win = vim.fn.win_getid(winnr)
   vim.api.nvim_set_current_win(win)
   vim.cmd.edit(loc.uri)

   local start = loc.range.start
   vim.api.nvim_win_set_cursor(win, { start.line + 1, start.character })
end

---@param mappings slang-server.ui.Mapping[]
---@param title string
---@param opts table?
function M.show_help(mappings, title, opts)
   local ui = require("slang-server._core.ui")
   local hl = require("slang-server._core.highlights")

   opts = vim.tbl_deep_extend("force", {
      size = {
         width = 50,
         height = vim.tbl_count(mappings),
      },
      border = {
         text = {
            bottom = "[q: quit]",
         },
      },
      enter = true,
      focusable = true,
   }, opts or {})

   local popup = ui.components.popup(title, opts)

   local event = require("nui.utils.autocmd").event
   popup:on({ event.BufLeave }, function()
      popup:unmount()
   end, { once = true })

   popup:map("n", "q", function()
      popup:unmount()
   end, { noremap = true })

   popup:map("n", "?", function()
      popup:unmount()
   end, { noremap = true })

   local max = 0
   for map, _ in pairs(mappings) do
      max = math.max(max, string.len(map))
   end
   local fmt = string.gsub("%-_._s", "_", max)

   local line_n = 1
   for map, spec in pairs(mappings) do
      local line = ui.NuiLine()
      line:append(string.format(fmt, map) .. " : " .. spec.desc, hl.HIER_SUBTLE)

      line:render(popup.bufnr, -1, line_n)
      line_n = line_n + 1
   end

   popup:mount()
end
return M
