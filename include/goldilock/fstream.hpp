// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <boost/filesystem.hpp>

namespace tipi::goldilock::exclusive_fstream
{

  struct FILE_closer {
    void operator()(std::FILE *fp) const { std::fclose(fp); }
  };

  inline std::fstream open(const char *filename)
  {
    bool excl = [filename] {
      std::unique_ptr<std::FILE, FILE_closer> fp(std::fopen(filename, "wx"));
      return !!fp;
    }();
    auto saveerr = errno;

    std::fstream stream;

    if (excl) {
      stream.open(filename);
    }
    else {
      stream.setstate(std::ios::failbit);
      errno = saveerr;
    }

    return stream;
  }

  inline std::fstream open(const std::string& filename) {
    return open(filename.data());
  }

  inline std::fstream open(const boost::filesystem::path& filename) {
    return open(filename.generic_string());
  }
}