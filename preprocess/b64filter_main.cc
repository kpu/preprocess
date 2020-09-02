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

		for (StringPiece line : in) {
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
					StringPiece line(child_out.ReadLine());
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

			// Just to check, next time we call Consume(), will we block? If so,
			// that means we've caught up with the producer. However, the order
			// the producer fills line_cnt_queue is first giving us a new line-
			// count and then sending the input to the sub-process. So if we do
			// not have a new line count yet, the sub-process definitely can't
			// have new output yet, and peek should block and once it unblocks
			// we expect to have that line-count waiting. If we still don't,
			// then what is this output that is being produced by the sub-
			// process?
			if (line_cnt_queue.Empty()) {
				// If peek throws EOF now our sub-process stopped before its
				// stdin was closed (producer produces the poison before it
				// closes the sub-process's stdin.)
				child_out.peek();
				
				// peek() came back. We have a line-number now, right? If not
				// sub-process is producing output without any input to base it
				// on. Which is bad.
				if (line_cnt_queue.Empty())
					UTIL_THROW(util::Exception, "sub-process is producing more output than it was given input at document " << doc_cnt);
			}
		}
	});

	int retval = preprocess::Wait(child);

	feeder.join();
	reader.join();
	
	return retval;
}
