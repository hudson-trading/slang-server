
--- Configuration schema

---@class (exact) slang-server.config.Configuration
---    The user's slang-server configuration
---@field kinds slang-server.config.Kinds?
---@field highlights slang-server.config.Highlights?
---@field hierarchy slang-server.config.Hierarchy?

---@class (exact) slang-server.config.Hierarchy
---@field position string?
---@field size integer?

---@class (exact) slang-server.config.Kinds
---@field instance slang-server.config.KindScoped?
---@field scope slang-server.config.KindScoped?
---@field port slang-server.config.Kind?
---@field param slang-server.config.Kind?
---@field reg slang-server.config.Kind?

---@class (exact) slang-server.config.Kind
---@field icon string?
---@field hl string?

---@class (exact) slang-server.config.KindScoped
---@field open slang-server.config.Kind?
---@field closed slang-server.config.Kind?

---@alias slang-server.config.Highlights table<string, vim.api.keyset.highlight>

--- Hierarchy view types

---@alias slang-server.hierarchy.Path string

---@class slang-server.hierarchy.State
---@field open boolean
---@field scope string?
---@field split NuiSplit?
---@field tree NuiTree?
---@field hover NuiPopup?
---@field cellTree NuiTree?
---@field cellSplit NuiSplit?
---@field sv_buf vim.fn.getbufinfo.ret.item?
---@field sv_win vim.fn.getwininfo.ret.item?

---@class slang-server.hierarchy.TreeNode: NuiTree.Node
---@field path string
---@field _uid string
---@field _populated boolean
---@field kind slang-server.SlangKind
---@field instName string
---@field instLoc slang-server.ScopedRange
---@field type string?
---@field value string?
---@field children slang-server.hierarchy.TreeNode[]?
---@field declName string?
---@field declLoc slang-server.ScopedRange?

---@class slang-server.hierarchy.MessageNode: NuiTree.Node
---@field text string
---@field _uid string

---@alias slang-server.hierarchy.HierNode slang-server.hierarchy.TreeNode | slang-server.hierarchy.MessageNode

---@class slang-server.hierarchy.InstNode: NuiTree.Node
---@field instPath string
---@field instLoc slang-server.SourceLoc
---@field last boolean
---@field _uid string

---@class slang-server.hierarchy.CellNode: NuiTree.Node
---@field declName string
---@field declLoc slang-server.SourceLoc
---@field instCount integer
---@field _uid string

---@alias slang-server.hierarchy.ScopeNode slang-server.hierarchy.InstNode | slang-server.hierarchy.CellNode | slang-server.hierarchy.MessageNode
---@alias slang-server.hierarchy.Node slang-server.hierarchy.HierNode | slang-server.hierarchy.ScopeNode

--- UI types

---@class slang-server.ui.Subcommand
---@field impl fun(args: string[], opts: table)
---@field complete? string | fun(subcmd_arg_lead: string): string[]

---@class slang-server.ui.Mapping
---@field impl fun(node:slang-server.hierarchy.Node?)
---@field opts table
---@field desc string
