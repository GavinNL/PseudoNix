# cpp2024

This is a sample C++ project template with the following features:

* Conan 2.4 for third party packages
* CMake for build 
* Sub libraries and executables
* Unit testing via Catch2
* Coverage Reports [![coverage report](https://gitlab.com/GavinNL/cpp2024/badges/master/coverage.svg)](https://gitlab.com/GavinNL/cpp2024/-/commits/master)
* Packaging (DEB, RPM)
* Continuous Integration for Linux and Windows builds
  * Gitlab-CI [![pipeline status](https://gitlab.com/GavinNL/cpp2024/badges/master/pipeline.svg)](https://gitlab.com/GavinNL/cpp2024/-/commits/master)
  * Github Workflows ![example workflow](https://github.com/GavinNL/cpp2024/actions/workflows/cmake-multi-platform.yml/badge.svg)

## Usage

Edit the top level `CMakeLists.txt` and change the project name. 

```bash
cd SRC_FOLDER
mkdir build && cd build

# Optional: Set which compiler you want to use (ie: clang/clang++)
export CXX=$(which g++)
export CC=$(which gcc)

# Set up your conan profile based on the current
# environment variables
conan profile detect --force 

# execute conan to install the packages you need
conan install --build missing \
              -s "compiler.cppstd=20" \
              -s:h "&:build_type=Debug" \ 
              -of=$PWD ../conanfile.txt 

# Note; the -s:h "&:build_type=Debug" is required if you want
# to use the packages in Release mode, but build your project in Debug mode

# Run cmake
cmake --preset conan-debug ..

# Build
cmake --build . --parallel

# Test
ctest

# Run coverage report (linux/gcc only. Requires gcovr)
cmake --build . --target coverage 


# Build packages
cpack -G DEB

cpack -G RPM
```

# Structure

This project is modeled after how Rust handles it packages. 
All modules (libraries/executables) that are to be built are in the `src` folder.

Each folder is a separate target whose name is the name of the folder


## Modules

A top level project can have multiple modules. These are executables, libraries, or header-only libraries.

There are three sample modules created in the `src` folder. Copy the one you want and rename the folder.

The module will be built according to the folder name.

The `CMakeLists.txt` file that is used for the module has the following:


```cmake
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

        # Targets you want to link to
        PUBLIC_TARGETS
            ${PROJECT_NAME}::interface
            stb::stb
        PRIVATE_TARGETS
            ${PROJECT_NAME}::coverage # add coverage
            ${PROJECT_NAME}::warnings

        # Compile time definitions
        PUBLIC_DEFINITIONS
            "HELLO=32"
        PRIVATE_DEFINITIONS
            "PRIVATE_HELLO=32"
    )


# Use this command to generate unit tests
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
```

# CMake Targets

There are a few cmake build targets that are created

## Coverage (Linux/gcc only)

You can run coverage reports if `-D[PROJECT_NAME]_ENABLE_COVER=True` is set.

```bash
cmake --build . --target coverage
```

This will generate the `coverage/report.xml` file as well as print the report to the terminal.

# Continuous Integration

CI pipelines for Gitlab-CI and Github Workflows are created for you. 

## Gitlab-CI

* ubuntu-22.04-gcc-11
* ubuntu-22.04-clang-14
* ubuntu-24.04-gcc-13
* ubuntu-24.04-clang-18

## Github Workflows

* ubuntu-22.04-gcc-11
* ubuntu-22.04-clang-14
* Windows-latest [Visual Studios 17]