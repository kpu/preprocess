#include "util/fake_ofstream.hh"
#include "util/file_piece.hh"
#include "util/file.hh"
#include "util/murmur_hash.hh"
#include "util/pcqueue.hh"
#include "util/pool.hh"

#include <string>
#include <thread>
#include <unordered_map>

#include <signal.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void Pipe(util::scoped_fd &first, util::scoped_fd &second) {
  int fds[2];
  UTIL_THROW_IF(pipe(fds), util::ErrnoException, "Creating pipe failed");
  first.reset(fds[0]);
  second.reset(fds[1]);
}

pid_t Launch(char *argv[], util::scoped_fd &in, util::scoped_fd &out) {
  util::scoped_fd process_in, process_out;
  Pipe(process_in, in);
  Pipe(out, process_out);
  pid_t pid = fork();
  UTIL_THROW_IF(pid == -1, util::ErrnoException, "Fork failed");
  if (pid == 0) {
    // Inside child process.
    prctl(PR_SET_PDEATHSIG, SIGTERM);
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

struct QueueEntry {
  // NULL pointer is poison.
  // value->data() == NULL means uninitialized
  StringPiece *value;
};

void Input(util::UnboundedSingleQueue<QueueEntry> &queue, util::scoped_fd &process_input, std::unordered_map<uint64_t, StringPiece> &cache, std::size_t flush_rate) {
  QueueEntry q_entry;
  {
    util::FakeOFStream process(process_input.get());
    std::pair<uint64_t, StringPiece> entry;
    std::size_t flush_count = flush_rate;
    for (StringPiece l : util::FilePiece(STDIN_FILENO)) {
      entry.first = util::MurmurHashNative(l.data(), l.size());
      std::pair<std::unordered_map<uint64_t, StringPiece>::iterator, bool> res(cache.insert(entry));
      if (res.second) {
        // New entry.  Send to captive process.
        process << l << '\n';
        // Guarantee we flush to process every so often.
        if (!--flush_count) {
          process.Flush();
          flush_count = flush_rate;
        }
      }
      // Pointer to hash table entry.
      q_entry.value = &res.first->second;
      // Deadlock here if the captive program buffers too many lines.
      queue.Produce(q_entry);
    }
  }
  process_input.reset();
  // Poison.
  q_entry.value = NULL;
  queue.Produce(q_entry);
}

// Read from queue.  If it's not in the cache, read the result from the captive
// process.
void Output(util::UnboundedSingleQueue<QueueEntry> &queue, util::scoped_fd &process_output) {
  util::FakeOFStream out(STDOUT_FILENO);
  util::FilePiece in(process_output.release());
  // We'll allocate the cached strings into a pool.
  util::Pool string_pool;
  QueueEntry q;
  while (queue.Consume(q).value) {
    StringPiece &value = *q.value;
    if (!value.data()) {
      // New entry, not cached.
      StringPiece got = in.ReadLine();
      // Allocate memory to store a copy of the line.
      char *copy_to = (char*)string_pool.Allocate(got.size());
      memcpy(copy_to, got.data(), got.size());
      value = StringPiece(copy_to, got.size());
    }
    out << value << '\n';
  }
}

int main(int argc, char *argv[]) {
  // The underlying program can buffer up to kQueueLength - kFlushRate lines.  If it goes above that, deadlock waiting for queue.
  const std::size_t kFlushRate = 4096;
  if (argc < 2) {
    std::cerr << "Acts as a cache around another program processing one line in, one line out from stdin to stdout.\n"
      "Usage: " << argv[0] << " slow arguments_to_slow\n";
    return 1;
  }
  util::scoped_fd in, out;
  pid_t child = Launch(argv + 1, in, out);
  // We'll deadlock if this queue is full and the program is buffering.
  util::UnboundedSingleQueue<QueueEntry> queue;
  // This cache has to be alive for Input and Output because Input passes pointers through the queue.
  std::unordered_map<uint64_t, StringPiece> cache;
  // Run Input and Output concurrently.  Arbitrarily, we'll do Output in the main thread.
  std::thread input([&queue, &in, &cache, kFlushRate]{Input(queue, in, cache, kFlushRate);});
  Output(queue, out);
  input.join();
  int status;
  UTIL_THROW_IF(-1 == waitpid(child, &status, 0), util::ErrnoException, "waitpid for child failed");
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  } else {
    return 256;
  }
}
