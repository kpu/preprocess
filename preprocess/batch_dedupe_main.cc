#include "util/file_piece.hh"
#include "util/compress.hh"
#include "util/murmur_hash.hh"
#include "util/pcqueue.hh"
#include "util/probing_hash_table.hh"
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <iomanip>
#include <iostream>
#include <boost/filesystem.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/iterator/transform_iterator.hpp>

namespace {

struct Options {
	std::vector<std::string> batches;
	std::vector<std::string> combined{"url.gz","source.gz"};
	std::vector<std::string> derived{"plain_text.gz", "sentences.gz", "sentences_en.gz"};
	std::string unique{"sentences.gz"};
	std::string output{"."};
	std::size_t size{1024 * 1024 * 1024};
	std::string glue{" "};
	bool verbose{false};
};

void ParseArgs(int argc, char *argv[], Options &out) {
	namespace po = boost::program_options;
	po::options_description visible("Batch options");
	visible.add_options()
		("combined,c", po::value(&out.combined)->multitoken(), "Columns that should be combined")
		("derived,d", po::value(&out.derived)->multitoken(), "Columns that are derived")
		("unique,u", po::value(&out.unique), "Column to deduplicate on")
		("output,o", po::value(&out.output), "Output path")
		("bytes,b", po::value(&out.size), "Maximum batch size")
		("glue,g", po::value(&out.glue), "Glue between combined values")
		("verbose,v", po::bool_switch(&out.verbose), "Print progress updates")
		("help,h", "Produce help message");

	po::options_description hidden("Hidden options");
	hidden.add_options()
		("input-file,i", po::value(&out.batches), "Input batches");

	po::positional_options_description positional;
	positional.add("input-file", -1);

	po::options_description opts;
	opts.add(visible).add(hidden);

	po::variables_map vm;
	po::store(po::command_line_parser(argc, argv).options(opts).positional(positional).run(), vm);
	po::notify(vm);

	if (vm.count("help")) {
		std::cerr << "Usage: [options] <path/to/batch> [<path> ...]" << "\n"
		          << "\n" << visible << "\n";
		std::exit(1);
	}
}

// Batch reader: reads an entry from all columns with each ReadRow
class Reader {
public:
	Reader(std::string const &path, std::vector<std::string> const &columns)
	: path_(path) {
		fhs_.reserve(columns.size());

		// Open all columns at the same time in the same order as `columns`
		for (auto &&column : columns) {
			std::string filename(path + "/" + column);
			fhs_.emplace_back(filename.c_str());
		}
	}

	bool ReadRowOrEOF(std::vector<util::StringPiece> &row) {
		// Make sure we have space for all columns
		row.resize(fhs_.size());

		for (std::size_t col = 0; col < fhs_.size(); ++col)
			if (!fhs_[col].ReadLineOrEOF(row[col]))
				return false;

		return true;
	}

private:
	std::string path_;
	std::vector<util::FilePiece> fhs_;
};

// Dispatches compressed write calls in a background thread.
// Joins (and thus blocks) on destruction.
class AsyncWriter {
public:
	AsyncWriter(std::string const &filename)
	: file_(util::CreateOrThrow(filename.c_str())) {
		writer_  = std::thread([this]() {
			util::GZipFileStream fout(this->file_.get());
			std::string text;
			while (!this->queue_.Consume(text).empty())
				fout << text;
		});
	}

	~AsyncWriter() {
		Close();
	}

	void Write(std::string &&text) {
		// Empty write would be pointless, and kill the thread. And we can safely
		// ignore an empty write I think.
		if (UTIL_LIKELY(!text.empty()))
			queue_.Produce(std::move(text));
	}

	void Close() {
		queue_.Produce(std::string());
		writer_.join();
	}

private:
	util::UnboundedSingleQueue<std::string> queue_;
	std::thread writer_;
	util::scoped_fd file_;
};

// Batch writer: writes unique columns on the go, will rotate files if any of
// the unique columns is about to exceed the specified (uncompressed) limit.
// Keeps track of which row files were split at so it can write the combined
// columns with the same splits afterwards.
class Writer {
public:
	Writer(std::string const &path, std::vector<std::string> const &columns, std::size_t limit)
	: path_(path),
		columns_(columns),
		limit_(limit),
		lines_written_(0),
		bytes_written_(columns.size(), limit + 1), // force rotate at start
		fhs_(columns.size()) {
		//
	}

	void Rotate() {
		// Store where we are now
		batch_offsets_.push_back(lines_written_);

		// Todo: use boost::filesystem::path?
		std::ostringstream path;
		path << path_ << "/" << batch_offsets_.size() << "/";
		boost::filesystem::create_directories(path.str());

		// Open columns in the new batch and reset write counters
		// (this also closes the old writers through destruction)
		for (std::size_t col = 0; col < columns_.size(); ++col) {
			std::string filename(path.str() + columns_[col]);
			fhs_[col].reset(new AsyncWriter(filename));
			bytes_written_[col] = 0;
		}
	}

	template <typename It> std::size_t WriteRow(It begin, It end) {
		// Assert we were passed the right number of columns
		assert(std::distance(begin, end) == columns_.size());

		// Check whether writing any of the columns would push us over the size limit
		auto written_it = bytes_written_.begin();
		for (auto it = begin; it != end; ++it) {
			if (*written_it++ + it->size() + 1 > limit_) {
				Rotate();
				break; // Don't rotate multiple times
			}
		}

		// Write each of the columns
		auto fh_it = fhs_.begin();
		written_it = bytes_written_.begin();
		for (auto it = begin; it != end; ++it) {
			std::string line;
			line.reserve(it->size() + 1);
			line.append(it->data(), it->size());
			line.append("\n");
			*written_it++ += line.size();
			(*fh_it++)->Write(std::move(line));
		}

		return lines_written_++;
	}

	template <typename It> void WriteColumn(std::string const &name, It begin, It end) {
		std::size_t batch = 0;
		std::size_t written = 0;
		std::size_t offset;

		// Yes this will start batches counting from 1 instead of 0. But so did
		// giashard. Let's keep that legacy around for a bit longer.
		for (std::size_t batch = 1; begin != end; ++batch) {
			if (batch < batch_offsets_.size())
				offset = batch_offsets_[batch];
			else
				offset = lines_written_ + 1; // or maxint, just something out of reach

			std::ostringstream path;
			path << path_ << "/" << batch << "/" << name;
		
			util::scoped_fd fd(util::CreateOrThrow(path.str().c_str()));
			util::GZipFileStream fout(fd.get());
			while (begin != end && written < offset) {
				fout << *begin++ << '\n';
				++written;
			}
		}

		UTIL_THROW_IF2(written != lines_written_, "WriteColumn(" << name << ") wrote " << written << " rows, expected " << lines_written_ << " rows");
	}

private:
	std::string path_; // path to directory where batches are written to
	std::vector<std::string> columns_;
	std::size_t limit_; // filesize limit per batch
	std::vector<std::size_t> batch_offsets_;
	std::size_t lines_written_;
	std::vector<std::size_t> bytes_written_;
	std::vector<std::unique_ptr<AsyncWriter>> fhs_;
};


struct Entry {
	typedef uint64_t Key;
	uint64_t key;
	std::size_t offset;
	uint64_t GetKey() const { return key; }
	void SetKey(uint64_t to) { key = to; }
};


template <typename T, typename V> std::size_t FindIndex(T const &container, V const &needle) {
	std::size_t index = 0;
	
	for (auto &&value : container) {
		if (value == needle)
			break;
		++index;
	}

	return index;
}

// Little wrapper around an unordered set that remembers the glue, and can be
// written to an FakeOStream as if it were an already concatenated string.
struct ConcatenatedSet {
	std::string const &glue;
	std::unordered_set<std::string> const &values;
};

template <class Derived> util::FakeOStream<Derived> &operator<<(util::FakeOStream<Derived> &os, ConcatenatedSet const &wrapper) {
	if (wrapper.values.empty())
		return os;

	auto it = wrapper.values.begin();
	os << *it;
	while (++it != wrapper.values.end())
		os << wrapper.glue << *it;
	
	return os;
}

// "function" that creates a ConcatenatedSet so it can be used as operation
// in a transform iterator.
struct Concat {
	std::string glue;

	Concat(std::string const &glue) : glue(glue) {
		//
	}

	ConcatenatedSet operator()(std::unordered_set<std::string> const &values) const {
		return ConcatenatedSet{.glue=glue, .values=values};
	}
};

} // namespace

int main(int argc, char *argv[]) {
	Options options;
	ParseArgs(argc, argv, options);

	std::size_t unique = FindIndex(options.derived, options.unique);
	UTIL_THROW_IF2(unique == options.derived.size(), "unique column has to be part of derived columns");

	Writer fout(options.output, options.derived, 1024 * 1024 * 1024);

	typedef util::AutoProbing<Entry, util::IdentityHash> Table;
	Table table;

	std::vector<std::string> columns(options.derived.size() + options.combined.size());
	std::copy(options.combined.begin(), options.combined.end(),
		std::copy(options.derived.begin(), options.derived.end(),
			columns.begin()));

	// For each combined column, for each unqiue row, we have a set of values.
	// Using std::string instead of StringPiece here because StringPiece doesn't
	// own its memory, and Reader will have progressed when we need these strings
	// again.
	std::vector<std::vector<std::unordered_set<std::string>>> combined_column_values(options.combined.size());

	std::size_t records_cnt = 0, unique_cnt = 0;

	std::vector<util::StringPiece> row(columns.size());

	for (std::size_t i = 0; i < options.batches.size(); ++i) {
		if (options.verbose)
			std::cerr << "Reading " << (i + 1) << "/" << options.batches.size()
			          << ": " << options.batches[i] << std::endl;

		Reader batch(options.batches[i], columns);
		while (batch.ReadRowOrEOF(row)) {
			++records_cnt;

			Entry entry;
			entry.key = util::MurmurHashNative(row[unique].begin(), row[unique].size()) + 1;
			
			Table::MutableIterator it;
			if (!table.FindOrInsert(entry, it)) {
				it->offset = fout.WriteRow(row.begin(), row.begin() + options.derived.size());
				
				for (std::size_t col = 0; col < options.combined.size(); ++col)
					combined_column_values[col].emplace_back(); // Add a new set

				++unique_cnt;
			}

			for (std::size_t col = 0; col < options.combined.size(); ++col) {
				util::StringPiece const &value = row[options.derived.size() + col];
				combined_column_values[col][it->offset].insert(std::string(value.data(), value.size()));
			}
		}

		if (options.verbose)
			std::cerr << "Kept " << unique_cnt << " out of " << records_cnt << " records so far"
			          << " (" << std::setprecision(2) << (100.0 * unique_cnt / records_cnt) << "%)"
			          << std::endl;
	}

	// Write the combined columns. Uses Concat which wraps the sets directly to
	// skip having to make a single string per row (as those might have become
	// quite large at this point.)
	for (std::size_t col = 0; col < options.combined.size(); ++col)
		fout.WriteColumn(options.combined[col],
			boost::make_transform_iterator(combined_column_values[col].begin(), Concat(options.glue)), 
			boost::make_transform_iterator(combined_column_values[col].end(), Concat(options.glue)));

	return 0;
}
