local M = {}

---@param parent_path string
---@param child_name string
---@param parent_kind slang-server.SlangKind?
---@return string
function M.join(parent_path, child_name, parent_kind)
   if parent_path == "" then
      return child_name
   elseif child_name:sub(1, 1) == "[" then
      return parent_path .. child_name
   elseif parent_kind == "Package" then
      return parent_path .. "::" .. child_name
   end
   return parent_path .. "." .. child_name
end

---@param path string
---@return string[]
function M.split(path)
   local parts = {}
   local current = ""
   local index = 1
   while index <= #path do
      local char = path:sub(index, index)
      local next_char = path:sub(index + 1, index + 1)
      if char == "." or (char == ":" and next_char == ":") then
         if current ~= "" then
            parts[#parts + 1] = current
            current = ""
         end
         index = index + (char == ":" and 2 or 1)
      else
         if char == "[" and current ~= "" then
            parts[#parts + 1] = current
            current = ""
         end
         current = current .. char
         if char == "]" then
            parts[#parts + 1] = current
            current = ""
         end
         index = index + 1
      end
   end
   if current ~= "" then
      parts[#parts + 1] = current
   end
   return parts
end

---@param children slang-server.navigation.TreeNode[]
---@param parts string[]
---@param index integer
---@param last_path string?
---@return slang-server.navigation.TreeNode?, integer
function M.resolve_child(children, parts, index, last_path)
   local part = parts[index]
   if not part then
      return nil, index
   end
   for _, child in ipairs(children) do
      if child.instName == part then
         return child, index
      end
   end
   local next_part = parts[index + 1]
   if next_part and next_part:sub(1, 1) == "[" then
      for _, child in ipairs(children) do
         if child.instName == part .. next_part then
            return child, index + 1
         end
      end
   end
   if part:sub(1, 1) == "[" and last_path then
      for _, child in ipairs(children) do
         if child.path == last_path .. part then
            return child, index
         end
      end
   end
   return nil, index
end

return M
