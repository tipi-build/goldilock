// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <goldilock/file.hpp>
#include <cxxopts.hpp>

using namespace std::chrono_literals;
using namespace std::string_literals;

int main(int argc, char **argv)
{
  cxxopts::Options options("support_app_append_to_file", "Append a string to an existing file F N times with S wait time between writes");

  options.add_options()
    ("s", "String to append to <f>", cxxopts::value<std::string>())
    ("n", "Number of times <s> should be appended to <f>", cxxopts::value<size_t>())
    ("f", "File name", cxxopts::value<std::string>())
    ("i", "Wait time between writes", cxxopts::value<size_t>()->default_value("100"))
    ("e", "Max failures before exiting with error", cxxopts::value<size_t>()->default_value("100"))
    ;
  
  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);

    // get values
    size_t repeat = result["n"].as<size_t>();
    size_t interval = result["i"].as<size_t>();
    size_t max_failures = result["e"].as<size_t>();
    std::string filename = result["f"].as<std::string>();
    std::string fragment = result["s"].as<std::string>();

    std::string lockfile = filename + ".lock"s;
    tipi::goldilock::file::touch_file(lockfile);
    boost::interprocess::file_lock flock(lockfile.data());       
    
    size_t times_success = 0;
    size_t failures = 0;

    while(times_success < repeat) {
      try {
        {
          boost::interprocess::scoped_lock<boost::interprocess::file_lock> e_lock(flock);
          std::ofstream outfile;
          outfile.open(filename, std::ios_base::app); 
          outfile << fragment;
        }
                
        times_success++;
      }
      catch(...) {
        failures++;

        if(failures >= max_failures) {
          std::cerr << "Too many failures writing to " << filename << std::endl; 
          return 2;
        }
      }

      std::this_thread::sleep_for(interval * 1ms);
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
    std::cerr << "Unhandled exception while parsing command line options" << std::endl;
    std::cout << options.help() << std::endl;
    return 1;
  }

  return 0;
}