function(CONCAT_FILES _filename)
    file(WRITE ${_filename} "")
    foreach(_current_FILE ${ARGN})
        file(READ ${_current_FILE} _contents)
        file(APPEND ${_filename} "${_contents}\n")
    endforeach(_current_FILE)
endfunction()

macro(InitGems)
    set(GEM_RB_FILES)
    set(GEM_SRC_FILES)
    set(GEMS)
    set(GEM_TEST_RB_FILES)
endmacro()
macro(AddGem _name)
    STRING(REGEX REPLACE "-" "_" name ${_name})
    file(GLOB temp_rb ${PROJECT_SOURCE_DIR}/mrbgems/mruby-${_name}/mrblib/*.rb)
    file(GLOB tests_rb ${PROJECT_SOURCE_DIR}/mrbgems/mruby-${_name}/test/*.rb)
    #CONCAT_FILES(${CMAKE_CURRENT_BINARY_DIR}/${name}-all.rb ${temp_rb})
    #COMPILE_RB_TO_IREP(${name} ${name}-all.rb)
    file(GLOB temp_src ${PROJECT_SOURCE_DIR}/mrbgems/mruby-${_name}/src/*.c*)
    list(APPEND GEM_RB_FILES ${temp_rb})
    list(APPEND GEMS ${name})
    list(APPEND GEM_SRC_FILES ${temp_src})
    list(APPEND GEM_TEST_RB_FILES ${tests_rb})
endmacro()
macro(FinalizeGems _filename)
    file(REMOVE ${_filename})
    file(APPEND ${_filename} "#include \"mruby.h\"\n")
    file(APPEND ${_filename} "void mrb_init_mrbgems_irep(mrb_state *);\n")
    foreach(gem ${GEMS})
        file(APPEND ${_filename} "void mrb_mruby_${gem}_gem_init(mrb_state *);\n")
        file(APPEND ${_filename} "void mrb_mruby_${gem}_gem_final(mrb_state *);\n")
    endforeach(gem)
    file(APPEND ${_filename} "void\nmrb_init_mrbgems(mrb_state *mrb) {\n")
    foreach(gem ${GEMS})
        file(APPEND ${_filename} "  mrb_mruby_${gem}_gem_init(mrb);\n")
    endforeach(gem)
    file(APPEND ${_filename} "mrb_init_mrbgems_irep(mrb);\n")

    file(APPEND ${_filename} "}\n")
    file(APPEND ${_filename} "void\nmrb_final_mrbgems(mrb_state *mrb) {\n")
    foreach(gem ${GEMS})
        file(APPEND ${_filename} "  mrb_mruby_${gem}_gem_final(mrb);\n")
    endforeach(gem)
    file(APPEND ${_filename} "}\n")
endmacro()
macro(FinalizeGemTests _filename)
    file(REMOVE ${_filename})
    file(APPEND ${_filename} "#include \"mruby.h\"\n")
    foreach(gem ${GEMS})
        file(APPEND ${_filename} "void mrb_mruby_${gem}_gem_test(mrb_state *);\n")
    endforeach(gem)
    file(APPEND ${_filename} "void\nmrbgemtest_init(mrb_state *mrb) {\n")
    foreach(gem ${GEMS})
        file(APPEND ${_filename} "  mrb_mruby_${gem}_gem_test(mrb);\n")
    endforeach(gem)
    file(APPEND ${_filename} "}\n")
endmacro()
