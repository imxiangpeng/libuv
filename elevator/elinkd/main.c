#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "elinkd.h"

static int _exit_request = 0;

static void _daemon_signal_action(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)siginfo;
    (void)sigcontext;

    printf("%s(%d): ...pgrp:%d.....signum:%d\n", __FUNCTION__, __LINE__, getpgrp(), signum);

    if (SIGTERM == signum) {
        if (_exit_request == 0) {
            _exit_request = 1;
            // send to all child process in current process group
            killpg(getpgrp(), SIGTERM);
        }
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    struct sigaction action;

    printf("elinkd %s\n", BUILD_TIMESTAMP);

    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    action.sa_sigaction = _daemon_signal_action;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGCHLD, &action, NULL);

    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    setpgid(0, 0);

    while (_exit_request == 0) {
        int status;
        pid_t pid = fork();

        if (pid == 0) {
            setpgid(0, getppid());
            prctl(PR_SET_PDEATHSIG, SIGTERM);

            memset(&action, 0, sizeof(action));
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_SIGINFO | SA_RESTART;
            action.sa_handler = SIG_DFL;
            sigaction(SIGTERM, &action, NULL);
            sigaction(SIGCHLD, &action, NULL);

            return elinkd_main(argc, argv);
        }

        printf("elinkd main started:%d\n", pid);

        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            printf("Service %d exited with code %d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Service %d killed by signal %d\n", pid, WTERMSIG(status));
        } else {
            printf("Service %d exited unexpectedly\n", pid);
        }
    }
    return 0;
}
