local M = {}

M = vim.tbl_deep_extend("error", M, require("slang-server._commands.setTopLevel"))
M = vim.tbl_deep_extend("error", M, require("slang-server._commands.setBuildFile"))
M = vim.tbl_deep_extend("error", M, require("slang-server._commands.hierarchy"))
M = vim.tbl_deep_extend("error", M, require("slang-server._commands.searchHierarchy"))
M = vim.tbl_deep_extend("error", M, require("slang-server._commands.selectActive"))
M = vim.tbl_deep_extend("error", M, require("slang-server._commands.openWaveform"))
M = vim.tbl_deep_extend("error", M, require("slang-server._commands.addToWaves"))

return M
