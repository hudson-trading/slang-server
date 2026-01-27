local hl = require("slang-server._core.highlights")
local ui = require("slang-server._core.ui")
local util = require("slang-server.util")
local hier = require("slang-server.navigation/hierarchy")
local cells = require("slang-server.navigation/cells")

local M = {}

---@type slang-server.navigation.State
M.state = { open = false }

-- M.state.sv_buf returns the most recently focused SV buffer info
-- M.state.sv_winnr returns the winnr of the most recently focused visible SV buffer
setmetatable(M.state, {
   ---@param _k string
   ---@return integer
   __index = function(self, _k)
      if _k == "sv_buf" then
         return util.last_buf({ buflisted = true, filetype = { "verilog", "systemverilog" } })
      elseif _k == "sv_win" then
         return util.last_win({ buflisted = true, filetype = { "verilog", "systemverilog" } })
      end
   end,
})

---@param split NuiSplit
---@param tree NuiTree
---@param mappings table<string, slang-server.ui.Mapping>
function M.map_keys(split, tree, mappings)
   for map, spec in pairs(mappings) do
      split:map("n", map, function()
         local node = tree:get_node()
         ---@cast node slang-server.navigation.Node
         spec.impl(node)
      end, spec.opts)
   end
end

---@param tree NuiTree
---@param msg string
---@param opts {parent: NuiTree.Node?, hl: string?}?
-- NOCOMMIT -- used by both hier and cells, how to handle this?
-- do hier and cells objects contain a reference to the parent object?
-- do I pass a function pointer down?
function M.message(tree, msg, opts)
   if not tree then
      return
   end

   opts = opts or {}
   local id
   if opts.parent then
      id = opts.parent:get_id() .. "__message"
   else
      id = "__message"
   end

   local text = ui.NuiText(msg, opts.hl)

   local msg_node = { ui.NuiTree.Node({ text = text, _uid = id }) }

   if opts.parent then
      tree:set_nodes(msg_node, opts.parent:get_id())
      tree:get_node(opts.parent:get_id()):expand()
   else
      tree:set_nodes(msg_node)
   end

   tree:render()
end

function M.on_close()
   if not M.state.open then
      return
   end
   M.state.open = false
   hier.on_close()
   cells.on_close()
end

---@param node NuiTree.Node
---@param line NuiLine
function M.make_comment_line(node, line)
   line:append(string.rep("  ", node:get_depth() - 1) .. " └╴", hl.HIER_SUBTLE)
   line:append(" ")
   line:append(node.text, "Comment")
end

---@param node slang-server.navigation.Node
---@return string
function M.get_node_id(node)
   return node._uid
end

---@param top slang-server.navigation.Path The top level at which to initialise the hierarchy
function M.show(top)
   if M.state.open then
      return
   end

   M.state.open = true

   hier.show(top)
   cells.show()

   vim.api.nvim_set_current_win(hier.state.split.winid)
end

return M
