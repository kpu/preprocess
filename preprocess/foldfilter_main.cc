#include <thread>
#include <deque>
#include <vector>
#include <climits>
#include <cstring>
#include <type_traits>
#include <unistd.h>
#include "util/exception.hh"
#include "util/pcqueue.hh"
#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/utf8.hh"
#include "preprocess/captive_child.hh"

namespace {

constexpr size_t not_found = -1;

struct wrap_options {
	// Maximum number of bytes (not unicode characters!) that may end up in a
	// single line.
	size_t column_width = 80;

	// Keep delimiters in the split lines, or separate them out into their own
	// queue.
	bool keep_delimiters_in_lines = true;

	// Order determines preference: the first one of these to occur in the
	// line will determine the wrapping point.
	std::vector<char32_t> delimiters{':', ',', ' ', '-', '.', '/'};
};

struct program_options : wrap_options {
	// argv for wrapped command. Should start with the program name and end
	// with NULL.
	char **child_argv = 0;
};

size_t find_delimiter(std::vector<char32_t> const &delimiters, char32_t character) {
  std::vector<char32_t>::const_iterator i = std::find(delimiters.begin(), delimiters.end(), character);
  if (i == delimiters.end()) return not_found;
  return i - delimiters.begin();
}

class DelimiterList {
  public:
    explicit DelimiterList(const std::vector<util::StringPiece> &delims)
      : size_(delims.size()), mem_(util::MallocOrThrow(size_ + SumLengths(delims))) {
      char *strs = reinterpret_cast<char *>(mem_.get());
      for (const util::StringPiece &delim : delims) {
        memcpy(strs, delim.data(), delim.size());
        strs[delim.size()] = 0;
        strs += delim.size() + 1;
      }
    }

    DelimiterList() : size_(0) {}

    class forward_iterator {
      public:
        forward_iterator(const DelimiterList &list)
          : strings_(reinterpret_cast<const char *>(list.mem_.get())) {}

        forward_iterator &operator++() {
          strings_ += strlen(strings_) + 1;
          return *this;
        }

        util::StringPiece operator*() const {
          return util::StringPiece(strings_);
        }

      private:
        const char *strings_;
    };

    size_t size() const { return size_; }

  private:
    // Number of strings.
    size_t size_;

    // The memory is null-delimited strings, size_ of them.
    util::scoped_malloc mem_;

    static size_t SumLengths(const std::vector<util::StringPiece> &delims) {
      size_t ret = 0;
      for (const util::StringPiece &i : delims) {
        ret += i.size();
      }
      return ret;
    }
};

void wrap_lines(util::StringPiece const &line, wrap_options const &options, std::deque<util::StringPiece> &out_lines, std::vector<util::StringPiece> &out_delimiters) {
  out_lines.clear();
  out_delimiters.clear();

	// Current byte position
	size_t pos = 0;

	// Length of line in bytes
	size_t length = line.size();
	
	// Byte position of last cut-off point
	size_t pos_last_cut = 0;

	// For each delimiter the byte position of its last occurrence
	std::vector<size_t> pos_delimiters(options.delimiters.size(), 0);

	// Position of the first delimiter we encountered up to pos. Reset
	// to pos + next char if it's not a delimiter.
	int32_t pos_first_delimiter = 0;

	while (pos < length) {
    size_t char_len;
		char32_t character = util::DecodeUTF8(line.data() + pos, line.end(), &char_len);
    pos += char_len;

		size_t delimiter_idx = find_delimiter(options.delimiters, character);

		if (delimiter_idx != not_found) {
			// Store pos_first_delimiter instead of pos because when we have
			// consecutive delimiters we want to chop em all off, even when
			// our ideal delimiter is somewhere in the middle.
			pos_delimiters[delimiter_idx] = pos_first_delimiter;
		} else {
			// Maybe the next char is a delimiter? pos is pointing to the next
			// one right now, U8_NEXT incremented it.
			pos_first_delimiter = pos;
		}

		// Do we need to introduce a break? If not, move to next character
		if (pos - pos_last_cut < options.column_width)
			continue;

		// Last resort if we didn't break on a delimiter: just chop where we are
		size_t pos_cut = pos;

		// Find a more ideal break point by looking back for a delimiter
		for (int32_t const &pos_delimiter : pos_delimiters) {
			if (pos_delimiter > pos_last_cut) {
				pos_cut = pos_delimiter;
				break;
			}
		}

		// Assume we cut without delimiters (i.e. the last resort scenario)
		size_t pos_cut_end = pos_cut;

		// Peek ahead to were after the cut we encounter our first not-a-delimiter
		// because that's the point were we resume.
		for (size_t pos_next = pos_cut_end; pos_cut_end < length; pos_cut_end = pos_next) {
			// When we're not skipping delimiters, don't send more bytes than
			// column_width in total a single line, even though we try to keep
			// the delimiters together.
			if (options.keep_delimiters_in_lines && pos_cut_end - pos_last_cut >= options.column_width)
				break;

      character = util::DecodeUTF8(line.data() + pos_next, line.end(), &char_len);
      pos_next += char_len;

			// First character after pos_cut is probably a delimiter, unless
			// we did a hard stop in the middle of a word, and we're not keeping
			// the delimiters.
			if (find_delimiter(options.delimiters, character) == not_found)
				break;
		}

		if (options.keep_delimiters_in_lines) {
			out_lines.push_back(line.substr(pos_last_cut, pos_cut_end - pos_last_cut));
			out_delimiters.emplace_back(util::StringPiece("", 0));
		} else {
			out_lines.push_back(line.substr(pos_last_cut, pos_cut - pos_last_cut));
			out_delimiters.emplace_back(util::StringPiece(line.data() + pos_cut, pos_cut_end - pos_cut));
		}

		pos_last_cut = pos_cut_end;
		pos = pos_cut_end;
	}

	// Push out any trailing bits. Or the empty bit.
	if (pos_last_cut < pos || pos == 0) {
		out_lines.push_back(line.substr(pos_last_cut, pos - pos_last_cut));
		out_delimiters.push_back(util::StringPiece("", 0));
	}
}

int usage(char **argv) {
	std::cerr << "usage: " << argv[0] << " [-w width] [-s] [-h] command [command-args ...]\n"
		    "\n"
		    "Options:\n"
		    "  -h        Display help\n"
		    "  -w <num>  Wrap lines to have at most <num> bytes\n"
		    "  -d <str>  Specify punctuation to break on. Order determines preference.\n"
		    "  -s        Skip passing punctuation around wrapping points to the command\n";
	return 1;
}

std::vector<char32_t> parse_delimiters(char *value) {
	std::vector<char32_t> delimiters;
  for (char32_t c : util::DecodeUTF8Range(value)) {
    delimiters.push_back(c);
  }
	return delimiters;
}

void parse_options(program_options &options, int argc, char **argv) {
	while (true) {
		switch(getopt(argc, argv, "+w:d:sh")) {
			case 'w':
				options.column_width = std::atoi(optarg);
				continue;

			case 'd':
				options.delimiters = parse_delimiters(optarg);
				continue;

			case 's':
				options.keep_delimiters_in_lines = false;
				continue;

			case 'h':
			case '?':
			default:
				std::exit(usage(argv));

			case -1:
				break;
		}
		break;
	}

	if (optind == argc)
		std::exit(usage(argv));

	options.child_argv = argv + optind;
}

} // namespace

int main(int argc, char **argv) {
	program_options options;

	parse_options(options, argc, argv);

	util::UnboundedSingleQueue<DelimiterList> queue;

	util::scoped_fd child_in_fd, child_out_fd;

	pid_t child = preprocess::Launch(options.child_argv, child_in_fd, child_out_fd);

	std::thread feeder([&child_in_fd, &queue, &options]() {
		util::FilePiece in(STDIN_FILENO);
		util::FileStream child_in(child_in_fd.release());

		std::deque<util::StringPiece> lines;
    std::vector<util::StringPiece> delimiters;
		for (util::StringPiece sentence : in) {

			// If there is nothing to wrap, it will end up with a single line
			// and a single empty delimiter.
			wrap_lines(sentence, options, lines, delimiters);
			// assert(lines.size() == delimiters.size());

			// When we're keeping delimiters all of these will be empty strings
			// but their amount at least will tell the reader thread how many
			// lines it needs to consume to reconstruct the single line.
      DelimiterList list(delimiters);
			queue.Produce(std::move(list));

			// Feed the document to the child.
			// Might block because it can cause a flush.
			for (auto const &line : lines)
				child_in << line << '\n';
		}

		// Tell the reader to stop
		queue.Produce({});

		// Flush (blocks) & close the child's stdin
		child_in.flush();
	});

	std::thread reader([&child_out_fd, &queue, &options]() {
		util::FileStream out(STDOUT_FILENO);
		util::FilePiece child_out(child_out_fd.release());

		DelimiterList delimiters;
		std::string sentence;

		for (size_t sentence_num = 1; queue.Consume(delimiters).size() > 0; ++sentence_num) {
			sentence.clear();
			
			// Let's assume that the wrapped process plus the chopped off
			// delimiters won't be more than twice the input we give it.
			sentence.reserve(delimiters.size() * 2 * options.column_width);

			try {
        DelimiterList::forward_iterator delimit(delimiters);
        for (size_t i = 0; i < delimiters.size(); ++i, ++delimit) {
					util::StringPiece line(child_out.ReadLine());
					sentence.append(line.data(), line.length());
          util::StringPiece delimiter(*delimit);
					sentence.append(delimiter.data(), delimiter.size());
				}
			} catch (util::EndOfFileException &e) {
				UTIL_THROW(util::Exception, "Sub-process stopped producing while expecting more lines for sentence " << sentence_num << ".");
			}

			// Yes, this might introduce a newline at the end of the file, but
			// yes that is what we generally want in our pipeline because we
			// might concatenate all these files and that will mess up if they
			// don't have a trailing newline.
			out << sentence << '\n';

			// Just to check, next time we call Consume(), will we block? If so,
			// that means we've caught up with the producer. However, the order
			// the producer fills queue is first giving us a new line-
			// count and then sending the input to the sub-process. So if we do
			// not have a new line count yet, the sub-process definitely can't
			// have new output yet, and peek should block and once it unblocks
			// we expect to have that line-count waiting. If we still don't,
			// then what is this output that is being produced by the sub-
			// process?
			if (queue.Empty()) {
				// If peek throws EOF now our sub-process stopped before its
				// stdin was closed (producer produces the poison before it
				// closes the sub-process's stdin.)
				child_out.peek();
				
				// peek() came back. We have a line-number now, right? If not
				// sub-process is producing output without any input to base it
				// on. Which is bad.
				if (queue.Empty())
					UTIL_THROW(util::Exception, "sub-process is producing more output than it was given input");
			}
		}
	});

	int retval = preprocess::Wait(child);

	// Order here doesn't matter that much. If either of the threads is blocked
	// while the other finishes, it's an error state and the finishing thread
	// will finish with an uncaught exception, which will just terminate()
	// everything.
	feeder.join();
	reader.join();
	
	return retval;
}
