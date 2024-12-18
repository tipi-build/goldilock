// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/predef.h>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>
#include <boost/process/handles.hpp>
#include <boost/scope_exit.hpp>
#if BOOST_OS_WINDOWS
#include <boost/winapi/process.hpp>
#endif

#include <cxxopts.hpp>

#include <goldilock/file.hpp>
#include <goldilock/fstream.hpp>
#include <goldilock/process_info.hpp>
#include <goldilock/string.hpp>
#include <goldilock/goldilock_spot.hpp>
#include <goldilock/version.hpp> // generated by build script - located in binary dir


namespace tipi::goldilock
{  
  namespace fs = boost::filesystem;
  namespace bp = boost::process;
  using namespace std::string_literals;
  using namespace std::chrono_literals;

  inline std::ostream nowhere_sink(0);

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

  //!\brief run a command in a bp::child() using the system shell (cmd/bash)
  template<class... Args>
  inline auto shell_run(Args&&...args)
  {
    #if BOOST_OS_WINDOWS
      static fs::path shell_executable = boost::process::search_path("cmd.exe");
      static std::string shell_command_arg = "/c";
    #else
      static fs::path shell_executable = boost::process::search_path("bash");
      static std::string shell_command_arg = "-c";
    #endif

    return bp::child{shell_executable, shell_command_arg, args...};
  }

  struct goldilock_cli_options {

    goldilock_cli_options()
      : options_{"goldilock", "goldilock - flexible file based locking and process barrier for the win"}
    {
      options_.add_options()
        ("v,verbose", "Verbose output", cxxopts::value<bool>()->default_value("false"))
        ("h,help", "Print usage")
        ("l,lockfile", "Lockfile(s) to acquire / release, specify as many as you want", cxxopts::value<std::vector<std::string>>())
        ("unlockfile", "Instead of running a command, have goldilock wait for all the specified unlock files to exist (those files will be deleted on exit)", cxxopts::value<std::vector<std::string>>())
        ("timeout", "In the case of --unlockfile, specify a timeout that should not be exceeded (in seconds, default to 60)", cxxopts::value<size_t>()->default_value("60"))
        ("no-timeout", "Do not timeout when using --unlockfile")
        ("detach", "Launch a detached copy with the same parameters otherwise")
        ("lock-success-marker", "A marker file to write when all logs got acquired", cxxopts::value<std::vector<std::string>>())
        ("watch-parent-process", "Unlock if the selected parent process exits", cxxopts::value<std::vector<std::string>>())
        ("search-nearest-parent-process", "By default --watch-parent-process looks up for the furthest removed parent process, set this flag to search for the nearest parent instead")
        ("version", "Print the version of goldilock")
      ;

      options_.allow_unrecognised_options();
      options_.custom_help("[OPTIONS] -- <command(s)...> any command line command that goldilock should run once the locks are acquired. After command returns, the locks are released and the return code forwarded. Standard I/O is forwarded unchanged");
      options_.show_positional_help();      
    }

    void parse(int argc, char **argv) {
      cxxopts::ParseResult cli_result = options_.parse(argc, argv);

      show_help = cli_result.count("help") > 0;
      show_version = cli_result.count("version") > 0;

      if(show_help || show_version) {
        valid_cli = true;
        return;
      }

      verbose = cli_result.count("verbose") > 0;
      detach = cli_result.count("detach") > 0;
      search_for_nearest_parent_process = cli_result.count("search-nearest-parent-process") > 0;

      run_command_mode = (cli_result.count("unlockfile") == 0); // e.g. there's no unlockfile...

      if(cli_result.count("watch-parent-process") > 0) {
        watch_parent_process_names = cli_result["watch-parent-process"].as<std::vector<std::string>>();
      }

      if(run_command_mode && cli_result.unmatched().size() == 0) {
        valid_cli = false;
        throw std::invalid_argument("You must supply a '-- <command>' argument for goldilock to run or specify --unlockfile <path> arguments");        
      }
      else {
        std::stringstream cmd_ss;
        for(const auto& arg : cli_result.unmatched()) {
          cmd_ss << arg << " ";
        }

        cmd_ss.seekp(-1, std::ios_base::end); // remove trailing whitespace
        command_mode_cmd = cmd_ss.str();
      }

      if(cli_result.count("lockfile") == 0) {
        throw std::invalid_argument("You must specify the [lockfile] positional argument");
        valid_cli = false;
      }
      
      if(cli_result.count("lock-success-marker") > 0) {
        success_markers = cli_result["lock-success-marker"].as<std::vector<std::string>>();

        for(const auto& marker : success_markers) {
          if(fs::exists(marker)) {
            fs::remove(marker);
          }
        }
      }

      if(cli_result.count("lockfile") > 0) {
        lockfiles = cli_result["lockfile"].as<std::vector<std::string>>();
      }

      if(cli_result.count("unlockfile") > 0) {
        unlockfiles = cli_result["unlockfile"].as<std::vector<std::string>>();
      }

      unlockfile_timeout = cli_result.count("no-timeout") > 0;
      unlockfile_timeout = cli_result["timeout"].as<size_t>();  // has a default value - cf. above

      valid_cli = true;
    }

    std::string help() {
      return options_.help();
    }

    std::string version_string() {
      std::stringstream ss;
      ss << "goldilock " << GOLDILOCK_VERSION << " (built from " << GOLDILOCK_GIT_REVISION << ")";
      return ss.str();
    }

    bool valid_cli = false;
    bool show_help = false;
    bool show_version = false;
    bool verbose = false;
    bool run_command_mode = false;
    bool search_for_nearest_parent_process = false;
    bool detach = false;

    size_t unlockfile_timeout = 0;
    bool unlockfile_notimeout = false;

    std::vector<std::string> watch_parent_process_names{};
    bool should_watch_parent_process() {
      return watch_parent_process_names.size() > 0;
    }
    
    std::vector<std::string> success_markers{};
    bool should_write_success_markers() {
      return success_markers.size() > 0;
    }

    std::vector<std::string> unlockfiles{};
    bool has_unlockfiles() {
      return unlockfiles.size() > 0;
    }

    // the actual command if any was passed
    std::string command_mode_cmd{};

    std::vector<std::string> lockfiles{};

  private:
    cxxopts::Options options_;
  };

  inline int goldilock_main(int argc, char **argv) {

    goldilock_cli_options options{};

    try {
      options.parse(argc, argv);
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

    if(options.show_version) {
      std::cout << options.version_string() << std::endl;
      return 0;
    }

    if(options.show_help) {
      std::cout << options.help() << std::endl;
      return ((options.valid_cli) ? 0 : 1);
    }

    auto &log = (options.verbose) ? std::cout : nowhere_sink;

    //
    // run in detached mode?
    //
    // this means spawning a child goldilock with the same parameters
    // except we replace --detach and have the child goldilock write
    // a lock success marker instead
    //
    // once the lock is acquired we return sucessfull from here
    if (options.detach) {
      std::stringstream cmd_ss;
      std::vector<std::string> all_argv(argv, argv + argc);

      auto temp_file = boost::filesystem::unique_path(); // this is our success marker
      fs::remove(temp_file); // make sure it's gone

      for(const auto& arg : all_argv) {
        if(arg == "--detach") {
          cmd_ss << " --lock-success-marker " << temp_file.generic_string();
        }
        else {
          cmd_ss << " " << arg;
        }
      }

      auto child_process = shell_run(cmd_ss.str(),
        #if BOOST_OS_WINDOWS
        new_window_handler(),
        #endif
        bp::std_out > bp::null, 
        bp::std_err > bp::null,
        bp::std_in < bp::null,
        boost::process::limit_handles
      );

      // wait for the child to die or the marker to show up:
      std::optional<size_t> child_ret;
      bool marker_appeared = false;

      while(!marker_appeared && !child_ret.has_value()) {
        std::this_thread::sleep_for(100ms);

        marker_appeared = fs::exists(temp_file);

        if(!child_process.running()) {
          child_ret = child_process.exit_code();
        }        
      }

      if(child_process.running() && marker_appeared) {
        child_process.detach();
        fs::remove(temp_file);
        return 0;
      }
      
      return child_ret.value_or(1); // only success if the child returned 0 too
    }

    //
    // normal operations 
    //

    std::map<fs::path, goldilock_spot> spots;
    std::map<fs::path, boost::interprocess::file_lock> file_locks;

    // take our spots in line and ensure the actual lockfiles are created
    for(const auto& lock_name : options.lockfiles) {      
      
      auto lockfile = fs::weakly_canonical(fs::path(lock_name));
      if(spots.find(lockfile) == spots.end()) {
        spots.emplace(lockfile, lockfile);
      }
      
      if(file_locks.find(lockfile) == file_locks.end()) {
        
        // make sure the lock file exits to start with
        std::string lockfile_str = lockfile.generic_string();
        goldilock::file::touch_file(lockfile_str);
        file_locks.emplace(lockfile, lockfile_str.data());

      }
    }
    
    boost::asio::io_context io;
    bool exit_requested = false;
    std::optional<bp::child> child_process;
    
    // handle signals and deal with any running child process in that case
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait(
      [&child_process, &exit_requested, &log](boost::system::error_code error, int signal_number) { 
        exit_requested = true;

        if(child_process.has_value() && child_process->joinable()) {
          try {
            child_process->terminate();
          }
          catch(...) {
            // it could be that the child received that signal already in which case we're done
            log <<  "Error while waiting on child process - possibly terminated by signal" << std::endl;
          }          
        }
      }
    );

    std::thread io_thread([&]() {
      io.run();
    });

    auto clean_stop_io = [&io_thread, &io]() {
      io.stop();
      
      if(io_thread.joinable()) {
        io_thread.join();
      }
    };

    // note: declared here to not get out of scope before the end of program
    boost::asio::steady_timer watch_parent_timer(io);


    //
    // watch parent process function
    //
    // if this is enabled we are watching <locking_parent_pid> to stay alive
    // if it's gone we exit & release the lock(s)

    std::optional<pid_t> locking_parent_pid;
    std::function<void(const boost::system::error_code&)> watch_parent_tick_fn;
    watch_parent_tick_fn = [&log, &locking_parent_pid, &exit_requested, &watch_parent_timer, &watch_parent_tick_fn](const boost::system::error_code& ec) {
      if(ec == boost::asio::error::operation_aborted) {
        log << "(watch_parent_tick_fn) aborted?: " << ec.what() << std::endl;
        return;
      }

      log << "(watch_parent_tick_fn) Checking parent running" << std::endl;

      // watch process
      if(locking_parent_pid && process_info::is_process_running(locking_parent_pid.value()) && !exit_requested) {

        log << "(watch_parent_tick_fn) still running parent: " << locking_parent_pid.value() << std::endl;
        // still up & running -> schedule next interval
        watch_parent_timer.expires_after(200ms);
        watch_parent_timer.async_wait(watch_parent_tick_fn); 
      }
      else {
        log << "(watch_parent_tick_fn) parent not running or exit_requested (" << std::to_string(exit_requested) << ")" << std::endl;
        exit_requested = true;
        return;
      }       
    };

    // start watching the process asap
    if(options.should_watch_parent_process()) {
      // determine parent process id
      locking_parent_pid = process_info::get_parent_pid_by_name(options.watch_parent_process_names, options.search_for_nearest_parent_process);

      if(!locking_parent_pid) {
        std::cerr << "Fatal: No parent process with any of the following names was found: ";

        for(const auto& n : options.watch_parent_process_names) {
          std::cerr << "'" << n << "' ";
        }

        std::cerr << std::endl;
        clean_stop_io();
        return 1;
      }

      log << "Watching parent process with pid: " << locking_parent_pid.value() << std::endl;

      // schedule first run
      watch_parent_timer.async_wait(watch_parent_tick_fn);
    }

    //
    // Main aquire all the locks loop
    //

    // make sure the locks we have gotten ourselves are being updated with our time!
    boost::asio::steady_timer hold_lock_timer(io);
    std::function<void(const boost::system::error_code&)> hold_lock_tick_fn;

    hold_lock_tick_fn = [&](const boost::system::error_code& ec) {
      if(ec == boost::asio::error::operation_aborted) {
        return;
      }

      log << "(hold_lock_tick_fn) tick " << std::endl;

      // watch process
      if(!exit_requested) {

        for(auto& [target, spot] : spots) {
          spot.update_spot();
        }

        // schedule next interval
        hold_lock_timer.expires_after(2s);
        hold_lock_timer.async_wait(hold_lock_tick_fn); 
        log << "(hold_lock_tick_fn) rescheduled" << std::endl;
      }   
    };

    // schedule first run
    hold_lock_timer.async_wait(hold_lock_tick_fn);

    bool got_all_locks = false;
    size_t do_update_counter = 0;
    size_t failed_all_locks_acquire = 0;

    while(!got_all_locks && !exit_requested) {

      size_t count_first_in_line = std::count_if(
        spots.begin(), 
        spots.end(), 
        [](const auto& pair) {
          return pair.second.is_first_in_line();
        });

      bool all_first_in_line = count_first_in_line == spots.size();
      bool some_first_in_line = count_first_in_line > 0;

      if(all_first_in_line) {
        got_all_locks = true;
        
        for(auto &[path, lock] : file_locks) {
          got_all_locks &= lock.try_lock_for(50ms);          
        }
      }

      if(some_first_in_line && !got_all_locks){
        failed_all_locks_acquire++;
      }

      // if we didn't manage to aquire the locks 100 times in a row, let's get back line 
      // so we don't deadlock (especially in cases where someones else got a partial lock)
      if(failed_all_locks_acquire > 300) { // 300 * 100ms = 30s
        failed_all_locks_acquire = 0;
        for(auto& [target, spot] : spots) {
          spot.get_in_line();
        }
      }

      if(got_all_locks) {
        break;
      }

      do_update_counter++;
      std::this_thread::sleep_for(100ms);
    }

    if(exit_requested) {
      clean_stop_io();
      return 1;
    }

    //
    // now we own all the locks either...
    //

    if(options.should_write_success_markers()) {
      for(const auto& marker : options.success_markers) {
        goldilock::file::touch_file(marker);
      }
    }    

    size_t goldilock_exit_code = 1;

    // ...run the passed command
    if(options.run_command_mode) {
      
      // setup the child process (wire up all i/o as passthrough)
      child_process = shell_run(io, options.command_mode_cmd, bp::std_out > stdout,  bp::std_err > stderr, bp::std_in < stdin);

      try {
        child_process->wait();
      }
      catch(...) {
        log <<  "Error while waiting on child process - possibly terminated by signal" << std::endl;
      }
      
      goldilock_exit_code = child_process->exit_code();
    }
    // ...or wather for unlock files to appear
    else {
      bool timed_out = false;
      bool found_all_files = false;

      auto timeout_timer = boost::asio::steady_timer(io);

      if (!options.unlockfile_notimeout) {
        timeout_timer.expires_after(std::chrono::seconds(options.unlockfile_timeout));
        timeout_timer.async_wait([&](const boost::system::error_code& ec) {
          timed_out = true; // don't care about cancellation
        });
      }

      while(!exit_requested && !found_all_files && !timed_out) {

        bool found_all = true;
        for(const auto& file : options.unlockfiles) {
          found_all &= fs::exists(file);
        }

        found_all_files = found_all;
        std::this_thread::sleep_for(50ms);
      }

      goldilock_exit_code = (found_all_files) ? 0 : 1;
      timeout_timer.cancel();

      if(found_all_files) {
        for(const auto& file : options.unlockfiles) {
          boost::system::error_code fsec;
          fs::remove(file, fsec); // doesn't throw / fail silently
        }
      }
    }
    
    // shutdown everything
    exit_requested = true;
    hold_lock_timer.cancel();
    watch_parent_timer.cancel();
    signals.cancel();
    spots.clear();
    file_locks.clear();
    clean_stop_io();

    return goldilock_exit_code;
  }
  
} // namespace tipi::goldilock


int main(int argc, char **argv)
{
  return tipi::goldilock::goldilock_main(argc, argv);
}