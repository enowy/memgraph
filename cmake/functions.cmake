# prints all included directories
function(list_includes)
    get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                      PROPERTY INCLUDE_DIRECTORIES)
    foreach(dir ${dirs})
          message(STATUS "dir='${dir}'")
    endforeach()
endfunction(list_includes)

# get file names from list of file paths
function(get_file_names file_paths file_names)
    set(file_names "")
    foreach(file_path ${file_paths})
        get_filename_component (file_name ${file_path} NAME_WE)
        list(APPEND file_names ${file_name})
    endforeach()
    set(file_names "${file_names}" PARENT_SCOPE)
endfunction()

MACRO(SUBDIRLIST result curdir)
    FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
    SET(dirlist "")
    FOREACH(child ${children})
        IF(IS_DIRECTORY ${curdir}/${child})
            LIST(APPEND dirlist ${child})
        ENDIF()
    ENDFOREACH()
    SET(${result} ${dirlist})
ENDMACRO()

function(disallow_in_source_build)
    get_filename_component(src_dir ${CMAKE_SOURCE_DIR} REALPATH)
    get_filename_component(bin_dir ${CMAKE_BINARY_DIR} REALPATH)
    message(STATUS "SOURCE_DIR" ${src_dir})
    message(STATUS "BINARY_DIR" ${bin_dir})
    # Do we maybe want to limit out-of-source builds to be only inside a
    # directory which contains 'build' in name?
    if("${src_dir}" STREQUAL "${bin_dir}")
        # Unfortunately, we cannot remove CMakeCache.txt and CMakeFiles here
        # because they are written after cmake is done.
        message(FATAL_ERROR "In source build is not supported! "
                "Remove CMakeCache.txt and CMakeFiles and then create a separate "
                "directory, e.g. 'build' and run cmake there.")
    endif()
endfunction()

# Takes a string of ';' separated VALUES and stores a new string in RESULT,
# where ';' is replaced with given SEP.
function(join values sep result)
    # Match non escaped ';' and replace it with separator. This doesn't handle
    # the case when backslash is escaped, e.g: "a\\\\;b" will produce "a;b".
    string(REGEX REPLACE "([^\\]|^);" "\\1${sep}" tmp "${values}")
    # Fix-up escapes by matching backslashes and removing them.
    string(REGEX REPLACE "[\\](.)" "\\1" tmp "${tmp}")
    set(${result} "${tmp}" PARENT_SCOPE)
endfunction()

# Returns a list of compile flags ready for gcc or clang.
function(get_target_cxx_flags target result)
    # First set the CMAKE_CXX_FLAGS variables, then append directory and target
    # options in that order. Definitions come last, directory then target.
    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type)
    set(flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type}}")
    get_directory_property(dir_opts DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                           COMPILE_OPTIONS)
    if(dir_opts)
        join("${dir_opts}" " " dir_opts)
        string(APPEND flags " " ${dir_opts})
    endif()
    get_target_property(opts ${target} COMPILE_OPTIONS)
    if(opts)
        join("${opts}" " " opts)
        string(APPEND flags " " ${opts})
    endif()
    get_directory_property(dir_defs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                           COMPILE_DEFINITIONS)
    if(dir_defs)
        join("${dir_defs}" " -D" dir_defs)
        string(APPEND flags " -D" ${dir_defs})
    endif()
    get_target_property(defs ${target} COMPILE_DEFINITIONS)
    if(defs)
        join("${defs}" " -D" defs)
        string(APPEND flags " -D" ${defs})
    endif()
    set(${result} ${flags} PARENT_SCOPE)
endfunction()

# Define `add_capnp` function for registering a capnp file for generation.
#
# The `define_add_capnp` expects 2 arguments:
#   * main_src_files -- variable to be updated with generated cpp files
#   * generated_capnp_files -- variable to be updated with generated hpp and cpp files
#
# The `add_capnp` function expects a single argument, path to capnp file.
# Each added file is standalone and we avoid recompiling everything.
macro(define_add_capnp main_src_files generated_capnp_files)
  function(add_capnp capnp_src_file)
    set(cpp_file ${CMAKE_CURRENT_SOURCE_DIR}/${capnp_src_file}.c++)
    set(h_file ${CMAKE_CURRENT_SOURCE_DIR}/${capnp_src_file}.h)
    add_custom_command(OUTPUT ${cpp_file} ${h_file}
      COMMAND ${CAPNP_EXE} compile -o${CAPNP_CXX_EXE} ${capnp_src_file} -I ${CMAKE_SOURCE_DIR}/src
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${capnp_src_file} capnproto-proj
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # Update *global* generated_capnp_files
    set(${generated_capnp_files} ${${generated_capnp_files}} ${cpp_file} ${h_file} PARENT_SCOPE)
    # Update *global* main_src_files
    set(${main_src_files} ${${main_src_files}} ${cpp_file} PARENT_SCOPE)
  endfunction(add_capnp)
endmacro(define_add_capnp)

# Lisp C++ Preprocessing

set(lcp_exe ${CMAKE_SOURCE_DIR}/tools/lcp)
set(lcp_src_files ${CMAKE_SOURCE_DIR}/src/lisp/lcp.lisp ${lcp_exe})

# Define `add_lcp` function for registering a lcp file for generation.
#
# The `define_add_lcp` expects 2 arguments:
#   * main_src_files -- variable to be updated with generated cpp files
#   * generated_lcp_files -- variable to be updated with generated hpp, cpp and capnp files
#
# The `add_lcp` function expects at least a single argument, path to lcp file.
# Each added file is standalone and we avoid recompiling everything.
# You may pass a CAPNP_SCHEMA <id> keyword argument to generate the Cap'n Proto
# serialization code from .lcp file. You still need to add the generated capnp
# file through `add_capnp` function. To generate the <id> use `capnp id`
# invocation, and specify it here. This preserves correct id information across
# multiple schema generations. If this wasn't the case, wrong typeId
# information will break RPC between different compilations of memgraph.
# Instead of CAPNP_SCHEMA, you may pass the CAPNP_DECLARATION option. This will
# generate serialization declarations but not the implementation.
macro(define_add_lcp main_src_files generated_lcp_files)
  function(add_lcp lcp_file)
    set(options CAPNP_DECLARATION)
    set(one_value_kwargs CAPNP_SCHEMA)
    set(multi_value_kwargs DEPENDS)
    # NOTE: ${${}ARGN} syntax escapes evaluating macro's ARGN variable; see:
    # https://stackoverflow.com/questions/50365544/how-to-access-enclosing-functions-arguments-from-within-a-macro
    cmake_parse_arguments(KW "${options}" "${one_value_kwargs}" "${multi_value_kwargs}" ${${}ARGN})
    string(REGEX REPLACE "\.lcp$" ".hpp" h_file
           "${CMAKE_CURRENT_SOURCE_DIR}/${lcp_file}")
    if (KW_CAPNP_SCHEMA AND NOT KW_CAPNP_DECLARATION)
      string(REGEX REPLACE "\.lcp$" ".capnp" capnp_file
             "${CMAKE_CURRENT_SOURCE_DIR}/${lcp_file}")
      set(capnp_id ${KW_CAPNP_SCHEMA})
      set(capnp_depend capnproto-proj)
      set(cpp_file ${CMAKE_CURRENT_SOURCE_DIR}/${lcp_file}.cpp)
      # Update *global* main_src_files
      set(${main_src_files} ${${main_src_files}} ${cpp_file} PARENT_SCOPE)
    endif()
    if (KW_CAPNP_DECLARATION)
      set(capnp_id "--capnp-declaration")
    endif()
    add_custom_command(OUTPUT ${h_file} ${cpp_file} ${capnp_file}
      COMMAND ${lcp_exe} ${lcp_file} ${capnp_id}
      VERBATIM
      DEPENDS ${lcp_file} ${lcp_src_files} ${capnp_depend} ${KW_DEPENDS}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    # Update *global* generated_lcp_files
    set(${generated_lcp_files} ${${generated_lcp_files}} ${h_file} ${cpp_file} ${capnp_file} PARENT_SCOPE)
  endfunction(add_lcp)
endmacro(define_add_lcp)
