// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary
#pragma once

#include <cctype>
#include <algorithm>
#include <string>

namespace tipi::goldilock::string {
  
  inline bool ichar_equals(char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) ==
           std::tolower(static_cast<unsigned char>(b));
  }

  inline bool iequals(const std::string& a, const std::string& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), ichar_equals);
  }

  inline bool ends_with(std::string const & value, std::string const & ending) {
    return (value.size() > ending.size()) 
      && std::equal(ending.rbegin(), ending.rend(), value.rbegin());
  }

  inline bool iends_with(std::string const & value, std::string const & ending) {
    return (value.size() > ending.size()) 
      && std::equal(ending.rbegin(), ending.rend(), value.rbegin(), ichar_equals);
  }
}