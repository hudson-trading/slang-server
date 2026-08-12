local config = require("slang-server._core.config").CONFIG

local M = {}

M.ns_id = vim.api.nvim_create_namespace("SlangServer")

M.HIER_HEADING = "SlangServerHeading"
M.HIER_TOP = "SlangServerHierarchyTop"
M.HIER_INSTANCE = "SlangServerInstance"
M.HIER_INSTANCE_ARRAY = "SlangServerInstanceArray"
M.HIER_SCOPE = "SlangServerScope"
M.HIER_SCOPE_ARRAY = "SlangServerScopeArray"
M.HIER_PACKAGE = "SlangPackage"
M.HIER_PORT = "SlangServerPort"
M.HIER_PARAM = "SlangServerParam"
M.HIER_REG = "SlangServerReg"

M.HIER_NORMAL = "Normal"
M.HIER_SUBTLE = "Comment"
M.HIER_VALUE = "Constant"
M.HIER_ERROR = "ErrorMsg"

for hl_name, hl_opts in pairs(config.highlights) do
   vim.api.nvim_set_hl(M.ns_id, hl_name, hl_opts)
end

vim.api.nvim_set_hl_ns(M.ns_id)

return M
