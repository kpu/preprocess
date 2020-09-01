if (NOT MSVC)
	set(THREADS pthread)
endif()

add_library(fields STATIC fields.cc)
add_library(captive_child STATIC captive_child.cc)
add_library(warc STATIC warc.cc)
add_library(base64 STATIC base64.cc)

# Explicitly list the executable files to be compiled
set(EXE_LIST
  apply_case
  b64filter
  cache
  commoncrawl_dedupe
  dedupe
  gigaword_unwrap
  order_independent_hash
  process_unicode
  remove_invalid_utf8
  remove_long_lines
  select_latin
  shard
  substitute
  train_case
  truecase
  vocab
  warc_parallel
)

set(PREPROCESS_LIBS preprocess_util ${Boost_LIBRARIES} ${THREADS})

foreach(exe ${EXE_LIST})
  add_executable(${exe} ${exe}_main.cc)
  target_link_libraries(${exe} ${PREPROCESS_LIBS})
  set_target_properties(${exe} PROPERTIES FOLDER executables)
endforeach(exe)

target_link_libraries(cache ${PREPROCESS_LIBS} fields captive_child)
target_link_libraries(shard ${PREPROCESS_LIBS} fields)
target_link_libraries(substitute ${PREPROCESS_LIBS} fields)
target_link_libraries(warc_parallel ${PREPROCESS_LIBS} warc captive_child)
target_link_libraries(b64filter base64 captive_child)

foreach(script text.sh gigaword_extract.sh resplit.sh unescape_html.perl heuristics.perl)
  configure_file(${script} ../bin/${script} COPYONLY)
endforeach()
