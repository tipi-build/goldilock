#define BOOST_TEST_MODULE test_benchmark
#include <boost/test/included/unit_test.hpp> 
#include <boost/test/data/test_case.hpp>

#include <boost/predef.h>

#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/process.hpp>
#include <boost/process/extend.hpp>
#include <boost/asio/detail/signal_init.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/scope_exit.hpp>
#include <boost/thread.hpp>

#include <test_helpers.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <thread>

#include <goldilock/file.hpp>
#include <goldilock/process_info.hpp>

 
namespace goldilock::test { 
  namespace fs = boost::filesystem;
  namespace bp = boost::process;

  using namespace std::literals;

  BOOST_AUTO_TEST_CASE(goldilocked_write_many_instances) {

    auto wd = get_goldilock_case_working_dir();
    fs::create_directories(wd);

    const fs::path support_app_append_to_file_bin = get_executable_path_from_test_env("support_app_append_to_file");
    const fs::path write_output_dest = wd / "test.txt";

    const std::string master_unlockfile = (wd / "master_unlockfile").generic_string();
    const std::string master_all_locks_acquired = (wd / "master_locked.marker").generic_string();
    const std::string gl_A_lockfile = (wd / "lockfile_A").generic_string();
    const std::string gl_B_lockfile = (wd / "lockfile_B").generic_string();


    std::thread t_master([&](){ 
      auto result = run_goldilock_command_in(wd, 
        "--lockfile", gl_A_lockfile,
        "--lockfile", gl_B_lockfile,
        "--verbose", 
        "--unlockfile", master_unlockfile,
        "--lock-success-marker", master_all_locks_acquired
      );      
      
      BOOST_REQUIRE(result.return_code == 0);
    });
    
    BOOST_REQUIRE(wait_for_file(master_all_locks_acquired));


    boost::asio::io_context ios;
    boost::asio::io_service::work work(ios);
    boost::thread_group threads;
    for (std::size_t i = 0; i < 1; ++i) {
      threads.create_thread([&ios]() {
        ios.run();
      });
    }
    
    std::chrono::steady_clock::time_point bench_start = std::chrono::steady_clock::now();
    const size_t tasks_expected = std::thread::hardware_concurrency() * 4; // 4 exe's per core...
    std::atomic_size_t tasks_done = 0;

    std::vector<bp::child> child_processes;

    // submit 100 goldilocks...
    for(size_t task_ix = 0; task_ix < tasks_expected; task_ix++) {

      auto fut_ptr = std::make_shared<std::future<std::string>>();
      child_processes.emplace_back(
        host_goldilock_executable_path(), "--lockfile", gl_A_lockfile, "--lockfile", gl_B_lockfile, "--", support_app_append_to_file_bin, "-s", std::to_string(task_ix) + ":"s, "-n", "5", "-f", write_output_dest.generic_string(), "-i", "1" ,      
        ios, bp::start_dir=wd, (bp::std_out & bp::std_err) > *fut_ptr, bp::std_in < bp::null,
        bp::on_exit=[task_ix=task_ix, &tasks_done, bench_start, &fut_ptr](int exit_code, std::error_code const& ec) {
          std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
          std::cout << "Task " << task_ix << " returned with code: " << exit_code << " (after " << std::chrono::duration_cast<std::chrono::milliseconds>(now - bench_start).count() << "ms)" << std::endl;
          
          if(ec) {
            std::cout << "Error code = " << ec.message() << std::endl;
          }

          if(exit_code != 0) {
            std::cout << "----------\n" << fut_ptr->get() << "\n-----------" << std::endl;
          }
          
          tasks_done++;
        }        
      );

      // don't start them too quickly to not overwhelm the os
      std::cout << "Starting task " << task_ix << std::endl;
      std::this_thread::sleep_for(100ms);
    }

    // now's the real starting point....
    bench_start = std::chrono::steady_clock::now();

    std::cout << "Releasing master lock" << std::endl;
    tipi::goldilock::file::touch_file_permissive(master_unlockfile);
    std::chrono::steady_clock::time_point bench_expiry = std::chrono::steady_clock::now() + 180s;

    std::cout << "Running for 180s..." << std::endl;

    while(std::chrono::steady_clock::now() < bench_expiry && tasks_done < tasks_expected) {
      std::cout << "Tasks completed: " << tasks_done << " out of " << tasks_expected << std::endl;
      std::this_thread::sleep_for(1s);
    }

    BOOST_ASSERT(tasks_done == tasks_expected);

    for(auto& chld : child_processes) {
      if(chld.running()) {
        chld.terminate();
      }
    }

    auto file_content = tipi::goldilock::file::read_file_content(write_output_dest);
    std::cout << "Testing goldilocked multilogfile write output:\n-------------\n" << file_content << "\n-------------\nExpecting no mixing of entries" << std::endl;

    for(size_t task_ix = 0; task_ix < tasks_expected; task_ix++) {
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"(("s + std::to_string(task_ix) + ":){5})"s})); // search for "<task_ix>:<task_ix>:<task_ix>:<task_ix>:<task_ix>:"
    }

    child_processes.clear();
    ios.stop();
    threads.join_all();
    t_master.join();



  }
}
