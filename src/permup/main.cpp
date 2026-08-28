#include "client.hpp"
#include <iostream>
#include <unistd.h>
#include <getopt.h>

void printUsage(const char* progname) {
    std::cout << "Usage: " << progname << " [options] command [arguments]\n"
              << "Options:\n"
              << "  -u user      Target user (default: root)\n"
              << "  -h user      Authenticating user (default: root)\n"
              << "  -l           List allowed users\n"
              << "  -?           Show this help\n"
              << "\n"
              << "Example:\n"
              << "  " << progname << " -u janusz -h root htop\n"
              << "  " << progname << " -l\n";
}

int main(int argc, char* argv[]) {
    std::string targetUser = "root";
    std::string authUser = "root";
    bool listOnly = false;
    int opt;
    
    while ((opt = getopt(argc, argv, "u:h:l")) != -1) {
        switch (opt) {
            case 'u':
                targetUser = optarg;
                break;
            case 'h':
                authUser = optarg;
                break;
            case 'l':
                listOnly = true;
                break;
            case '?':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }
    
    if (!listOnly && optind >= argc) {
        std::cerr << "Error: No command specified\n";
        printUsage(argv[0]);
        return 1;
    }
    
    std::string command;
    if (!listOnly) {
        for (int i = optind; i < argc; ++i) {
            if (i > optind) command += " ";
            command += argv[i];
        }
    }
    
    permup::Client client;
    if (!client.initialize()) {
        return 1;
    }
    
    return client.run(targetUser, authUser, command, listOnly);
}