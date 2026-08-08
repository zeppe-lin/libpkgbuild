// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <iostream>

#define TEST_CHECK(expr)                                                       \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::cerr << __FILE__ << ':' << __LINE__                                 \
                << ": check failed: " #expr << '\n';                          \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

#define TEST_PKGBUILD_THROWS(code_value, expr)                                 \
  do {                                                                         \
    bool caught_ = false;                                                      \
    try {                                                                      \
      (void)(expr);                                                            \
    } catch (const pkgbuild::error& error_) {                                  \
      caught_ = true;                                                          \
      TEST_CHECK(error_.code() == (code_value));                               \
    }                                                                          \
    TEST_CHECK(caught_);                                                       \
  } while (false)
