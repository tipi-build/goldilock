#define BOOST_TEST_MODULE test_basics
#include <boost/test/included/unit_test.hpp> 

#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/process.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include <test_helpers.hpp>
#include <string>
#include <vector>
#include <optional>
#include <thread>

#include <goldilock/file.hpp>

 
namespace goldilock::test { 
  namespace fs = boost::filesystem;
  namespace bp = boost::process;

  using namespace std::literals;

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
    for(size_t ret = 0; ret < 255; ret++) {
      auto result = run_goldilock_command_in(wd, "--lockfile", "test.lock", "--", "exit", std::to_string(ret));
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

    auto wait_for_file = [](const fs::path& path) {
      bool found_file = false;
      size_t retries = 50;
      while(--retries > 0) {
        found_file = fs::exists(path); 
        if(!found_file) {
          std::this_thread::sleep_for(50ms);
        }
        else {
          break;
        }
      }

      return found_file;
    };
    
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
}
