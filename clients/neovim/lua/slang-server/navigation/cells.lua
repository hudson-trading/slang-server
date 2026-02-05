local ui = require("slang-server._core.ui")
local hl = require("slang-server._core.highlights")
local client = require("slang-server._lsp.client")
local handlers = require("slang-server.handlers")
local util = require("slang-server.util")

local M = {}

---@type slang-server.navigation.cells.State
M.state = {}

function M.on_close()
   vim.api.nvim_buf_delete(M.state.split.bufnr, { force = true })
   M.state.tree = nil
   M.state.split = nil
end

---@param node slang-server.navigation.ScopeNode
---@param parent_node slang-server.navigation.CellNode?
local function prepare_node(node, parent_node)
   local navigation = require("slang-server.navigation")
   local line = ui.NuiLine()

   if node.text then
      navigation.make_comment_line(node, line)
   elseif node.instPath then
      line:append(node.last and "   └╴" or "   ├╴", hl.HIER_SUBTLE)
      line:append(node.instPath, hl.HIER_INSTANCE)
   else
      local expander
      if not node:is_expanded() then
         expander = "  "
      else
         expander = "  "
      end
      line:append(expander, hl.HIER_NORMAL)
      line:append(node.declName, hl.HIER_SCOPE)
      line:append(" (")
      line:append(tostring(node.instCount), hl.HIER_VALUE)
      line:append(")")
   end

   return line
end

---@param node slang-server.navigation.ScopeNode
local function scope_jump(node)
   local navigation = require("slang-server.navigation")
   local hier = require("slang-server.navigation/hierarchy")
   local instPath = nil
   if node and node.instLoc then
      util.jump_loc(node.instLoc, navigation.state.sv_win.winnr)
      instPath = node.instPath
   elseif node and node.declLoc then
      util.jump_loc(node.declLoc, navigation.state.sv_win.winnr)
      local children = node:get_child_ids()
      if children then
         local child = M.state.tree:get_node(children[1])
         if child and child.instPath then
            instPath = child.instPath
         end
      end
   end

   if not instPath then
      return
   end

   hier.open_remainder(nil, true, instPath, true)
end

---@param insts slang-server.lsp.QualifiedInstance[]
---@param cell NuiTree.Node
---@param render boolean?
local function show_insts(insts, cell, render)
   local navigation = require("slang-server.navigation")
   if not navigation.state.open then
      return
   end

   local nodes = {}
   for idx, inst in ipairs(insts) do
      local inst_node = {}
      inst_node._uid = inst.instPath
      inst_node.last = idx == #insts

      inst_node = vim.tbl_deep_extend("error", inst_node, inst)

      ---@cast inst_node slang-server.navigation.InstNode

      nodes[#nodes + 1] = ui.NuiTree.Node(inst_node)
   end

   M.state.tree:set_nodes(nodes, cell:get_id())
   cell:expand()

   if render then
      M.state.tree:render()
   end
end

---@param split NuiSplit
---@param tree NuiTree
local function map_keys(split, tree)
   local navigation = require("slang-server.navigation")
   ---@type table<string, slang-server.ui.Mapping[]>
   local mappings
   mappings = {
      ["<cr>"] = {
         impl = scope_jump,
         opts = { noremap = true },
         desc = "Jump to node in source",
      },
      ["<space>"] = {
         impl = function(node)
            if not node or not node.declName then
               return
            end

            if node:is_expanded() and node:collapse() then
               tree:render()
            elseif node:has_children() then
               node:expand()
               tree:render()
            else
               if not navigation.state.sv_buf then
                  vim.notify("No SV buffer", vim.log.levels.ERROR)
               end

               client.getInstancesOfModule(navigation.state.sv_buf.bufnr, {
                  on_success = function(resp)
                     show_insts(resp, node, true)
                  end,
                  on_failure = handlers.defaultOnFailure,
               }, { moduleName = node.declName })

               navigation.message(M.state.tree, "Loading instances...", { parent = node, hl = hl.HIER_SUBTLE })
            end
         end,
         opts = { noremap = true },
         desc = "Expand / collapse node",
      },
      ["q"] = {
         impl = function()
            split:unmount()
         end,
         opts = { noremap = true },
         desc = "Close",
      },
      ["?"] = {
         impl = function()
            util.show_help(mappings, "Cell view")
         end,
         opts = { noremap = true },
         desc = "Show help",
      },
   }

   navigation.map_keys(split, tree, mappings)
end

---@param insts slang-server.lsp.InstanceSet[]
local function show_nodes(insts)
   local navigation = require("slang-server.navigation")
   if not navigation.state.open then
      return
   end

   for _, node in ipairs(M.state.tree:get_nodes()) do
      M.state.tree:remove_node(node:get_id())
   end

   for _, cell in ipairs(insts) do
      local cell_node = {}
      cell_node._uid = "__DECL__" .. cell.declName

      cell_node = vim.tbl_deep_extend("error", cell_node, cell)

      ---@cast cell_node slang-server.navigation.CellNode

      local cell_nui_node = ui.NuiTree.Node(cell_node)
      M.state.tree:add_node(cell_nui_node)
      if cell.inst then
         show_insts({ cell.inst }, cell_nui_node)
      end
   end

   M.state.tree:render()
end

function M.show()
   local navigation = require("slang-server.navigation")
   local hier = require("slang-server.navigation/hierarchy")

   if not hier.state.split then
      return
   end

   local split = ui.NuiSplit({
      relative = {
         type = "win",
         winid = hier.state.split.winid,
      },
      position = "bottom",
      size = "40%",
      win_options = {
         signcolumn = "no",
         number = false,
         relativenumber = false,
      },
   })

   local event = require("nui.utils.autocmd").event
   split:on(event.BufUnload, navigation.on_close, { once = true })
   split:on(event.WinClosed, navigation.on_close, { once = true })

   split:mount()

   local tree = ui.NuiTree({
      prepare_node = prepare_node,
      get_node_id = navigation.get_node_id,
      bufnr = split.bufnr,
   })

   map_keys(split, tree)

   M.state.split = split
   M.state.tree = tree

   if not navigation.state.sv_buf then
      vim.notify("No SV buffer", vim.log.levels.ERROR)
   end

   navigation.message(tree, "Loading cells...", { hl = hl.HIER_SUBTLE })

   client.getScopesByModule(navigation.state.sv_buf.bufnr, {
      on_success = function(resp)
         show_nodes(resp)
      end,
      on_failure = handlers.defaultOnFailure,
   })

   vim.api.nvim_buf_set_name(split.bufnr, "Slang-server: Cells")
end

return M
