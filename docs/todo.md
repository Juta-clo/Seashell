- [ ] simplify plan lifetime logic (maybe time for explicit plan module?)
- [ ] config file? for prompt style initially

### PTY HARNESS

- [ ] Detect the exit status of seashell. Useful if seshell segfaults
- [ ] Allow the test repl to restart tests without exiting
- [ ] Make a script or program to relaunch seashell (vg replacement)

**LEAK!!!**

I think its todo with the new "everything has a plan" logic. It really
is time to make the plan seams and life time clear.

==1258339== 18 (8 direct, 10 indirect) bytes in 1 blocks are definitely lost
==1258339==    at 0x4849835: realloc (vg_replace_malloc.c:1741)
==1258339==    by 0x40E7F7: da_charp_reserve (deps/linc_tools/src/darr.c:81)
==1258339==    by 0x40E891: da_charp_push (deps/linc_tools/src/darr.c:81)
==1258339==    by 0x402BF0: expand_word (src/expander.c:388)
==1258339==    by 0x40284E: ex_expand (src/expander.c:428)
==1258339==    by 0x4071BD: line_to_ast (src/main.c:78)
==1258339==    by 0x4069F4: main (src/main.c:392)
==1258339==

==1266443== 366 (160 direct, 206 indirect) bytes in 1 blocks are definitely lost
==1266443==    at 0x4849835: realloc (vg_replace_malloc.c:1741)
==1266443==    by 0x40E9F7: da_charp_reserve (deps/linc_tools/src/darr.c:81)
==1266443==    by 0x40EA91: da_charp_push (deps/linc_tools/src/darr.c:81)
==1266443==    by 0x40339A: expand_globs (src/expander.c:344)
==1266443==    by 0x402B26: expand_word (src/expander.c:393)
==1266443==    by 0x40284E: ex_expand (src/expander.c:441)
==1266443==    by 0x4073BD: line_to_ast (src/main.c:78)
==1266443==    by 0x406BF4: main (src/main.c:392)
==1266443== 

### Resolved

1. What happens if an interactive termianal program (that modifies termios) is
   suspended via SIGSTOP or doesn't handle SIGTSTP?
    - We save the initial state and restore it. vice versa for the shell.

2. Should job_state manage job id assignments?
    - No need for now. A andor chain asks for the same id immediately and
      synchrnously after the last job in the chain has ended so it will always
      be available.

3. Should we allow $ to show up inside of variable key/values?
    - Hell no.

4. Should we enforce encapsulation using opaque pointers?
    - Forgot what this was in reference too. But thats more of an api decision.
      Just don't access state when you shouldn't, easy!

5. Should we enforce obj init failing means its ok to call obj free?
    - Yes. But for types where it makes sense. For example the dynamic array.
      It's already super simple and has the very nice of property of it being
      zeroed out is valid state.

6. Should functions that mutate state revert the changes on failure?
    - Currently not really worth it. For most errors they are fatal which
      brings EVERYTHING down so why bother? If that ever changes we would need
      to think about this.

7. Should we make a light emulation layer so pty tests don't just compare raw
   bytes? For exmpale converting bytes into operations like mv_curs_up(1),
   erase_line(), etc. Then comparing the actual screen state rather then just
   raw bytes being identicle. The pty test harness could then interact with the
   emulator instead for example feed(...) -> cells\[x,y\] = "..." See libvterm.
    - Not make, but yes! vterm.h is awesome and buys basically exactly this
      without having to actaully build the thing.

8. Is fg_jid enough to replace the subshell boolean?
    - No! Definitely no! Even if there wasn't any cases where they diverged
      they are semantically different enough that its worth keeping them
      separate.

9. Is the simpler void * dyn array strictly just better?
    - For this project and the data structures I use (lots of nested dynamic
      arrays, no arenas) I think the generated typed ones are better. If not
      everysingle time I needed to do nested access I would first have to
      define a variable, then from there do access. Even just doing that once
      is a pain.

10. Should we make some read/write wrappers? to alleviate potential mixing of C
    streams and linux fd's as well as buffering issues.
    - Yes! Its a todo item in the linc_tools library.

11. Should we expand 1 andor chain at a time?
    - We could but I don't think it buys much. Its cheap as hell to expand the
      entire and or chain at once so even if we don't use it its whatever. It
      buys simplicity.

12. Should we allow add_jobs to free pline_data
    - This is now a thing. Mostly because we actaully pass in pline_data in
      order to get the command names for richer job builtin output.

13. Should we make x-funcs err_exit directly on failure
    - This is now handled by the xfuncs module from linc_tools.
