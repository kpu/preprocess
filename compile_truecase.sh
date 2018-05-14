#Dyanmic
g++ -I.  -O3 -march=native util/utf8.cc util/ersatz_progress.cc util/file.cc util/file_piece.cc util/mmap.cc util/read_compressed.cc util/scoped.cc util/string_piece.cc preprocess/truecase_main.cc util/exception.cc util/murmur_hash.cc util/integer_to_string.cc util/spaces.cc -licuuc -licudata -o truecase_dynamic
#Static
g++ -I.  -DU_STATIC_IMPLEMENTATION -O3 -march=native util/utf8.cc util/ersatz_progress.cc util/file.cc util/file_piece.cc util/mmap.cc util/read_compressed.cc util/scoped.cc util/string_piece.cc preprocess/truecase_main.cc util/exception.cc util/murmur_hash.cc util/integer_to_string.cc util/spaces.cc -licuuc -licudata -ldl -lm -o truecase_static -pthread -static
