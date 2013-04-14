# build tools/mrbc executable

file(GLOB MRBC_SRC_C "*.cpp")
add_executable(mrbc ${MRBC_SRC_C} $<TARGET_OBJECTS:mruby_object>)
target_link_libraries(mrbc ${MRUBY_LIBS})

install(TARGETS mrbc RUNTIME DESTINATION bin)
