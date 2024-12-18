#define BOOST_TEST_MODULE test_signals_robustness
#include <boost/test/included/unit_test.hpp> 
#include <boost/test/data/test_case.hpp>

#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/process.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/random_generator.hpp>

#include <test_helpers.hpp>
#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <chrono>

#include <goldilock/file.hpp>
#include <goldilock/process_info.hpp>

 
namespace goldilock::test { 
  namespace fs = boost::filesystem;
  namespace bp = boost::process;

  using namespace std::chrono_literals;
  using namespace std::string_literals;

  inline std::string sig_name(size_t sig) {

    static std::map<size_t, std::string> sigmap{
      { SIGINT, "SIGINT" },
      { SIGTERM, "SIGTERM" }
    };

    return sigmap.at(sig);
  }

  static auto TEST_DATA_goldilock_lock_sig_injections__signals = {
    SIGINT,
    SIGTERM
  };

  BOOST_DATA_TEST_CASE(
    goldilock_lock_sig_injections,
    boost::unit_test::data::make(TEST_DATA_goldilock_lock_sig_injections__signals), 
    td_signal
  ){
    auto wd = get_goldilock_case_working_dir();
    fs::create_directories(wd);

    std::cout << "Running attempts for signal: " << sig_name(td_signal) << std::endl;
    std::cout << "Test case working dir: " << wd << std::endl;

    static std::string success_marker_path = (wd / "success.marker").generic_string();

    #if BOOST_OS_WINDOWS
      static std::string sleep_command = bp::search_path("timeout.exe").generic_string();
    #else
      static std::string sleep_command = bp::search_path("sleep").generic_string();
    #endif

    std::vector<std::string> command = {
      host_goldilock_executable_path(),
      "--lockfile", "mylock",
      "--lock-success-marker", success_marker_path,
      "--verbose",
      "--", sleep_command, "60" // <- 60! seconds to be extra long, we're recording the times and will be using this as a plausibility check
    };

    size_t count_success = 0;
    std::vector<size_t> attempt_times_ms; 
    size_t count_attempts = 0;
    size_t count_attempts_with_goldilock_proc_terminated_message = 0; // sanity check: count the number of times we get to see the "Error while waiting on child process - possibly terminated by signal" message so we know we actually trigger it - requires --verbose
    const size_t max_attempts = 100;   

    boost::asio::io_service ios;
    boost::asio::io_service::work work(ios);
    std::thread ios_thread([&ios]() { 
      ios.run(); 
    });

    while(count_attempts++ < max_attempts) {
      
      if(fs::exists(success_marker_path)) {
        fs::remove(success_marker_path);
      }

      std::cout << "Running attempt #" << count_attempts << std::endl;
      std::cout << "---------------------" << std::endl;
      std::chrono::steady_clock::time_point ts_attempt_begin = std::chrono::steady_clock::now();
      

      std::future<std::string> fut;
      bp::child child_process{command, ios, bp::start_dir=wd, (bp::std_out & bp::std_err) > fut, bp::std_in < stdin};
      
      while(!fs::exists(success_marker_path)) {
        std::this_thread::sleep_for(50ms);
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

        if(std::chrono::duration_cast<std::chrono::seconds>(now - ts_attempt_begin).count() > 30) {
          throw std::runtime_error("Failed to acquire goldilock within 30s - something's off - aborting");
        }
      }

      std::cout << "(test - sending signal to child process " << sig_name(td_signal) << " [" << td_signal << "])" << std::endl;
      kill(child_process.native_handle(), td_signal);
  
      try {
        if(child_process.joinable()) {
          child_process.wait();
        }
      }
      catch(...) {
        std::cout << "Caught exception while waiting for child to join - ignoring: " << boost::current_exception_diagnostic_information() << std::endl;
      }

      std::chrono::steady_clock::time_point ts_attempt_end = std::chrono::steady_clock::now();

      std::string output = fut.get();
      boost::algorithm::trim_right(output);

      std::cout << output << std::endl;
      std::cout << "---------------------" << std::endl;

      if(boost::regex_search(output, boost::regex{"uncaught exception"}) == false) {
        count_success++;
        size_t attempt_duration = std::chrono::duration_cast<std::chrono::milliseconds>(ts_attempt_end - ts_attempt_begin).count();
        attempt_times_ms.push_back(attempt_duration);
        
        std::cout 
          << "Attempt took:" << attempt_duration << "ms\n" 
          << "---------------------" << std::endl;
      }

      if(boost::regex_search(output, boost::regex{"Error while waiting on child process - possibly terminated by signal"}) == false) {
        count_attempts_with_goldilock_proc_terminated_message++;
      }
    }

    ios.stop();
    ios_thread.join();

    BOOST_REQUIRE(count_success == max_attempts);
    BOOST_REQUIRE(count_attempts_with_goldilock_proc_terminated_message > 0);

    auto average_attempt_time_ms = std::accumulate(attempt_times_ms.begin(), attempt_times_ms.end(), 0.0) / attempt_times_ms.size();    
    std::cout << "Average attempt time " << average_attempt_time_ms << "ms" << std::endl;
    BOOST_REQUIRE(average_attempt_time_ms < 10000);
  }
}
