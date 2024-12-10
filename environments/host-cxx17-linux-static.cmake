include_guard()

include(${CMAKE_CURRENT_LIST_DIR}/host-cxx17.cmake)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=lld -stdlib=libc++ -static-libstdc++ -static-libgcc /usr/local/share/.tipi/clang/4f846ee/lib/libc++.a /usr/local/share/.tipi/clang/4f846ee/lib/libc++abi.a" CACHE STRING "statically link" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc /usr/local/share/.tipi/clang/4f846ee/lib/libc++.a /usr/local/share/.tipi/clang/4f846ee/lib/libc++abi.a" CACHE STRING "statically link" FORCE)