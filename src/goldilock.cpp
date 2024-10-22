// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary

#include <iostream>
#include <boost/predef.h>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <boost/process.hpp>
#include <boost/scope_exit.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <string>
#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>
#include <map>
#include <optional>
#include <chrono>
#include <set> 
#include <cxxopts.hpp>
#include <goldilock/file.hpp>
#include <goldilock/fstream.hpp>
#include <goldilock/process_info.hpp>
#include <goldilock/string.hpp>
#include <goldilock/version.hpp> // generated

#if BOOST_OS_WINDOWS
#include <boost/winapi/process.hpp>
#endif

namespace tipi::goldilock
{  
  namespace fs = boost::filesystem;
  namespace bp = boost::process;
  using namespace std::string_literals;
  using namespace std::chrono_literals;

  inline std::string get_random_uuid() {
    static boost::uuids::random_generator uuid_gen;
    return boost::lexical_cast<std::string>(uuid_gen());
  }

  std::string getenv_or_default(const char* var_name, const std::string& default_value = "") {
    auto val = std::getenv(var_name);

    if(val != nullptr) {
      return std::string(val);
    }

    return default_value;
  }

  inline std::ostream nowhere_sink(0);

  //!\brief get the numerial index suffixed to a lockfile from its filename
  inline std::optional<size_t> get_lockfile_index(const fs::path& lockfile, const fs::path& p) {
    static std::map<fs::path, boost::regex> rx_cache;

    if(rx_cache.find(lockfile) == rx_cache.end()) {
      const boost::regex esc("[.^$|()\\[\\]{}*+?\\\\]");
      const std::string rep("\\\\&");

      std::string lockfile_name = lockfile.filename().generic_string();
      std::string lockfile_name_rx_str = regex_replace(lockfile_name, esc, rep, boost::match_default | boost::format_sed);
      lockfile_name_rx_str += + "\\.(?<ix>[[:digit:]]+)$";
      rx_cache[lockfile] = boost::regex(lockfile_name_rx_str);
    }

    std::optional<size_t> result;

    boost::smatch matches;
    std::string fullpath = p.filename().generic_string();
    if(boost::regex_match(fullpath, matches, rx_cache.at(lockfile))) {
      result = boost::lexical_cast<size_t>(matches["ix"].str());     
    }

    return result;
  }

  // forward decl
  struct goldilock_spot;  
  std::map<fs::path, goldilock_spot> list_lockfile_spots(const fs::path& lockfile_path);

  struct goldilock_spot {     

    goldilock_spot(const fs::path& lockfile_path)
      : lockfile_{lockfile_path}
      , owned_{true}      
      , guid_{get_random_uuid()}
      , spot_index_{0}
    {
      get_in_line();      
    }

    //!\brief get a new spot in line and update the spot_index
    size_t get_in_line() {
      if(!owned_) {
        throw std::runtime_error("Cannot update someone else's lockfile: "s + lockfile_.generic_string());
      }

      if(current_spot_file_ && fs::exists(current_spot_file_.value())) {
        fs::remove(current_spot_file_.value());
        current_spot_file_.reset();
      }

      bool got_spot = false;
      while(!got_spot) {

        // try to get to own the spot
        auto spots = list_lockfile_spots(lockfile_);

        auto max_spot_it = std::max_element(
          spots.begin(),
          spots.end(),
          [](const auto& a, const auto& b) { 
            return a.second.get_spot_index() < b.second.get_spot_index(); 
          }
        );

        if(max_spot_it != spots.end()) {
          spot_index_ = max_spot_it->second.spot_index_ + 1;
        }

        auto now = std::chrono::system_clock::now();
        timestamp_ = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        fs::path spot_path = get_spot_path();

        {
          auto lockfile_stream = exclusive_fstream::open(spot_path, "wx");
          if(lockfile_stream.is_open()) {
            boost::archive::text_oarchive oa(lockfile_stream);
            oa << *this;

            lockfile_stream.close();
          }
        }

        // now read and see if the contents are as expected
        {
          auto read_back = goldilock_spot::read_from(spot_path, lockfile_);
          got_spot = (read_back.get_guid() == get_guid() && read_back.get_timestamp() ==  get_timestamp());
        }

        if(got_spot) {
          current_spot_file_ = spot_path;
        }
      }

      return spot_index_;
    }

    ~goldilock_spot() {
      // expire this spot
      if(owned_ && current_spot_file_ && fs::exists(get_spot_path())) {
        boost::system::error_code fsec;
        fs::remove(get_spot_path(), fsec); // doesn't throw / fail silently
      }
    }

    void update_spot() {
      if(!owned_) {
        throw std::runtime_error("Cannot update someone else's lockfile: "s + lockfile_.generic_string());
      }

      auto now = std::chrono::system_clock::now();
      timestamp_ = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
      std::ofstream ofs(get_spot_path().generic_string());
      boost::archive::text_oarchive oa(ofs);
      oa << *this;
    }

    static goldilock_spot read_from(const fs::path& spot_on_disk, const fs::path& lockfile_path) {
      goldilock_spot result;

      // boost::serialization
      std::ifstream ifs(spot_on_disk.generic_string());
      boost::archive::text_iarchive ia(ifs);
      ia >> result;

      result.lockfile_ = fs::weakly_canonical(lockfile_path);
      result.owned_ = false;
      result.spot_index_ = get_lockfile_index(result.lockfile_, spot_on_disk).value();      
      return result;
    }

    bool is_first_in_line() const {
      // try to get to own the spot
      auto spots = list_lockfile_spots(lockfile_);

      auto min_spot_it = std::min_element(
        spots.begin(),
        spots.end(),
        [](const auto& a, const auto& b) { 
          return a.second.get_spot_index() < b.second.get_spot_index(); 
        }
      );

      if(min_spot_it != spots.end()) {
        return min_spot_it->second.guid_ == guid_;
      }

      return false;
    }

    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
      ar &timestamp_;
      ar &guid_;
    }

    fs::path get_spot_path() const {
      if(current_spot_file_) {
        return current_spot_file_.value();
      }

      auto parent_folder = lockfile_.parent_path();
      auto filename = lockfile_.filename().generic_string();
      
      return parent_folder / (filename + "."s + std::to_string(spot_index_));
    }

    fs::path get_lockfile_path() const {
      return lockfile_;
    }

    size_t get_spot_index() const {
      return spot_index_;
    }

    std::string get_guid() {
      return guid_;
    }

    //!\brief our own or someone else's?
    bool is_owned() const {
      return owned_;
    }

    //!\brief is the spot expired
    size_t get_timestamp() const {
      return timestamp_;
    }

    bool is_valid(uint16_t lifetime_seconds = 60) const {
      auto end_of_validity = timestamp_ + lifetime_seconds; // expires after 60s
      auto now = std::chrono::system_clock::now();
      auto unix_ts_now = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
      return end_of_validity >= unix_ts_now;
    }

    bool is_expired(uint16_t lifetime_seconds = 60) const {
      return !is_valid(lifetime_seconds);
    }

  private:
    goldilock_spot() { /* for deserialization */ }
    //!\brief absolute path as resolved
    fs::path lockfile_;

    std::optional<fs::path> current_spot_file_;

    //!\brief our spot in line
    size_t spot_index_ = 0;

    //!\brief a UUID to verify that process data == file level contents
    std::string guid_;

    //!\brief our own or someone else's?
    bool owned_ = false;

    //!\brief is the spot expired
    size_t timestamp_ = 0;
  };

  //!\brief list all lockfiles given a lockfile path "waiting in line"
  inline std::map<fs::path, goldilock_spot> list_lockfile_spots(const fs::path& lockfile_path) {
    std::map<fs::path, goldilock_spot> result;
    fs::path parent_path = fs::weakly_canonical(fs::path(lockfile_path)).parent_path();     

    for(auto & directory_entry : boost::filesystem::directory_iterator(parent_path)) {

      if(!directory_entry.is_regular_file()) {
        continue;
      }

      auto locker_in_line = get_lockfile_index(lockfile_path, directory_entry.path());

      // this means it is a potentially valid lock file
      if(locker_in_line.has_value()) {

        try {
          auto spot = goldilock_spot::read_from(directory_entry.path(), lockfile_path);
          if(spot.is_expired()) {
            fs::remove(directory_entry.path());
          }
          else {
            result.insert({ directory_entry.path(), spot });
          }
        }
        catch(...) {
          std::cout << "Warning - deleting broken lock spot:" << directory_entry.path() << std::endl;
          boost::system::error_code fsec;
          fs::remove(directory_entry.path(), fsec); // fail silently...
        }
      }
    }

    return result;
  }


  struct new_window_handler : ::boost::process::detail::handler_base
  {
    #if BOOST_OS_WINDOWS
    // this function will be invoked at child process constructor before spawning process
    template <class WindowsExecutor>
    void on_setup(WindowsExecutor &e) const
    {
      e.creation_flags = boost::winapi::CREATE_NEW_CONSOLE_;
    }
    #endif
  };

  inline int goldilock_main(int argc, char **argv) {
    cxxopts::Options options("goldilock", "goldilock - flexible file based locking and process barrier for the win");
    options.add_options()
      ("v,verbose", "Verbose output", cxxopts::value<bool>()->default_value("false"))
      ("h,help", "Print usage")
      ("l,lockfile", "Lockfile(s) to acquire / release, specify as many as you want", cxxopts::value<std::vector<std::string>>())
      ("unlockfile", "Instead of running a command, have goldilock wait for all the specified unlock files to exist (those files will be deleted on exit)", cxxopts::value<std::vector<std::string>>())
      ("timeout", "In the case of --unlockfile, specify a timeout that should not be exceeded (in seconds, default to 60)", cxxopts::value<size_t>()->default_value("60"))
      ("no-timeout", "Do not timeout when using --unlockfile")
      ("detach", "Launch a detached copy with the same parameters otherwise")
      ("version", "Print the version of goldilock")
    ;

    //options.parse_positional({"lockfile"});
    options.allow_unrecognised_options();
    options.custom_help("[OPTIONS] -- <command(s)...> any command line command that goldilock should run once the locks are acquired. After command returns, the locks are released and the return code forwarded. Standard I/O is forwarded unchanged");
    options.show_positional_help();

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

    bool goldilock_run_command = (cli_result.count("unlockfile") == 0);

    if(goldilock_run_command && cli_result.unmatched().size() == 0) {
      cli_err = "You must supply a '-- <command>' argument for goldilock to run or specify --unlockfile <path> arguments";
      valid_cli = false;
    }

    if(cli_result.count("lockfile") == 0) {
      cli_err = "You must specify the [lockfile] positional argument";
      valid_cli = false;
    }

    if (cli_result.count("version")) {
      std::cout << "goldilock " << GOLDILOCK_VERSION << " (built from " << GOLDILOCK_GIT_REVISION << ")" << std::endl;
      return 0;
    }

    bool show_help = cli_result.count("help") > 0;

    if (!valid_cli || show_help) {
      if(!show_help) {
        std::cerr << cli_err << std::endl;
      }
      std::cout << options.help() << std::endl;
      return ((valid_cli) ? 0 : 1);
    }

    fs::path shell;
    std::string shell_arg1;
    #if BOOST_OS_WINDOWS
      shell = boost::process::search_path("cmd.exe");
      shell_arg1 = "/c";
    #else
      shell = boost::process::search_path("bash");
      shell_arg1 = "-c";
    #endif

    if (cli_result.count("detach")) {
      std::stringstream cmd_ss;

      std::vector<std::string> all_argv(argv, argv + argc);

      for(const auto& arg : all_argv) {
        if(arg != "--detach") {
          cmd_ss << " " << arg;
        }
      }

      auto child_process = bp::child{shell, shell_arg1, cmd_ss.str(), new_window_handler()};
      child_process.detach();
      return 0;
    }

    auto &log = (cli_result.count("verbose")) ? std::cout : nowhere_sink;

    std::map<fs::path, goldilock_spot> spots;
    std::map<fs::path, boost::interprocess::file_lock> file_locks;

    for(const auto& lock_name : cli_result["lockfile"].as<std::vector<std::string>>()) {      
      auto lockfile = fs::weakly_canonical(fs::path(lock_name));
      if(spots.find(lockfile) == spots.end()) {
        spots.emplace(lockfile, lockfile);
      }
      
      if(file_locks.find(lockfile) == file_locks.end()) {
        
        // make sure the lock file exits to start with
        std::string lockfile_str = lockfile.generic_string();
        std::fstream ofs(lockfile_str, std::ios::out | std::ios::trunc | std::ios::in | std::ios::binary);
        ofs.close();
        file_locks.emplace(lockfile, lockfile_str.data());
      }
    }
    
    boost::asio::io_context io;
    bool exit_requested = false;
    std::optional<bp::child> child_process;
    
    // ensure the locks get cleaned on CTRL+C cancellation as well etc
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
      signals.async_wait(
        [&child_process, &exit_requested](boost::system::error_code error, int signal_number) { 
          exit_requested = true;

          if(child_process.has_value() && child_process->joinable()) {
            child_process->terminate();
          }
        }
      );

    std::thread io_thread([&]() {
      io.run();
    });

    bool got_all_locks = false;
    size_t do_update_counter = 0;
    size_t failed_all_locks_acquire = 0;

    while(!got_all_locks && !exit_requested) {

      if(do_update_counter % 20 == 0) { // 20x100ms = 2s
        do_update_counter = 0;

        for(auto& [target, spot] : spots) {
          spot.update_spot();
        }         
      }

      size_t count_first_in_line = 0;
      for(auto &[path, spot] : spots) {
        if(spot.is_first_in_line()) {
          count_first_in_line++;
        }
      }

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
      if(failed_all_locks_acquire > 10) {
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
      io.stop();
      io_thread.join();
      return 1;
    }

    bool watched_process_is_running = true;

    // make sure the locks we have gotten ourselves are being updated with our time!
    std::thread lock_holder_thread([&]() {

      size_t update = 0;

      try {

      while(watched_process_is_running && !exit_requested) {

        if(update % 20 == 0) { // 20x50ms = 1s
          update = 0;

          for(auto& [target, spot] : spots) {
            spot.update_spot();
          }         
        }

        update++;
        std::this_thread::sleep_for(50ms);
      }
      }
      catch(...) {
        std::cout << "Exxx" << std::endl;
      }
    });


    size_t goldilock_exit_code = 1;


    if(goldilock_run_command) {
      
      std::stringstream cmd_ss;
      for(const auto& arg : cli_result.unmatched()) {
        cmd_ss << arg << " ";
      }

      // setup the child process
      child_process = bp::child{io, shell, shell_arg1, cmd_ss.str(), bp::std_out > stdout,  bp::std_err > stderr, bp::std_in < stdin };
      child_process->wait();
      signals.cancel();

      goldilock_exit_code = child_process->exit_code();

    }
    else {

      auto files_to_watch = cli_result["unlockfile"].as<std::vector<std::string>>();

      // watch the files:
      bool timed_out = false;
      bool found_all_files = false;

      auto timer = boost::asio::steady_timer(io);

      if (cli_result.count("no-timeout") == 0) {

        // timeout expiration...
        auto timeout_duration = cli_result["timeout"].as<size_t>();        
        timer.expires_after(std::chrono::seconds(timeout_duration));
        timer.async_wait([&](const boost::system::error_code& ec) {
          timed_out = true; // don't care about cancellation
        });

      }

      while(!exit_requested && !found_all_files && !timed_out) {

        bool found_all = true;
        for(const auto& file : files_to_watch) {
          found_all &= fs::exists(file);
        }

        found_all_files = found_all;
        std::this_thread::sleep_for(50ms);
      }

      goldilock_exit_code = (found_all_files) ? 0 : 1;

      timer.cancel();

      if(found_all_files) {
        for(const auto& file : files_to_watch) {
          boost::system::error_code fsec;
          fs::remove(file, fsec); // doesn't throw / fail silently
        }
      }
    }
    

    watched_process_is_running = false;
    io.stop();

    if(lock_holder_thread.joinable()) {
      lock_holder_thread.join();
    }

    if(io_thread.joinable()) {
      io_thread.join();
    }
    
    return goldilock_exit_code;
  }
  
} // namespace tipi::goldilock


int main(int argc, char **argv)
{
  return tipi::goldilock::goldilock_main(argc, argv);
}