// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#pragma once

#include <boost/predef.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <boost/filesystem.hpp>

namespace tipi::goldilock::file {
  using namespace std::string_literals;
  namespace fs = boost::filesystem;

  inline std::string read_file_content(const char *filename) {
    std::ifstream t(filename, std::ios_base::binary);
    std::string str;

    t.seekg(0, std::ios::end);   
    str.reserve(t.tellg());
    t.seekg(0, std::ios::beg);
    str.assign((std::istreambuf_iterator<char>(t)),
                std::istreambuf_iterator<char>());

    return str;
  }

  inline std::string read_file_content(const std::string& filename) {
    return read_file_content(filename.data());
  }

  inline std::string read_file_content(const boost::filesystem::path& filename) {
    return read_file_content(filename.generic_string());
  }

  inline void touch_file(const boost::filesystem::path& path) {
    // by testing if we can trunc it we'll know if we can write to it...
    std::fstream ofs(path.generic_string(), std::ios::out | std::ios::trunc | std::ios::in | std::ios::binary);

    if(!ofs.is_open() || ofs.bad()) {
      throw std::runtime_error("Failed to touch file '"s + path.generic_string() + "'"s);
    }

    ofs.close();
  }

  //!\brief same as touch_file() except the files get chmod-ed 666 so that multiple users can share the file
  inline void touch_file_permissive(const boost::filesystem::path& path) {
    bool set_perms = !fs::exists(path);

    touch_file(path);
    
    if(set_perms) {    
      // we *might* get to the seldom case that someones else (re)created the file in beween us touching it and
      // thus has the ownership.
      boost::system::error_code ec;
      fs::permissions(path, fs::add_perms|fs::owner_write|fs::group_write|fs::others_write, ec);
    }   
  }
}