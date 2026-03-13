cmake_minimum_required(VERSION 3.10)

################################################################################
# Project name
#
#  many targets will be created:
#   myProject::warnings
#   myProject:
#
#
#  add_subdirectory(third_party/PseudoNix)
#  target_link_libraries(main PseudoNix::PseudoNix)
#
################################################################################
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project( PseudoNix
         VERSION 0.1.0.0
         LANGUAGES CXX C)

################################################################################

if(PROJECT_IS_TOP_LEVEL)
    # automatically call conan install
    # if using --presets
    #if(CC_ENABLE)
    #    include(CMakeAutoConan.cmake)
    #    cc_configure()
    #endif()
    include(cmake/extras.cmake)
endif()


# The following options are set to TRUE if you are building this project as a top 
# project.
# If you are building it as part of a sub_module, then they are false since you
# dont want to propagate these to your project.
# You can set them individually if you like
option( ${PROJECT_NAME}_BUILD_UNIT_TESTS        "Build the unit tests for this library"                ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_BUILD_EXAMPLES          "Build examples"                                       ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_ENABLE_COVERAGE         "Enable Coverage. After build, execute: make coverage" ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_ENABLE_WARNINGS         "Enable Strict Warnings"                               ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_WARNINGS_AS_ERRORS      "Treat compiler warnings as errors"                    ${PROJECT_IS_TOP_LEVEL})


################################################################################
# Top level INTERFACE library which can be used to provide all the
# information for all targets built with this to
################################################################################
add_library( ${PROJECT_NAME} INTERFACE )
add_library( ${PROJECT_NAME}::${PROJECT_NAME}  ALIAS ${PROJECT_NAME})

target_compile_features( ${PROJECT_NAME}
                          INTERFACE
                              cxx_std_20)

target_compile_definitions( ${PROJECT_NAME}
                              INTERFACE
#                                CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
#                                CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
                                )

target_include_directories(${PROJECT_NAME} INTERFACE include)

if(PROJECT_IS_TOP_LEVEL)
    # if we're the top level project
    # enable warnings/errors for the project
    set_project_warnings(${PROJECT_NAME})

    add_library(${PROJECT_NAME}_coverage INTERFACE)
    set_project_coverage(${PROJECT_NAME}_coverage)
    set(ADDITIONAL_TEST_LIBRARIES ${PROJECT_NAME}_coverage)
endif()
################################################################################



if( ${PROJECT_NAME}_BUILD_EXAMPLES OR ${PROJECT_NAME}_BUILD_UNIT_TESTS)
    find_package(readerwriterqueue REQUIRED)
    find_package(concurrentqueue   REQUIRED)

    set(third_party_libs readerwriterqueue::readerwriterqueue concurrentqueue::concurrentqueue )
    if(NOT EMSCRIPTEN)
        find_package(LibArchive        REQUIRED)
        find_package(ZLIB              REQUIRED)
        set(third_party_libs ${third_party_libs} LibArchive::LibArchive ZLIB::ZLIB )
    endif()
endif()



if( ${PROJECT_NAME}_BUILD_EXAMPLES)

    ################################################################################
    get_filename_component(CC_FILE_NAME  "${CMAKE_C_COMPILER}"   NAME_WE )
    get_filename_component(CXX_FILE_NAME "${CMAKE_CXX_COMPILER}" NAME_WE )

    if( "${CC_FILE_NAME}" MATCHES "emcc" OR "${CXX_FILE_NAME}" MATCHES "emcc" OR EMSCRIPTEN)
        #message("Compiling with emc")
        #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_SDL=2")
        #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s STANDALONE_WASM=1 -s WASM_BIGINT=1")
        #set(CMAKE_EXECUTABLE_SUFFIX_CXX ".html")
        set(SDL_TARGET "")
    else()
        find_package(SDL2 REQUIRED)
        set(SDL_TARGET SDL2::SDL2 SDL2::SDL2main)
    endif()
    ################################################################################

    find_package(ImGui REQUIRED)
    
    add_executable(terminal examples/terminal.cpp)
    target_link_libraries(terminal ${PROJECT_NAME}::${PROJECT_NAME} ${SDL_TARGET}  ${third_party_libs})
    target_include_directories(terminal SYSTEM
                                        # For ImGui, set them as system includes
                                        # so the compiler doesnt warn about errors
                                        PUBLIC
                                            ${CMAKE_BINARY_DIR}/conan/imgui_src/include
                                            ${CMAKE_BINARY_DIR}/conan/imgui_src/res/bindings
                                            ${CMAKE_BINARY_DIR}/conan/imgui_src/res
                                            ${CMAKE_BINARY_DIR}/conan/imgui_src/res/misc/cpp
                                            ${CMAKE_BINARY_DIR}/archive
                              )

    if(EMSCRIPTEN)
        target_compile_options(terminal PRIVATE "-sUSE_SDL=2")
        target_link_options(   terminal PRIVATE "-sUSE_SDL=2")
        set_target_properties( terminal PROPERTIES SUFFIX ".html")

        add_executable(js_terminal examples/js_terminal.cpp)
        set_target_properties( js_terminal PROPERTIES SUFFIX ".html")
        target_link_libraries(js_terminal ${PROJECT_NAME}::${PROJECT_NAME} ${third_party_libs} )
        target_link_options(js_terminal PRIVATE
            --no-entry
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=64MB
            -sMAXIMUM_MEMORY=512MB
            -sENVIRONMENT=web
            #-sSTANDALONE_WASM=1
            -sEXPORTED_FUNCTIONS=['_malloc','_free','_init','_update','_input']
            -sEXPORTED_RUNTIME_METHODS=['cwrap','ccall'])
    else()

        add_executable(main examples/main.cpp)
        target_link_libraries(main ${PROJECT_NAME}::${PROJECT_NAME} ${third_party_libs} )
        target_include_directories(main PUBLIC
                                            ${CMAKE_BINARY_DIR}/archive
                                  )

        add_executable(example1 examples/example1.cpp)
        target_link_libraries(example1 ${PROJECT_NAME}::${PROJECT_NAME} ${third_party_libs} )

        add_executable(example2 examples/example2.cpp)
        target_link_libraries(example2 ${PROJECT_NAME}::${PROJECT_NAME} ${third_party_libs} )

        add_executable(example3 examples/example3.cpp)
        target_link_libraries(example3 ${PROJECT_NAME}::${PROJECT_NAME} ${third_party_libs} )

        add_executable(example4 examples/example4.cpp)
        target_link_libraries(example4 ${PROJECT_NAME}::${PROJECT_NAME} ${third_party_libs} )

    endif()

endif()


if(NOT EMSCRIPTEN)
    # enable testing on the root folder so that we can run ctest from it,
    # otherwise we will have to CD into each sub folder to run the unit tests
    if( ${PROJECT_NAME}_BUILD_UNIT_TESTS)
        enable_testing()
        add_subdirectory(test)
    endif()
endif()
