// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include <cxxopts.hpp>
#include <goldilock/file.hpp>
#include <goldilock/fstream.hpp>
#include <goldilock/process_info.hpp>
#include <goldilock/string.hpp>

namespace tipi::goldilock
{  
  std::string getenv_or_default(const char* var_name, const std::string& default_value = "") {
    auto val = std::getenv(var_name);

    if(val != nullptr) {
      return std::string(val);
    }

    return default_value;
  }

  inline std::ostream nowhere_sink(0);

  inline int goldilock_main(int argc, char **argv) {
    using namespace tipi::goldilock;
    using namespace std::chrono_literals;

    cxxopts::Options options("goldilock", "goldilock - parent process id based concurrency barrier");
    options.add_options()
      ("v,verbose", "Verbose output", cxxopts::value<bool>()->default_value("false"))
      ("h,help", "Print usage")
      ("lockfile", "Lockfile to acquire / release", cxxopts::value<std::string>())
      ("acquire", "Try to acquire the lock", cxxopts::value<bool>())
      ("release", "Try to release the lock", cxxopts::value<bool>())
      ("s,search_parent", "Search locking parent process by name (comma separate list e.g. name1,name2,name3). If unspecified immediate parent process of goldilock is used. By default the farthest parent process matching the search term is selected.", cxxopts::value<std::vector<std::string>>()) 
      ("n,nearest", "If -s is enabled, search for the nearest parent matching the search")
    ;

    options.parse_positional({"lockfile"});

    cxxopts::ParseResult cli_result;

    try {
      cli_result = options.parse(argc, argv);
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

    std::string cli_err = "";
    bool valid_cli = true;

    if(cli_result.count("acquire") + cli_result.count("release") == 0) {
      cli_err = "You must --acquire or --release";
      valid_cli = false;
    }

    if(cli_result.count("acquire") + cli_result.count("release") > 1) {
      cli_err = "You cannot --acquire and --release simultaneously";
      valid_cli = false;
    }

    if(cli_result.count("lockfile") == 0) {
      cli_err = "You must specify the [lockfile] positional argument";
      valid_cli = false;
    }

    if(!valid_cli) {
      std::cerr << cli_err << std::endl;
      std::cout << options.help() << std::endl;
      return 1;
    }

    if (cli_result.count("help")) {
      std::cout << options.help() << std::endl;
      return 0;
    }

    auto &log = (cli_result.count("verbose")) ? std::cout : nowhere_sink;

    //
    auto lockfile_path = cli_result["lockfile"].as<std::string>();    

    // get the process id of the locking parent process
    std::function<std::optional<pid_t>(void)> get_locking_parent_process_id = [&]() -> std::optional<pid_t> {
      if(cli_result.count("search_parent")) {
        bool search_nearest = cli_result.count("nearest") > 0;
        return process_info::get_parent_pid_by_name(cli_result["search_parent"].as<std::vector<std::string>>(), search_nearest);
      }
      else {
        return process_info::get_parent_pid();
      }
    };

    // get our parent process id:
    auto locking_parent_process_id = get_locking_parent_process_id();

    if(!locking_parent_process_id) {
      std::cout << "No parent process with any of the following names was found: ";

      for(const auto& n : cli_result["search_parent"].as<std::vector<std::string>>()) {
        std::cout << "'" << n << "' ";
      }

      std::cout << std::endl;
      return 1;
    }

    auto ppid = locking_parent_process_id.value();

    auto get_lock = [&log](std::string lock_path, pid_t pid) -> bool {
      log << "Trying to acquire exclusive write access to " << lock_path  << std::endl;     
      
      auto lockfile_stream = exclusive_fstream::open(lock_path);
      if(lockfile_stream.is_open()) {
        lockfile_stream << pid << std::flush;
        log << "Success" << std::endl;
        return true;
      }

      return false;
    };

    auto get_locking_process_id = [&log](std::string lock_path) -> pid_t {
      log << "Reading exising lock file:" << lock_path << std::endl;
      auto lockfile_content = file::read_file_content(lock_path);
      return std::stoll(lockfile_content);
    };

    auto acquire_lock = [&]() -> bool {

      // get exclusive access to the lock file
      if(get_lock(lockfile_path, ppid)) {
        return true;
      }
      
      // read the lock file
      try {
        pid_t original_locker_parent_pid = get_locking_process_id(lockfile_path);

        log << "PID of original goldilock parent process = " << original_locker_parent_pid << std::endl;
        if(original_locker_parent_pid == ppid) {
          log << "Same as current parent process = " << ppid << " / reentry success" << std::endl;
          return true;  // goldilock is reentrant by parent process id just to be safe
        }

        // delete the lock file if the parent process isn't running and try to race again
        if(!process_info::is_process_running(original_locker_parent_pid)) {
          log << "Removing expired lock file" << std::endl;
          std::filesystem::remove(lockfile_path);
        }

        log << "Previous parent process still running - waiting" << std::endl;
      }
      catch(...) { /* oh well */ }

      return false;
    };

    auto release_lock = [&]() -> bool {

      bool got_release_lock = false;
      bool released_goldilock = false;

      std::string release_lock = lockfile_path + ".goldilock_release.lock";

      while(!got_release_lock) {
        if(get_lock(release_lock, process_info::get_processid())) { 
          got_release_lock = true;
        }
        else {
          pid_t original_locker_parent_pid = get_locking_process_id(release_lock);

          if(!process_info::is_process_running(original_locker_parent_pid)) {
            log << "Removing expired lock file" << std::endl;
            std::filesystem::remove(release_lock);
          }
          else {
            log << "Process active waiting for exit / pid = " << original_locker_parent_pid << std::endl;
            std::this_thread::sleep_for(50ms);
          }
        }
      }    
      
      try {
        log << "Reading exising lock file" << std::endl;

        auto lockfile_content = file::read_file_content(lockfile_path);
        pid_t original_locker_parent_pid = std::stoll(lockfile_content);

        if(original_locker_parent_pid == ppid) {
          log << "Freeing lock" << std::endl;
          std::filesystem::remove(lockfile_path);    
          released_goldilock = true;    
        }
        else {
          log << "Parent process ID not matching - skipping" << std::endl;
        }
      }
      catch(...) { /* oh well */ }

      std::filesystem::remove(release_lock);
      return released_goldilock;
    };


    if(cli_result.count("acquire")) {
      bool got_lock = acquire_lock();  
      while(!got_lock) {
        std::this_thread::sleep_for(50ms);
        got_lock = acquire_lock();
      }

      return 0;
    }
    else {
      auto success = release_lock();
      return (success) ? 0 : 1;
    }
  }
  
} // namespace tipi::goldilock


int main(int argc, char **argv)
{
  return tipi::goldilock::goldilock_main(argc, argv);
}