
--- Configuration schema

---@class (exact) slang-server.config.Configuration
---    The user's slang-server configuration
---@field kinds slang-server.config.Kinds?
---@field highlights slang-server.config.Highlights?
---@field navigation slang-server.config.Navigation?
---@field keymaps slang-server.config.Keymaps?

---@class (exact) slang-server.config.Navigation
---@field position string?
---@field width integer?
---@field wrap boolean?
---@field hierarchy slang-server.config.NavigationHierarchy?
---@field cells slang-server.config.NavigationCells?

---@class (exact) slang-server.config.NavigationHierarchy
---@field keymaps slang-server.config.HierarchyKeymaps?

---@class (exact) slang-server.config.NavigationCells
---@field show boolean?
---@field height integer?
---@field keymaps slang-server.config.CellsKeymaps?

---@alias slang-server.config.Key string|false

---@class (exact) slang-server.config.Keymap
---@field key string?
---@field enabled boolean?

---@class (exact) slang-server.config.Keymaps
---@field enable_defaults boolean?
---@field hierarchy slang-server.config.Keymap?
---@field findInstance slang-server.config.Keymap?
---@field focus slang-server.config.Keymap?
---@field selectActive slang-server.config.Keymap?

---@class (exact) slang-server.config.HierarchyKeymaps
---@field yank_path slang-server.config.Key?
---@field yank_value slang-server.config.Key?
---@field yank_file slang-server.config.Key?
---@field jump slang-server.config.Key?
---@field jump_to_declaration slang-server.config.Key?
---@field toggle slang-server.config.Key?
---@field close slang-server.config.Key?
---@field help slang-server.config.Key?

---@class (exact) slang-server.config.CellsKeymaps
---@field jump slang-server.config.Key?
---@field toggle slang-server.config.Key?
---@field find_instance slang-server.config.Key?
---@field close slang-server.config.Key?
---@field help slang-server.config.Key?

---@class (exact) slang-server.config.Kinds
---@field instance slang-server.config.Kind?
---@field instancearray slang-server.config.Kind?
---@field scope slang-server.config.Kind?
---@field scopearray slang-server.config.Kind?
---@field interfaceport slang-server.config.Kind?
---@field interfaceportarray slang-server.config.Kind?
---@field package slang-server.config.Kind?
---@field port slang-server.config.PortKinds?
---@field param slang-server.config.Kind?
---@field logic slang-server.config.Kind?
---@field reg slang-server.config.Kind?

---@class (exact) slang-server.config.Kind
---@field icon string?
---@field hl string?

---@class (exact) slang-server.config.PortKinds
---@field input slang-server.config.Kind?
---@field output slang-server.config.Kind?
---@field inout slang-server.config.Kind?

---@alias slang-server.config.Highlights table<string, vim.api.keyset.highlight>

---@class (exact) slang-server.SearchHierarchyOptions
---@field bufnr integer?
---@field on_error fun(message: string)?

--- Navigation types

---@alias slang-server.navigation.Path string

---@class slang-server.navigation.State
---@field open boolean
---@field source_winid integer?
---@field sv_buf vim.fn.getbufinfo.ret.item?
---@field sv_win vim.fn.getwininfo.ret.item?

---@class slang-server.navigation.hierarchy.State
---@field hover NuiPopup?
---@field split NuiSplit?
---@field tree NuiTree?
---@field generation integer
---@field buffer_guard integer?

---@class slang-server.navigation.cells.State
---@field tree NuiTree?
---@field split NuiSplit?
---@field generation integer
---@field buffer_guard integer?

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
---@field __children slang-server.navigation.TreeNode[]?
---@field _child_ids string[]?

---@class slang-server.navigation.MessageNode: NuiTree.Node
---@field text string
---@field _uid string

---@alias slang-server.navigation.HierNode slang-server.navigation.TreeNode | slang-server.navigation.MessageNode

---@class slang-server.navigation.InstNode: NuiTree.Node
---@field instPath string
---@field instLoc slang-server.ScopedRange
---@field last boolean
---@field _uid string

---@class slang-server.navigation.CellNode: NuiTree.Node
---@field declName string
---@field declLoc slang-server.ScopedRange
---@field instCount integer
---@field _uid string

---@alias slang-server.navigation.ScopeNode slang-server.navigation.InstNode | slang-server.navigation.CellNode | slang-server.navigation.MessageNode
---@alias slang-server.navigation.Node slang-server.navigation.HierNode | slang-server.navigation.ScopeNode

--- UI types

---@class slang-server.ui.Subcommand
---@field impl fun(args: string[], opts: table, bufnr: integer)
---@field complete? string | fun(subcmd_arg_lead: string): string[]
---@field desc string
---@field required_commands string[]
---@field context fun(args: string[]): integer

---@class slang-server.ui.Mapping
---@field impl fun(node:slang-server.navigation.Node?)
---@field opts table
---@field desc string
