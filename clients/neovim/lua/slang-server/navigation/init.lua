local hl = require("slang-server._core.highlights")
local ui = require("slang-server._core.ui")
local client = require("slang-server._lsp.client")
local capabilities = require("slang-server._lsp.capabilities")
local handlers = require("slang-server.handlers")
local hier = require("slang-server.navigation.hierarchy")
local cells = require("slang-server.navigation.cells")

local M = {}

---@type slang-server.navigation.State
M.state = { open = false }

---Keep a navigation window dedicated to its buffer, redirecting attempted buffer
---changes to the source window instead.
---@param state { split: NuiSplit?, buffer_guard: integer? }
function M.protect_window(state)
   local split = assert(state.split)
   local winid = split.winid
   local bufnr = split.bufnr

   state.buffer_guard = vim.api.nvim_create_autocmd("BufEnter", {
      callback = function(args)
         if not M.state.open or not vim.api.nvim_win_is_valid(winid) then
            return
         end
         if vim.api.nvim_get_current_win() ~= winid or args.buf == bufnr then
            return
         end

         local source_winid = M.state.source_winid
         if source_winid and vim.api.nvim_win_is_valid(source_winid) then
            vim.api.nvim_win_set_buf(source_winid, args.buf)
         else
            vim.notify("Cannot open buffer: invalid source window", vim.log.levels.ERROR)
         end

         vim.api.nvim_win_set_buf(winid, bufnr)
      end,
   })
end

---@param state { buffer_guard: integer? }
function M.unprotect_window(state)
   if state.buffer_guard then
      pcall(vim.api.nvim_del_autocmd, state.buffer_guard)
      state.buffer_guard = nil
   end
end

---Return whether a component's navigation split is still active for an async request.
---@param state { generation: integer, tree: NuiTree?, split: NuiSplit? }
---@param generation integer
---@return boolean
function M.session_active(state, generation)
   return M.state.open
      and state.generation == generation
      and state.tree ~= nil
      and state.split ~= nil
      and vim.api.nvim_win_is_valid(state.split.winid)
end

---Return the most relevant source buffer that has an attached slang-server client.
---@return vim.fn.getbufinfo.ret.item?
local function source_buf()
   local bufnr = capabilities.get_source_context()
   if not capabilities.get_client(bufnr) then
      return nil
   end
   return vim.fn.getbufinfo(bufnr)[1]
end

-- M.state.sv_buf returns the most relevant attached SV buffer info.
-- M.state.sv_win returns a visible window containing that buffer.
setmetatable(M.state, {
   ---@param _k string
   ---@return integer
   __index = function(self, _k)
      if _k == "sv_buf" then
         return source_buf()
      elseif _k == "sv_win" then
         local source = source_buf()
         if not source then
            return nil
         end
         local winid = vim.fn.bufwinid(source.bufnr)
         if winid == -1 then
            return nil
         end
         return vim.fn.getwininfo(winid)[1]
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

---@param mappings table<string, slang-server.ui.Mapping>
---@param key slang-server.config.Key?
---@param spec slang-server.ui.Mapping
function M.add_mapping(mappings, key, spec)
   if key then
      mappings[key] = spec
   end
end

---@param tree NuiTree
---@param msg string
---@param opts {parent: NuiTree.Node?, hl: string?}?
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

---Focus the most recently used source window and return its buffer.
---@return integer?
function M.focus_source()
   local source_win = M.state.sv_win
   if not source_win or not capabilities.get_client(source_win.bufnr) then
      vim.notify("Cannot jump to location: invalid target window", vim.log.levels.ERROR)
      return nil
   end

   vim.api.nvim_set_current_win(source_win.winid)
   return source_win.bufnr
end

---Ask the server to open a hierarchy path in the remembered source window.
---@param hier_path string
function M.show_hier_location(hier_path)
   local bufnr = M.focus_source()
   if not bufnr then
      return
   end

   client.showHierLocation(bufnr, {
      on_success = function() end,
      on_failure = handlers.defaultOnFailure,
   }, { hierPath = hier_path, takeFocus = true })
end

---@param top slang-server.navigation.Path The top level at which to initialise the hierarchy
---@param focus_path boolean? Move the hierarchy cursor to the resolved path
function M.show(top, focus_path)
   if M.state.open then
      hier.reveal(top, { focus = focus_path })
      if hier.state.split and vim.api.nvim_win_is_valid(hier.state.split.winid) then
         vim.api.nvim_set_current_win(hier.state.split.winid)
      end
      return
   end

   M.state.open = true
   M.state.source_winid = vim.api.nvim_get_current_win()

   hier.show(top, focus_path)
   cells.show()

   vim.api.nvim_set_current_win(hier.state.split.winid)
end

return M
