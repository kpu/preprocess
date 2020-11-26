#include "preprocess/captive_child.hh"
#include "preprocess/fields.hh"

#include "util/file_stream.hh"
#include "util/file.hh"
#include "util/file_piece.hh"
#include "util/murmur_hash.hh"
#include "util/pcqueue.hh"
#include "util/pool.hh"
#include "util/string_piece.hh"

#include <string>
#include <thread>
#include <unordered_map>
#include <limits>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

using namespace preprocess;

struct Options {
  std::string key;
  char field_separator;
};

struct QueueEntry {
  // NULL pointer is poison.
  // value->data() == NULL means uninitialized
  StringPiece *value;
};

struct HashWithSeed {
  HashWithSeed() { hash = 0; }
  void operator()(StringPiece sp) { size_t result = util::MurmurHashNative(sp.data(), sp.size(), hash); hash = result; }
  size_t get_hash() { return hash; }

private:
  size_t hash;
};

void Input(util::UnboundedSingleQueue<QueueEntry> &queue, util::scoped_fd &process_input, std::unordered_map<uint64_t, StringPiece> &cache, std::size_t flush_rate, Options &options) {
  QueueEntry q_entry;
  {
    util::FileStream process(process_input.get());
    std::pair<uint64_t, StringPiece> entry;
    std::size_t flush_count = flush_rate;
    // Parse column numbers, if given using --key option, into an integer vector (comma separated integers)
    std::vector<FieldRange> indices;
    ParseFields(options.key.c_str(), indices);
    for (StringPiece l : util::FilePiece(STDIN_FILENO)) {
      HashWithSeed callback= HashWithSeed();
      RangeFields(l, indices, options.field_separator, callback);
      entry.first = callback.get_hash();
      std::pair<std::unordered_map<uint64_t, StringPiece>::iterator, bool> res(cache.insert(entry));
      if (res.second) {
        // New entry.  Send to captive process.
        process << l << '\n';
        // Guarantee we flush to process every so often.
        if (!--flush_count) {
          process.flush();
          flush_count = flush_rate;
        }
      }
      // Pointer to hash table entry.
      q_entry.value = &res.first->second;
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
  util::FileStream out(STDOUT_FILENO);
  util::FilePiece in(process_output.release());
  // We'll allocate the cached strings into a pool.
  util::Pool string_pool;
  // string_pool will return NULL if the first allocation is for empty string.  But we use NULL to indicate a missing value.
  // This forces the string_pool to always return non-NULL.
  string_pool.Allocate(1);
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
  const std::size_t kFlushRate = 4096;

  // Take into account the number of arguments given to `cache` to delete them from the argv provided to Launch function
  Options opt;
  int skip_args = 1;
  for (int arg = 1; arg < argc; arg += 2) {
    if (!strcmp(argv[arg], "-k") || !strcmp(argv[arg], "-t") || !strcmp(argv[arg], "--key") || !strcmp(argv[arg], "--field_separator")) {
      skip_args += 2;
    } else {
      break;
    }
  }
  namespace po = boost::program_options;
  po::options_description desc("Acts as a cache around another program processing one line in, one line out from stdin to stdout. Input lines with the same key will get the same output value without passing them to the underlying program.  These options control what the key is");
  desc.add_options()
    ("key,k", po::value(&opt.key)->default_value("-"), "Column(s) key to use as the deduplication string")
    ("field_separator,t", po::value<char>(&opt.field_separator)->default_value('\t'), "use a field separator instead of tab");
  if (argc == 1) {
    std::cerr << "Usage: " << argv[0] << " [-k 1] [-t ,] cat\n" << desc;
    return 1;
  }
  po::variables_map vm;
  po::store(po::parse_command_line(skip_args, argv, desc), vm);
  po::notify(vm);

  util::scoped_fd in, out;
  pid_t child = Launch(argv + skip_args, in, out);
  util::UnboundedSingleQueue<QueueEntry> queue;
  // This cache has to be alive for Input and Output because Input passes pointers through the queue.
  std::unordered_map<uint64_t, StringPiece> cache;
  // Run Input and Output concurrently.  Arbitrarily, we'll do Output in the main thread.
  std::thread input([&queue, &in, &cache, kFlushRate, &opt]{Input(queue, in, cache, kFlushRate, opt);});
  Output(queue, out);
  input.join();
  return Wait(child);
}

