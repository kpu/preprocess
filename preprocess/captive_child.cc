#include "preprocess/captive_child.hh"

#include "util/exception.hh"
#include "util/file.hh"

#include <signal.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

namespace preprocess {

namespace {
void Pipe(util::scoped_fd &first, util::scoped_fd &second) {
  int fds[2];
  UTIL_THROW_IF(pipe(fds), util::ErrnoException, "Creating pipe failed");
  first.reset(fds[0]);
  second.reset(fds[1]);
}
} // namespace

pid_t Launch(char *argv[], util::scoped_fd &in, util::scoped_fd &out) {
  util::scoped_fd process_in, process_out;
  Pipe(process_in, in);
  Pipe(out, process_out);
  pid_t pid = fork();
  UTIL_THROW_IF(pid == -1, util::ErrnoException, "Fork failed");
  if (pid == 0) {
    // Inside child process.
    #ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    #endif
    UTIL_THROW_IF(-1 == dup2(process_in.get(), STDIN_FILENO), util::ErrnoException, "dup2 failed for process stdin from " << process_in.get());
    UTIL_THROW_IF(-1 == dup2(process_out.get(), STDOUT_FILENO), util::ErrnoException, "dup2 failed for process stdout from " << process_out.get());
    in.reset();
    out.reset();
    execvp(argv[0], argv);
    util::ErrnoException e;
    std::cerr << "exec " << argv[0] << " failed: " << e.what() << std::endl;
    abort();
  }
  // Parent closes parts it doesn't need in destructors.
  return pid;
}

int Wait(pid_t child) {
  int status;
  UTIL_THROW_IF(-1 == waitpid(child, &status, 0), util::ErrnoException, "waitpid for child failed");
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  } else {
    return 256;
  }
}

} // namespace preprocess

