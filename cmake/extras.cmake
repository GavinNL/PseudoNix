message("*****************************************************")
message("EXTRA TARGETS:")
message("*****************************************************")

add_library( ${PROJECT_NAME}_coverage INTERFACE)
add_library( ${PROJECT_NAME}::coverage ALIAS ${PROJECT_NAME}_coverage)

add_library(${PROJECT_NAME}_warnings INTERFACE)
add_library(${PROJECT_NAME}::warnings ALIAS ${PROJECT_NAME}_warnings)

add_library(${PROJECT_NAME}_warnings_error INTERFACE)
add_library(${PROJECT_NAME}::error ALIAS ${PROJECT_NAME}_warnings_error)
target_compile_options(${PROJECT_NAME}_warnings_error INTERFACE -Werror)

if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)

    target_compile_options(${PROJECT_NAME}_coverage
                                INTERFACE
                                    --coverage -g -O0 -fprofile-arcs -ftest-coverage)

    target_link_libraries( ${PROJECT_NAME}_coverage
                            INTERFACE --coverage -g -O0 -fprofile-arcs -ftest-coverage)



    add_custom_target(coverage
        COMMAND rm -rf coverage
        COMMAND mkdir -p coverage
        #COMMAND ${CMAKE_MAKE_PROGRAM} test
        #COMMAND gcovr . -r ${CMAKE_SOURCE_DIR} --html-details --html -o coverage/index.html -e ${CMAKE_SOURCE_DIR}/test/third_party;
        COMMAND gcovr . -r ${CMAKE_SOURCE_DIR} --xml -o coverage/report.xml -e ${CMAKE_SOURCE_DIR}/third_party;
        COMMAND gcovr . -r ${CMAKE_SOURCE_DIR} -o coverage/report.txt -e ${CMAKE_SOURCE_DIR}/third_party;
        COMMAND cat coverage/report.txt

        #COMMAND lcov --no-external --capture --directory ${CMAKE_BINARY_DIR} --output-file coverage2.info
        #COMMAND genhtml coverage.info --output-directory lcov-report
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR} 
    )

endif()

    
#==========
# from here:
#
# https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md

function(set_project_warnings project_name)
  option(${PROJECT_NAME}_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" TRUE)

  set(MSVC_WARNINGS
      /W4 # Baseline reasonable warnings
      /w14242 # 'identfier': conversion from 'type1' to 'type1', possible loss
              # of data
      /w14254 # 'operator': conversion from 'type1:field_bits' to
              # 'type2:field_bits', possible loss of data
      /w14263 # 'function': member function does not override any base class
              # virtual member function
      /w14265 # 'classname': class has virtual functions, but destructor is not
              # virtual instances of this class may not be destructed correctly
      /w14287 # 'operator': unsigned/negative constant mismatch
      /we4289 # nonstandard extension used: 'variable': loop control variable
              # declared in the for-loop is used outside the for-loop scope
      /w14296 # 'operator': expression is always 'boolean_value'
      /w14311 # 'variable': pointer truncation from 'type1' to 'type2'
      /w14545 # expression before comma evaluates to a function which is missing
              # an argument list
      /w14546 # function call before comma missing argument list
      /w14547 # 'operator': operator before comma has no effect; expected
              # operator with side-effect
      /w14549 # 'operator': operator before comma has no effect; did you intend
              # 'operator'?
      /w14555 # expression has no effect; expected expression with side- effect
#      /w14619 # pragma warning: there is no warning number 'number'
      /w14640 # Enable warning on thread un-safe static member initialization
      /w14826 # Conversion from 'type1' to 'type_2' is sign-extended. This may
              # cause unexpected runtime behavior.
      /w14905 # wide string literal cast to 'LPSTR'
      /w14906 # string literal cast to 'LPWSTR'
      /w14928 # illegal copy-initialization; more than one user-defined
              # conversion has been implicitly applied

      # Disable the following errors
      /wd4101
      /wd4201

  )

  set(CLANG_WARNINGS
      -Wall
      -Wextra # reasonable and standard
      -Wshadow # warn the user if a variable declaration shadows one from a
               # parent context
      -Wnon-virtual-dtor # warn the user if a class with virtual functions has a
                         # non-virtual destructor. This helps catch hard to
                         # track down memory errors
      -Wold-style-cast # warn for c-style casts
      -Wcast-align # warn for potential performance problem casts
      -Wunused # warn on anything being unused
      -Woverloaded-virtual # warn if you overload (not override) a virtual
                           # function
      -Wpedantic # warn if non-standard C++ is used
      -Wconversion # warn on type conversions that may lose data
      -Wsign-conversion # warn on sign conversions
      -Wnull-dereference # warn if a null dereference is detected
      -Wdouble-promotion # warn if float is implicit promoted to double
      -Wformat=2 # warn on security issues around functions that format output
                 # (ie printf)
      -Wno-gnu-zero-variadic-macro-arguments
  )

  if (${PROJECT_NAME}_WARNINGS_AS_ERRORS)
      set(CLANG_WARNINGS ${CLANG_WARNINGS} -Werror)
      set(MSVC_WARNINGS ${MSVC_WARNINGS} /WX )
  endif()

  set(GCC_WARNINGS
      ${CLANG_WARNINGS}
      -Wmisleading-indentation # warn if identation implies blocks where blocks
                               # do not exist
      -Wduplicated-cond # warn if if / else chain has duplicated conditions
      -Wduplicated-branches # warn if if / else branches have duplicated code
      -Wlogical-op # warn about logical operations being used where bitwise were
                   # probably wanted
#      -Wuseless-cast # warn if you perform a cast to the same type
      -Wno-class-memaccess
      -Wno-stringop-overflow
      -Wsuggest-override
      -Wno-gnu-zero-variadic-macro-arguments
  )

  if(MSVC)
    set(_PROJECT_WARNINGS ${MSVC_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(_PROJECT_WARNINGS ${CLANG_WARNINGS})
  else()
    set(_PROJECT_WARNINGS ${GCC_WARNINGS})
  endif()

  get_target_property(type ${project_name} TYPE)

  if (${type} STREQUAL "INTERFACE_LIBRARY")
      target_compile_options(${project_name} INTERFACE ${_PROJECT_WARNINGS})
  else()
      target_compile_options(${project_name} PRIVATE ${_PROJECT_WARNINGS})
  endif()


endfunction()
#==========

set_project_warnings(${PROJECT_NAME}_warnings)

message("New Target: ${PROJECT_NAME}::coverage")
message("New Target: ${PROJECT_NAME}::warnings")
message("New Target: ${PROJECT_NAME}::error")
message("*****************************************************\n\n\n")







###################################################################
# MakeMod function
#
# This function creates a new "module" (not a c++20 module, a Rust-like
# module.
#
# Source files are compiled into a library if src/lib.cpp exists
# Source files are compiled into an executble if src/main.cpp exists
#
# Optional Variables:
#
# MakeMod(PUBLIC_TARGETS
#             ${PROJECT_NAME}::interface
#             KTX::ktx
#             stb::stb
#         PRIVATE_TARGETS
#             KTX::ktx
#         PUBLIC_DEFINITIONS
#             "HELLO=32"
#         PRIVATE_DEFINITIONS
#             "HELLO=32"
#     )
###################################################################



function(MakeTests)
    # Parse arguments
    set(options)
    set(oneValueArgs)
    set(multiValueArgs PUBLIC_TARGETS PRIVATE_TARGETS PUBLIC_DEFINITIONS PRIVATE_DEFINITIONS)
    cmake_parse_arguments(MAKE_TESTS "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_filename_component(library_MODULE ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    string(REPLACE " " "_" library_MODULE ${library_MODULE})

    if( NOT ${PROJECT_NAME}_BUILD_UNIT_TESTS OR NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/test")
        return()
    endif()

    find_package(Catch2 REQUIRED)
    enable_testing()

    file(GLOB files "test/*.cpp")

    set(TESTCASE_PREFIX "${PROJECT_NAME}-${library_MODULE}")

    message("\n    UNIT TESTS:")
    foreach(file ${files})

        get_filename_component(file_basename ${file} NAME_WE)

        set(UNIT_EXE_NAME  ${PROJECT_NAME}-${library_MODULE}-${file_basename} )
        set(UNIT_TEST_NAME ${PROJECT_NAME}-${library_MODULE}-${file_basename} )

        add_executable( ${UNIT_EXE_NAME} ${file} )

        target_compile_definitions(${UNIT_EXE_NAME} PRIVATE
                                        CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                                        CMAKE_CURRENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test"
                                        CMAKE_CURRENT_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}/test")

        message("        ${UNIT_EXE_NAME}")
        target_link_libraries( ${UNIT_EXE_NAME} PUBLIC
                                                    Catch2::Catch2WithMain
                                                    ${UNIT_TEST_LINK_TARGETS}
                                                    ${MAKE_TESTS_PRIVATE_TARGETS}
                                                    ${MAKE_TESTS_PUBLIC_TARGETS})

        target_compile_definitions(${UNIT_EXE_NAME} PRIVATE
                                        ${MAKE_TESTS_PRIVATE_DEFINITIONS})

        target_compile_definitions(${UNIT_EXE_NAME} PRIVATE
                                        ${MAKE_TESTS_PUBLIC_DEFINITIONS})

        add_test( NAME ${UNIT_TEST_NAME}
                  COMMAND ${UNIT_EXE_NAME}
        )

        list(APPEND unitTestsList ${UNIT_EXE_NAME})

        set_target_properties(${UNIT_EXE_NAME}
            PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
        )
    endforeach()
    message("*****************************************************")


endfunction()


function(MakeMod)
    # Parse arguments
    set(options)
    set(oneValueArgs NAME OUTPUT_TARGET_TYPE)
    set(multiValueArgs PUBLIC_TARGETS PRIVATE_TARGETS PUBLIC_DEFINITIONS PRIVATE_DEFINITIONS SRC_FILES)
    cmake_parse_arguments(MAKE_MOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(library_MODULE "${MAKE_MOD_NAME}")

    if("${library_MODULE}" STREQUAL "")
        get_filename_component(library_MODULE ${CMAKE_CURRENT_SOURCE_DIR} NAME)
        string(REPLACE " " "_" library_MODULE ${library_MODULE})
    endif()


    list(LENGTH MAKE_MOD_SRC_FILES list_length)
    if(list_length EQUAL 0)
        message(STATUS "SRC_FILES var not set. Using all src files in folder")
        file(GLOB_RECURSE srcFiles ${CMAKE_CURRENT_SOURCE_DIR}/src/* )
    else()
        set(srcFiles ${MAKE_MOD_SRC_FILES})
        message(STATUS "The list is not empty.")
    endif()

    if( "${MAKE_MOD_OUTPUT_TARGET_TYPE}" STREQUAL "")

        # User did not set OUTPUT_TARGET_TYPE. try to infer
        if( EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
            set(MAKE_MOD_OUTPUT_TARGET_TYPE "EXECUTABLE")
        elseif( EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/lib.cpp)
            set(MAKE_MOD_OUTPUT_TARGET_TYPE "LIBRARY")
        elseif(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include" AND NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src)
            set(MAKE_MOD_OUTPUT_TARGET_TYPE "INTERFACE")
        else()
            message(FATAL_ERROR "If OUTPUT_TARGET_TYPE is not empty, then it must be either: \"EXECUTABLE\", \"LIBRARY\", \"HEADER_ONLY_LIBRARY\" ")
        endif()
    endif()


    message("*****************************************************")
    if( "${MAKE_MOD_OUTPUT_TARGET_TYPE}" STREQUAL "INTERFACE")

        message(" INTERFACE LIBRARY: ${PROJECT_NAME}::${library_MODULE}")
        # Header only library
        add_library( ${library_MODULE} INTERFACE )
        add_library( ${PROJECT_NAME}::${library_MODULE} ALIAS ${library_MODULE} )

        target_include_directories( ${library_MODULE}
                                    INTERFACE
                                       "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

        target_compile_definitions(${library_MODULE} INTERFACE ${MAKE_MOD_PRIVATE_DEFINITIONS})
        target_compile_definitions(${library_MODULE} INTERFACE  ${MAKE_MOD_PUBLIC_DEFINITIONS})
    else()

        if( EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/lib.cpp AND
            EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

            message(FATAL_ERROR "Both lib.cpp and main.cpp exist in ${CMAKE_CURRENT_SOURCE_DIR}/src. Only one can exist. If lib.cpp exists, then this module will be compiled into a library. If main.cpp exists, then this module will be compiled into an executable")
        endif()


        message(STATUS " TARGET TYPE: ---${MAKE_MOD_OUTPUT_TARGET_TYPE}---")

        if( "${MAKE_MOD_OUTPUT_TARGET_TYPE}" STREQUAL "LIBRARY")

            message(" LIBRARY: ${PROJECT_NAME}::${library_MODULE}")

            list(APPEND ${PROJECT_NAME}_ALL_LIBS ${subdir})

            add_library( ${library_MODULE} ${srcFiles} )
            add_library( ${PROJECT_NAME}::${library_MODULE} ALIAS ${library_MODULE} )

            target_compile_definitions(${library_MODULE} PRIVATE ${MAKE_MOD_PRIVATE_DEFINITIONS})
            target_compile_definitions(${library_MODULE} PUBLIC  ${MAKE_MOD_PUBLIC_DEFINITIONS})

            target_include_directories( ${library_MODULE}
                                            PUBLIC
                                                "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

            if( ${PROJECT_NAME}_PACKAGE_INCLUDE_LIBS )

                install(
                    TARGETS
                       ${library_MODULE}
                    LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                    ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                    RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
                    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
                )

            endif()

        elseif( "${MAKE_MOD_OUTPUT_TARGET_TYPE}" STREQUAL "EXECUTABLE")

            message(" EXECUTABLE: ${library_MODULE}")
            add_executable( ${library_MODULE} ${srcFiles} )

            if(  ${PROJECT_NAME}_PACKAGE_INCLUDE_BINS )

                install(
                    TARGETS
                       ${library_MODULE}
                    LIBRARY  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                    ARCHIVE  DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                    RUNTIME  DESTINATION "${CMAKE_INSTALL_BINDIR}"
                    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
                )

            endif()

            target_compile_definitions(${library_MODULE} PRIVATE ${MAKE_MOD_PRIVATE_DEFINITIONS})
            target_compile_definitions(${library_MODULE} PRIVATE  ${MAKE_MOD_PUBLIC_DEFINITIONS})

        else()
            message(FATAL_ERROR "Invalid OUTPUT_TARGET_TYPE: ${MAKE_MOD_OUTPUT_TARGET_TYPE}")
        endif()

        target_link_libraries(${library_MODULE} PUBLIC  ${MAKE_MOD_PUBLIC_TARGETS} )
        target_link_libraries(${library_MODULE} PRIVATE ${MAKE_MOD_PRIVATE_TARGETS} )

        message("    SOURCE FILES:")
        foreach(target ${srcFiles})
            message("        ${target}")
        endforeach()

        message("    PUBLIC TARGETS:")
        foreach(target ${MAKE_MOD_PUBLIC_TARGETS})
            message("        ${target}")
        endforeach()

        message("    PRIVATE TARGETS:")
        foreach(target ${MAKE_MOD_PRIVATE_TARGETS})
            message("        ${target}")
        endforeach()

        message("    PUBLIC DEFINITIONS:")
        foreach(target ${MAKE_MOD_PUBLIC_DEFINITIONS})
            message("        ${target}")
        endforeach()

        message("    PRIVATE DEFINITIONS:")
        foreach(target ${MAKE_MOD_PRIVATE_DEFINITIONS})
            message("        ${target}")
        endforeach()

        if( ${PROJECT_NAME}_ENABLE_COVERAGE )
          #  target_link_libraries( ${library_MODULE}  PRIVATE   ${PROJECT_NAME}::coverage  )
        endif()

        if( ${PROJECT_NAME}_ENABLE_WARNINGS )
        #    target_link_libraries( ${library_MODULE}  PRIVATE   ${PROJECT_NAME}::warnings )
        endif()

        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include")

           if(${PROJECT_NAME}_PACKAGE_INCLUDE_HEADERS )
               install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include
                       DESTINATION ${CMAKE_INSTALL_PREFIX}
                       FILES_MATCHING PATTERN "*"
               )
           endif()

        endif()

        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/share")

           install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/share
                   DESTINATION ${CMAKE_INSTALL_PREFIX}
                   FILES_MATCHING PATTERN "*"
           )

        endif()

    endif()

endfunction()