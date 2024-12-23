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

  inline void docker_build_prep_copy_executable(const std::string& executable_name, const fs::path& context_root) {

    std::cout << " - copying executable to docker context: " << executable_name << std::endl;
    fs::path test_env_executable = get_executable_path_from_test_env(executable_name);
    
    // we might have an override goldilock to use (I'm on windows... duh)
    try {
      const fs::path test_env_OVERRIDE = get_executable_path_from_test_env("OVERRIDE_"s + executable_name);
      test_env_executable = test_env_OVERRIDE;
      std::cout << " - using overridden " << test_env_OVERRIDE << " at: " << test_env_executable << std::endl;
    }
    catch(...) { /* ignore */ }
    
    const fs::path target_path = (context_root / executable_name).generic_path();
    if(fs::exists(target_path)) {
      fs::remove(target_path);
    }

    fs::copy(test_env_executable, target_path);
  }

  inline void docker_build_image(const fs::path& context_root, const std::string image_tag) {
    std::cout << "Buiding docker container " << context_root << " (target tag: '" << image_tag << "')" << std::endl;
    docker_build_prep_copy_executable("goldilock", context_root);
    docker_build_prep_copy_executable("support_app_append_to_file", context_root);

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

  bool stop_container(const std::string& id, bool kill = false) {      
    auto result = run_docker_cmd((kill) ? "kill" : "stop", id);

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

  struct docker_global_fixture {
    docker_global_fixture() { 
      std::cout << "Global fixture / ensuring docker image is available" << std::endl;
      fs::path test_data_root{get_string_from_env("GOLDILOCK_TEST_ROOT_DIRECTORY")};
      docker_build_image(test_data_root / "Dockerfiles" / "minimal", goldilock_minimal_image_tag);
    }
  };

  //!\brief collect the containers and stop them
  struct docker_case_fixture {
    docker_case_fixture() { 
      // nil
    }

    ~docker_case_fixture() { 
      std::cout << "Test case fixture / cleaning up containers" << std::endl;
      for(const auto& container_id : container_ids) {
        stop_container(container_id, /*kill*/ true);
        rm_container(container_id);
      }
    }

    std::vector<std::string> container_ids{};

    template <class... Param> 
    inline std::string start_container(const std::string& command = "sleep infinity", Param &&... args) {
      std::cout << "Starting container with image " << goldilock_minimal_image_tag << std::endl;
      auto result = run_docker_cmd("run", "--init", "-d", args..., goldilock_minimal_image_tag, command);

      if(result.return_code != 0) {
        throw std::runtime_error("docker failed to start container");
      }

      std::cout << " -> container id " << result.output << std::endl;

      // this is a global...
      container_ids.push_back(result.output);
      return result.output; // this is the container id
    }
  };


  // one fixture for everyone to ensure that the image(s) exit \ö/ 
  BOOST_TEST_GLOBAL_FIXTURE(docker_global_fixture);

  BOOST_FIXTURE_TEST_CASE(goldilock_version_returns_success, docker_case_fixture) {
    auto id = start_container();
    auto result = run_docker_cmd("exec", id, "/usr/bin/goldilock", "--version"s);
    BOOST_REQUIRE(result.return_code == 0);
    BOOST_REQUIRE(boost::regex_search(result.output, boost::regex{"goldilock v([\\d]+\\.[\\d]+\\.[\\d]+) \\(built from [\\w]{7}(?:-dirty)?\\)"}));


    // verify we're actually running the expected version inside the container
    auto host_run_result = run_goldilock_command("--version");
    BOOST_REQUIRE(host_run_result.return_code == 0);
    BOOST_REQUIRE(host_run_result.output == result.output);
  }

  BOOST_FIXTURE_TEST_CASE(goldilock_help_returns_success, docker_case_fixture) {
    auto id = start_container();
    auto result = run_docker_cmd("exec", id, "/usr/bin/goldilock", "--help"s);
    BOOST_REQUIRE(result.return_code == 0);
    std::cout << "goldilock help:\n------------\n" << result.output << "\n------------" << std::endl;
  }

  BOOST_FIXTURE_TEST_CASE(goldilock_docker_volume, docker_case_fixture) {
    // docker in docker // wsl path mapping trickery
    const auto fallback_shared_volume_path = get_goldilock_case_working_dir().generic_path().generic_string();
    const std::string  test_env__SHARED_VOLUME_CONTAINER = get_string_from_env("GOLDILOCK_TEST_DIND_SHARED_VOLUME_CONTAINER", fallback_shared_volume_path);
    const std::string  test_env__SHARED_VOLUME_HOST = get_string_from_env("GOLDILOCK_TEST_DIND_SHARED_VOLUME_HOST", fallback_shared_volume_path);
    const std::string  test_env__SHARED_VOLUME_TEST = get_string_from_env("GOLDILOCK_TEST_DIND_SHARED_VOLUME_TEST", fallback_shared_volume_path);

    if(!fs::is_directory(test_env__SHARED_VOLUME_HOST)) {
      fs::create_directories(test_env__SHARED_VOLUME_HOST);
    }    

    std::cout << "test_env__SHARED_VOLUME_CONTAINER =" << test_env__SHARED_VOLUME_CONTAINER << std::endl;
    std::cout << "test_env__SHARED_VOLUME_HOST =" << test_env__SHARED_VOLUME_HOST << std::endl;

    auto container_1_id = start_container("sleep infinity", /* additional docker args */ "-v", test_env__SHARED_VOLUME_HOST + ":"s + test_env__SHARED_VOLUME_CONTAINER);
    auto container_2_id = start_container("sleep infinity", /* additional docker args */ "-v", test_env__SHARED_VOLUME_HOST + ":"s + test_env__SHARED_VOLUME_CONTAINER);
    auto container_3_id = start_container("sleep infinity", /* additional docker args */ "-v", test_env__SHARED_VOLUME_HOST + ":"s + test_env__SHARED_VOLUME_CONTAINER);

    auto get_container_path = [&test_env__SHARED_VOLUME_CONTAINER](const fs::path& p) {
      return (test_env__SHARED_VOLUME_CONTAINER / p).generic_path().generic_string();
    };

    auto get_testenv_path = [&test_env__SHARED_VOLUME_TEST](const fs::path& p) {
      return (test_env__SHARED_VOLUME_TEST / p).generic_path().generic_string();
    };

    auto delete_if_exist = [](const fs::path& p) {
      if(fs::exists(p)) {
        fs::remove(p);
      }
    };

    // test the the volume mount works for all the containers by checking that a file
    // 
    {
      std::string volume_mount_test_file = "volume_mount_test.txt";
      std::string volume_mount_test_file_test_path = get_testenv_path(volume_mount_test_file);

      // clear leftovers
      delete_if_exist(volume_mount_test_file_test_path);

      BOOST_REQUIRE(!run_docker_cmd("exec", "--workdir", test_env__SHARED_VOLUME_CONTAINER, container_1_id, "cat", volume_mount_test_file));
      BOOST_REQUIRE(!run_docker_cmd("exec", "--workdir", test_env__SHARED_VOLUME_CONTAINER, container_2_id, "cat", volume_mount_test_file));
      BOOST_REQUIRE(!run_docker_cmd("exec", "--workdir", test_env__SHARED_VOLUME_CONTAINER, container_3_id, "cat", volume_mount_test_file));
      
      std::cout << "Touching volume roundtrip test file at: " << volume_mount_test_file_test_path << std::endl;
      tipi::goldilock::file::touch_file_permissive(volume_mount_test_file_test_path);

      auto run_volume_mount_check = [&volume_mount_test_file, &test_env__SHARED_VOLUME_CONTAINER] (const std::string id) {
        std::cout << "Context: run_volume_mount_check(" << id << ")" << std::endl;

        std::string roundtrip_cmd = "ls -la && echo -n "s + id + " >> "s + volume_mount_test_file + " && cat "s + volume_mount_test_file;
        auto res = run_docker_cmd("exec", "--workdir", test_env__SHARED_VOLUME_CONTAINER, id, "/bin/sh", "-c", roundtrip_cmd);

        BOOST_REQUIRE(res);
        std::cout << " -> run_volume_mount_check(" << id << ") SUCCESS" << std::endl;
        std::cout << "Contents of volume_mount_test_file_test_path in container:\n-------------\n" << res.output << "\n-------------" << std::endl;
      };

      run_volume_mount_check(container_1_id);
      run_volume_mount_check(container_2_id);
      run_volume_mount_check(container_3_id);

      // now we're expecting that the volume_mount_test_file contains all three container ids in a particular order!
      auto file_content = tipi::goldilock::file::read_file_content(volume_mount_test_file_test_path);
      std::cout << "Contents of volume_mount_test_file_test_path in test env:\n-------------\n" << file_content << "\n-------------" << std::endl;
      BOOST_REQUIRE(file_content == (container_1_id + container_2_id + container_3_id));
    }

    // the actual test of goldilock
    {
      const std::string master_lockfile_name = "master.lock";
      const std::string stage2_lockfile_name = "stage2.lock";
      bool test_flag_expect_master_released = false;

      auto get_container_lock_aquired_marker_name = [](const std::string& id) {
        return "lock_acquired_"s + id + ".marker";
      };
      
      std::string write_dest_file = "destination.txt";
      std::string write_dest_file_testenv_path = get_testenv_path(write_dest_file);
      delete_if_exist(write_dest_file_testenv_path);

      auto write_letter_container_fn = [&](const std::string& id, std::string chr, size_t interval) {

        std::stringstream ss_cmd;

        ss_cmd   << "/usr/bin/goldilock"
          << " " << "--lockfile " << get_container_path(stage2_lockfile_name)
          << " " << "--lock-success-marker " << get_container_path(get_container_lock_aquired_marker_name(id))
          << " " << "--"  
          << " " << "support_app_append_to_file -s " << chr << " -n  100 -f " << get_container_path(write_dest_file) << " -i " << interval          
        ;

        auto result = run_docker_cmd("exec", "-e", "TEST=2", "--workdir", test_env__SHARED_VOLUME_CONTAINER, id, "sh", "-c", ss_cmd.str());
        BOOST_REQUIRE(test_flag_expect_master_released);
        BOOST_REQUIRE(result);
      };

       auto write_letter_host_fn = [&](std::string chr, size_t interval) {
        const std::string support_app_append_to_file_bin = get_executable_path_from_test_env("support_app_append_to_file");
        auto result = run_goldilock_command_in(test_env__SHARED_VOLUME_HOST, 
          "--lockfile", get_testenv_path(master_lockfile_name), 
          "--lockfile", get_testenv_path(stage2_lockfile_name),
          "--", 
            support_app_append_to_file_bin, "-s", chr, "-n", "100", "-f", write_dest_file_testenv_path, "-i", std::to_string(interval)
        );
        BOOST_REQUIRE(result.return_code == 0);
      };

      // we're testing the unlockfile and multilock file acquisition at the same time here...
      // let's have container 1 hold only the master lock
      std::string master_lock_acquired_marker = "master_lock_acquired.marker";
      std::string master_lock_acquired_marker_testenv_path = get_testenv_path(master_lock_acquired_marker);
      delete_if_exist(master_lock_acquired_marker_testenv_path);      
      
      std::string master_unlockfile_name = "master.unlockfile";
      std::string master_unlockfile_testenv_path = get_testenv_path(master_unlockfile_name);
      delete_if_exist(master_unlockfile_testenv_path); 

      std::thread t_container1([&](){ 
        std::stringstream ss_cmd;

        ss_cmd   << "/usr/bin/goldilock"
          << " " << "--lockfile " << get_container_path(master_lockfile_name)
          << " " << "--unlockfile " << get_container_path(master_unlockfile_name)
          << " " << "--lock-success-marker " << get_container_path(master_lock_acquired_marker)
          << " " << "--timeout 30" // fail if we don't get unlocked withing 30s
        ;

        auto result = run_docker_cmd("exec", "-e", "TEST=1", "--workdir", test_env__SHARED_VOLUME_CONTAINER, container_1_id, "sh", "-c", ss_cmd.str());
        BOOST_REQUIRE(test_flag_expect_master_released);
        BOOST_REQUIRE(result);
      });

      // make sure the master lock is taken
      wait_for_file(get_testenv_path(master_lock_acquired_marker));
      
      // now start the other processes
      std::thread t_host([&](){ write_letter_host_fn("H", 2); });
      std::thread t_container2([&](){ write_letter_container_fn(container_2_id, "2", 2); });
      std::thread t_container3([&](){ write_letter_container_fn(container_3_id, "3", 1); });

      BOOST_REQUIRE(!fs::exists(write_dest_file_testenv_path));

      test_flag_expect_master_released = true;
      tipi::goldilock::file::touch_file(master_unlockfile_testenv_path);

      wait_for_file(get_testenv_path(get_container_lock_aquired_marker_name(container_2_id)), 50, 100ms);
      wait_for_file(get_testenv_path(get_container_lock_aquired_marker_name(container_3_id)), 50, 100ms);

      t_host.join();
      t_container1.join();
      t_container2.join();
      t_container3.join();

      BOOST_REQUIRE(fs::exists(write_dest_file_testenv_path));

      auto file_content = tipi::goldilock::file::read_file_content(write_dest_file_testenv_path);
      std::cout << "Testing dockerized goldilocked interleaved write output:\n-------------\n" << file_content << "\n-------------\nExpecting no mixing of H|2|3" << std::endl;
      
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"^([H23]{300})$"}));
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[H]{100}"}));
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[2]{100}"}));
      BOOST_REQUIRE(boost::regex_search(file_content, boost::regex{"[3]{100}"}));

    }
    
  }

}
