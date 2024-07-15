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

  const char* ws = " \t\n\r\f\v";

  inline std::string& rtrim(std::string& s, const char* t = ws) {
      s.erase(s.find_last_not_of(t) + 1);
      return s;
  }

  inline std::string& ltrim(std::string& s, const char* t = ws) {
      s.erase(0, s.find_first_not_of(t));
      return s;
  }

  inline std::string& trim(std::string& s, const char* t = ws) {
      return ltrim(rtrim(s, t), t);
  }
}