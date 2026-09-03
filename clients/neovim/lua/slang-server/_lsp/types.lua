
---@alias slang-server.SourceLoc {character: integer, line: integer}

---@alias slang-server.SourceRange { ["start"]: slang-server.SourceLoc, ["end"]: slang-server.SourceLoc }

---@alias slang-server.Uri string

---@alias slang-server.ScopedRange {range: slang-server.SourceRange, uri: slang-server.Uri}

---@alias slang-server.SlangKind
---| '"Instance"'
---| '"InstanceArray"'
---| '"Scope"'
---| '"ScopeArray"'
---| '"InterfacePort"'
---| '"InterfacePortArray"'
---| '"Param"'
---| '"Port"'
---| '"Logic"'
---| '"Package"'

---@class slang-server.lsp.Item
---@field kind slang-server.SlangKind
---@field instName string
---@field instLoc slang-server.ScopedRange

---@class slang-server.lsp.Var : slang-server.lsp.Item
---@field type string

---@class slang-server.lsp.Param : slang-server.lsp.Var
---@field value string

---@class slang-server.lsp.Scope : slang-server.lsp.Item
---@field type string?
---@field children slang-server.lsp.Item[]

---@class slang-server.lsp.Instance : slang-server.lsp.Item
---@field declName string
---@field declLoc slang-server.ScopedRange

---@class slang-server.lsp.FilledInstance : slang-server.lsp.Scope, slang-server.lsp.Instance

---@alias slang-server.lsp.Node slang-server.lsp.Item | slang-server.lsp.Var | slang-server.lsp.Scope | slang-server.lsp.FilledInstance

---@class slang-server.lsp.QualifiedInstance
---@field instPath string
---@field instLoc slang-server.ScopedRange

---@class slang-server.lsp.InstanceSet
---@field declName string
---@field declLoc slang-server.ScopedRange
---@field instCount integer
---@field inst slang-server.lsp.QualifiedInstance?

---@class slang-server.lsp.ScopeStep
---@field path string
---@field children slang-server.lsp.Node[]

---@class slang-server.lsp.ActivateInstanceParams
---@field hierPath string
---@field interactionSource string

---@class slang-server.lsp.ClientState
---@field active_path string?

---@class slang-server.lsp.HierarchySearchItem
---@field name string
---@field path string
---@field kind slang-server.SlangKind
---@field description string?
---@field containerName string?

---@class slang-server.lsp.HierarchySearchResult
---@field totalResults integer
---@field matches slang-server.lsp.HierarchySearchItem[]

---@alias RespHandlers {on_success: fun(resp: any), on_failure?: fun(message: string)}
