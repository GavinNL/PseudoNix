cmake_minimum_required(VERSION 3.10)


################################################################################
# This Top-level project is for a single Top-level library
#
# A single library
# Unit tests for the library
# A set of executables which use this library
################################################################################



################################################################################
# Project name
#
#  many targets will be created:
#   myProject::warnings
#   myProject:
################################################################################
project( eBash
        VERSION 1.0.0.0
        LANGUAGES CXX)

################################################################################


# The following options are set to TRUE if you are building this project as a top 
# project.
# If you are building it as part of a sub_module, then they are false since you
# dont want to propagate these to your project.
# You can set them individually if you like
option( ${PROJECT_NAME}_BUILD_UNIT_TESTS        "Build the unit tests for this library"                ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_ENABLE_COVERAGE         "Enable Coverage. After build, execute: make coverage" ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_ENABLE_WARNINGS         "Enable Strict Warnings"                               ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_PACKAGE_INCLUDE_SOURCE  "Include source files when building the package"       ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_WARNINGS_AS_ERRORS      "Treat compiler warnings as errors"                    ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_PACKAGE_INCLUDE_HEADERS "Include source headers when building the package"     ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_PACKAGE_INCLUDE_LIBS    "Include Library files when building the package"      ${PROJECT_IS_TOP_LEVEL})
option( ${PROJECT_NAME}_PACKAGE_INCLUDE_BINS    "Include Executable files when building the package"   ${PROJECT_IS_TOP_LEVEL})

include(cmake/extras.cmake)




################################################################################
get_filename_component(CC_FILE_NAME  "${CMAKE_C_COMPILER}"   NAME_WE )
get_filename_component(CXX_FILE_NAME "${CMAKE_CXX_COMPILER}" NAME_WE )

if( "${CC_FILE_NAME}" MATCHES "emcc" OR "${CXX_FILE_NAME}" MATCHES "emcc" OR EMSCRIPTEN)
    message("Compiling with emc")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_SDL=2 -s FETCH=1")
    set(CMAKE_EXECUTABLE_SUFFIX_CXX ".html")
    set(SDL_TARGET "")
else()

    find_package(SDL2 REQUIRED)
    set(SDL_TARGET SDL2::SDL2)
endif()
################################################################################

################################################################################
# Find all the packages we need
#
# Each module in the src/ folder can also define its own find_package 
# if they are meant to be private
#
################################################################################
#find_package(fmt REQUIRED)
#find_package(stb REQUIRED)
#find_package(glm REQUIRED)
################################################################################



################################################################################
# Top level INTERFACE library which can be used to provide all the
# information for all targets built with this t
################################################################################
set(outName              interface)   # name of the library within the proje
                                            # Will create the target ${PROJECT_NAME}::${outName}

set(PublicLinkedTargets  "" )               # Any additional public targets you want to include
#-------------------------------------------------------------------------------
add_library( ${outName} INTERFACE )
add_library( ${PROJECT_NAME}::${outName}  ALIAS ${outName})

target_compile_features( ${outName}
                          INTERFACE
                              cxx_std_20)

target_compile_definitions( ${outName}
                              INTERFACE
                                CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                                CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
                                )

target_include_directories(${outName} INTERFACE include)
target_link_libraries( ${outName}  INTERFACE  ${PublicLinkedTargets})

################################################################################


add_executable(main main.cpp)
target_link_libraries(main ${PROJECT_NAME}::interface)


add_executable(terminal terminal.cpp)
target_link_libraries(terminal ${PROJECT_NAME}::interface ${SDL_TARGET})
target_include_directories(terminal PUBLIC
                                    # For ImGui, set them as system includes
                                    # so the compiler doesnt warn about errors
                                    SYSTEM
                                    ${CMAKE_BINARY_DIR}/imgui_src/include
                                    ${CMAKE_BINARY_DIR}/imgui_src/res/bindings
                                    ${CMAKE_BINARY_DIR}/imgui_src/res
                                    ${CMAKE_BINARY_DIR}/imgui_src/res/misc/cpp
                          )

# enable testing on the root folder so that we can run ctest from it,
# otherwise we will have to CD into each sub folder to run the unit tests
if( ${PROJECT_NAME}_BUILD_UNIT_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
