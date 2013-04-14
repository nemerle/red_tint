# build tools/mruby executable

file(GLOB MRUBYBIN_SRC_C "*.cpp")
add_executable(mruby ${MRUBYBIN_SRC_C})
target_link_libraries(mruby libmruby_static ${MRUBY_LIBS})

install(TARGETS mruby RUNTIME DESTINATION bin)
