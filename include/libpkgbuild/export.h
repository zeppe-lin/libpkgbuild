// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(PKGBUILD_BUILDING_LIBRARY)
#define PKGBUILD_API __declspec(dllexport)
#else
#define PKGBUILD_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define PKGBUILD_API __attribute__((visibility("default")))
#else
#define PKGBUILD_API
#endif
