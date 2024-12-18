// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>
#include <boost/process/handles.hpp>
#if BOOST_OS_WINDOWS
#include <boost/winapi/process.hpp>
#endif
#include <boost/predef.h>

#include <cxxopts.hpp>
#include <goldilock/process_info.hpp>

#if BOOST_OS_WINDOWS
// boost process handler to tell windows to create a new console when setting up
// the new process which is the best equivalent of running a detached process
struct new_window_handler : ::boost::process::detail::handler_base
{
  template <class Executor>
  void on_setup(Executor &e) const
  {
    e.creation_flags = boost::winapi::CREATE_NEW_CONSOLE_;
  }      
};
#endif

int main(int argc, char **argv)
{
  using namespace std::chrono_literals;
  namespace fs = boost::filesystem;
  namespace bp = boost::process;

  cxxopts::Options options("support_app_launcher", "Launch processes passed after -- in detached mode and wait for -w <file> to appeach to quit");

  options.add_options()
    ("w", "Stop this process when this file apears", cxxopts::value<std::string>())
    ("p", "PID file where this process will write its own process id", cxxopts::value<std::string>())
  ;
  
  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);
    std::string watchfile = result["w"].as<std::string>();

    if(result.count("p") > 0) {
      std::string pidfile = result["p"].as<std::string>();
      std::fstream ofs(pidfile, std::ios::out | std::ios::trunc | std::ios::in | std::ios::binary);
      ofs << tipi::goldilock::process_info::get_processid();
    }
    
    std::vector<std::string> all_argv(argv, argv + argc);

    auto ix_first_glob = std::find(all_argv.begin(), all_argv.end(), "--");
    std::vector<std::string> child_argv{ix_first_glob + 1, all_argv.end()};

    if(child_argv.size() == 0) {
      throw std::runtime_error("This application needs '-- ...application + arguments...' to be supplied");
    }

    std::string child_executable = child_argv.front();
    child_argv.erase(child_argv.begin());

    // doing the child launching in it own scope so
    // things get cleaned up to avoid handles hanging arround
    {      
      #if BOOST_OS_LINUX  || (BOOST_OS_MACOS && BOOST_ARCH_AARCH64)
      signal(SIGCHLD, SIG_IGN); // we don't want to handle child signals to avoid them handing around as zombies
      #endif

      bp::child child_process{
        child_executable,
        child_argv,
        #if BOOST_OS_WINDOWS
        new_window_handler(),
        #endif
        bp::std_out > bp::null, 
        bp::std_err > bp::null,
        bp::std_in < bp::null,
        boost::process::limit_handles
      };

      child_process.detach();
    }
    // wait for the child to die or the marker to show up:
    bool marker_appeared = false;
    
    while(!marker_appeared) {
      std::this_thread::sleep_for(50ms);
      marker_appeared = fs::exists(watchfile);
    }
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