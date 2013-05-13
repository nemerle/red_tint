# build the core mruby C files

find_package(BISON)
bison_target(mruby parse.y "${CMAKE_CURRENT_BINARY_DIR}/parse.cpp")
SET(MRUBY_SRC_C
    array.cpp
    backtrace.cpp
    class.cpp
    codegen.cpp
    compar.cpp
    compile.cpp
    crc.cpp
    dump.cpp
    enum.cpp
    error.cpp
    etc.cpp
    gc.cpp
    hash.cpp
    init.cpp
    load.cpp
    kernel.cpp
    numeric.cpp
    object.cpp
    lexer.cpp
    parser_support.cpp
    pool.cpp
    print.cpp
    proc.cpp
    range.cpp
    state.cpp
    string.cpp
    symbol.cpp
    variable.cpp
    vm.cpp
    ../include/mrbconf.h
    ../include/mruby.h
    ../include/mruby/array.h
    ../include/mruby/class.h
    ../include/mruby/compile.h
    ../include/mruby/mem_manager.h
    ../include/mruby/data.h
    ../include/mruby/dump.h
    ../include/mruby/irep.h
    ../include/mruby/numeric.h
    ../include/mruby/variable.h
    ../include/mruby/proc.h
    ../include/mruby/value.h
    ../include/mruby/string.h
    ../include/mruby/node.h

)
SET(MRUBY_SRC_H
    error.h
    opcode.h
    re.h
    value_array.h
)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
list(APPEND MRUBY_SRC_C "${CMAKE_CURRENT_BINARY_DIR}/parse.cpp")
add_library(mruby_object OBJECT ${MRUBY_SRC_C} ${BISON_mruby_OUTPUTS} ${MRUBY_SRC_H})
