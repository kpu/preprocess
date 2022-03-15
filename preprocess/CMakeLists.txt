if (NOT MSVC)
	set(THREADS pthread)
endif()

add_library(fields STATIC fields.cc)
add_library(captive_child STATIC captive_child.cc)
add_library(warc STATIC warc.cc)
add_library(base64 STATIC base64.cc)

# Explicitly list the executable files to be compiled
set(EXE_LIST
  b64filter
  base64_number
  cache
  commoncrawl_dedupe
  dedupe
  docenc
  foldfilter
  gigaword_unwrap
  idf
  mmhsum
  order_independent_hash
  remove_invalid_utf8
  remove_long_lines
  shard
  substitute
  vocab
  warc_parallel
)

set(ICU_EXE_LIST
  apply_case
  truecase
  train_case
  process_unicode
  simple_cleaning
  )
if(USE_ICU)
  set(EXE_LIST ${EXE_LIST} ${ICU_EXE_LIST})
endif(USE_ICU)

set(PREPROCESS_LIBS preprocess_util ${Boost_LIBRARIES} ${THREADS})

foreach(exe ${EXE_LIST})
  add_executable(${exe} ${exe}_main.cc)
  target_link_libraries(${exe} ${PREPROCESS_LIBS})
  set_target_properties(${exe} PROPERTIES FOLDER executables)
endforeach(exe)

target_link_libraries(b64filter ${PREPROCESS_LIBS} base64 captive_child)
target_link_libraries(base64_number ${PREPROCESS_LIBS} base64 captive_child)
target_link_libraries(cache ${PREPROCESS_LIBS} fields captive_child)
target_link_libraries(dedupe ${PREPROCESS_LIBS} fields)
target_link_libraries(docenc ${PREPROCESS_LIBS} base64)
target_link_libraries(foldfilter ${PREPROCESS_LIBS} captive_child)
target_link_libraries(shard ${PREPROCESS_LIBS} fields)
target_link_libraries(simple_cleaning ${PREPROCESS_LIBS} fields)
target_link_libraries(substitute ${PREPROCESS_LIBS} fields)
target_link_libraries(warc_parallel ${PREPROCESS_LIBS} warc captive_child)

if(USE_ICU)
  foreach(exe ${ICU_EXE_LIST})
    target_link_libraries(${exe} preprocess_icu)
  endforeach(exe)
endif(USE_ICU)

foreach(script text.sh gigaword_extract.sh resplit.sh unescape_html.perl heuristics.perl)
  configure_file(${script} ../bin/${script} COPYONLY)
endforeach()
