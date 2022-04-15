/*
 * MIT License
 * 
 * Copyright (c) 2020 Leopoldo Pla
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
 */

#include "warcreader.hh"
#include <stdlib.h>
#include <iostream>

namespace warc2text {
    WARCReader::WARCReader(){
        warc_filename = "";
        file = nullptr;

        buf = new uint8_t[BUFFER_SIZE];
        scratch = new uint8_t[BUFFER_SIZE];

        s.zalloc = nullptr;
        s.zfree = nullptr;
        s.opaque = nullptr;
        s.avail_in = 0;
        s.next_in = buf;

        if (inflateInit2(&s, 32) != Z_OK) {
          std::cerr << "Failed to init zlib\n";
          abort();
        }
    }

    WARCReader::WARCReader(const std::string& filename) : WARCReader() {
        openFile(filename);
    }

    WARCReader::~WARCReader(){
        delete[] buf;
        delete[] scratch;
        inflateEnd(&s);
        closeFile();
    }

    bool WARCReader::getRecord(std::string& out, std::size_t max_size){
        int inflate_ret = 0;
        out.clear();
        std::size_t len;
        bool skip_record = false;
        while (inflate_ret != Z_STREAM_END) {
            if (s.avail_in == 0) {
                len = readChunk();
                if (len <= 0) {
                    // nothing more to read
                    out.clear();
                    return false;
                }
                s.avail_in = len;
                s.next_in = buf;
            }
            // inflate until either stream end is reached, or there is no more data
            while (inflate_ret != Z_STREAM_END && s.avail_in != 0) {
                s.next_out = scratch;
                s.avail_out = BUFFER_SIZE;
                inflate_ret = inflate(&s, Z_NO_FLUSH);
                if (inflate_ret != Z_OK && inflate_ret != Z_STREAM_END) {
                    std::cerr << "WARC " << warc_filename << ": error during decompressing\n";
                    out.clear();
                    return false;
                }
                if (not skip_record) out.append(scratch, scratch + (BUFFER_SIZE - s.avail_out));
                if (out.size() > max_size) {
                    std::cerr << "WARC " << warc_filename << ": skipping large record\n";
                    out.clear();
                    skip_record = true;
                }
            }
            if (inflate_ret == Z_STREAM_END) {
                if (inflateReset(&s) != Z_OK) {
                  std::cerr << "Failed to reset zlib\n";
                  abort();
                }
                // next in and avail_in are updated while inflating, so no need to update them manually
            }
        }
        return true;
    }

    void WARCReader::openFile(const std::string& filename){
        warc_filename = filename;
        if (filename.empty() || filename == "-")
            file = std::freopen(nullptr, "rb", stdin); // make sure stdin is open in binary mode
        else file = std::fopen(filename.c_str(), "r");
        if (!file) {
            std::cerr << "WARC " << filename << ": file opening failed, skipping this WARC\n";
        }
    }

    void WARCReader::closeFile() {
        if (file) std::fclose(file);
    }

    std::size_t WARCReader::readChunk(){
        std::size_t len = std::fread(buf, sizeof(uint8_t), BUFFER_SIZE, file);
        if (std::ferror(file) && !std::feof(file)) {
            std::cerr << "WARC " << warc_filename << ": error during reading\n";
            return 0;
        }
        return len;
    }

} // warc2text
