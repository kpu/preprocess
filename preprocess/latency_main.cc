#include "preprocess/captive_child.hh"
#include "util/file.hh"
#include "util/file_piece.hh"
#include "util/file_stream.hh"

void ReadJustLine(int fd, std::string &result, std::size_t &valid) {
  valid = 0;
  while (true) {
    if (result.size() < valid + 1024) {
      result.resize(valid + 1024);
    }
    std::size_t got = util::PartialRead(fd, &result[0] + valid, result.size() - valid);
    UTIL_THROW_IF2(!got, "No line to read.");
    const char *newline = std::find(result.data() + valid, result.data() + valid + got, '\n');
    valid += got;
    if (newline == result.data() + valid - 1) {
      // terminal newline
      return;
    }
    UTIL_THROW_IF2(newline < result.data() + valid - 1, "Excess content after newline.");
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " child process and arguments" << std::endl;
    return 1;
  }
  util::scoped_fd child_in, child_out;
  pid_t child = preprocess::Launch(argv + 1, child_in, child_out);
  const char kNewline = '\n';
  std::string result;
  util::FileStream out(1);
  for (util::StringPiece line : util::FilePiece(0, "stdin")) {
    util::WriteOrThrow(child_in.get(), line.data(), line.size() + 1 /* including newline */);
    std::size_t valid = 0;
    ReadJustLine(child_out.get(), result, valid);
    out.write(result.data(), valid);
  }
  child_in.reset();
  preprocess::Wait(child);
}

