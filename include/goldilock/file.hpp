// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <filesystem>

namespace tipi::goldilock::file {

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

  inline std::string read_file_content(const std::filesystem::path& filename) {
    return read_file_content(filename.generic_string());
  }

}