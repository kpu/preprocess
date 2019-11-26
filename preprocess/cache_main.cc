#include "preprocess/captive_child.hh"
#include "preprocess/fields.hh"

#include "util/fake_ofstream.hh"
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
#include <sys/prctl.h>
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
    util::FakeOFStream process(process_input.get());
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

// Parse arguments using boost::program_options
void ParseArgs(int argc, char *argv[], Options &out) {
  namespace po = boost::program_options;
  po::options_description desc("Acts as a cache around another program processing one line in, one line out from stdin to stdout.");
  desc.add_options()
    ("key,k", po::value(&out.key)->default_value("-1"), "Column(s) key to use as the deduplication string")
    ("field_separator,t", po::value<char>(&out.field_separator)->default_value('\t'), "use a field separator instead of tab");
  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
}

int main(int argc, char *argv[]) {
  // The underlying program can buffer up to kQueueLength - kFlushRate lines.  If it goes above that, deadlock waiting for queue.
  const std::size_t kFlushRate = 4096;
  
  // Take into account the number of arguments given to `cache` to delete them from the argv provided to Launch function
  Options opt;
  int skip_args = 1;
  if ( !strcmp(argv[1],"-k") || !strcmp(argv[1],"-t") || !strcmp(argv[1],"--key") || !strcmp(argv[1],"--field_separator")){
    skip_args += 2;
  }
  if (argc > 3){
    if ( !strcmp(argv[3],"-k") || !strcmp(argv[3],"-t") || !strcmp(argv[3],"--key") || !strcmp(argv[3],"--field_separator")){
      skip_args += 2;
    }
  }
  ParseArgs(skip_args, argv, opt);

  util::scoped_fd in, out;
  pid_t child = Launch(argv + skip_args, in, out);
  // We'll deadlock if this queue is full and the program is buffering.
  util::UnboundedSingleQueue<QueueEntry> queue;
  // This cache has to be alive for Input and Output because Input passes pointers through the queue.
  std::unordered_map<uint64_t, StringPiece> cache;
  // Run Input and Output concurrently.  Arbitrarily, we'll do Output in the main thread.
  std::thread input([&queue, &in, &cache, kFlushRate, &opt]{Input(queue, in, cache, kFlushRate, opt);});
  Output(queue, out);
  input.join();
  return Wait(child);
}

