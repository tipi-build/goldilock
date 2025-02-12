// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <goldilock/file.hpp>
#include <cxxopts.hpp>

using namespace std::chrono_literals;
using namespace std::string_literals;
namespace fs = boost::filesystem;

int main(int argc, char **argv)
{
  cxxopts::Options options("support_app_delete", "Delete files starting with the given name really quickly as they appear");

  options.add_options()
    ("f", "File name starting pattern (e.g. passing 'mylock' will delete all files starting with the name mylock in the same folder)", cxxopts::value<std::string>())
    ("t", "Number of deleter threads to run concurrently", cxxopts::value<size_t>()->default_value("8"))
    ("d", "The number of seconds to run the deletion", cxxopts::value<size_t>()->default_value("0"))
    ;
  
  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);

    // get values
    std::string filename_pattern = result["f"].as<std::string>();
    size_t duration_raw = result["d"].as<size_t>();
    size_t num_threads = result["t"].as<size_t>();

    // run until that point in time
    std::chrono::steady_clock::time_point expiry = std::chrono::steady_clock::now() + duration_raw * 1s;

    fs::path filename_path{filename_pattern};

    if(!filename_path.is_absolute()) {
      filename_path = fs::current_path() / filename_pattern;
    }

    if(filename_path == "") {
      std::cout << "-f needs to be a non-empty value!" << std::endl;
      return 1;
    }
    
    boost::filesystem::path parent_path = filename_path.parent_path();

    bool expired = false;

    auto fn_deleter = [&](size_t thread_ix) {

      while(!expired) {

        for(auto & directory_entry : boost::filesystem::directory_iterator(parent_path)) {

          if(expired) {
            break;
          }

          if(directory_entry.is_regular_file()) { // just some safety so we don't delete important things by accident
            auto direntry_filename = directory_entry.path().filename().generic_string();

            if(boost::algorithm::starts_with(direntry_filename, filename_pattern)) {
              boost::system::error_code ec;
              boost::filesystem::remove(directory_entry.path(), ec);
            }
          }
        }

      }
    };

    std::vector<std::thread> threads{};
    threads.reserve(num_threads);

    for(size_t ix = 0; ix < num_threads; ix++) {
      threads.emplace_back(fn_deleter, ix);
    }

    while(!expired) {
      expired = expiry < std::chrono::steady_clock::now();
      std::this_thread::sleep_for(10ms);
    }

    for(auto& t : threads) {
      if(t.joinable()) {
        t.join();
      }
    }
  
    return 0;
  }
  catch (const std::exception &exc)
  {
    std::cerr << exc.what() << std::endl;
    std::cout << options.help() << std::endl;
    return 1;
  }
  catch(...) {
    std::cerr << "(deleter) Unhandled exception while parsing command line options" << std::endl;
    std::cout << options.help() << std::endl;
    return 1;
  }

  return 0;
}