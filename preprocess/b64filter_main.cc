#include <thread>
#include <unistd.h>
#include "preprocess/base64.hh"
#include "preprocess/captive_child.hh"
#include "util/exception.hh"
#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/pcqueue.hh"


namespace {

struct Document {
	size_t line_cnt;
	bool has_trailing_newline;
};

} // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cerr << "usage: " << argv[0] << " command [command-args...]\n";
		return 1;
	}

	util::UnboundedSingleQueue<Document> line_cnt_queue;

	util::scoped_fd child_in_fd, child_out_fd;

	pid_t child = preprocess::Launch(argv + 1, child_in_fd, child_out_fd);

	std::thread feeder([&child_in_fd, &line_cnt_queue]() {
		util::FilePiece in(STDIN_FILENO);
		util::FileStream child_in(child_in_fd.get());

		// Decoded document buffer
		std::string doc;

		for (util::StringPiece line : in) {
			preprocess::base64_decode(line, doc);

			// Description of the document
			Document doc_desc{
				.line_cnt = 0,
				.has_trailing_newline = doc.back() == '\n',
			};

			// Make the the document end with a new line. This to make sure
			// the next doc we send to the child will be on its own line and the
			// line_cnt is correct.
			if (!doc_desc.has_trailing_newline)
				doc.push_back('\n');

			doc_desc.line_cnt = count(doc.cbegin(), doc.cend(), '\n');
			
			// Send line count first to the reader, so it can start reading as
			// soon as we start feeding the document to the child.
			line_cnt_queue.Produce(std::move(doc_desc));

			// Feed the document to the child.
			// Might block because it can cause a flush.
			child_in << doc;
		}

		// Tell the reader to stop
		line_cnt_queue.Produce(Document{
			.line_cnt = 0,
			.has_trailing_newline = false
		});

		// Flush (blocks) & close the child's stdin
		child_in.flush();
		child_in_fd.reset();
	});

	std::thread reader([&child_out_fd, &line_cnt_queue]() {
		util::FileStream out(STDOUT_FILENO);
		util::FilePiece child_out(child_out_fd.release());

		size_t doc_cnt = 0;
		Document document;
		std::string doc;

		while (line_cnt_queue.Consume(document).line_cnt > 0) {
			++doc_cnt;

			doc.clear();
			doc.reserve(document.line_cnt * 4096); // 4096 is not a typical line length

			try {
				while (document.line_cnt-- > 0) {
        util::StringPiece line(child_out.ReadLine());
					doc.append(line.data(), line.length());

					// ReadLine eats line endings. Between lines we definitely
					// need to add them back. Whether we add the last one depends
					// on whether the original document had a trailing newline.
					if (document.line_cnt > 0 || document.has_trailing_newline)
						doc.push_back('\n');
				}
			} catch (util::EndOfFileException &e) {
				UTIL_THROW(util::Exception, "Sub-process stopped producing while expecting more lines while processing document " << doc_cnt);
			}

			std::string encoded_doc;
			preprocess::base64_encode(doc, encoded_doc);
			out << encoded_doc << '\n';
		}

		// Assert that we have consumed all the output of the child program.
		try {
			// peek() should now fail on an end of file, the loop above should
			// already have consumed all output that's there.
			child_out.peek();

			UTIL_THROW(util::Exception, "sub-process is producing more output than it was given input");
		} catch (util::EndOfFileException &e) {
			// Good!
		}
	});

	int retval = preprocess::Wait(child);

	feeder.join();
	reader.join();
	
	return retval;
}
