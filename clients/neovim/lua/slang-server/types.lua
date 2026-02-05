
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

--- Navigation types

---@alias slang-server.navigation.Path string

---@class slang-server.navigation.State
---@field open boolean
---@field sv_buf vim.fn.getbufinfo.ret.item?
---@field sv_win vim.fn.getwininfo.ret.item?

---@class slang-server.navigation.hierarchy.State
---@field hover NuiPopup?
---@field split NuiSplit?
---@field tree NuiTree?

---@class slang-server.navigation.cells.State
---@field tree NuiTree?
---@field split NuiSplit?

---@class slang-server.navigation.TreeNode: NuiTree.Node
---@field path string
---@field _uid string
---@field _populated boolean
---@field kind slang-server.SlangKind
---@field instName string
---@field instLoc slang-server.ScopedRange
---@field type string?
---@field value string?
---@field children slang-server.navigation.TreeNode[]?
---@field declName string?
---@field declLoc slang-server.ScopedRange?

---@class slang-server.navigation.MessageNode: NuiTree.Node
---@field text string
---@field _uid string

---@alias slang-server.navigation.HierNode slang-server.navigation.TreeNode | slang-server.navigation.MessageNode

---@class slang-server.navigation.InstNode: NuiTree.Node
---@field instPath string
---@field instLoc slang-server.SourceLoc
---@field last boolean
---@field _uid string

---@class slang-server.navigation.CellNode: NuiTree.Node
---@field declName string
---@field declLoc slang-server.SourceLoc
---@field instCount integer
---@field _uid string

---@alias slang-server.navigation.ScopeNode slang-server.navigation.InstNode | slang-server.navigation.CellNode | slang-server.navigation.MessageNode
---@alias slang-server.navigation.Node slang-server.navigation.HierNode | slang-server.navigation.ScopeNode

--- UI types

---@class slang-server.ui.Subcommand
---@field impl fun(args: string[], opts: table)
---@field complete? string | fun(subcmd_arg_lead: string): string[]

---@class slang-server.ui.Mapping
---@field impl fun(node:slang-server.navigation.Node?)
---@field opts table
---@field desc string
