cmake_minimum_required(VERSION 3.10)

################################################################################
# Find all the packages we need
################################################################################
#find_package(stb REQUIRED)

MakeMod(
        # NAME               # Optional
        #     srcA           #   Default is name of folder
        # SRC_FILES          # OPTIONAL
        #    src/lib.cpp     #   Default is all files in src
        # OUTPUT_TARGET_TYPE # OPTIONAL
        #     "INTERFACE"    #   "LIBRARY"    if src/lib.cpp exists, 
                             #   "EXECUTABLE" if src/main.cpp exists
                             #   "INTERFACE " if not LIBRARY and not EXECUTABLE
        PUBLIC_TARGETS
#           ${PROJECT_NAME}::interface 
        PRIVATE_TARGETS
            ${PROJECT_NAME}::coverage # add coverage
            ${PROJECT_NAME}::warnings # make sure all warnings are turned on
        PUBLIC_DEFINITIONS
            "HELLO=32"
        PRIVATE_DEFINITIONS
            "PRIVATE_HELLO=32"
    )

# All src files in the test folder will be
# compiled into an executable and executed as a 
# test. If you want more fine grain control over
# how the test is executed, you will have to do that manually
MakeTests(PUBLIC_TARGETS
            headerOnly
            fmt::fmt
            ${PROJECT_NAME}::warnings
          PRIVATE_DEFINITIONS
             MY_UNIT_TEST_DEF="Hello world"
)
################################################################################



