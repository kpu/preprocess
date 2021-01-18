#pragma once
#include <string>
#include "util/string_piece.hh"

namespace preprocess {

void base64_encode(const util::StringPiece &in, std::string &out);

void base64_decode(const util::StringPiece &in, std::string &out);

} // namespace preprocess
