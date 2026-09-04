- [ ] config file? for prompt style initially

### PTY HARNESS

- [ ] Implement resize handling for ncurses debug ui
- [ ] Allow the test repl to restart tests without exiting
- [ ] Make a script or program to relaunch seashell (vg replacement)
- [ ] decouple ncurses debug display with pty test state. For example the
      debug display should not have to know about the verbose_mode flag.
- [ ] Figure out what pty_test.c really needs to expose in header

### Unresolved

- What happens if an interactive termianal program (that modifies termio)
  is suspended via SIGKILL or doesn't handle SIGTSTP?

- Should job_state manage job id assignments?

- Should we allow $ to show up inside of variable key/values?

- Should we enforce encapsulation using opaque pointers?

- Should we enforce obj init failing means its ok to call obj free?

- Should functions that mutate state revert the changes on failure?

- Should we make a light emulation layer so pty tests don't just compare raw
  bytes? For exmpale converting bytes into operations like mv_curs_up(1),
  erase_line(), etc. Then comparing the actual screen state rather then
  just raw bytes being identicle. The pty test harness could then interact
  with the emulator instead for example feed(...) -> cells\[x,y\] = "..."
  See libvterm.

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
