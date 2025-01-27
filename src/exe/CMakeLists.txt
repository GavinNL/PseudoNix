cmake_minimum_required(VERSION 3.10)


################################################################################
# Find all the packages we need
################################################################################
find_package(stb REQUIRED)

MakeMod(PUBLIC_TARGETS
            ${PROJECT_NAME}::interface
            stb::stb
        PRIVATE_TARGETS
            ${PROJECT_NAME}::warnings
        PUBLIC_DEFINITIONS
            "HELLO=32"
        PRIVATE_DEFINITIONS
            "HELLO=32"
    )
################################################################################



