-- Example config file; symlink to ~/.seashell.lua

local cred = "\27[31m"
local cmag = "\27[35m"
local ccyan = "\27[36m"
local coff = "\27[m"

local prompt

local function push(...)
    for _, str in ipairs({...}) do
        prompt = prompt .. str
    end
end

return {
    prompt = function(ctx)
        prompt = ""

        push(ccyan)

        if ctx.cwd == ctx.home then
            push("~ ")
        else
            push(ctx.cwd:match("([^/]+)$"))
        end

        push(coff)

        if ctx.git then
            push(" ", cmag, "(" , coff, ctx.git.branch, cmag, ")", coff)
        end

        push(" ")

        if ctx.last.status == 0 then
            push(cmag, "> ", coff)
        else
            push(cred, "> ", coff)
        end

        return prompt
    end
}
