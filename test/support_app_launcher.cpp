// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary
#include <boost/predef.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/process.hpp>
#include <boost/process/handles.hpp>
#include <boost/process/extend.hpp>
#include <boost/asio/detail/signal_init.hpp>
#include <boost/asio.hpp>
#if BOOST_OS_WINDOWS
#include <boost/winapi/process.hpp>
#endif

#if BOOST_OS_LINUX || BOOST_OS_MACOS
#include <syslog.h>
#include <unistd.h>
#endif

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

  cxxopts::Options options("support_app_launcher", "Launch processes passed after -- in detached mode and wait for -w <file> to appeach to quit. Can daemonize itself \\ö/");

  options.add_options()
    ("w,watch", "Stop this process when this file apears", cxxopts::value<std::string>())
    ("d,daemonize", "Deamonize this before launching child process", cxxopts::value<bool>())
    ("l,logfile", "Logfile for the daemon's standard io if -d,--daemonize is used", cxxopts::value<std::string>()->default_value("support_app_launcher_log"))
    ("p,pid", "PID file where this process will write its own process id", cxxopts::value<std::string>())
  ;
  
  cxxopts::ParseResult result;
  try {
    result = options.parse(argc, argv);

    bool deamonize = result.count("d") > 0;

    fs::path child_launch_workdir = fs::current_path();
    fs::path watchfile = result["w"].as<std::string>();
    fs::path stdiologfile = result["l"].as<std::string>();
    std::optional<fs::path> pidfile;

    watchfile = fs::absolute(watchfile);
    stdiologfile = fs::absolute(stdiologfile);

    if(result.count("p") > 0) {
      pidfile = fs::absolute(result["p"].as<std::string>());
    }

    boost::asio::io_context io_context;
    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait(
      [&](boost::system::error_code /*ec*/, int signo)
      {
        std::cout << "Received signal: " << signo << std::endl;
        std::cout << "- > stopping" << std::endl;
        io_context.stop();
        std::cout << " x io context stopped successfully" << std::endl;
      });

    // should we deamonize?
    if(deamonize) {

      // Note on why we don't do this all the time:
      // we are using support_app_launcher to simulate goldilock watching *some* parent process so we need
      // the hierarchy to acutally exist. 
      // 
      // We must however be able to also start the child process in a way we can ignore SIGCHLD
      // so that child processes get reaped by the system and are not zombiified.
      // 
      // We cannot however ignore SIGCHLD in the test application because it breaks other parts
      // os boost process and makes it impossible to start other children.
      //
      // This is not an issue here as support_app_launcher only ever will launch one child.

      io_context.notify_fork(boost::asio::io_context::fork_prepare);

      // Fork the process and have the parent exit. If the process was started
      // from a shell, this returns control to the user. Forking a new process is
      // also a prerequisite for the subsequent call to setsid().
      if (pid_t pid = fork()) {
        if (pid > 0)
        {
          // We're in the parent process and need to exit.
          exit(0);
        }
        else
        {
          std::cerr << "First fork failed" << std::endl;
          return 1;
        }
      }

      // Note: don't do setsid and chdir as per https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html
      // recommendations
      #if BOOST_OS_LINUX

      // Become the new session leader. This detaches it from the terminal.
      setsid();

      // A process inherits its working directory from its parent. This could be
      // on a mounted filesystem, which means that the running daemon would
      // prevent this filesystem from being unmounted. Changing to the root
      // directory avoids this problem.
      chdir("/"); // this, we might not want to do on MacOS 

      #endif

      // The file mode creation mask is also inherited from the parent process.
      // We don't want to restrict the permissions on files created by the
      // daemon, so the mask is cleared.
      umask(0);

      // A second fork ensures the process cannot acquire a controlling terminal.
      if (pid_t pid = fork())
      {
        if (pid > 0)
        {
          exit(0);
        }
        else
        {
          syslog(LOG_ERR | LOG_USER, "Second fork failed: %m");
          return 1;
        }
      }

      // Close the standard streams. This decouples the daemon from the terminal that started it.
      close(0); close(1); close(2);

      // We don't want the daemon to have any standard input.
      if (open("/dev/null", O_RDONLY) < 0)
      {
        syslog(LOG_ERR | LOG_USER, "Unable to open /dev/null: %m");
        return 1;
      }

      // Send standard output to a log file.
      if(open(stdiologfile.generic_string().data(), O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) < 0) {
        syslog(LOG_ERR | LOG_USER, "Unable to open output file %s: %m", stdiologfile.generic_string().data());
        return 1;
      }

      std::cout << "Standard output redirected to logfile" << std::endl;

      // Also send standard error to the same log file.
      if(dup(1) < 0) {
        syslog(LOG_ERR | LOG_USER, "Unable to dup output descriptor: %m");
        return 1;
      }

      std::cerr << "Standard error redirected to logfile" << std::endl;

      // Inform the io_context that we have finished becoming a daemon. The
      // io_context uses this opportunity to create any internal file descriptors
      // that need to be private to the new process.
      io_context.notify_fork(boost::asio::io_context::fork_child);

      // The io_context can now be used normally.
      syslog(LOG_INFO | LOG_USER, "Daemonized sucessfully");
      std::cout << "Daemonized sucessfully" << std::endl;
    }

    if(pidfile.has_value()) {
      std::fstream ofs(pidfile.value().generic_string(), std::ios::out | std::ios::trunc | std::ios::in | std::ios::binary);
      ofs << tipi::goldilock::process_info::get_processid();
    }

    // doing the child launching in it own scope so
    // things get cleaned up to avoid handles hanging arround
    {      
      std::vector<std::string> all_argv(argv, argv + argc);

      auto ix_first_glob = std::find(all_argv.begin(), all_argv.end(), "--");
      std::vector<std::string> child_argv{ix_first_glob + 1, all_argv.end()};

      if(child_argv.size() == 0) {
        throw std::runtime_error("This application needs '-- ...application + arguments...' to be supplied");
      }

      #if BOOST_OS_LINUX
      std::signal(SIGCHLD, SIG_IGN); // we don't want to handle child signals to avoid them handing around as zombies
      boost::asio::detail::signal_init<> init_; // Ignore SIGPIPE on Unixes (boost asio doesn't do it by default on linux and mac)
      #endif

      bp::child child_process{
        child_argv,
        #if BOOST_OS_WINDOWS
        new_window_handler(),
        #endif
        bp::std_out > bp::null, 
        bp::std_err > bp::null,
        bp::std_in < bp::null,
        bp::limit_handles,
        bp::start_dir=child_launch_workdir  // this preserves the workdir as it was passed to the initial parent process (pre-daemonization)
      };

      child_process.detach();
    }

    boost::asio::steady_timer watch_file_timer(io_context);

    std::function<void(const boost::system::error_code&)> watch_file_tick_fn;
    watch_file_tick_fn = [&](const boost::system::error_code& ec) {
      if(ec == boost::asio::error::operation_aborted) {
        return;
      }

      // re-schedule if the file doesn't exist
      if(!fs::exists(watchfile)) {
        watch_file_timer.expires_after(50ms);
        watch_file_timer.async_wait(watch_file_tick_fn);
      }
      else {
        io_context.stop();
      }
    };

    // schedule a first run
    watch_file_timer.async_wait(watch_file_tick_fn);

    io_context.run();

    if(deamonize) {
      syslog(LOG_INFO | LOG_USER, "Quitting");
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