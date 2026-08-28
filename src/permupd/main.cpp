#include "daemon.hpp"
#include <iostream>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>

int main(int argc, char* argv[]) {
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        pid_t pid = fork();
        if (pid < 0) {
            return 1;
        }
        if (pid > 0) {
            return 0;
        }
        
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    openlog("permupd", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    
    permup::Daemon daemon;
    
    if (!daemon.initialize()) {
        syslog(LOG_ERR, "Failed to initialize daemon");
        return 1;
    }
    
    syslog(LOG_INFO, "permupd started");
    
    int ret = daemon.run();
    
    syslog(LOG_INFO, "permupd stopped");
    closelog();
    
    return ret;
}