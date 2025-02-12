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


  struct task_wrapper {
    size_t task_index;
    bool started;
    bp::child child_process;

    template <class... Param>
    task_wrapper(size_t ix, boost::asio::io_context& io_context, Param &&... cmd) 
      : task_index{ix}
      , started{false}
      , child_process{io_context, cmd..., bp::extend::on_success=[this](auto & exec) { started = true; }}
    {
      //
    }
  };

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
    
    // a small not to future self and elsewhom will be reading this 
    // this concurrency test not only tests if many goldilock instances will
    // be able to work through the queue, but does this in an interlocking
    // fashion because each goldilock tries to acquire two distinct lockfile
    // so there's a theoretical (nCores * mult) * (nCores * mult) combinatorial of sequences
    // that might have to be tried in the very worst case. 
    //
    // So setting mult = 4 as below, is quite a lot - independently of which
    // machine we're looking at    
    const size_t tasks_expected = std::thread::hardware_concurrency() * 4;
    
    std::atomic_size_t tasks_done = 0;
    std::atomic_size_t tasks_failed = 0;
    bool master_lock_released = false;

    std::vector<std::shared_ptr<task_wrapper>> tasks;

    size_t task_ix = 0;
    size_t task_creation_failed = 0;
    while(task_ix < tasks_expected) {

      std::cout << "Creating task " << task_ix << std::endl;
      auto task = std::make_shared<task_wrapper>(task_ix, ios, 
        host_goldilock_executable_path(), "--lockfile", gl_A_lockfile, "--lockfile", gl_B_lockfile, "--", support_app_append_to_file_bin, "-s", std::to_string(task_ix) + ":"s, "-n", "5", "-f", write_output_dest.generic_string(), "-i", "1" ,
        bp::start_dir=wd, bp::std_out > stdout, bp::std_err > stderr, bp::std_in < bp::null,
        bp::on_exit=[task_ix=task_ix, &tasks_done, &tasks_failed, &bench_start, &master_lock_released](int exit_code, std::error_code const& ec) {
          std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
          std::cout << "Task " << task_ix << " returned with code: " << exit_code << " (after " << std::chrono::duration_cast<std::chrono::milliseconds>(now - bench_start).count() << "ms)" << std::endl;
          
          if(ec) {
            std::cout << "Error code = " << ec.message() << std::endl;
          }

          if(!master_lock_released) {
            std::cout << "Task finished before master lock was released!" << std::endl;
          }

          tasks_done++;
          
          if(exit_code != 0) {
            tasks_failed++;
            BOOST_REQUIRE(exit_code == 0);
          }
        }        
      );

      std::cout << " - waiting to for task " << task_ix << " to start" << std::endl;

      size_t wait_iteration = 0;
      while(!task->started && task->child_process.running()) {
        std::cout << "." << std::to_string(task->started) << std::ends;
        std::this_thread::sleep_for(10ms);

        if(wait_iteration++ % 50 == 0) { /* every half a second or so */
          std::cout << "." << std::to_string(task->started) << std::ends;
        }        
      }

      if(task->started && task->child_process.running()) {
        std::cout << " - SUCCESS" << std::endl;
        task_ix++;
        tasks.push_back(task);
      }
      else {
        std::cout << " - FAILED" << std::endl;
        BOOST_REQUIRE(task_creation_failed++ < 10); // should not fail too often...
      }
    }
    
    // now's the real starting point....
    bench_start = std::chrono::steady_clock::now();

    std::cout << "Releasing master lock" << std::endl;
    master_lock_released = true;
    tipi::goldilock::file::touch_file_permissive(master_unlockfile);
    
    std::chrono::steady_clock::time_point ts_master_lock_released = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point bench_expiry = ts_master_lock_released + 180s;

    std::cout << "Running for 180s..." << std::endl;

    while(std::chrono::steady_clock::now() < bench_expiry && tasks_done < tasks_expected) {
      std::cout << "(status) tasks completed: " << tasks_done << " out of " << tasks_expected << std::endl;
      std::this_thread::sleep_for(1s);
    }

    BOOST_ASSERT(tasks_done == tasks_expected);

    std::chrono::steady_clock::time_point ts_all_tasks_finished = std::chrono::steady_clock::now();
    auto all_tasks_duration = std::chrono::duration_cast<std::chrono::milliseconds>(ts_all_tasks_finished - ts_master_lock_released).count();
    std::cout << "All tasks completed in: " << all_tasks_duration << "ms" << std::endl;
    std::cout << "Average time per tasks: " << (all_tasks_duration / tasks_expected)  << "ms" << std::endl;

    for(auto& chld : tasks) {
      if(chld->child_process.running()) {
        chld->child_process.terminate();
      }
    }

    BOOST_ASSERT(tasks_failed == 0);


    auto file_content = tipi::goldilock::file::read_file_content(write_output_dest);
    std::cout << "Testing goldilocked write output:\n-------------\n" << file_content << "\n-------------\nExpecting no mixing of entries" << std::endl;

    for(size_t task_ix = 0; task_ix < tasks_expected; task_ix++) {
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"(("s + std::to_string(task_ix) + ":){5})"s})); // search for "<task_ix>:<task_ix>:<task_ix>:<task_ix>:<task_ix>:"
    }

    tasks.clear();
    ios.stop();
    threads.join_all();
    t_master.join();

  }
}
