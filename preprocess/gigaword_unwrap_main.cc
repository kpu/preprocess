#include "util/file_piece.hh"
#include "util/string_piece.hh"
#include "util/string_piece_hash.hh"

#include <boost/unordered_map.hpp>

#include <cstring>
#include <iostream>

/* This extracts data from gigaword XML files.  It puts each <P>, <HEADLINE>,
 * and <DATELINE> on a line with a <P> after it.  This output format is
 * intended for split-sentences.pl.  
 */

const char *nyt_parentheses[] = {
  "(MORE)",
  "(PICTURE)",
  "(PICTURES)",
  "(BC-[A-Z -]*)",
  "(END OPTIONAL TRIM)",
  "(BEGIN OPTIONAL TRIM)",
  "(CORRECT)",
  "(REPEAT)",
  "(UNDATED)",
  "(BEGIN ITALICS HERE)",
  "(AM-NYT-BUDGET)",
  "(END ITALICS HERE)",
  "(AM-ADD-NYT-BUDGET)",
  "(FIRST OPTIONAL TRIM BEGINS)",
  "(FIRST OPTIONAL TRIM ENDS)",
  "(END BRACKET)",
  "(BEGIN BRACKET)",
  "(PM-BUDGET-NYT)",
  "(OPTIONAL TRIM FOLLOWS)",
  "(END ITAL)",
  "(OPTIONAL TRIM BEGINS)",
  "(OPTIONAL TRIM ENDS)",
  "(GRAPHIC)",
  "(SECOND OPTIONAL TRIM BEGINS)",
  "(SECOND OPTIONAL TRIM ENDS)",
  "(SECOND OPTIONAL TRIM FOLLOWS)",
  "(RECASTS)",
  "(STORY CAN END HERE -- OPTIONAL MATERIAL FOLLOWS)",
  "(STORY CAN END HERE)",
  "(END BLOOMBERG NYTNS BUDGET)",
  "(END ITALICS)",
  "(END OF SECOND OPTIONAL TRIM)",
  "(SECOND TAKE FOLLOWS)",
  "(BEGIN ITALICS)",
  "(FIRST OPTIONAL TRIM BEGINS HERE)",
  "(THIRD OPTIONAL TRIM ENDS)",
  "(THIRD OPTIONAL TRIM BEGINS)",
  "(AM-SPORTS-NYT-BUDGET)",
  "(HORIZONTAL)",
  "(BACK-TO-SCHOOL)",
  "(COLUMN)",
  "(HISPANIC-HERITAGE-MONTH)",
  "(PM-NYT-BUDGET)",
  "(THIRD OPTIONAL TRIM FOLLOWS)",
  "(ENDITAL)",
  "(SECOND OPTIONAL TRIM BEGINS HERE)",
  "(RESENDING FOR THOSE WHO MAY HAVE MISSED THIS)",
  "(END ITALS)",
  "(OPTIONAL TRIM)",
  "(REPETITION)",
  "(REPEATING FOR ALL NEEDING)",
  "(EDITORIAL)",
  "( END OF TEXT )",
  "(CAN TRIM HERE)",
  "(RESENDING)",
  "(GRAPHICS)",
  "(END OF THIRD OPTIONAL TRIM)",
  "(AM-R-NYT-BUDGET)",
  "(FOURTH OPTIONAL TRIM BEGINS)",
  "(VERTICAL)",
  "(FOURTH OPTIONAL TRIM ENDS)",
  "(END BOLD)",
  "(OPTIONAL TRIM ENDS HERE)",
  "(THIRD OPTIONAL TRIM BEGINS HERE)",
  "(OPTIONAL MATERIAL FOLLOWS - STORY MAY END HERE)",
  "(GRAPHICS-FILES)",
  "(CONTINUED ON NEXT TAKE)",
  "(REQUESTED REPETITION)",
  "(END NEW YORK TIMES NEWS SERVICE BUDGET)",
  "(PERSONAL-FINANCE-ADVISORY-NYT)",
  "(ITALICS)",
  "(BEG ITAL)",
  "(BOLD)",
  "(ITALICS ON)",
  "(BEG BOLD)",
  "(END OF FIRST TRIM)",
  "(UNDERLINE)",
  "(NEWS ANALYSIS)",
  "(ITALICS OFF)",
  "(STORY CAN END HERE. OPTIONAL 2ND TAKE FOLLOWS.)",
  "(STORY CAN END HERE. OPTIONAL 3RD TAKE FOLLOWS.)"
};

boost::unordered_map<util::StringPiece, const char *> nyt_parentheses_set;

void CheckReplaceEntity(std::string &line, size_t pos, const char *pattern, char with) {
  if (!strncasecmp(line.c_str() + pos, pattern, strlen(pattern))) {
    line.replace(pos, strlen(pattern), 1, with);
  }
}

void MungeLine(std::string &line) {
  boost::unordered_map<util::StringPiece, const char *>::const_iterator found;
  // Parenthesized stuff
  for (size_t pos = line.find('('); pos != std::string::npos;) {
    size_t right = line.find(')', pos + 1);
    if (right != std::string::npos) {
      // Include ')' in range [pos, end)
      size_t end = right + 1;
      if (nyt_parentheses_set.end() != (found = nyt_parentheses_set.find(util::StringPiece(line.c_str() + pos, end - pos)))) {
        line.replace(pos, end - pos, found->second);
        pos = line.find('(', pos);
      } else if(!strncmp(line.c_str() + pos + 1, "BC-", 3)) {
        line.erase(pos, end - pos);
        pos = line.find('(', pos);
      } else {
        pos = line.find('(', pos + 1);
      }
    } else {
      pos = line.find('(', pos + 1);
    }
  }


  for (size_t pos = line.find("``"); pos != std::string::npos; pos = line.find("``", pos + 1)) {
    line.replace(pos, 2, 1, '"');
  }
  for (size_t pos = line.find("''"); pos != std::string::npos; pos = line.find("''", pos + 1)) {
    line.replace(pos, 2, 1, '"');
  }

  // XML entities: these always produce at least one character.
  for (size_t pos = line.find('&'); pos != std::string::npos; pos = line.find('&', pos + 1)) {
    size_t right = line.find(';', pos + 1);
    if (right != std::string::npos) {
      // Not the most efficient way, but these are rare.  
      CheckReplaceEntity(line, pos, "&lt;", '<');
      CheckReplaceEntity(line, pos, "&gt;", '>');
      CheckReplaceEntity(line, pos, "&amp;", '&');
      CheckReplaceEntity(line, pos, "&apos;", '\'');
      CheckReplaceEntity(line, pos, "&quot;", '"');
    }
  }
}

void ProcessText(util::FilePiece &in, const std::string &close, std::ostream &out, std::string &dupe_detect) {
  bool content = false;
  std::string line;
  util::StringPiece l;
  while (true) {
    try {
      l = in.ReadLine();
    } catch (const util::EndOfFileException &) { break; }
    if (l == close) break;
    if (l.empty() || (l.data()[0] != '<') || (l.data()[l.size() - 1] != '>')) {
      line.assign(l.data(), l.size());
      MungeLine(line);
      if (!line.empty()) content = true;
      if (dupe_detect != line) {
        out << line;
      }
      if (!line.empty() && line[line.size() - 1] != '-') {
        out << ' ';
      }
      std::swap(line, dupe_detect);
    }
  }
  // Why two lines?  This is intended to be piped to the sentence breaker.  
  if (content) out << "\n<P>\n";
}

void ProcessGigaword(util::FilePiece &in, std::ostream &out) {
  const std::string head_close("</HEADLINE>"), p_close("</P>"), date_close("</DATELINE>"), text_close("</TEXT>");
  util::StringPiece line;
  std::string dupe_detect;
  while (true) {
    try {
      line = in.ReadLine();
    } catch (const util::EndOfFileException &) { return; }
    if (line == "<HEADLINE>") {
      ProcessText(in, head_close, out, dupe_detect);
    } else if (line == "<P>") {
      ProcessText(in, p_close, out, dupe_detect);
    } else if (line == "<DATELINE>") {
      ProcessText(in, date_close, out, dupe_detect);
    } else if (line == "<TEXT>") {
      ProcessText(in, text_close, out, dupe_detect);
    }
  }
}

int main() {
  for (const char **i = nyt_parentheses; i != nyt_parentheses + sizeof(nyt_parentheses) / sizeof(const char*); ++i) {
    nyt_parentheses_set.insert(std::make_pair(*i, ""));
  }
  nyt_parentheses_set["(BEGIN BRACKET)"] = "[";
  nyt_parentheses_set["(END BRACKET)"] = "]";
  nyt_parentheses_set["(UNDERSCORE)"] = "_";
  nyt_parentheses_set["(TILDE)"] = "~";
  nyt_parentheses_set["(ASTERISK)"] = "*";
  nyt_parentheses_set["(AT SIGN)"] = "@";
  nyt_parentheses_set["(AT)"] = "@";
  nyt_parentheses_set["(EQUALS)"] = "=";

  util::FilePiece in(0, NULL, &std::cerr);

  ProcessGigaword(in, std::cout);
  return 0;
}
