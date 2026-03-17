/*
 * Daemon utilities for weather station services
 * Handles PID files, signal handling, daemon forking, etc.
 */

#ifndef DAEMON_H
#define DAEMON_H

#include <signal.h>
#include "logging.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Daemon state structure */
typedef struct {
    volatile int running;           /* Set to 0 to stop daemon */
    volatile int reload_config;     /* Set to 1 to reload config */
    char *pid_file;                /* Path to PID file */
    Logger *logger;                /* Logger instance */
    void (*cleanup_fn)(void);      /* Optional cleanup function */
} DaemonState;

/* Initialize daemon state
 * @param state Daemon state structure
 * @param logger Logger instance (can be NULL)
 * @param pid_file Path to PID file (can be NULL)
 * @param cleanup_fn Optional cleanup function (can be NULL)
 * @return WS_SUCCESS on success
 */
int daemon_init(DaemonState *state, Logger *logger, 
                const char *pid_file, void (*cleanup_fn)(void));

/* Fork and daemonize process
 * Parent exits, child continues as daemon
 * @return 0 in child (daemon), exits in parent
 */
int daemon_fork(void);

/* Write PID to file
 * @param pid_file Path to PID file
 * @return WS_SUCCESS on success, WS_ERROR on failure
 */
int daemon_write_pid_file(const char *pid_file);

/* Remove PID file
 * @param pid_file Path to PID file
 */
void daemon_remove_pid_file(const char *pid_file);

/* Setup signal handlers
 * Handles SIGTERM, SIGINT, SIGHUP
 * @param state Daemon state
 */
void daemon_setup_signals(DaemonState *state);

/* Check if daemon should stop
 * @param state Daemon state
 * @return 1 if should stop, 0 if should continue
 */
int daemon_should_stop(DaemonState *state);

/* Check if daemon should reload config
 * @param state Daemon state
 * @return 1 if should reload, resets flag to 0
 */
int daemon_should_reload(DaemonState *state);

/* Run daemon main loop
 * @param state Daemon state
 * @param loop_fn Function to call in each iteration
 * @param interval_ms Sleep interval between iterations
 */
void daemon_run(DaemonState *state, void (*loop_fn)(void *), int interval_ms);

/* Cleanup daemon resources
 * Removes PID file and calls cleanup function
 * @param state Daemon state
 */
void daemon_cleanup(DaemonState *state);

/* Signal handler (internal use) */
void daemon_signal_handler(int sig);

/* Get singleton daemon state (for signal handler) */
DaemonState* daemon_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* DAEMON_H */
