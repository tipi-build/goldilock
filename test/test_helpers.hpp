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

  struct run_cmd_result_t {
    std::string output;
    size_t return_code;
  };

  /// @brief Wraps boost::process::system() and returns 
  /// @tparam ...Param 
  /// @param ...cmd 
  /// @return run_cmd_result_t containing the return code and console output as string
  template <class... Param> 
  inline run_cmd_result_t run_cmd(Param &&... cmd) {
    run_cmd_result_t result{};
  
    std::future<std::string> out;
    result.return_code = bp::system(cmd..., (bp::std_err & bp::std_out) > out, bp::std_in.close());
    
    // trim end newlines/spaces
    result.output = out.get();
    boost::algorithm::trim_right(result.output);

    return result;
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
    fs::path goldilock_path = fs::current_path() / "src" / host_goldilock_executable_name();

    if(!fs::exists(goldilock_path)) {
      throw std::runtime_error("Goldilock executable not found at the expected path: " + goldilock_path.generic_string());
    }
    
    return goldilock_path.generic_string();
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

}