- [ ] Add generic allocator interface to linc_tools
- [ ] Add Arena allocater to linc_tools
- [ ] Add linked list module to linc_tools
- [ ] test stopping andor chain with plines left
- [ ] config file? for prompt style initially

### Unresolved

- Should we allow $ to show up inside of variable key/values?

- Should we enforce encapsulation using opaque pointers?

- Should we enforce obj init failing means its ok to call obj free?

- Should functions that mutate state revert the changes on failure?

- Should we make a light emulation layer so pty tests don't just compare raw
  bytes? For exmpale converting bytes into operations like mv_curs_up(1),
  erase_line(), etc. Then comparing the actual screen state rather then
  just raw bytes being identicle. The pty test harness could then interact
  with the emulator instead for example feed(...) -> cells\[x,y\] = "..."

- Is fg_jid enough to replace the subshell boolean?

- Is the simpler void * dyn array strictly just better?

- Should we make some read/write wrappers? to alleviate potential mixing
  of C streams and linux fd's as well as buffering issues.

- How should requests of job ids work, in order to reuse the same job id
  for each job in an andor chain?

- Should we expand 1 andor chain at a time?

- Should we allow add_jobs to free pline_data

- Should we make x-funcs err_exit directly on failure

- If we need to test builtins, consider moving jctl back into sh_env
