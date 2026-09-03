local config = require("slang-server._core.config").CONFIG
local ui = require("slang-server._core.ui")
local hl = require("slang-server._core.highlights")
local client = require("slang-server._lsp.client")
local handlers = require("slang-server.handlers")
local util = require("slang-server.util")
local hierarchy_path = require("slang-server.navigation.path")

local M = {}
local reveal_generation = 0

local expandable_kinds = {
   Instance = true,
   Scope = true,
   InstanceArray = true,
   ScopeArray = true,
   InterfacePort = true,
   InterfacePortArray = true,
   Package = true,
}

---@param kind slang-server.SlangKind
---@return boolean
local function is_expandable_kind(kind)
   return expandable_kinds[kind] == true
end

---@type slang-server.navigation.hierarchy.State
M.state = { generation = 0 }

function M.on_close()
   require("slang-server.navigation").unprotect_window(M.state)
   M.state.generation = M.state.generation + 1
   vim.api.nvim_buf_delete(M.state.split.bufnr, { force = true })
   M.state.tree = nil
   M.state.split = nil
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

      if is_expandable_kind(node.kind) then
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

      treeNode.path = hierarchy_path.join(
         parent_node and parent_node.path or "",
         node.instName,
         parent_node and parent_node.kind or nil
      )
      treeNode._uid = treeNode.path

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

---@param node NuiTree.Node
local function focus_tree(node)
   local _, start_linenr = M.state.tree:get_node(node:get_id())
   vim.api.nvim_win_set_cursor(M.state.split.winid, { start_linenr, 0 })
   vim.api.nvim_win_call(M.state.split.winid, function()
      vim.cmd("normal! zz")
   end)
   vim.api.nvim_set_current_win(M.state.split.winid)
end

---@param node slang-server.navigation.TreeNode
local function lazy_open(node)
   local navigation = require("slang-server.navigation")
   local generation = M.state.generation
   local tree = M.state.tree

   -- Don't reload if the node is already populated
   if node._populated then
      node:expand()
      M.state.tree:render()
   else
      navigation.message(M.state.tree, "Loading scope...", { parent = node, hl = hl.HIER_SUBTLE })

      if not navigation.state.sv_buf then
         vim.notify("No SV buffer", vim.log.levels.ERROR)
      end

      client.getScope(navigation.state.sv_buf.bufnr, {
         on_success = function(resp)
            if not navigation.state.open or M.state.generation ~= generation or M.state.tree ~= tree then
               return
            end
            local children = parse_nodes(resp, node)
            M.state.tree:set_nodes(children, node:get_id())
            node._populated = true
            node:expand()
            M.state.tree:render()
         end,
         on_failure = function(message)
            if
               navigation.state.open
               and M.state.generation == generation
               and M.state.tree == tree
            then
               handlers.defaultOnFailure(message)
            end
         end,
      }, { hierPath = node.path })
   end
end

---@param split NuiSplit
---@param tree NuiTree
local function map_keys(split, tree)
   local navigation = require("slang-server.navigation")
   ---@type table<string, slang-server.ui.Mapping>
   local mappings = {}
   local keys = assert(config.navigation and config.navigation.hierarchy.keymaps)
   navigation.add_mapping(mappings, keys.yank_path, {
         impl = function(node)
            if node and node.path then
               util.yank_and_notify(node.path)
            end
         end,
         opts = { noremap = true },
         desc = "Yank hierarchical node path",
      })
   navigation.add_mapping(mappings, keys.yank_value, {
         impl = function(node)
            if node and node.value then
               util.yank_and_notify(node.value)
            end
         end,
         opts = { noremap = true },
         desc = "Yank node value",
      })
   navigation.add_mapping(mappings, keys.yank_file, {
         impl = function(node)
            if node and node.instLoc and node.instLoc.uri then
               util.yank_and_notify(vim.uri_to_fname(node.instLoc.uri))
            end
         end,
         opts = { noremap = true },
         desc = "Yank enclosing file path",
      })
   navigation.add_mapping(mappings, keys.jump, {
         impl = function(node)
            if node and node.path then
               navigation.show_hier_location(node.path)
            end
         end,
         opts = { noremap = true },
         desc = "Jump to node in source",
      })
   navigation.add_mapping(mappings, keys.jump_to_declaration, {
         impl = function(node)
            if node and node.declLoc then
               local source_win = navigation.state.sv_win
               util.jump_loc(node.declLoc, source_win and source_win.winnr)
            end
         end,
         opts = { noremap = true },
         desc = "Jump to node declaration in source",
      })
   navigation.add_mapping(mappings, keys.toggle, {
         impl = function(node)
            if not node then
               return
            end

            if node:is_expanded() and node:collapse() then
               tree:render()
            else
               lazy_open(node)
            end
         end,
         opts = { noremap = true },
         desc = "Expand / collapse node",
      })
   navigation.add_mapping(mappings, keys.close, {
         impl = function()
            split:unmount()
         end,
         opts = { noremap = true },
         desc = "Close",
      })
   navigation.add_mapping(mappings, keys.help, {
         impl = function()
            util.show_help(mappings, "Hierarchy view")
         end,
         opts = { noremap = true },
         desc = "Show help",
      })

   navigation.map_keys(split, tree, mappings)
end

---@param parent slang-server.navigation.TreeNode?
---@return slang-server.navigation.TreeNode[]
local function get_children(parent)
   if not parent then
      return M.state.tree:get_nodes()
   end

   local children = {}
   for _, id in ipairs(parent:get_child_ids()) do
      local child = M.state.tree:get_node(id)
      if child then
         children[#children + 1] = child
      end
   end
   return children
end

---@param node slang-server.navigation.TreeNode
---@return slang-server.navigation.TreeNode
local function preserve_subtree(node)
   local children = get_children(node)
   for index, child in ipairs(children) do
      children[index] = preserve_subtree(child)
   end
   node.__children = children
   node._child_ids = nil
   return node
end

---@param parent slang-server.navigation.TreeNode?
---@param nodes slang-server.lsp.Node[]
---@return slang-server.navigation.TreeNode[]
local function refresh_children(parent, nodes)
   local existing = {}
   for _, child in ipairs(get_children(parent)) do
      if child.instName then
         existing[child.instName] = child
      end
   end

   local refreshed = {}
   for _, node in ipairs(nodes) do
      local child = existing[node.instName]
      if child then
         for key, value in pairs(node) do
            if key ~= "children" then
               child[key] = value
            end
         end
         preserve_subtree(child)
         refreshed[#refreshed + 1] = child
      else
         refreshed[#refreshed + 1] = parse_nodes({ node }, parent)[1]
      end
   end

   if parent then
      M.state.tree:set_nodes(refreshed, parent:get_id())
      parent._populated = true
      parent:expand()
   else
      M.state.tree:set_nodes(refreshed)
   end
   return refreshed
end

---@class slang-server.navigation.RevealOptions
---@field focus boolean?

---Reveal a hierarchy path without opening its source location.
---@param path string
---@param opts slang-server.navigation.RevealOptions?
function M.reveal(path, opts)
   opts = opts or {}
   local navigation = require("slang-server.navigation")
   local source = navigation.state.sv_buf
   if not source then
      vim.notify("No SV buffer", vim.log.levels.ERROR)
      return
   end

   local generation = M.state.generation
   local tree = M.state.tree
   if not tree then
      return
   end
   reveal_generation = reveal_generation + 1
   local request_generation = reveal_generation

   local function active()
      return reveal_generation == request_generation
         and navigation.session_active(M.state, generation)
         and M.state.tree == tree
   end

   if #tree:get_nodes() == 0 then
      navigation.message(tree, "Loading hierarchy...", { hl = hl.HIER_SUBTLE })
   end
   client.getScopes(source.bufnr, {
      on_success = function(scopes)
         if not active() then
            return
         end

         local parts = hierarchy_path.split(path)
         local current
         local last_resolved
         local index = 1

         while index <= #parts do
            local step = scopes[index]
            if not step then
               break
            end
            local children = refresh_children(current, step.children)
            local child, resolved_index = hierarchy_path.resolve_child(
               children,
               parts,
               index,
               last_resolved and last_resolved.path or nil
            )
            if not child then
               break
            end
            last_resolved = child
            current = child
            index = resolved_index + 1
         end

         local final_step = scopes[index]
         if last_resolved and index > #parts and final_step then
            refresh_children(last_resolved, final_step.children)
         elseif #parts == 0 and scopes[1] then
            refresh_children(nil, scopes[1].children)
         end

         tree:render()
         if index <= #parts then
            vim.notify(
               string.format(
                  "Could not resolve hierarchy path '%s' at '%s' (last resolved: '%s')",
                  path,
                  parts[index],
                  last_resolved and last_resolved.path or "root"
               ),
               vim.log.levels.WARN
            )
         end
         if opts.focus and last_resolved then
            focus_tree(last_resolved)
         end
      end,
      on_failure = function(message)
         if active() then
            handlers.defaultOnFailure(message)
         end
      end,
   }, { hierPath = path })
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
---@param focus_path boolean? Move the hierarchy cursor to the resolved path
function M.show(top, focus_path)
   local navigation = require("slang-server.navigation")
   local navigation_config = config.navigation
   local hierarchy_config = navigation_config.hierarchy
   local split = ui.NuiSplit({
      relative = "win",
      position = navigation_config.position,
      size = navigation_config.width,
      buf_options = {
         bufhidden = "hide",
      },
      win_options = {
         signcolumn = "no",
         number = false,
         relativenumber = false,
         wrap = navigation_config.wrap,
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
      get_node_id = navigation.get_node_id,
      bufnr = split.bufnr,
   })

   map_keys(split, tree)

   M.state.split = split
   navigation.protect_window(M.state)
   M.state.tree = tree
   M.state.generation = M.state.generation + 1

   M.reveal(top, { focus = focus_path })
end

return M
