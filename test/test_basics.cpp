#define BOOST_TEST_MODULE test_basics
#include <boost/test/included/unit_test.hpp> 
#include <boost/test/data/test_case.hpp>

#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/process.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

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

  BOOST_AUTO_TEST_CASE(goldilock_help_returns_success) {    
    auto result = run_goldilock_command("--help");
    BOOST_REQUIRE(result.return_code == 0);
  }

  BOOST_AUTO_TEST_CASE(goldilock_version_parseable) {
    auto result = run_goldilock_command("--version");
    BOOST_REQUIRE(result.return_code == 0);
    BOOST_REQUIRE(boost::regex_search(result.output, boost::regex{"goldilock v([\\d]+\\.[\\d]+\\.[\\d]+) \\(built from [\\w]{7}(?:-dirty)?\\)"}));
  }

  BOOST_AUTO_TEST_CASE(goldilock_forwards_child_process_output) {
    const std::string lockfile_name = "test.lock";
    const std::string random_input_for_echo = "TEST-"s + to_string(boost::uuids::random_generator()()) + "-TEST";
    auto wd = get_goldilock_case_working_dir();
    BOOST_REQUIRE(!fs::exists(wd));

    fs::create_directories(wd);
    auto result = run_goldilock_command_in(wd, "--lockfile", "test.lock", "--", "echo", random_input_for_echo);

    BOOST_REQUIRE(result.return_code == 0);
    BOOST_REQUIRE(result.output == random_input_for_echo);
  }

  BOOST_AUTO_TEST_CASE(goldilock_forwards_child_process_return_code) {
    const std::string lockfile_name = "test.lock";
    auto wd = get_goldilock_case_working_dir();
    BOOST_REQUIRE(!fs::exists(wd));
    fs::create_directories(wd);

    // note: on windows we could test up to full 16bits length, but this is kinda overkill already
    for(size_t ret = 0; ret < 128; ret++) {
      auto result = run_goldilock_command_in(wd, "--lockfile", "test.lock", "--", "exit", std::to_string(ret));
      std::cout<<"retrun code is "<<result.return_code<<" and expected is " <<ret<<std::endl;
      BOOST_REQUIRE(result.return_code == ret);
    }    

    for(size_t ret = 128; ret < 255; ret++) {
      auto result = run_goldilock_command_in(wd, "--lockfile", "test.lock", "--", "exit", std::to_string(ret));
      std::cout<<"retrun code is "<<result.return_code<<" and expected is " <<ret<<std::endl;
      BOOST_REQUIRE(result.return_code == ret);
    }    
  }

  // check the backing tool works as expected
  BOOST_AUTO_TEST_CASE(test_support_tools_support_app_append_to_file) {
    auto wd = get_goldilock_case_working_dir();
    fs::create_directories(wd);

    fs::path test_file = wd / "test.txt";
    // 
    auto support_app_append_to_file_bin = fs::current_path() / "test" / host_executable_name("support_app_append_to_file");

    {
      auto result = run_cmd(bp::start_dir=wd, support_app_append_to_file_bin, "-s", "x", "-n", "10", "-f", test_file.generic_string(), "-i", "10"); // add 10x "x" to test.txt with 10ms interval
      BOOST_REQUIRE(result.return_code == 0);
      auto file_content = tipi::goldilock::file::read_file_content(test_file);
      BOOST_REQUIRE(file_content == "xxxxxxxxxx");
    }    

    {
      auto result = run_cmd(bp::start_dir=wd, support_app_append_to_file_bin, "-s", "Y", "-n", "1", "-f", test_file.generic_string(), "-i", "10"); // add 1x "Y" to test.txt with 10ms interval
      BOOST_REQUIRE(result.return_code == 0);
      auto file_content = tipi::goldilock::file::read_file_content(test_file);
      BOOST_REQUIRE(file_content == "xxxxxxxxxxY");
    }

    // now for interleaved writes
    fs::path test_file_interleaved = wd / "test_interleaved.txt";
    {

      auto write_letter_fn = [&](std::string chr, size_t interval) {
        auto result = run_cmd(bp::start_dir=wd, support_app_append_to_file_bin, "-s", chr, "-n", "100", "-f", test_file_interleaved.generic_string(), "-i", std::to_string(interval));
        BOOST_REQUIRE(result.return_code == 0);
      };

      std::thread t1([&](){ write_letter_fn("A", 1); });
      std::thread t2([&](){ write_letter_fn("b", 2); });
      std::thread t3([&](){ write_letter_fn("Z", 5); });

      t1.join();
      t2.join();
      t3.join();

      auto file_content = tipi::goldilock::file::read_file_content(test_file_interleaved);
      std::cout << "Testing interleaved write output:\n-------------\n" << file_content << "\n-------------\nExpecting some mix" << std::endl;
      
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"^([AbZ]{300})$"}));
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[A]{100}"}) == false);
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[b]{100}"}) == false);
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[Z]{100}"}) == false);
    }
  }

  BOOST_AUTO_TEST_CASE(simple_goldilocked_write) {
    auto wd = get_goldilock_case_working_dir();
    fs::create_directories(wd);

    const fs::path support_app_append_to_file_bin = fs::current_path() / "test" / host_executable_name("support_app_append_to_file");
    const std::string lockfile_name = "test.lock";
    const fs::path write_output_dest = wd / "test.txt";

    auto write_letter_fn = [&](std::string chr, size_t interval) {
      auto result = run_goldilock_command_in(wd, "--lockfile", "test.lock", "--", support_app_append_to_file_bin, "-s", chr, "-n", "100", "-f", write_output_dest.generic_string(), "-i", std::to_string(interval));
      BOOST_REQUIRE(result.return_code == 0);
    };

    std::thread t1([&](){ write_letter_fn("A", 1); });
    std::thread t2([&](){ write_letter_fn("b", 2); });
    std::thread t3([&](){ write_letter_fn("Z", 1); });

    t1.join();
    t2.join();
    t3.join();

    auto file_content = tipi::goldilock::file::read_file_content(write_output_dest);
    std::cout << "Testing goldilocked interleaved write output:\n-------------\n" << file_content << "\n-------------\nExpecting no mixing of A|b|Z" << std::endl;
    
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"^([AbZ]{300})$"}));
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[A]{100}"}));
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[b]{100}"}));
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[Z]{100}"}));
  }

  BOOST_AUTO_TEST_CASE(goldilocked_write_multiple_lockfiles) {

    auto wd = get_goldilock_case_working_dir();
    fs::create_directories(wd);

    const fs::path support_app_append_to_file_bin = fs::current_path() / "test" / host_executable_name("support_app_append_to_file");
    const fs::path write_output_dest = wd / "test.txt";

    // Test a situation with multiple lockfiles
    //
    // goldilock "Master" will lock lockfile_A lockfile_B lockfile_C lockfile_D
    // goldilock "A"      will lock lockfile_A lockfile_B lockfile_C
    // goldilock "B"      will lock            lockfile_B lockfile_C
    // goldilock "C"      will lock            lockfile_B            lockfile_D
    // goldilock "D"      will lock lockfile_A lockfile_B lockfile_C lockfile_D
    //
    // We will start the Master first and have it wait for an unlock file to appear
    // then, we will start all other goldilocks and release the Master

    const std::string master_unlockfile = (wd / "master_unlockfile").generic_string();
    const std::string master_all_locks_acquired = (wd / "master_locked.marker").generic_string();
    const std::string gl_A_lockfile = (wd / "lockfile_A").generic_string();
    const std::string gl_B_lockfile = (wd / "lockfile_B").generic_string();
    const std::string gl_C_lockfile = (wd / "lockfile_C").generic_string();
    const std::string gl_D_lockfile = (wd / "lockfile_D").generic_string();


    std::thread t_master([&](){ 
      auto result = run_goldilock_command_in(wd, 
        "--lockfile", gl_A_lockfile,
        "--lockfile", gl_B_lockfile,
        "--lockfile", gl_C_lockfile,
        "--lockfile", gl_D_lockfile,
        "--unlockfile", master_unlockfile,
        "--lock-success-marker", master_all_locks_acquired
      );      
      
      BOOST_REQUIRE(result.return_code == 0);
    });
    
    BOOST_REQUIRE(wait_for_file(master_all_locks_acquired));

    //
    // now start all the other goldilocks
    std::thread t_gl_A([&](){ 
      auto result = run_goldilock_command_in(wd, 
        "--lockfile", gl_A_lockfile, "--lockfile", gl_B_lockfile, "--lockfile", gl_C_lockfile, 
        "--", 
          support_app_append_to_file_bin, 
            "-s", "A", // <---
            "-n", "100", 
            "-f", write_output_dest.generic_string(), 
            "-i", "1"
      );
      BOOST_REQUIRE(result.return_code == 0);
    });

    std::thread t_gl_B([&](){ 
      auto result = run_goldilock_command_in(wd, 
        "--lockfile", gl_B_lockfile, "--lockfile", gl_C_lockfile, 
        "--", 
          support_app_append_to_file_bin, 
            "-s", "B",  // <---
            "-n", "100", 
            "-f", write_output_dest.generic_string(), 
            "-i", "1"
      );
      BOOST_REQUIRE(result.return_code == 0);
    });

    std::thread t_gl_C([&](){ 
      auto result = run_goldilock_command_in(wd, 
        "--lockfile", gl_B_lockfile, "--lockfile", gl_D_lockfile, 
        "--", 
          support_app_append_to_file_bin, 
            "-s", "C",  // <---
            "-n", "100", 
            "-f", write_output_dest.generic_string(), 
            "-i", "1"
      );
      BOOST_REQUIRE(result.return_code == 0);
    });

    std::thread t_gl_D([&](){ 
      auto result = run_goldilock_command_in(wd,
        "--lockfile", gl_A_lockfile, "--lockfile", gl_B_lockfile, "--lockfile", gl_C_lockfile, "--lockfile", gl_D_lockfile, 
        "--", 
          support_app_append_to_file_bin, 
            "-s", "D",  // <---
            "-n", "100", 
            "-f", write_output_dest.generic_string(), 
            "-i", "1"
      );
      BOOST_REQUIRE(result.return_code == 0);
    });


    // here, none of the goldilocks above should be released or have started their configured app
    // so the output should not yet exist!
    BOOST_REQUIRE(!fs::exists(write_output_dest));

    // this should release 
    tipi::goldilock::file::touch_file(master_unlockfile);
    BOOST_REQUIRE(wait_for_file(write_output_dest));

    t_master.join();
    t_gl_A.join();
    t_gl_B.join();
    t_gl_C.join();
    t_gl_D.join();

    auto file_content = tipi::goldilock::file::read_file_content(write_output_dest);
    std::cout << "Testing goldilocked multilogfile write output:\n-------------\n" << file_content << "\n-------------\nExpecting no mixing of A|B|C|D" << std::endl;
    
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"^([ABCD]{400})$"}));
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[A]{100}"}));
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[B]{100}"}));
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[C]{100}"}));
    BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[D]{100}"}));
  }

  static auto TEST_DATA_goldilock_lock_watch_parent_process__search_nearest = {
    true,
    false
  };

  BOOST_DATA_TEST_CASE(
    goldilock_lock_watch_parent_process, 
    boost::unit_test::data::make(TEST_DATA_goldilock_lock_watch_parent_process__search_nearest), 
    td_search_nearest
  ){
    auto wd = get_goldilock_case_working_dir();
    fs::create_directories(wd);

    std::cout << "Working directory: " << wd << std::endl;

    const std::string launcher_exe_name = host_executable_name("support_app_launcher");
    const std::string support_app_launcher_bin = (fs::current_path() / "test" / launcher_exe_name).generic_string();

    const std::string path_goldilock_watching_parent_lock_acquired_marker = (wd / "watcher_all_locks.marker").generic_string();
    const std::string path_goldilock_stage2_lock_acquired_marker = (wd / "stage2_all_locks.marker").generic_string();
    const std::string lockfile = (wd / "lockfile").generic_string();

    const std::string path_pidfile_A = (wd / "pidfile_level_A").generic_string();
    const std::string path_pidfile_B = (wd / "pidfile_level_B").generic_string();
    const std::string path_pidfile_C = (wd / "pidfile_level_C").generic_string();

    const std::string path_watchfile_A = (wd / "watchfile_level_A").generic_string();
    const std::string path_watchfile_B = (wd / "watchfile_level_B").generic_string();
    const std::string path_watchfile_C = (wd / "watchfile_level_C").generic_string();

    std::vector<std::string> launch_chain;

    auto add_launcher_lvl = [&](const std::string& watchfile, const std::string& pidfile) {      
      launch_chain.push_back(support_app_launcher_bin);
      launch_chain.push_back("-w");
      launch_chain.push_back(watchfile);
      launch_chain.push_back("-p");
      launch_chain.push_back(pidfile);
      launch_chain.push_back("--");
    };  

    add_launcher_lvl(path_watchfile_A, path_pidfile_A);  
    add_launcher_lvl(path_watchfile_B, path_pidfile_B);
    add_launcher_lvl(path_watchfile_C, path_pidfile_C);

    // have one goldilock:
    // - grab a lock
    // - tell us it's got the lock
    // - wait until the matched parent process exits
    launch_chain.push_back(host_goldilock_executable_path());
    launch_chain.push_back("--lockfile");
    launch_chain.push_back(lockfile);
    launch_chain.push_back("--unlockfile");
    launch_chain.push_back("will_never_be_there.txt");
    launch_chain.push_back("--lock-success-marker");
    launch_chain.push_back(path_goldilock_watching_parent_lock_acquired_marker);
    launch_chain.push_back("--watch-parent-process");
    launch_chain.push_back(launcher_exe_name);
    
    if(td_search_nearest) {
      std::cout << "Adding search nearest parameter to goldilock command" << std::endl;
      launch_chain.push_back("--search-nearest-parent-process");
    }

    std::cout << "Running command: '";
    for(const auto& arg : launch_chain) {
      std::cout << arg << " ";
    }
    std::cout << "'" << std::endl;

    auto check_process_running = [](const fs::path& pidfile) {
      std::string fcontents = tipi::goldilock::file::read_file_content(pidfile);
      return tipi::goldilock::process_info::is_process_running(std::stoi(fcontents));
    };

    auto test_launcher_exited = [&](const fs::path& pidfile) {
      size_t retries = 10;      
      bool launcher_running = check_process_running(pidfile);
      
      while(launcher_running && --retries > 0) {
        launcher_running = check_process_running(pidfile);
        if(launcher_running) {
          std::this_thread::sleep_for(100ms);
        }
      }

      std::cout << "Is running (" << pidfile << ") :" << std::to_string(launcher_running) << std::endl;;

      return !launcher_running;
    };

    // run in bg
    std::thread t_watcher([&](){ 
      auto result = run_cmd(bp::start_dir=wd, launch_chain);
      BOOST_REQUIRE(result.return_code == 0);
    });
    
    BOOST_REQUIRE(wait_for_file(path_pidfile_A));
    BOOST_REQUIRE(wait_for_file(path_pidfile_B));
    BOOST_REQUIRE(wait_for_file(path_pidfile_C));
    BOOST_REQUIRE(wait_for_file(path_goldilock_watching_parent_lock_acquired_marker));

    bool expected_stage2_to_return = false;

    std::thread t_stage2([&](){ 
      BOOST_REQUIRE(expected_stage2_to_return == false);
      auto result = run_goldilock_command_in(wd, "--lockfile", lockfile, "--lock-success-marker", path_goldilock_stage2_lock_acquired_marker, "--", "exit", "0");
      BOOST_REQUIRE(result.return_code == 0);
      BOOST_REQUIRE(expected_stage2_to_return == true);
    });
    
    BOOST_REQUIRE(wait_for_file(path_goldilock_stage2_lock_acquired_marker) == false);
    
    BOOST_REQUIRE(check_process_running(path_pidfile_A));
    BOOST_REQUIRE(check_process_running(path_pidfile_C));

    // in any case we should be able to kill the "middle one" and keep up the lock
    // as we're not watching that one anyway...
    {
      BOOST_REQUIRE(check_process_running(path_pidfile_B));
      tipi::goldilock::file::touch_file(path_watchfile_B);
      BOOST_REQUIRE(test_launcher_exited(path_pidfile_B));

      // !! we hold the lock in the watcher still
      BOOST_REQUIRE(wait_for_file(path_goldilock_stage2_lock_acquired_marker) == false);
    }
    
    if(td_search_nearest) {
      // if goldilock is watching the "nearest" launcher, then we should be able to kill
      // levels A and B and still be waiting
      BOOST_REQUIRE(check_process_running(path_pidfile_A));
      tipi::goldilock::file::touch_file(path_watchfile_A);
      BOOST_REQUIRE(test_launcher_exited(path_pidfile_A));
    }
    else {
      // if we're watching the furthest launcher we can kill B and C and should be holding
      // the "watcher lock"
      BOOST_REQUIRE(check_process_running(path_pidfile_C));
      tipi::goldilock::file::touch_file(path_watchfile_C);
      BOOST_REQUIRE(test_launcher_exited(path_pidfile_C)); 
    }

    // !! we hold the lock in the watcher still
    BOOST_REQUIRE(wait_for_file(path_goldilock_stage2_lock_acquired_marker) == false);

    // now we will have the watched launcher terminate
    // and check the the stage2 gets the lock and exists as planned
    expected_stage2_to_return = true;

    if(td_search_nearest) {
      BOOST_REQUIRE(check_process_running(path_pidfile_C));
      tipi::goldilock::file::touch_file(path_watchfile_C);
      BOOST_REQUIRE(test_launcher_exited(path_pidfile_C));
    }
    else {
      BOOST_REQUIRE(check_process_running(path_pidfile_A));
      tipi::goldilock::file::touch_file(path_watchfile_A);
      BOOST_REQUIRE(test_launcher_exited(path_pidfile_A)); 
    }

    // we got the lock!!
    BOOST_REQUIRE(wait_for_file(path_goldilock_stage2_lock_acquired_marker));

    // exit
    t_watcher.join();
    t_stage2.join();

    BOOST_REQUIRE(test_launcher_exited(path_pidfile_A)); 
    BOOST_REQUIRE(test_launcher_exited(path_pidfile_B)); 
    BOOST_REQUIRE(test_launcher_exited(path_pidfile_C)); 

  }
}
