/*
 *    MIT License
 * 
 * warcreader.hh Copyright (c) 2020 Leopoldo Pla
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 */
#ifndef WARC2TEXT_WARCREADER_HH
#define WARC2TEXT_WARCREADER_HH

#include "zlib.h"
#include <string>

namespace warc2text {
    class WARCReader {
        public:
            WARCReader();
            explicit WARCReader(const std::string& filename);
            bool getRecord(std::string& out, std::size_t max_size = 1024*1024*20); //20MB
            ~WARCReader();
        private:
            std::FILE* file;
            std::string warc_filename;
            z_stream s{};
            static const std::size_t BUFFER_SIZE = 4096;
            uint8_t* buf;
            uint8_t* scratch;

            void openFile(const std::string& filename);
            void closeFile();
            std::size_t readChunk();
    };
}

#endif
