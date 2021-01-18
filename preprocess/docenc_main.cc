#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <vector>
#include "util/file_stream.hh"
#include "util/file_piece.hh"
#include "util/string_piece.hh"
#include "util/tokenize_piece.hh"
#include "preprocess/base64.hh"


namespace {

enum Mode {
	ENCODE,
	DECODE
};


void prefix_lines(std::string const &input, util::FileStream &out, std::string const prefix) {
	for (util::TokenIter<util::SingleCharacter, false> line_it(input, '\n'); line_it; ++line_it)
		out << prefix << *line_it << '\n';
}


size_t decode(util::FilePiece &in, util::FileStream &out, char delimiter, std::vector<size_t> const &indices, bool print_document_index, bool &delimiter_encountered) {
	size_t document_index = 0;
	std::vector<size_t>::const_iterator indices_it(indices.begin());

	for (util::StringPiece line : in) {
		++document_index;

		if (!indices.empty()) {
			if (*indices_it != document_index) {
				continue; // skip document
			} else {
				indices_it++;
			}
		}

		std::string document;
		preprocess::base64_decode(line, document);

		if (!delimiter_encountered && document.find(delimiter == '\n' ? std::string("\n\n") : std::string(&delimiter, 1)) != std::string::npos)
			delimiter_encountered = true;

		if (print_document_index)
			prefix_lines(document, out, std::to_string(document_index) + "\t");
		else
			out << document;

		out << delimiter;

		// Have we found all our indices? Then stop early
		if (!indices.empty() && indices_it == indices.end())
			break;
	}

	return document_index;
}


size_t encode(util::FilePiece &in, util::FileStream &out, char delimiter, std::vector<size_t> const &indices) {
	size_t document_index = 0;
	
	std::vector<size_t>::const_iterator indices_it(indices.begin());

	bool is_eof = false;
	while (!is_eof) {
		std::string document;
		
		// Start accumulating lines that make up a document
    util::StringPiece line;
		while (true) {
			// Is this the end of the input? Then stop reading this document and
			// also stop processing documents in general.
			if (!in.ReadLineOrEOF(line, delimiter, true)) {
				is_eof = true;
				break;
			}

			// Is this the document delimiter when using \n\n as delimiter? Stop
			// reading this document.
			if (delimiter == '\n' && line.empty())
				break;
			
			document.append(line.data(), line.size());
			
			// Add back the \n delimiter for lines (they were chopped off by
			// ReadLineOrEOF)
			if (delimiter == '\n')
				document.push_back('\n');

			// If we are not using the double newline as document separator the
			// ReadLineOrEOF will have consumed a single document in a single
			// read. So we can stop the loop.
			if (delimiter != '\n')
				break;
		}

		// Don't bother printing anything for that last empty doc
		if (is_eof && document.empty())
			break;
		
		++document_index;

		// Check whether this is a document we care about
		if (!indices.empty()) {
			if (*indices_it != document_index) {
				continue; // skip document
			} else {
				indices_it++;

				// Check whether we can stop processing altogether after this one 
				if (indices_it == indices.end())
					is_eof = true;
			}
		}

		std::string encoded_document;
		preprocess::base64_encode(util::StringPiece(document.data(), document.size()), encoded_document);
		out << encoded_document << '\n';
	}

	return document_index;
}


int usage(char program_name[]) {
	std::cerr << "Usage: " << program_name << " [ index ... ] [ files ... ]\n"
		"Convert plain text documents to base64 and vice versa.\n"
	    "\n"
	    "Indices:\n"
	    "  N    Single document index, starting with 1\n"
	    "  M-N  Index range, i.e. 1-3 expands to 1 2 3\n"
	    "\n"
	    "Options:\n"
	    "  -d   Decode; convert base64 encoded documents to text (default: encode)\n"
	    "  -0   Use nullbyte as document delimiter (default: blank line)\n"
	    "  -q   Do not print a warning when the document delimiter shows up\n"
	    "       inside a document.\n"
	    "  -n   Prefix each line with the document index\n";
	return 1;
}


bool parse_range(const char *arg, std::vector<size_t> &indices) {
	std::stringstream sin(arg);
	
	// Try to read a number
	size_t start;
	if (!(sin >> start))
		return false;

	// Was that all? Done!
	if (sin.peek() == EOF) {
		indices.push_back(start);
		return true;
	}
		
	// See whether we can read the second part of e.g. "1-3"
	size_t end;
	if (sin.get() != '-' || !(sin >> end))
		return false;

	UTIL_THROW_IF(start > end, util::Exception, "Cannot understand " << arg
		<< ": " << start << " is larger than " << end << ".\n");

	// Was that all? Great!
	if (sin.peek() == EOF) {
		while (start <= end)
			indices.push_back(start++);
		return true;
	}

	// There is more, I don't understand
	return false;
}

} // namespace


int main(int argc, char **argv) {
	Mode mode = ENCODE;
	
	char delimiter = '\n'; // default: second newline
	bool print_document_index = false;
	bool print_warnings = true;

	std::vector<util::FilePiece> files;
	std::vector<std::size_t> indices;
	
	try {
		for (int i = 1; i < argc; ++i) {
			if (argv[i][0] == '-') {
				switch (argv[i][1]) {
					case 'd':
						mode = DECODE;
						break;

					case 'q':
						print_warnings = false;
						break;

					case '0':
						delimiter = '\0';
						break;

					case 'n':
						print_document_index = true;
						break;

					default:
						UTIL_THROW(util::Exception, "Unknown option " << argv[i] << ".\n");
				}
			} else if (parse_range(argv[i], indices)) {
				// Okay!
			} else {
				files.emplace_back(argv[i]);
			}
		}
	} catch (util::Exception &e) {
		std::cerr << e.what();
		return usage(argv[0]);
	}

	if (print_document_index && mode == ENCODE)
		std::cerr << "Warning: using -n won't do anything in encode mode.\n";
	
	// Sort the indices from small to large so in decode() and encode() we can
	// more easily check whether the document is in the range.
	std::sort(indices.begin(), indices.end());

	// If no files are passed in, read from stdin
	if (files.empty())
		files.emplace_back(STDIN_FILENO);

	util::FileStream out(STDOUT_FILENO);

	size_t document_count = 0;

	for (util::FilePiece &in : files) {
		// Initialize this with true to skip checks altogether
		bool delimiter_encountered = !print_warnings;
		
		switch (mode) {
			case DECODE:
				document_count += decode(in, out, delimiter, indices, print_document_index, delimiter_encountered);
				break;
			case ENCODE:
				document_count += encode(in, out, delimiter, indices);
				break;
		}

		if (print_warnings && delimiter_encountered)
			std::cerr << "Warning: document separator occurs in documents in " << in.FileName() << ".\n";
	}

	return 0;
}
