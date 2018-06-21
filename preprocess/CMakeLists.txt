if (NOT MSVC)
	set(THREADS pthread)
endif()

# Explicitly list the executable files to be compiled
set(EXE_LIST
  apply_case
  commoncrawl_dedupe
  dedupe
  gigaword_unwrap
  process_unicode
  remove_invalid_utf8
  remove_long_lines
  select_latin
  shard
  train_case
  truecase
  vocab
)

set(PREPROCESS_LIBS preprocess_util ${Boost_LIBRARIES} ${THREADS})

foreach(exe ${EXE_LIST})
  add_executable(${exe} ${exe}_main.cc)
  target_link_libraries(${exe} ${PREPROCESS_LIBS})
  set_target_properties(${exe} PROPERTIES FOLDER executables)
endforeach(exe)

foreach(script text.sh gigaword_extract.sh resplit.sh unescape_html.perl heuristics.perl)
  configure_file(${script} ../bin/${script} COPYONLY)
endforeach()
