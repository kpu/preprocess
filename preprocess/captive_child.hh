#pragma once

#include <sys/types.h>

namespace util { class scoped_fd; }

namespace preprocess {

// Launch a child process.  The child's stdin and stdout pipes will be returned as in and out.
pid_t Launch(char *argv[], util::scoped_fd &in, util::scoped_fd &out);

// Wait for a child to finish and return an appropriate status for it.
int Wait(pid_t child);

} // namespace preprocess
