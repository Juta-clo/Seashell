-- Example config file; symlink to ~/.seashell.lua

return {
    prompt = function(ctx)
        local prompt = ""

        if ctx.cwd == ctx.home then
            prompt = prompt .. "~ "
        else
            prompt = ctx.cwd:match("([^/]+)$")
        end

        if ctx.git then
            prompt = prompt .. " *" .. ctx.git.branch .. " "
        end

        return prompt .. "> "
    end
}
