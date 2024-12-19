// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary
#pragma once

#include <stdint.h>
#include <map>
#include <functional>
#include <optional>
#include <cctype>
#include <algorithm>
#include <string>

#include "string.hpp"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <process.h>

#if _MSC_VER
using pid_t = DWORD;
#endif
#elif __APPLE__
#include <libproc.h>
#else
#include <boost/filesystem.hpp>
#include <fstream>
#include <streambuf>
#endif

namespace tipi::goldilock::process_info {

  struct proc_info {
    pid_t pid;
    pid_t parent_pid;
    std::string name;
  };

  inline std::map<pid_t, proc_info> get_process_map() {

    std::map<pid_t, proc_info> result;

    #ifdef _WIN32
    auto handle_closer = [](HANDLE h) {
      if(h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
      } 
    };

    using win_handle = std::unique_ptr<void, decltype(handle_closer)>;    
    auto hSnapshot = win_handle(INVALID_HANDLE_VALUE, handle_closer);
    
    HANDLE htemp = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (htemp == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("CreateToolhelp32Snapshot() failed with error " + std::to_string(GetLastError()));
    }
    hSnapshot.reset(htemp);
   
    PROCESSENTRY32 ptentry;
    ptentry.dwSize = sizeof(ptentry);

    bool ok = Process32First(hSnapshot.get(), &ptentry);
    if (!ok) {
      throw std::runtime_error("Process32First() failed with error " + std::to_string(GetLastError()));
    }

    const std::string exe_suffix = ".exe";
    
    while (ok) {
      proc_info pi{};
      pi.pid = ptentry.th32ProcessID;
      pi.parent_pid = ptentry.th32ParentProcessID;
      pi.name = std::string(ptentry.szExeFile);

      if(goldilock::string::iends_with(pi.name, exe_suffix)) {
        pi.name.erase(pi.name.size() - exe_suffix.size());
      }

      result[ptentry.th32ProcessID] = pi;

      ok = Process32Next(hSnapshot.get(), &ptentry);
    }

    #elif __APPLE__

    int bufsize = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    pid_t pids[2 * bufsize / sizeof(pid_t)];  // be ready to take twice as many just for good measure

    bufsize = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    size_t num_pids_read = bufsize / sizeof(pid_t);    

    for (int i = 0; i < num_pids_read; i++) {
        struct proc_bsdshortinfo proc_info_short;
        int st = proc_pidinfo(pids[i], PROC_PIDT_SHORTBSDINFO, 0, &proc_info_short, PROC_PIDT_SHORTBSDINFO_SIZE);

        if (st == PROC_PIDT_SHORTBSDINFO_SIZE) {
            proc_info pi{};
            pi.pid = proc_info_short.pbsi_pid;
            pi.parent_pid = proc_info_short.pbsi_ppid;
            pi.name = std::string(proc_info_short.pbsi_comm);
            result[proc_info_short.pbsi_pid] = pi;
        }
    }

    #else

    // https://man7.org/linux/man-pages/man5/proc.5.html
    // definition of /proc/<pid>/stat from fs/proc/array.c.

    for (auto& entry : boost::filesystem::directory_iterator{"/proc"}) {

      auto dirname = entry.path().filename().generic_string();

      if(!dirname.empty() && std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {

        auto stat_file = entry.path() / "stat";

        std::ifstream stat_stream(stat_file.generic_string());
        if(stat_stream.fail()) {
          continue;
        }

        std::string token;
        size_t token_ix = 0;
        bool success = false;

        proc_info pi{};

        while (std::getline(stat_stream, token, ' ')) {
          switch(token_ix++) {
            case 0:
              pi.pid = std::atoi(token.data());
              break;
            case 1:
              goldilock::string::trim(token, "()");
              pi.name = token;
              break;
            case 3:
              pi.parent_pid = std::atoi(token.data());
              break;
            case 5:
              success = true;
              break;
            default:
              break;
          }

          if(success) {
            break;  // done reading that proc/<pid>/stat
          }            
        }

        if(success) {
          result[pi.pid] = pi;
        }        
      }

    }

    #endif

    return result;
  }

  
  #ifdef _WIN32
  
  #include <tlhelp32.h>
  #include <process.h>
  
  inline pid_t get_parent_pid()
  {
    HANDLE hSnapshot = INVALID_HANDLE_VALUE;

    PROCESSENTRY32 pe32;
    DWORD ppid = 0, pid = GetCurrentProcessId();

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if(hSnapshot != INVALID_HANDLE_VALUE) {
      ZeroMemory(&pe32, sizeof(pe32));
      pe32.dwSize = sizeof(pe32);

      if(Process32First(hSnapshot, &pe32)) {
        do{
          if(pe32.th32ProcessID == pid){
            ppid = pe32.th32ParentProcessID;
            break;
          }
        }
        while(Process32Next(hSnapshot, &pe32));
      }
    }
    
    if(hSnapshot != INVALID_HANDLE_VALUE) {
      CloseHandle(hSnapshot);
    } 
    
    return ppid;
  }

  inline pid_t get_processid() {
    return _getpid();
  }


  inline bool is_process_running(pid_t pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    DWORD ret = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return ret == WAIT_TIMEOUT;
  } 


  #else /* Linux & macOS */
  #include <unistd.h>

  inline pid_t get_parent_pid()
  {
    return getppid();
  }

  inline pid_t get_processid() {
    return getpid();
  }

  inline bool is_process_running(pid_t pid) {
    return getpgid(pid) >= 0; // we're allowed to inspect that even for processes that are not children of ours :P
  }

  #endif

  inline std::vector<proc_info> get_parent_processes() {
    auto proc_map = get_process_map();

    auto get_parent_procinfo = [&proc_map](pid_t parent_pid) -> std::optional<proc_info> {      
      if(proc_map.find(parent_pid) != proc_map.end()) {
        return proc_map.at(parent_pid);
      }      
      return std::nullopt;
    };


    std::vector<proc_info> proc_stack;
    bool found_last = false;
    pid_t previous_pid = get_processid();

    while(!found_last) {
      auto parent_pi = get_parent_procinfo(previous_pid);

      if(!parent_pi || parent_pi.value().parent_pid == previous_pid) {
        found_last = true;
      }

      if(parent_pi) {
        proc_stack.push_back(parent_pi.value());
        previous_pid = parent_pi.value().parent_pid;
      }
    }

    return proc_stack;
  }


  //!\brief search for the farthest away parent process with any one of the provided process names (case insensitive search)
  inline std::optional<pid_t> get_parent_pid_by_name(const std::vector<std::string>& process_names, bool search_nearest) {
    
    auto proc_stack = get_parent_processes();

    std::optional<pid_t> match = std::nullopt;

    for(const auto& pi : proc_stack) {
      for(const auto& needle_raw : process_names) {
        #if BOOST_OS_LINUX || BOOST_OS_MACOS
        std::string needle = needle_raw.substr(0, 15);  // kernel truncates at 15 chars
        #else
        std::string needle = needle_raw;
        #endif

        if(tipi::goldilock::string::iequals(pi.name, needle)) {
          match = pi.pid;
          break;
        }
      }

      if(search_nearest && match.has_value()) {
        break;
      }
    }

    return match;
  }

  inline bool is_pid_a_parent_process(pid_t needle) {
    auto proc_stack = get_parent_processes();

    auto it = std::find_if(
      proc_stack.begin(), 
      proc_stack.end(), 
      [&](const proc_info& entry) { return entry.pid == needle; }
    );

    return it != proc_stack.end();
  }


}