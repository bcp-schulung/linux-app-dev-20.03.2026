/*
 * Daemon utilities implementation
 */

#include "../include/daemon.h"
#include "../include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

/* Singleton state for signal handler */
static DaemonState *g_daemon_state = NULL;

int daemon_init(DaemonState *state, Logger *logger,
                const char *pid_file, void (*cleanup_fn)(void)) {
    if (state == NULL) {
        return WS_ERROR_INVALID_ARG;
    }
    
    memset(state, 0, sizeof(DaemonState));
    
    state->running = 1;
    state->reload_config = 0;
    state->logger = logger;
    state->cleanup_fn = cleanup_fn;
    
    if (pid_file != NULL) {
        state->pid_file = strdup(pid_file);
        if (state->pid_file == NULL) {
            return WS_ERROR_MEMORY;
        }
    }
    
    /* Set as singleton for signal handler */
    g_daemon_state = state;
    
    return WS_SUCCESS;
}

int daemon_fork(void) {
    pid_t pid = fork();
    
    if (pid < 0) {
        /* Fork failed */
        return WS_ERROR;
    }
    
    if (pid > 0) {
        /* Parent process - exit */
        exit(0);
    }
    
    /* Child process (daemon) */
    
    /* Create new session */
    if (setsid() < 0) {
        return WS_ERROR;
    }
    
    /* Change working directory to root */
    if (chdir("/") < 0) {
        return WS_ERROR;
    }
    
    /* Redirect standard file descriptors to /dev/null */
    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null >= 0) {
        dup2(dev_null, STDIN_FILENO);
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        if (dev_null > STDERR_FILENO) {
            close(dev_null);
        }
    }
    
    return WS_SUCCESS;
}

int daemon_write_pid_file(const char *pid_file) {
    if (pid_file == NULL) {
        return WS_ERROR_INVALID_ARG;
    }
    
    FILE *fp = fopen(pid_file, "w");
    if (fp == NULL) {
        return WS_ERROR_IO;
    }
    
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
    
    return WS_SUCCESS;
}

void daemon_remove_pid_file(const char *pid_file) {
    if (pid_file != NULL) {
        unlink(pid_file);
    }
}

void daemon_signal_handler(int sig) {
    DaemonState *state = daemon_get_state();
    if (state == NULL) {
        return;
    }
    
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            if (state->logger != NULL) {
                LOG_INFO(state->logger, "Received signal %d, shutting down gracefully...", sig);
            }
            state->running = 0;
            break;
            
        case SIGHUP:
            if (state->logger != NULL) {
                LOG_INFO(state->logger, "Received SIGHUP, reloading configuration...");
            }
            state->reload_config = 1;
            break;
    }
}

void daemon_setup_signals(DaemonState *state) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = daemon_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

int daemon_should_stop(DaemonState *state) {
    return (state == NULL) ? 1 : !state->running;
}

int daemon_should_reload(DaemonState *state) {
    if (state == NULL) {
        return 0;
    }
    
    if (state->reload_config) {
        state->reload_config = 0;  /* Reset flag */
        return 1;
    }
    
    return 0;
}

void daemon_run(DaemonState *state, void (*loop_fn)(void *), int interval_ms) {
    if (state == NULL || loop_fn == NULL) {
        return;
    }
    
    while (state->running) {
        /* Check for config reload */
        if (daemon_should_reload(state)) {
            /* Config reload is handled by the caller between iterations */
        }
        
        /* Execute main loop function */
        loop_fn(state);
        
        /* Sleep if still running */
        if (state->running && interval_ms > 0) {
            usleep(interval_ms * 1000);
        }
    }
}

void daemon_cleanup(DaemonState *state) {
    if (state == NULL) {
        return;
    }
    
    if (state->logger != NULL) {
        LOG_INFO(state->logger, "Cleaning up...");
    }
    
    /* Call cleanup function if provided */
    if (state->cleanup_fn != NULL) {
        state->cleanup_fn();
    }
    
    /* Remove PID file */
    if (state->pid_file != NULL) {
        daemon_remove_pid_file(state->pid_file);
        free(state->pid_file);
        state->pid_file = NULL;
    }
}

DaemonState* daemon_get_state(void) {
    return g_daemon_state;
}
