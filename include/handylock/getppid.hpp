#pragma once

#include <stdint.h>

namespace tipi::handylock {

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>

inline uint32_t getppid()
{
    HANDLE hSnapshot = INVALUD_HANDLE_VALUE;
    PROCESSENTRY32 pe32;
    DWORD ppid = 0, pid = GetCurrentProcessId();

    hSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    __try{
        if( hSnapshot == INVALID_HANDLE_VALUE ) __leave;

        ZeroMemory( &pe32, sizeof( pe32 ) );
        pe32.dwSize = sizeof( pe32 );
        if( !Process32First( hSnapshot, &pe32 ) ) __leave;

        do{
            if( pe32.th32ProcessID == pid ){
                ppid = pe32.th32ParentProcessID;
                break;
            }
        }while( Process32Next( hSnapshot, &pe32 ) );

    }
    __finally{
        if( hSnapshot != INVALID_HANDLE_VALUE ) CloseHandle( hSnapshot );
    }
    return ppid;
}


#else

#include <unistd.h>

inline uint32_t getppid()
{
    return ::getppid();
}

#endif

}