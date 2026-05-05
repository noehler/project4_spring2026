#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

#define N 13
#define MAX_LINE 256
#define MAX_ARGS 21
#define MSG_SIZE 256

extern char **environ;

struct message {
    char source[50];
    char target[50];
    char msg[MSG_SIZE];
};

char uName[50];

char *allowed[N] = {
    "cp", "touch", "mkdir", "ls", "pwd", "cat",
    "grep", "chmod", "diff", "cd", "exit", "help",
    "sendmsg"
};

int isAllowed(const char *cmd);
void printHelp(void);
void sendmsg(char *user, char *target, char *msg);
void* messageListener(void *arg);

int isAllowed(const char *cmd) {
    for (int i = 0; i < N; i++) {
        if (strcmp(cmd, allowed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

void printHelp(void) {
    printf("The allowed commands are:\n");
    for (int i = 0; i < N; i++) {
        printf("%d: %s\n", i + 1, allowed[i]);
    }
}

void sendmsg(char *user, char *target, char *msg) {
    struct message m;

    memset(&m, 0, sizeof(struct message));

    strncpy(m.source, user, sizeof(m.source) - 1);
    strncpy(m.target, target, sizeof(m.target) - 1);
    strncpy(m.msg, msg, sizeof(m.msg) - 1);

    int fd = open("serverFIFO", O_WRONLY);

    if (fd < 0) {
        perror("open serverFIFO");
        return;
    }

    if (write(fd, &m, sizeof(struct message)) < 0) {
        perror("write serverFIFO");
    }

    close(fd);
}

void* messageListener(void *arg) {
    struct message m;

    int fd = open(uName, O_RDONLY);
    if (fd < 0) {
        perror("open user FIFO");
        pthread_exit(NULL);
    }

    while (1) {
        int n = read(fd, &m, sizeof(struct message));

        if (n == sizeof(struct message)) {
            printf("Incoming message from %s: %s\n", m.source, m.msg);
            fflush(stdout);
        }
    }

    close(fd);
    pthread_exit(NULL);
}

int main(int mainArgc, char *mainArgv[]) {
    char line[MAX_LINE];

    if (mainArgc < 2) {
        printf("Usage: ./rsh username\n");
        return 1;
    }

    strncpy(uName, mainArgv[1], sizeof(uName) - 1);
    uName[sizeof(uName) - 1] = '\0';

    if (mkfifo(uName, 0666) != 0 && errno != EEXIST) {
        perror("mkfifo user FIFO");
        return 1;
    }

    pthread_t tid;

    if (pthread_create(&tid, NULL, messageListener, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    while (1) {

        if (fgets(line, sizeof(line), stdin) == NULL) {
            return 0;
        }

        if (strcmp(line, "\n") == 0) {
            continue;
        }

        line[strcspn(line, "\n")] = '\0';

        char *cmdArgv[MAX_ARGS];
        int cmdArgc = 0;

        char *token = strtok(line, " \t");

        while (token != NULL && cmdArgc < MAX_ARGS - 1) {
            cmdArgv[cmdArgc++] = token;
            token = strtok(NULL, " \t");
        }

        cmdArgv[cmdArgc] = NULL;

        if (cmdArgc == 0) {
            continue;
        }

        if (!isAllowed(cmdArgv[0])) {
            printf("NOT ALLOWED!\n");
            continue;
        }

        if (strcmp(cmdArgv[0], "exit") == 0) {
            unlink(uName);
            return 0;
        }

        if (strcmp(cmdArgv[0], "help") == 0) {
            printHelp();
            continue;
        }

        if (strcmp(cmdArgv[0], "cd") == 0) {
            if (cmdArgc > 2) {
                printf("-rsh: cd: too many arguments\n");
                continue;
            }

            if (cmdArgc == 1) {
                continue;
            }

            if (chdir(cmdArgv[1]) != 0) {
                perror("cd");
            }

            continue;
        }

        if (strcmp(cmdArgv[0], "sendmsg") == 0) {
            if (cmdArgc < 3) {
                printf("Usage: sendmsg target message\n");
                continue;
            }

            char msg[MAX_LINE] = "";

            for (int i = 2; i < cmdArgc; i++) {
                strncat(msg, cmdArgv[i], sizeof(msg) - strlen(msg) - 1);

                if (i < cmdArgc - 1) {
                    strncat(msg, " ", sizeof(msg) - strlen(msg) - 1);
                }
            }

            sendmsg(uName, cmdArgv[1], msg);
            continue;
        }

        pid_t pid;
        int status;

        if (posix_spawnp(&pid, cmdArgv[0], NULL, NULL, cmdArgv, environ) != 0) {
            perror("posix_spawnp");
            continue;
        }

        waitpid(pid, &status, 0);
    }

    return 0;
}