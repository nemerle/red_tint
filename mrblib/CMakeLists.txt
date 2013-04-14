# transform mruby's standard lib into a C library

set(MRBLIB_SRC_RB
    array.rb
    class.rb
    compar.rb
    enum.rb
    error.rb
    hash.rb
    kernel.rb
    numeric.rb
    print.rb
    range.rb
    string.rb
)
# generate a single rb file from all existing ones
CONCAT_FILES("${CMAKE_CURRENT_BINARY_DIR}/mrblib.rbtmp" ${MRBLIB_SRC_RB})
CONCAT_FILES("${CMAKE_CURRENT_BINARY_DIR}/mrbgems.rbtmp" ${GEM_RB_FILES})

# mruby compile and generate C byte array representation
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mrblib_all.c"
    DEPENDS mrbc "${CMAKE_CURRENT_BINARY_DIR}/mrblib.rbtmp" ${MRBLIB_SRC_RB}
    COMMAND mrbc -Bmrblib_irep -o"${CMAKE_CURRENT_BINARY_DIR}/mrblib_all.c"
                 "${CMAKE_CURRENT_BINARY_DIR}/mrblib.rbtmp"
)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mrb_gems_all.c"
    DEPENDS mrbc "${CMAKE_CURRENT_BINARY_DIR}/mrbgems.rbtmp" ${GEM_RB_FILES}
    COMMAND mrbc -Bmrbgems_irep -o"${CMAKE_CURRENT_BINARY_DIR}/mrb_gems_all.c"
                 "${CMAKE_CURRENT_BINARY_DIR}/mrbgems.rbtmp"
)
ADD_CUSTOM_TARGET(generate_mrblib_all DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/mrblib_all.c
    ${CMAKE_CURRENT_BINARY_DIR}/mrb_gems_all.c)
SET(compiled_libs ${CMAKE_CURRENT_BINARY_DIR}/mrblib_all.c)
SET(compiled_gems ${CMAKE_CURRENT_BINARY_DIR}/mrb_gems_all.c)
configure_file(init_mrblib.c.cmake ${CMAKE_CURRENT_BINARY_DIR}/init_mrblib.cpp)

# generate final static libmruby archive library

finalizeGems("${CMAKE_CURRENT_BINARY_DIR}/gems.cpp")

add_library(libmruby_static STATIC
  ${CMAKE_CURRENT_BINARY_DIR}/init_mrblib.cpp
  ${GEM_SRC_FILES}
  ${CMAKE_CURRENT_BINARY_DIR}/gems.cpp
  $<TARGET_OBJECTS:mruby_object>
)
add_dependencies(libmruby_static generate_mrblib_all)
set_target_properties(libmruby_static PROPERTIES OUTPUT_NAME mruby)

install(TARGETS libmruby_static
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  )
