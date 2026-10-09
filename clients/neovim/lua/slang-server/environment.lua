local M = {}

-- Strip JSONC extensions outside strings before handing syntax validation to Neovim.
local function decode_jsonc(text)
   local tokens = {}
   local i = 1
   while i <= #text do
      local char = text:sub(i, i)
      local pair = text:sub(i, i + 1)
      if char == '"' then
         local start = i
         i = i + 1
         while i <= #text do
            local current = text:sub(i, i)
            i = i + 1
            if current == "\\" then
               i = i + 1
            elseif current == '"' then
               break
            end
         end
         tokens[#tokens + 1] = text:sub(start, i - 1)
      elseif pair == "//" then
         i = text:find("[\r\n]", i + 2) or (#text + 1)
         tokens[#tokens + 1] = " "
      elseif pair == "/*" then
         local finish = text:find("*/", i + 2, true)
         if not finish then
            error("unterminated JSON comment")
         end
         i = finish + 2
         tokens[#tokens + 1] = " "
      else
         tokens[#tokens + 1] = char
         i = i + 1
      end
   end
   for index, token in ipairs(tokens) do
      if token == "," then
         local next_index = index + 1
         while tokens[next_index] and tokens[next_index]:match("^%s+$") do
            next_index = next_index + 1
         end
         if tokens[next_index] == "}" or tokens[next_index] == "]" then
            local previous = index - 1
            while tokens[previous] and tokens[previous]:match("^%s+$") do
               previous = previous - 1
            end
            if tokens[previous] == "{" or tokens[previous] == "[" or tokens[previous] == "," then
               error("unexpected JSON comma")
            end
            tokens[index] = ""
         end
      end
   end
   return vim.json.decode(table.concat(tokens))
end

---Load literal startup overrides in workspace, user, local order.
---@param root string? Workspace root
---@param user_home string? User config directory; defaults to Neovim's inherited HOME
---@return table<string, string>
function M.load(root, user_home)
   user_home = user_home or vim.env.HOME
   local files = {}
   if root then
      files[#files + 1] = root .. "/.slang/server.json"
   end
   if user_home and user_home ~= "" then
      files[#files + 1] = user_home .. "/.slang/server.json"
   end
   if root then
      files[#files + 1] = root .. "/.slang/local/server.json"
   end
   local env = {}
   for _, path in ipairs(files) do
      local file, message, code = io.open(path, "r")
      local ok, values = true, nil
      if file then
         local text = file:read("*a")
         file:close()
         ok, values = pcall(function()
            local config = decode_jsonc(text)
            if type(config) ~= "table" or vim.islist(config) then
               error("expected a configuration object")
            end
            if config.env == nil then
               return {}
            end
            if type(config.env) ~= "table" or vim.islist(config.env) then
               error("env must be an object mapping variable names to strings")
            end
            for name, value in pairs(config.env) do
               if
                  type(name) ~= "string"
                  or name == ""
                  or name:find("[=%z]")
                  or type(value) ~= "string"
                  or value:find("%z")
               then
                  error(
                     "invalid env entry "
                        .. vim.inspect(name)
                        .. ": expected a variable name and string value without NUL bytes"
                  )
               end
            end
            return config.env
         end)
      elseif code ~= 2 and code ~= 20 then
         ok, values = false, message
      end
      if not ok then
         vim.notify(
            "Failed to load slang-server environment from " .. path .. ": " .. tostring(values),
            vim.log.levels.ERROR
         )
      elseif values then
         env = vim.tbl_extend("force", env, values)
      end
   end
   return env
end

return M
