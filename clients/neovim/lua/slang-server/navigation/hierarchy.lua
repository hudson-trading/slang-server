local config = require("slang-server._core.config").CONFIG
local ui = require("slang-server._core.ui")
local hl = require("slang-server._core.highlights")
local client = require("slang-server._lsp.client")
local handlers = require("slang-server.handlers")
local util = require("slang-server.util")

local M = {}

---@type slang-server.navigation.hierarchy.State
M.state = {}

function M.on_close()
   vim.api.nvim_buf_delete(M.state.split.bufnr, { force = true })
   M.state.tree = nil
   M.state.split = nil
end

---@param split NuiSplit
---@param tree NuiTree
local function map_hier_keys(split, tree)
   local navigation = require("slang-server.navigation")
   ---@type table<string, slang-server.ui.Mapping>
   local mappings
   mappings = {
      ["yn"] = {
         impl = function(node)
            if node and node.path then
               util.yank_and_notify(node.path)
            end
         end,
         opts = { noremap = true },
         desc = "Yank hierarchical node path",
      },
      ["yv"] = {
         impl = function(node)
            if node and node.value then
               util.yank_and_notify(node.value)
            end
         end,
         opts = { noremap = true },
         desc = "Yank node value",
      },
      ["yf"] = {
         impl = function(node)
            if node and node.instLoc and node.instLoc.uri then
               local uri = string.gsub(node.instLoc.uri, "^file://", "")
               util.yank_and_notify(uri)
            end
         end,
         opts = { noremap = true },
         desc = "Yank enclosing file path",
      },
      ["<cr>"] = {
         impl = function(node)
            if node and node.instLoc then
               util.jump_loc(node.instLoc, navigation.state.sv_win.winnr)
            end
         end,
         opts = { noremap = true },
         desc = "Jump to node in source",
      },
      ["gd"] = {
         impl = function(node)
            if node and node.declLoc then
               util.jump_loc(node.declLoc, navigation.state.sv_win.winnr)
            end
         end,
         opts = { noremap = true },
         desc = "Jump to node declaration in source",
      },
      ["<space>"] = {
         impl = function(node)
            if not node then
               return
            end

            if node:is_expanded() and node:collapse() then
               tree:render()
            else
               M._lazy_open(node)
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
            util.show_help(mappings, "Hierarchy view")
         end,
         opts = { noremap = true },
         desc = "Show help",
      },
   }

   navigation.map_keys(split, tree, mappings)
end

---@param parent slang-server.navigation.TreeNode?
---@param root boolean?
---@param remaining_path slang-server.navigation.Path?
---@param from_cell boolean?
function M.open_remainder(parent, root, remaining_path, from_cell)
   local path = parent and parent.path or ""
   print("OPEN REMAINDER -- " .. path .. " -- " .. remaining_path) -- NOCOMMIT
   if remaining_path ~= nil and remaining_path ~= "" then
      local sep_loc = string.find(remaining_path, "[.[]", 2) or (string.len(remaining_path) + 1)
      local is_bracket = string.sub(remaining_path, sep_loc, sep_loc) == "["
      local before_sep = string.sub(remaining_path, 0, sep_loc - 1)
      local after_sep = string.sub(remaining_path, sep_loc + (is_bracket and 0 or 1))
      local before_is_bracket = string.sub(before_sep, 1, 1) == "["
      print(
         "ROOT = "
            .. tostring(root)
            .. " BEFORE SEP = "
            .. before_sep
            .. " AFTER SEP = "
            .. after_sep
            .. " PATH = "
            .. path
      ) -- NOCOMMIT
      path = path .. ((before_is_bracket or root) and "" or ".") .. before_sep
      M._lazy_open(path, false, after_sep, from_cell)
   end
end

---@param node slang-server.navigation.HierNode
---@param parent_node slang-server.navigation.TreeNode?
local function prepare_node(node, parent_node)
   local navigation = require("slang-server.navigation")
   local line = ui.NuiLine()

   if node.text then
      navigation.make_comment_line(node, line)
   else
      local decoration = config.kinds[string.lower(node.kind)]
      local expander = " "

      if
         node.kind == "Instance"
         or node.kind == "Scope"
         or node.kind == "InstanceArray"
         or node.kind == "ScopeArray"
         or node.kind == "Package"
      then
         if node.children and not node:is_expanded() then
            expander = ""
         else
            expander = ""
         end
      elseif node.kind == "Port" then
         if string.find(node.type, "^input") then
            decoration = decoration.input
         elseif string.find(node.type, "^output") then
            decoration = decoration.output
         else
            decoration = decoration.inout
         end
      end

      local box = " "
      if parent_node then
         local last_node = M.state.tree:get_node(parent_node:get_child_ids()[#parent_node:get_child_ids()])
         if last_node then
            box = node:get_id() == last_node:get_id() and " └╴" or " ├╴"
         end
      end

      local hint
      if (node.kind == "Instance" or node.kind == "InstanceArray") and node.declName and node.declName ~= "" then
         hint = node.declName
      elseif node.type and node.type ~= "" then
         hint = node.type
      end

      line:append(string.rep("  ", node:get_depth() - 1) .. box, hl.HIER_SUBTLE)
      line:append(expander, hl.HIER_NORMAL)
      line:append(" " .. decoration.icon, decoration.hl)
      line:append(" " .. node.instName, decoration.hl)
      if hint then
         line:append(" " .. hint, hl.HIER_SUBTLE)
      end
   end

   return line
end

-- Convert LSP nodes to TreeNodes
---@param nodes slang-server.lsp.Node[]
---@param parent_node slang-server.navigation.TreeNode?
---@return slang-server.navigation.TreeNode[]
local function parse_nodes(nodes, parent_node)
   local nui_nodes = {}
   for _, node in ipairs(nodes) do
      local treeNode = {}

      local sep = string.match(node.instName, "%[%d+%]") and "" or "."
      treeNode.path = parent_node and (parent_node.path .. sep .. node.instName) or node.instName
      treeNode._uid = parent_node and (parent_node._uid .. sep .. node.instName) or node.instName

      if node.children then
         treeNode._populated = #node.children > 0
      else
         treeNode._populated = false
      end

      treeNode = vim.tbl_deep_extend("error", treeNode, node)

      ---@cast treeNode slang-server.navigation.TreeNode

      nui_nodes[#nui_nodes + 1] = ui.NuiTree.Node(treeNode, parse_nodes(treeNode.children or {}, treeNode))
   end

   return nui_nodes
end

---@param nodes slang-server.lsp.Node[]
---@param parent slang-server.navigation.TreeNode?
---@param root boolean?
---@param remaining_path slang-server.navigation.Path?
---@param from_cell boolean?
local function show_nodes(nodes, parent, root, remaining_path, from_cell)
   local navigation = require("slang-server.navigation")
   if not navigation.state.open then
      return
   end

   local tree_nodes
   if root then
      print("SHOW NODES ROOT") -- NOCOMMIT
      for _, node in ipairs(nodes) do
         -- NOCOMMIT -- node is unused, loop is weird
         tree_nodes = parse_nodes(nodes)
         M.state.tree:set_nodes(tree_nodes)
      end
   -- If the parent_path is already in the tree, we want to append children to the existing node
   elseif parent then
      print("SHOW NODES PARENT -- " .. parent.path) -- NOCOMMIT
      tree_nodes = parse_nodes(nodes, parent)
      M.state.tree:set_nodes(tree_nodes, parent:get_id())

      parent._populated = true
      parent:expand()
   else
      print("SHOW NODES OTHER ROOT?") -- NOCOMMIT
      tree_nodes = parse_nodes(nodes)
      M.state.tree:set_nodes(tree_nodes)

      for _, node in ipairs(tree_nodes) do
         node:expand()
      end
   end

   M.state.tree:render()
   -- NOCOMMIT
   if parent then
      print("SHOW NODES -- " .. parent.path .. " REMAINING -- " .. remaining_path)
   else
      print("SHOW NODES -- nil" .. " REMAINING -- " .. remaining_path)
   end
   M.open_remainder(parent, root, remaining_path, from_cell)
end

---@param node NuiTree.Node
local function focus_hier_tree(node)
   local _, start_linenr = M.state.tree:get_node(node:get_id())
   vim.api.nvim_win_set_cursor(M.state.split.winid, { start_linenr, 0 })
   vim.api.nvim_win_call(M.state.split.winid, function()
      vim.cmd("normal! zz")
   end)
end

-- `path` can be string or nil, with nil representing $root and returning the first top level instance (TODO:)
-- If `path` is given and does not exist in the hierarchy, it is treated as a root node
-- If `path` is given and exists in the hierarchy, it is considered a subscope to be populated
---@param path_or_node slang-server.navigation.Path | slang-server.navigation.TreeNode
---@param root boolean?
---@param remaining_path slang-server.navigation.Path?
---@param from_cell boolean?
function M._lazy_open(path_or_node, root, remaining_path, from_cell)
   local navigation = require("slang-server.navigation")
   local node
   local path

   if type(path_or_node) == "string" then
      node = M.state.tree:get_node(path_or_node) --[[@as slang-server.navigation.TreeNode?]]
      path = path_or_node
      print("PATH -- " .. path .. " NODE IS NIL " .. tostring(node == nil)) -- NOCOMMIT
   else
      node = path_or_node
      path = node.path
   end

   -- Don't reload if the node is already populated
   if node and node._populated then
      node:expand()
      M.state.tree:render()
      if from_cell then
         focus_hier_tree(node)
      end
      print("OPEN POPULATED -- " .. node.path) -- NOCOMMIT
      M.open_remainder(node, root, remaining_path, from_cell)
   else
      navigation.message(M.state.tree, "Loading scope...", { parent = node, hl = hl.HIER_SUBTLE })
      if node and from_cell then
         focus_hier_tree(node)
      end

      if not navigation.state.sv_buf then
         vim.notify("No SV buffer", vim.log.levels.ERROR)
      end

      if node then
         print("GET SCOPE -- " .. node.path) -- NOCOMMIT
      else
         print("GET SCOPE -- nil" .. " ROOT -- " .. tostring(root)) -- NOCOMMIT
      end
      client.getScope(navigation.state.sv_buf.bufnr, {
         on_success = function(resp)
            show_nodes(resp, node, root, remaining_path, from_cell)
         end,
         on_failure = handlers.defaultOnFailure,
      }, { hierPath = path })
   end
end

local function on_hover()
   local navigation = require("slang-server.navigation")
   if not navigation.state.open then
      return
   end

   if M.state.hover then
      M.state.hover:unmount()
   end

   local selected = M.state.tree:get_node()
   if not (selected and selected.value) then
      return
   end

   M.state.hover = ui.components.hover(selected.value)

   local event = require("nui.utils.autocmd").event
   M.state.hover:on({ event.BufLeave }, function()
      M.state.hover:unmount()
   end, { once = true })

   local line = ui.NuiLine()
   line:append(selected.value, hl.HIER_VALUE)
   line:render(M.state.hover.bufnr, -1, 1)

   M.state.hover:mount()
end

---@param top slang-server.navigation.Path The top level at which to initialise the hierarchy
function M.show(top)
   local navigation = require("slang-server.navigation")
   local hierarchy_config = config.hierarchy
   local split = ui.NuiSplit({
      relative = "win",
      position = hierarchy_config.position,
      size = hierarchy_config.size,
      win_options = {
         signcolumn = "no",
         number = false,
         relativenumber = false,
      },
   })

   local event = require("nui.utils.autocmd").event
   split:on(event.BufUnload, navigation.on_close, { once = true })
   split:on(event.WinClosed, navigation.on_close, { once = true })
   split:on(event.CursorMoved, on_hover)

   split:mount()

   vim.api.nvim_buf_set_name(split.bufnr, "Slang-server: Hierarchy")

   local tree = ui.NuiTree({
      prepare_node = prepare_node,
      -- NOCOMMIT -- annoying bug was here -- why didn't M.get_node_id cause an error?
      get_node_id = navigation.get_node_id,
      bufnr = split.bufnr,
   })

   map_hier_keys(split, tree)

   M.state.split = split
   M.state.tree = tree

   M._lazy_open("", true, top)
end

return M
