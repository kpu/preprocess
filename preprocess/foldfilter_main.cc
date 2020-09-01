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

using namespace std;

constexpr size_t not_found = -1;

struct wrap_options {
	// Maximum number of bytes (not unicode characters!) that may end up in a
	// single line.
	size_t column_width = 80;

	// Keep delimiters in the split lines, or separate them out into their own
	// queue.
	bool keep_delimiters = true;

	// Order determines preference: the first one of these to occur in the
	// line will determine the wrapping point.
	vector<UChar32> delimiters{':', ',', ' ', '-', '.', '/'};
};

struct program_options : wrap_options {
	// argv for wrapped command. Should start with the program name and end
	// with NULL.
	char **child_argv = 0;
};

size_t find_delimiter(vector<UChar32> const &delimiters, UChar32 character) {
	for (size_t i = 0; i < delimiters.size(); ++i)
		if (character == delimiters[i])
			return i;

	return not_found;
}

pair<deque<StringPiece>,deque<string>> wrap_lines(StringPiece const &line, wrap_options const &options) {
	deque<StringPiece> out_lines;

	deque<string> out_delimiters;

	// Current byte position
	int32_t pos = 0;

	// Length of line in bytes
	int32_t length = line.size();
	
	// Byte position of last cut-off point
	int32_t pos_last_cut = 0;

	// For each delimiter the byte position of its last occurrence
	vector<int32_t> pos_delimiters(options.delimiters.size(), 0);

	// Position of the first delimiter we encountered up to pos. Reset
	// to pos + next char if it's not a delimiter.
	int32_t pos_first_delimiter = 0;

	while (pos < length) {
		UChar32 character;

		U8_NEXT(line.data(), pos, length, character);
		
		if (character < 0)
			throw utf8::NotUTF8Exception(line);

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
		if (pos - pos_last_cut < static_cast<int32_t>(options.column_width))
			continue;

		// Last resort if we didn't break on a delimiter: just chop where we are
		int32_t pos_cut = pos;

		// Find a more ideal break point by looking back for a delimiter
		for (int32_t const &pos_delimiter : pos_delimiters) {
			if (pos_delimiter > pos_last_cut) {
				pos_cut = pos_delimiter;
				break;
			}
		}

		// Assume we cut without delimiters (i.e. the last resort scenario)
		int32_t pos_cut_end = pos_cut;

		// Peek ahead to were after the cut we encounter our first not-a-delimiter
		// because that's the point were we resume.
		for (int32_t pos_next = pos_cut_end; pos_cut_end < length; pos_cut_end = pos_next) {
			// When we're not skipping delimiters, don't send more bytes than
			// column_width in total a single line, even though we try to keep
			// the delimiters together.
			if (options.keep_delimiters && pos_cut_end - pos_last_cut >= static_cast<int32_t>(options.column_width))
				break;

			U8_NEXT(line.data(), pos_next, length, character);

			if (character < 0)
				throw utf8::NotUTF8Exception(line);

			// First character after pos_cut is probably a delimiter, unless
			// we did a hard stop in the middle of a word, and we're not keeping
			// the delimiters.
			if (find_delimiter(options.delimiters, character) == not_found)
				break;
		}

		if (options.keep_delimiters) {
			out_lines.push_back(line.substr(pos_last_cut, pos_cut_end - pos_last_cut));
			out_delimiters.emplace_back("");
		} else {
			out_lines.push_back(line.substr(pos_last_cut, pos_cut - pos_last_cut));
			out_delimiters.emplace_back(line.substr(pos_cut, pos_cut_end - pos_cut).data(), pos_cut_end - pos_cut);
		}

		pos_last_cut = pos_cut_end;
		pos = pos_cut_end;
	}

	// Push out any trailing bits. Or the empty bit.
	if (pos_last_cut < pos || pos == 0) {
		out_lines.push_back(line.substr(pos_last_cut, pos - pos_last_cut));
		out_delimiters.push_back("");
	}

	return make_pair(out_lines, out_delimiters);
}

int usage(char **argv) {
	cerr << "usage: " << argv[0] << " [-w width] [-s] [-h] command [command-args ...]\n"
		    "\n"
		    "Options:\n"
		    "  -h        Display help\n"
		    "  -w <num>  Wrap lines to have at most <num> bytes\n"
		    "  -d <str>  Specify punctuation to break on. Order determines preference.\n"
		    "  -s        Skip passing punctuation around wrapping points to the command\n";
	return 1;
}

vector<UChar32> parse_delimiters(char *value) {
	int32_t length = strlen(value);
	int32_t pos = 0;
	
	vector<UChar32> delimiters;
	delimiters.reserve(length);

	while (pos < length) {
		UChar32 delimiter;
		U8_NEXT(value, pos, length, delimiter);
		
		if (delimiter < 0)
			throw utf8::NotUTF8Exception(value);
		
		delimiters.push_back(delimiter);
	}

	return delimiters;
}

void parse_options(program_options &options, int argc, char **argv) {
	while (true) {
		switch(getopt(argc, argv, "+w:d:sh")) {
			case 'w':
				options.column_width = atoi(optarg);
				continue;

			case 'd':
				options.delimiters = parse_delimiters(optarg);
				continue;

			case 's':
				options.keep_delimiters = false;
				continue;

			case 'h':
			case '?':
			default:
				exit(usage(argv));

			case -1:
				break;
		}
		break;
	}

	if (optind == argc)
		exit(usage(argv));

	options.child_argv = argv + optind;
}

int main(int argc, char **argv) {
	program_options options;

	parse_options(options, argc, argv);

	util::UnboundedSingleQueue<deque<string>> queue;

	util::scoped_fd child_in_fd, child_out_fd;

	pid_t child = preprocess::Launch(options.child_argv, child_in_fd, child_out_fd);

	thread feeder([&child_in_fd, &queue, &options]() {
		util::FilePiece in(STDIN_FILENO);
		util::FileStream child_in(child_in_fd.get());

		for (StringPiece sentence : in) {
			deque<StringPiece> lines;
			deque<string> delimiters;

			// If there is nothing to wrap, it will end up with a single line
			// and a single empty delimiter.
			tie(lines, delimiters) = wrap_lines(sentence, options);
			// assert(lines.size() == delimiters.size());

			// When we're keeping delimiters all of these will be empty strings
			// but their amount at least will tell the reader thread how many
			// lines it needs to consume to reconstruct the single line.
			queue.Produce(delimiters);

			// Feed the document to the child.
			// Might block because it can cause a flush.
			for (auto const &line : lines)
				child_in << line << '\n';
		}

		// Tell the reader to stop
		queue.Produce(deque<string>());

		// Flush (blocks) & close the child's stdin
		child_in.flush();
		child_in_fd.reset();
	});

	thread reader([&child_out_fd, &queue, &options]() {
		util::FileStream out(STDOUT_FILENO);
		util::FilePiece child_out(child_out_fd.release());

		deque<string> delimiters;
		string sentence;

		for (size_t sentence_num = 1; queue.Consume(delimiters).size() > 0; ++sentence_num) {
			sentence.clear();
			
			// Let's assume that the wrapped process plus the chopped off
			// delimiters won't be more than twice the input we give it.
			sentence.reserve(delimiters.size() * 2 * options.column_width);

			try {
				while (!delimiters.empty()) {
					StringPiece line(child_out.ReadLine());
					sentence.append(line.data(), line.length());
					sentence.append(delimiters.front());
					delimiters.pop_front();
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
