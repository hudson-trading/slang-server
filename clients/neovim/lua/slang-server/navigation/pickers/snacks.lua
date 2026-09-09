local M = {}

function M.available()
   local ok, snacks = pcall(require, "snacks")
   return ok and snacks.picker ~= nil
end

---@param opts slang-server.navigation.SearchPickerOptions
function M.open(opts)
   local snacks = require("snacks")
   snacks.picker.pick({
      title = "Search hierarchy",
      live = true,
      supports_live = true,
      finder = function(_, ctx)
         return function(cb)
            opts.search(ctx.filter.search, function(result)
               if ctx.async:aborted() then
                  return
               end
               for _, item in ipairs(result.matches) do
                  cb({ text = item.path, item = item, description = item.description })
               end
               ctx.async:resume()
            end)
            ctx.async:suspend()
         end
      end,
      format = function(item)
         return { { item.text }, { item.description and (" — " .. item.description) or "", "Comment" } }
      end,
      confirm = function(picker, item)
         picker:close()
         if item then
            opts.select(item.item)
         end
      end,
   })
end

return M
