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

set(PREPROCESS_LIBS kenlm_util ${Boost_LIBRARIES} ${THREADS})

AddExes(EXES ${EXE_LIST}
  LIBRARIES ${PREPROCESS_LIBS})
