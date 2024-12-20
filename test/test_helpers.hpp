#pragma once

#define BOOST_TEST_NO_MAIN
#include <boost/test/included/unit_test.hpp>
#undef BOOST_TEST_NO_MAIN

#include <boost/filesystem.hpp>
#include <boost/process.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/regex.hpp>
#include <iostream>
#include <string>
#include <optional>


namespace goldilock::test { 
  namespace fs = boost::filesystem;
  namespace bp = boost::process;
  using namespace std::string_literals;
  using namespace std::chrono_literals;

  struct run_cmd_result_t {
    std::string output;
    size_t return_code;

    operator bool() const {
        return return_code == 0;
    }
  };

  /// @brief Wraps boost::process::system() and returns 
  /// @tparam ...Param 
  /// @param ...cmd 
  /// @return run_cmd_result_t containing the return code and console output as string
  template <class... Param> 
  inline run_cmd_result_t run_cmd(Param &&... cmd) {
    run_cmd_result_t result{};
  
    std::future<std::string> out;
    result.return_code = bp::system(cmd..., (bp::std_err & bp::std_out) > out, bp::std_in < stdin);
    
    // trim end newlines/spaces
    result.output = out.get();
    boost::algorithm::trim_right(result.output);

    return result;
  }

  inline std::string get_string_from_env(std::string variable_name, std::optional<std::string> default_value = std::nullopt) {
    auto env_val = std::getenv(variable_name.data());
    if(env_val == nullptr) {
      if(default_value.has_value()) {
        return default_value.value();
      }

      throw std::runtime_error("You need to define the environment variable "s + variable_name + " to run this test");
    }

    return std::string{env_val};
  }

  inline std::string get_executable_path_from_test_env(const std::string& exec_name) {
    std::string env_variable_name = "GOLDILOCK_TEST_BUILD_APP__"s + exec_name;
    fs::path exe_path{get_string_from_env(env_variable_name)};

    if(!fs::exists(exe_path)) {
      throw std::runtime_error("Test envirnment published executable not found at the expected path: " + exe_path.generic_string());
    }
    
    return exe_path.generic_path().generic_string();
  }

  inline std::string host_executable_name(const std::string& exec_name) {
    #if BOOST_OS_WINDOWS
      return exec_name + ".exe"s;
    #else
      return exec_name;
    #endif
  }

  inline std::string host_goldilock_executable_name() {
    return host_executable_name("goldilock");
  }

  //!\brief get the path to the goldilock executable in the build folder
  inline std::string host_goldilock_executable_path() {
    return get_executable_path_from_test_env("goldilock");
  }

  inline fs::path get_goldilock_case_working_dir(const std::optional<fs::path>& working_directory = std::nullopt) {

    if(working_directory.has_value()) {
      return working_directory.value();
    }

    auto mk_unique_path = []() { return fs::temp_directory_path() / fs::unique_path(); };

    fs::path result = mk_unique_path();
    size_t attempt = 0;

    while(fs::exists(result)) {
      if(attempt++ > 10) {
        throw std::runtime_error("Something is very from with the filesystem. Could not generate a non-existent unique path in temp in 10 attempts");
      }

      result = mk_unique_path();
    }

    return result;
  }

  //!\brief run a goldilock command in a specified workdir. If no working directory is provided, create a random temporary location
  template <class... Param> 
  inline run_cmd_result_t run_goldilock_command_in(const fs::path& working_directory, Param &&... args) {

    auto goldilock_exe = host_goldilock_executable_path();
    fs::create_directories(working_directory);

    std::cout << "Running goldilock command: '" << goldilock_exe;
    ((std::cout << ' ' << std::forward<Param>(args)), ...); 
    std::cout << "'" << std::endl;

    std::cout << "Working directory: " << working_directory.generic_string() << std::endl;    
      
    auto result = goldilock::test::run_cmd(bp::start_dir=working_directory, goldilock_exe, args...);   
    
    std::cout << "Command output:\n" 
      << "------------\n" 
      << result.output 
      << "\n------------" 
      << std::endl;

    return result;
  }

  template <class... Param> 
  inline run_cmd_result_t run_goldilock_command(Param &&... args) {

    fs::path wd = get_goldilock_case_working_dir();
    return run_goldilock_command_in(wd, args...);
  }


    //!\brief run a goldilock command in a specified workdir. If no working directory is provided, create a random temporary location
  template <class... Param> 
  inline bp::child start_goldilock_command_in(const fs::path& working_directory, Param &&... args) {

    auto goldilock_exe = host_goldilock_executable_path();
    fs::create_directories(working_directory);

    std::cout << "Starting goldilock command: '" << goldilock_exe;
    ((std::cout << ' ' << std::forward<Param>(args)), ...); 
    std::cout << "'" << std::endl;

    std::cout << "Working directory: " << working_directory.generic_string() << std::endl;    
      
    auto result = goldilock::test::run_cmd(bp::start_dir=working_directory, goldilock_exe, args...);   
    
    std::cout << "Command output:\n" 
      << "------------\n" 
      << result.output 
      << "\n------------" 
      << std::endl;

    return result;
  }

  //!\brief wait for a file to apear
  inline bool wait_for_file(const fs::path& path, size_t retries = 50, std::chrono::milliseconds retry_interval = 50ms) {
    bool found_file = false;
    while(--retries > 0) {
      found_file = fs::exists(path); 
      if(!found_file) {
        std::this_thread::sleep_for(retry_interval);
      }
      else {
        break;
      }
    }

    return found_file;
  }    


}