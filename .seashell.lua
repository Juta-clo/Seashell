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
    export = {
        PATH = os.getenv("HOME") .. "/.local/bin:" .. os.getenv("PATH"),
        EDITOR = os.getenv("HOME") .. "./local/bin/nvim",
        MANPAGER = 'nvim +Man!',
        XMAKE_COLORTERM = "color8",
        LS_COLORS = dofile(".ls_colors.lua")
    },
    prompt = function(ctx)
        prompt = ""

        push(ccyan)

        if ctx.cwd == ctx.home then
            push("~")
        else
            push(ctx.cwd:match("([^/]+)$") or "/")
        end

        push(coff)

        if ctx.git then
            push(" ", cmag, "(", coff, ctx.git.branch)

            if ctx.git.dirty == 1 then
                push(ccyan, "*", coff)
            end

            if ctx.git.untracked == 1 then
                push(ccyan, "+", coff)
            end

            push(cmag, ")", coff)
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
