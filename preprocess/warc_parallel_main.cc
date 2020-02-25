#include "captive_child.hh"
#include "warc.hh"

#include "util/compress.hh"
#include "util/fake_ofstream.hh"
#include "util/file.hh"
#include "util/fixed_array.hh"
#include "util/pcqueue.hh"

#include <sys/types.h>
#include <sys/wait.h>

#include <mutex>
#include <string>
#include <thread>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

namespace preprocess {
namespace {

// Thread to read from queue and dump to a worker.  Steals process_in.
void InputToProcess(util::PCQueue<std::string> *queue, int process_in) {
  // Steal fd for consistency with OutputFromProcess.
  util::scoped_fd fd(process_in);
  std::string warc;
  while (true) {
    queue->ConsumeSwap(warc);
    if (warc.empty()) return;
    util::WriteOrThrow(process_in, warc.data(), warc.size());
  }
}

// Thread to write from a worker to output.  Steals process_out.
void OutputFromProcess(bool compress, int process_out, util::FakeOFStream *out, std::mutex *out_mutex) {
  WARCReader reader(process_out);
  std::string str;
  if (compress) {
    std::string compressed;
    while (reader.Read(str)) {
      util::GZCompress(str, compressed);
      std::lock_guard<std::mutex> guard(*out_mutex);
      *out << compressed;
    }
  } else {
    while (reader.Read(str)) {
      std::lock_guard<std::mutex> guard(*out_mutex);
      *out << str;
    }
  }
}

// Thread to read WARC input from a file.  Steals from.  Does not poison the queue.
void ReadInput(int from, util::PCQueue<std::string> *queue) {
  preprocess::WARCReader reader(from);
  std::string str;
  while (reader.Read(str)) {
    queue->ProduceSwap(str);
  }
}

// A child process going from WARC to WARC.
class Worker {
  public:
    Worker(util::PCQueue<std::string> &in, util::FakeOFStream &out, std::mutex &out_mutex, bool compress, char *argv[]) {
      util::scoped_fd in_file, out_file;
      Launch(argv, in_file, out_file);
      input_ = std::thread(InputToProcess, &in, in_file.release());
      output_ = std::thread(OutputFromProcess, compress, out_file.release(), &out, &out_mutex);
    }

    void Join() {
      input_.join();
      output_.join();
    }

  private:
    std::thread input_, output_;
};

void ChildReaper(std::size_t expect) {
  try {
    for (; expect; --expect) {
      int wstatus;
      pid_t process = waitpid(-1, &wstatus, 0);
      UTIL_THROW_IF(-1 == process, util::ErrnoException, "waitpid");
      UTIL_THROW_IF(!WIFEXITED(wstatus), util::Exception, "Child process " << process << " terminated abnormally.");
      UTIL_THROW_IF(WEXITSTATUS(wstatus), util::Exception, "Child process " << process << " terminated with code " << WEXITSTATUS(wstatus) << ".");
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    abort();
  }
}

class WorkerPool {
  public:
    WorkerPool(std::size_t number, util::FakeOFStream &out, bool compress, char *argv[]) : in_(number), workers_(number) {
      for (std::size_t i = 0; i < number; ++i) {
        workers_.push_back(in_, out, out_mutex_, compress, argv);
      }
      child_reaper_ = std::thread(ChildReaper, number);
    }

    util::PCQueue<std::string> &InputQueue() { return in_; }

    void Join() {
      // Poison all of them first.
      std::string str;
      for (std::size_t i = 0; i < workers_.size(); ++i) {
        in_.Produce(str);
      }
      for (Worker &i : workers_) {
        i.Join();
      }
      child_reaper_.join();
    }
    
  private:
    util::PCQueue<std::string> in_;
    std::mutex out_mutex_;
    util::FixedArray<Worker> workers_;

    std::thread child_reaper_;
};

struct Options {
  std::vector<std::string> inputs;
  std::size_t workers;
  bool compress;
};

void ParseBoostArgs(int restricted_argc, int real_argc, char *argv[], Options &out) {
  namespace po = boost::program_options;
  po::options_description desc("Arguments");
  desc.add_options()
    ("help,h", po::bool_switch(), "Show this help message")
    ("inputs,i", po::value(&out.inputs)->multitoken(), "Input files, which will be read in parallel and jumbled together.  Default: read from stdin.")
    ("jobs,j", po::value(&out.workers)->default_value(std::thread::hardware_concurrency()), "Number of child process workers to use.")
    ("gzip,z", po::bool_switch(&out.compress), "Compress output in gzip format");
  po::variables_map vm;
  po::store(po::command_line_parser(restricted_argc, argv).options(desc).run(), vm);
  if (real_argc == 1 || vm["help"].as<bool>()) {
    std::cerr <<
      "Parallelizes WARC to WARC processing by wrapping a child process.\n"
      "Example that just does cat: " << argv[0] << " cat\n"
      "Arguments can be specified to control threads and files. Use -- to\n"
      "distinguish between file names and the command to wrap.\n" <<
      desc <<
      "Examples:\n" <<
      argv[0] << " -j 20 ./process_warc.sh\n" <<
      argv[0] << " -i a.warc b.warc -- ./process_warc.sh\n"
      "process_warc.sh is expected to take WARC on stdin and produce WARC on stdout.\n";
    exit(1);
  }
  po::notify(vm);
}

// Figuring out where the command line for the child is.
char **FindChild(int argc, char *argv[]) {
  // Pass help over to boost.
  if (argc == 1) return argv + 1;
  bool used_inputs = false;
  for (int i = 1; i < argc;) {
    char *a = argv[i];
    if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
      // Help, doesn't matter, just make sure command is past that.
      return argv + i + 1;
    } else if (!strcmp(a, "--jobs") || !strcmp(a, "-j")) {
      UTIL_THROW_IF2(i + 1 == argc, "Expected argument to jobs");
      i += 2;
    } else if (!strcmp(a, "--inputs") || !strcmp(a, "-i")) {
      used_inputs = true;
      // Consume everything that doesn't begin with -.
      for (++i; i < argc && argv[i][0] != '-'; ++i) {}
    } else if (!strcmp(a, "--")) {
      return argv + i + 1;
    } else {
      // Presumably a command line program?
      UTIL_THROW_IF2(a[0] == '-', "Unrecognized option " << a);
      return argv + i;
    }
  }
  std::cerr << "Did not find a child process to run on the command line.\n";
  if (used_inputs) {
    std::cerr << "When using --inputs, remember to terminate with --.\n";
  }
  exit(1);
}

void Run(const Options &options, char *child[]) {
  util::FakeOFStream out(1);

  WorkerPool pool(options.workers, out, options.compress, child);

  util::FixedArray<std::thread> readers(options.inputs.empty() ? 1 : options.inputs.size());
  if (options.inputs.empty()) {
    readers.push_back(ReadInput, 0, &pool.InputQueue());
  } else {
    for (const std::string &name : options.inputs) {
      readers.push_back(ReadInput, util::OpenReadOrThrow(name.c_str()), &pool.InputQueue());
    }
  }
  for (std::thread &r : readers) {
    r.join();
  }
  pool.Join();
}

} // namespace
} // namespace preprocess

int main(int argc, char *argv[]) {
  char **child = preprocess::FindChild(argc, argv);
  preprocess::Options options;
  preprocess::ParseBoostArgs(child - argv, argc, argv, options);
  Run(options, child);
}
