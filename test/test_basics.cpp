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

  
}
