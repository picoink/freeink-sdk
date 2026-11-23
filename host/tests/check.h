#pragma once
#include <cstdlib>
#include <iostream>
#define CHECK(condition)                                                                 \
  do {                                                                                   \
    if (!(condition)) {                                                                  \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #condition << '\n'; \
      std::exit(EXIT_FAILURE);                                                           \
    }                                                                                    \
  } while (false)
