cmake_minimum_required(VERSION 3.10)

################################################################################
# Find all the packages we need
################################################################################
find_package(stb REQUIRED)

MakeMod(
        # NAME               # Optional
        #     libA           #   Default is name of folder
        # SRC_FILES          # OPTIONAL
        #    src/lib.cpp     #   Default is all files in src
        # OUTPUT_TARGET_TYPE # OPTIONAL
        #     "LIBRARY"      #   "LIBRARY"    if src/lib.cpp exists, 
                             #   "EXECUTABLE" if src/main.cpp exists
                             #   "INTERFACE " if not LIBRARY and not EXECUTABLE
        PUBLIC_TARGETS
            ${PROJECT_NAME}::interface
            stb::stb
        PRIVATE_TARGETS
            ${PROJECT_NAME}::coverage # add coverage
            ${PROJECT_NAME}::warnings
        PUBLIC_DEFINITIONS
            "HELLO=32"
        PRIVATE_DEFINITIONS
            "PRIVATE_HELLO=32"
    )

# All src files in the test folder will be
# compiled into an executable and executed as a 
# test
MakeTests(PUBLIC_TARGETS
            ${PROJECT_NAME}::libA
            fmt::fmt
            ${PROJECT_NAME}::warnings
          PRIVATE_DEFINITIONS
             MY_UNIT_TEST_DEF="Hello world"
)
################################################################################



