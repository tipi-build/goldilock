#define BOOST_TEST_MODULE test_basics
#include <boost/test/included/unit_test.hpp> 
#include <boost/test/data/test_case.hpp>

#include <boost/predef.h>

#include <boost/filesystem.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/process.hpp>
#include <boost/asio/detail/signal_init.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/scope_exit.hpp>

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

  inline std::string docker_bin() {
    static const std::string docker_path = bp::search_path(host_executable_name("docker")).generic_path().generic_string();
    assert(docker_path != "");
    return docker_path;
  }

  inline std::string docker_container_status(const std::string& name) {
    auto ret = run_cmd(docker_bin(), "inspect -f {{.State.Status}} "s + name);
    return ret.output;
  }

  template <class... Param> 
  inline run_cmd_result_t run_docker_cmd(Param &&... args) {

    std::cout << "Running docker command: " << docker_bin();
    ((std::cout << ' ' << std::forward<Param>(args)), ...); 
    std::cout << std::endl;

    auto result = goldilock::test::run_cmd(docker_bin(), args...);
    
    if(result.return_code != 0) {
      std::cout << "Command output:\n" 
        << "------------\n" 
        << result.output 
        << "\n------------" 
        << std::endl;
    }    

    return result;
  }

  inline void docker_build_image(const fs::path& context_root, const std::string image_tag) {
    std::cout << "Buiding docker container " << context_root << " (target tag: '" << image_tag << "')" << std::endl;
    fs::path test_env_goldilock = get_executable_path_from_test_env("goldilock");
    
    // we might have an override goldilock to use (I'm on windows... duh)
    try {
      const fs::path test_env_OVERRIDE_goldilock = get_executable_path_from_test_env("OVERRIDE_goldilock");
      test_env_goldilock = test_env_OVERRIDE_goldilock;
      std::cout << "Using overridden goldilock at: " << test_env_goldilock << std::endl;
    }
    catch(...) { /* ignore */ }
    
    const fs::path context_goldilock_path = (context_root / "goldilock").generic_path();
    if(fs::exists(context_goldilock_path)) {
      fs::remove(context_goldilock_path);
    }

    fs::copy(test_env_goldilock, context_goldilock_path);

    std::vector<std::string> docker_build_cmd_args = { "build", "--tag", image_tag, "." };

    // just for docker build...
    bp::environment env = boost::this_process::environment();
    env["DOCKER_BUILDKIT"] = "1";

    std::cout << "Running docker command: " << docker_bin() << boost::algorithm::join(docker_build_cmd_args, " ") << std::endl;
    auto result = goldilock::test::run_cmd(env, bp::start_dir=context_root, docker_bin(), docker_build_cmd_args);

    if(result.return_code != 0) {
      std::cout << "Command output:\n" 
        << "------------\n" 
        << result.output 
        << "\n------------" 
        << std::endl;

      throw std::runtime_error("docker build failed");
    }
  }

  bool stop_container(const std::string& id) {      
    auto result = run_docker_cmd("stop", id);

    if(result.return_code != 0) {
      throw std::runtime_error("docker stop failed");
    }

    return id  == result.output;
  }
  
  bool rm_container(const std::string& id) {
    auto result = run_docker_cmd("rm", id);

    if(result.return_code != 0) {
      throw std::runtime_error("docker stop failed");
    }

    return id  == result.output;
  }

  const std::string goldilock_minimal_image_tag = "goldilock-"s + to_string(boost::uuids::random_generator()()).substr(0, 12);
  static std::vector<std::string> container_ids{};

  struct docker_fixture {
    docker_fixture() { 
      std::cout << "Global fixture / ensuring docker image is available" << std::endl;
      fs::path test_data_root{get_string_from_env("GOLDILOCK_TEST_ROOT_DIRECTORY")};
      docker_build_image(test_data_root / "Dockerfiles" / "minimal", goldilock_minimal_image_tag);
    }

    ~docker_fixture() { 
      std::cout << "Global fixture / cleaning up containers" << std::endl;
      for(const auto& container_id : container_ids) {
        stop_container(container_id);
        rm_container(container_id);
      }
    }

    //inline 
  };

  template <class... Param> 
  inline std::string start_container(const std::string& command = "sleep infinity", Param &&... args) {
    auto result = run_docker_cmd("run", "-d", args..., goldilock_minimal_image_tag, command);

    if(result.return_code != 0) {
      throw std::runtime_error("docker failed to start container");
    }

    // this is a global...
    container_ids.push_back(result.output);
    return result.output; // this is the container id
  }

  // one fixture for everyone \ö/
  BOOST_TEST_GLOBAL_FIXTURE(docker_fixture);

  BOOST_AUTO_TEST_CASE(goldilock_version_returns_success) {
    auto id = start_container();
    auto result = run_docker_cmd("exec", id, "/usr/bin/goldilock", "--version"s);
    BOOST_REQUIRE(result.return_code == 0);
    BOOST_REQUIRE(boost::regex_search(result.output, boost::regex{"goldilock v([\\d]+\\.[\\d]+\\.[\\d]+) \\(built from [\\w]{7}(?:-dirty)?\\)"}));


    // verify we're actually running the expected version inside the container
    auto host_run_result = run_goldilock_command("--version");
    BOOST_REQUIRE(host_run_result.return_code == 0);
    BOOST_REQUIRE(host_run_result.output == result.output);
  }

  BOOST_AUTO_TEST_CASE(goldilock_help_returns_success) {
    auto id = start_container();
    auto result = run_docker_cmd("exec", id, "/usr/bin/goldilock", "--help"s);
    BOOST_REQUIRE(result.return_code == 0);
    std::cout << "goldilock help:\n------------\n" << result.output << "\n------------" << std::endl;
  }

  BOOST_AUTO_TEST_CASE(goldilock_docker_volume) {

  }

}
