// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#include <goldilock/file.hpp>
#include <goldilock/fstream.hpp>
#include <goldilock/process_info.hpp>

namespace tipi::goldilock
{  
  void show_help(const char *arg0) {
    std::cout << "Unexpected command line argmuments count.\nUsage: " << arg0 << " <path to lock file> [acquire|release]" << std::endl;
  }

  std::string getenv_or_default(const char* var_name, const std::string& default_value = "") {
    auto val = std::getenv(var_name);

    if(val != nullptr) {
      return std::string(val);
    }

    return default_value;
  }

  bool is_verbose() {
    auto val = getenv_or_default("VERBOSE", "0");
    return val != "0" && val != "false" && val != "FALSE";
  }

  inline std::ostream nowhere_sink(0);
  const std::string LOCK_ACTION_ACQUIRE = "acquire";
  const std::string LOCK_ACTION_RELEASE = "release";

  inline int goldilock_main(int argc, char **argv) {
    auto &log = (is_verbose()) ? std::cout : nowhere_sink;

    using namespace tipi::goldilock;
    using namespace std::chrono_literals;

    std::vector<std::string> cli_args{argv + 1, argv + argc};

    if (cli_args.size() != 2) {
      show_help(argv[0]);
      return 1;
    }

    std::string lockfile_path = cli_args[0];
    std::string goldilock_action = cli_args[1];

    if(goldilock_action != LOCK_ACTION_ACQUIRE && goldilock_action != LOCK_ACTION_RELEASE) {
      show_help(argv[0]);
      return 1;
    }

    // get our parent process id:
    auto ppid = process_info::get_parent_pid();

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


    if(goldilock_action == LOCK_ACTION_ACQUIRE) {
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