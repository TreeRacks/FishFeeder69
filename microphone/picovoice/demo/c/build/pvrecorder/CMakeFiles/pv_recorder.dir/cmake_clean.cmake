file(REMOVE_RECURSE
  "libpv_recorder.pdb"
  "libpv_recorder.so"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/pv_recorder.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
