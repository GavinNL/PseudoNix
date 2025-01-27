#include <catch2/catch_all.hpp>
#include <iostream>
#include <fmt/format.h>
#include <headerOnly/header.h>

SCENARIO( " Scenario 1" )
{
    std::cout << fmt::format("Top-lev Source Directory: {}\n", CMAKE_SOURCE_DIR);
    std::cout << fmt::format("Current Source Directory: {}\n", CMAKE_CURRENT_SOURCE_DIR);
    std::cout << fmt::format("Current Binary Directory: {}\n", CMAKE_CURRENT_BINARY_DIR);
    std::cout << fmt::format("MY_UNIT_TEST_DEF        : {}\n", MY_UNIT_TEST_DEF);

    REQUIRE(func_header_only() == 32);

    REQUIRE(HELLO == 32);
}
