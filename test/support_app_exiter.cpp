// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <cxxopts.hpp>

using namespace std::chrono_literals;

int main(int argc, char **argv)
{
  cxxopts::Options options("support_app_exiter", "Exits with a given return code");

  options.add_options()
    ("r,return-code", "Exit with this return code - defaults to 0", cxxopts::value<size_t>()->default_value("0"))
    ("w,wait", "The number of ms to sleep before returning", cxxopts::value<size_t>()->default_value("0"))
  ;
  
  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);
    size_t ret_code = result["r"].as<size_t>();
    size_t sleep_ms = result["w"].as<size_t>();


    if(sleep_ms > 0) {
      std::this_thread::sleep_for(sleep_ms * 1ms);
    }

    return ret_code;
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

  return 1;
}