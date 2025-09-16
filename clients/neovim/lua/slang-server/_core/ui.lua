local M = {}

M.NuiMenu = require("nui.menu")
M.NuiPopup = require("nui.popup")
M.NuiText = require("nui.text")
M.NuiLine = require("nui.line")
M.NuiSplit = require("nui.split")
M.NuiTree = require("nui.tree")

M.components = {}

---@type nui_popup_options
local _popup_opts = {
   position = "50%",
   relative = "editor",
   border = {
      style = "rounded",
      padding = { 1, 2 },
      text = { top_align = "center", bottom_align = "center" },
   },
   buf_options = {
      modifiable = false,
      readonly = true,
   },
   win_options = {
      winhighlight = "Normal:Normal,FloatBorder:Normal",
   },
}

---@param title string
---@param menu_opts nui_menu_options
---@param popup_opts nui_popup_options?
---@return NuiMenu
function M.components.menu(title, menu_opts, popup_opts)
   local opts = vim.tbl_deep_extend("force", _popup_opts, { border = { text = { top = title } } })
   opts = vim.tbl_deep_extend("force", opts, popup_opts or {})

   return M.NuiMenu(opts, menu_opts or {})
end

---@param title string
---@param popup_opts nui_popup_options?
function M.components.popup(title, popup_opts)
   title = "[" .. title .. "]"
   local opts = vim.tbl_deep_extend("force", _popup_opts, { border = { text = { top = title } } })
   opts = vim.tbl_deep_extend("force", opts, popup_opts or {})

   return M.NuiPopup(opts)
end

---@type nui_popup_options
local _hover_opts = {
   enter = false,
   focusable = false,
   size = {
      height = 1,
   },
   relative = "cursor",
   position = {
      row = 1,
      col = 0,
   },
   border = {
      style = "none",
      padding = { 0, 1 },
   },
   buf_options = {
      modifiable = false,
      readonly = true,
   },
}

---@param text string
---@param popup_opts nui_popup_options?
function M.components.hover(text, popup_opts)
   local opts = vim.tbl_deep_extend("force", _hover_opts, { size = { width = string.len(text) } })
   opts = vim.tbl_deep_extend("force", opts, popup_opts or {})

   return M.NuiPopup(opts)
end

return M
