// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#pragma once

#include <boost/predef.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <boost/filesystem.hpp>

#if BOOST_OS_LINUX
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#elif BOOST_OS_WINDOWS
#error No impl yet
#endif


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

  //
  //
  inline void touch_file(const boost::filesystem::path& path) {
    std::fstream ofs(path.generic_string(), std::ios::out | std::ios::trunc | std::ios::in | std::ios::binary);
    ofs.close();
  }


  inline void touch_file_permissive(const boost::filesystem::path& path) {
    bool set_perms = !fs::exists(path);

    // by testing if we can trunc it we'll know if we can write to it...
    std::fstream ofs(path.generic_string(), std::ios::out | std::ios::trunc | std::ios::in | std::ios::binary);

    if(set_perms) {
      try {
        fs::permissions(path, fs::add_perms|fs::owner_write|fs::group_write|fs::others_write);
      }
      catch(...) {
        //
      }      
    }
    
    ofs.close();
    
    /*
    // note: saving for later
    #if BOOST_OS_LINUX
    ssize_t nrd;
    int fd;
    
    
    fd = open(path.generic_string().data(), O_CREAT | O_WRONLY | O_TRUNC);

    // file mode 0666 / r/w for everyone 
    // note: a well timed crash could still result in us creating the file
    // before chmodding it but this is probaby less of an issue than to have
    // M/T issues with umask()-ing before + after calls to open(), especially
    // as goldilock is not single threaded
    fchmod(fd,  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    
    if (fd == -1) {
      throw std::runtime_error("Error opening file: "s + strerror(errno));
    }

    if (close(fd) == -1) {
      throw std::runtime_error("Error opening file: "s + strerror(errno));
    }
    #elif BOOST_OS_WINDOWS
    // todo
    #else
    # error No implementation of touch_file_permissive() for this O/S
    #endif*/
  }
}