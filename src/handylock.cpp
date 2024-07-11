#include <iostream>
#include <filesystem>

#include <handylock/getppid.hpp>


int main(int argc, char** argv) {

    std::cout << "Parent PID: " << handylock::getppid() << std::endl;

}