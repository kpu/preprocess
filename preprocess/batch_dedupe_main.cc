#include "util/file_piece.hh"
#include "util/compress.hh"
#include "util/murmur_hash.hh"
#include "util/probing_hash_table.hh"
#include <memory>
#include <numeric>
#include <string>
#include <unordered_set>
#include <vector>
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

	std::cerr << "Unique column:" << out.unique << "\n";
	std::cerr << "\nColumns to combine with '" << out.glue << "':\n";
	for (std::string const &column : out.combined)
		std::cerr << "- " << column << "\n";
	std::cerr << "\nColumns to keep only one value of:\n";
	for (std::string const &column : out.derived)
		std::cerr << "- " << column << "\n";
}

class Reader {
public:
	Reader(std::string const &path, std::vector<std::string> const &columns)
	: path_(path),
		columns_(columns)
	{
		fhs_.reserve(columns.size());

		for (auto &&column : columns) {
			std::string filename(path + "/" + column);
			fhs_.emplace_back(filename.c_str());
		}
	}

	bool ReadRowOrEOF(std::vector<util::StringPiece> &row) {
		row.resize(fhs_.size());

		for (std::size_t col = 0; col < fhs_.size(); ++col)
			if (!fhs_[col].ReadLineOrEOF(row[col]))
				return false;

		return true;
	}

private:
	std::string path_;
	std::vector<std::string> columns_;
	std::vector<util::FilePiece> fhs_;
};

class Writer {
public:
	Writer(std::string const &path, std::vector<std::string> const &columns, std::size_t limit)
	: path_(path),
		columns_(columns),
		limit_(limit),
		lines_written_(0),
		bytes_written_(columns.size(), limit + 1), // force rotate at start
		fhs_(columns.size())
	{
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
			fhs_[col].reset(new util::GZipFileStream(util::CreateOrThrow(filename.c_str())));
			bytes_written_[col] = 0;
		}
	}

	template <typename It> std::size_t WriteRow(It begin, It end) {
		assert(std::distance(begin, end) == columns_.size());

		// Check whether writing any of the columns would push us over the size limit
		auto written_it = bytes_written_.begin();
		for (auto it = begin; it != end; ++it) {
			if (*written_it++ + it->size() + 1 > limit_) {
				Rotate();
				break; // Don't rotate multiple times
			}
		}

		auto fh_it = fhs_.begin();
		written_it = bytes_written_.begin();
		for (auto it = begin; it != end; ++it) {
			*(*fh_it++) << *it << '\n';
			*written_it++ += it->size() + 1; // plus newline
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
		
			util::GZipFileStream fout(util::CreateOrThrow(path.str().c_str())); // todo: compress
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
	std::vector<std::unique_ptr<util::CompressedFileStream>> fhs_;
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

// Needlessly complicated string concatenation operator that does only one
// memory allocation if I got it right.
struct Concat {
	std::string glue;

	Concat(std::string const &glue) : glue(glue) {
		//
	}

	std::string operator()(std::unordered_set<std::string> const &values) const {
		std::size_t glue_size = glue.size();

		std::size_t size = std::accumulate(values.begin(), values.end(), 0,
			[glue_size](std::size_t size, std::string const &value) {
				return size + value.size() + glue_size; //` plus a space
			});

		if (size == 0)
			return std::string();

		std::string out;
		out.reserve(size - glue_size); // Glue only between, so -1

		auto it = values.begin();
		out.append(*it);
		while (++it != values.end()) {
			out.append(glue);
			out.append(*it);
		}

		assert(out.size() == size - glue_size); // Did I get it right?

		return out;
	}
};

} // namespace

int main(int argc, char *argv[]) {
	Options options;
	ParseArgs(argc, argv, options);

	std::size_t unique = FindIndex(options.derived, options.unique);
	UTIL_THROW_IF2(unique == options.derived.size(), "unique column has to be part of derived columns");

	Writer fout(".", options.derived, 1024 * 1024 * 1024);

	typedef util::AutoProbing<Entry, util::IdentityHash> Table;
	Table table;

	std::vector<std::string> columns(options.derived.size() + options.combined.size());
	std::copy(options.combined.begin(), options.combined.end(),
		std::copy(options.derived.begin(), options.derived.end(),
			columns.begin()));

	// For each combined column, for each unqiue row, we have a set of values.
	// Using std::string instead of StringPiece here because StringPiece doesnt
	// own its memory, and Reader will have progressed when we need these strings
	// again.
	std::vector<std::vector<std::unordered_set<std::string>>> combined_column_values(options.combined.size());

	std::vector<util::StringPiece> row(columns.size());

	for (auto &&path : options.batches) {
		Reader batch(path, columns);
		while (batch.ReadRowOrEOF(row)) {
			Entry entry;
			entry.key = util::MurmurHashNative(row[unique].begin(), row[unique].size()) + 1;
			
			Table::MutableIterator it;
			if (!table.FindOrInsert(entry, it)) {
				it->offset = fout.WriteRow(row.begin(), row.begin() + options.derived.size());
				
				for (std::size_t col = 0; col < options.combined.size(); ++col)
					combined_column_values[col].emplace_back(); // Add a new set
			}

			for (std::size_t col = 0; col < options.combined.size(); ++col) {
				util::StringPiece const &value = row[options.derived.size() + col];
				combined_column_values[col][it->offset].insert(std::string(value.data(), value.size()));
			}
		}
	}

	for (std::size_t col = 0; col < options.combined.size(); ++col)
		fout.WriteColumn(options.combined[col],
			boost::make_transform_iterator(combined_column_values[col].begin(), Concat(options.glue)), 
			boost::make_transform_iterator(combined_column_values[col].end(), Concat(options.glue)));

	return 0;
}
