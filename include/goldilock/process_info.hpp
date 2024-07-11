// Copyright 2024 Yannic Staudt, tipi technologies Ltd and the goldilock contributors
// SPDX-License-Identifier: GPL-2.0-only OR Proprietary
#pragma once

#include <stdint.h>

namespace tipi::goldilock::process_info {

  #ifdef _WIN32
  #include <windows.h>
  #include <tlhelp32.h>
  #include <process.h>

  inline pid_t get_parent_pid()
  {
    HANDLE hSnapshot = INVALUD_HANDLE_VALUE;
    PROCESSENTRY32 pe32;
    DWORD ppid = 0, pid = GetCurrentProcessId();

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    __try{
      if(hSnapshot == INVALID_HANDLE_VALUE)
        __leave;

      ZeroMemory(&pe32, sizeof( pe32 ));
      pe32.dwSize = sizeof( pe32 );

      if(!Process32First( hSnapshot, &pe32 )) 
        __leave;

      do{
        if( pe32.th32ProcessID == pid ){
          ppid = pe32.th32ParentProcessID;
          break;
        }
      }
      while(Process32Next(hSnapshot, &pe32));

    }
    __finally{
        if( hSnapshot != INVALID_HANDLE_VALUE ) CloseHandle( hSnapshot );
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

}