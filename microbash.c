/*
 * Micro-bash v2.2
 *
 * Programma sviluppato a supporto del laboratorio di Sistemi di
 * Elaborazione e Trasmissione dell'Informazione del corso di laurea
 * in Informatica presso l'Università degli Studi di Genova, a.a. 2024/2025.
 *
 * Copyright (C) 2020-2024 by Giovanni Lagorio <giovanni.lagorio@unige.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifndef NO_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif
#include <stdint.h>

void fatal(const char *const msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

void fatal_errno(const char *const msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void *my_malloc(size_t size) {
    void *rv = malloc(size);
    if (!rv)
        fatal_errno("my_malloc");
    return rv;
}

void *my_realloc(void *ptr, size_t size) {
    void *rv = realloc(ptr, size);
    if (!rv)
        fatal_errno("my_realloc");
    return rv;
}

char *my_strdup(char *ptr) {
    char *rv = strdup(ptr);
    if (!rv)
        fatal_errno("my_strdup");
    return rv;
}

#define malloc I_really_should_not_be_using_a_bare_malloc
#define realloc I_really_should_not_be_using_a_bare_realloc
#define strdup I_really_should_not_be_using_a_bare_strdup

static const int NO_REDIR = -1;

typedef enum { CHECK_OK = 0, CHECK_FAILED = -1 } check_t;

static const char *const CD = "cd";

typedef struct {
        int n_args;
        char **args; // in an execv*-compatible format; i.e., args[n_args]=0
        char *out_pathname; // 0 if no output-redirection is present
        char *in_pathname;  // 0 if no input-redirection is present
} command_t;

typedef struct {
        int n_commands;
        command_t **commands;
} line_t;

void free_command(command_t *const c) {
    assert(
        c == 0 || c->n_args == 0 ||
        (c->n_args > 0 && c->args[c->n_args] ==
                              0)); /* sanity-check: if c is not null, then it is
                                      either empty (in case of parsing error) or
                                      its args are properly NULL-terminated */
                                   /*** TO BE DONE START ***/
    for (int i = 0; i < c->n_args; ++i) {
        free(c->args[i]);
    }
    free(c->out_pathname);
    free(c->in_pathname);
    free(c->args);
    free(c);

    /*** TO BE DONE END ***/
}

void free_line(line_t *const l) {
    assert(l == 0 || l->n_commands >= 0); /* sanity-check */
                                          /*** TO BE DONE START ***/
    for (int i = 0; i < l->n_commands; ++i) {
        free_command(l->commands[i]);
    }
    free(l->commands);
    free(l);
    /*** TO BE DONE END ***/
}

#ifdef DEBUG
void print_command(const command_t *const c) {
    if (!c) {
        printf("Command == NULL\n");
        return;
    }
    printf("[ ");
    for (int a = 0; a < c->n_args; ++a)
        printf("%s ", c->args[a]);
    assert(c->args[c->n_args] == 0);
    printf("] ");
    printf("in: %s out: %s\n", c->in_pathname, c->out_pathname);
}

void print_line(const line_t *const l) {
    if (!l) {
        printf("Line == NULL\n");
        return;
    }
    printf("Line has %d command(s):\n", l->n_commands);
    for (int a = 0; a < l->n_commands; ++a)
        print_command(l->commands[a]);
}
#endif

command_t *parse_cmd(char *const cmdstr) {
    static const char *const BLANKS = " \t";
    command_t *const result = my_malloc(sizeof(*result));
    memset(result, 0, sizeof(*result));
    char *saveptr, *tmp;
    tmp = strtok_r(cmdstr, BLANKS, &saveptr);
    while (tmp) {
        result->args =
            my_realloc(result->args, (result->n_args + 2) * sizeof(char *));
        if (*tmp == '<') {
            if (result->in_pathname) {
                fprintf(stderr, "Parsing error: cannot have more than one "
                                "input redirection\n");
                goto fail;
            }
            if (!tmp[1]) {
                fprintf(
                    stderr,
                    "Parsing error: no path specified for input redirection\n");
                goto fail;
            }
            result->in_pathname = my_strdup(tmp + 1);
        } else if (*tmp == '>') {
            if (result->out_pathname) {
                fprintf(stderr, "Parsing error: cannot have more than one "
                                "output redirection\n");
                goto fail;
            }
            if (!tmp[1]) {
                fprintf(stderr, "Parsing error: no path specified for output "
                                "redirection\n");
                goto fail;
            }
            result->out_pathname = my_strdup(tmp + 1);
        } else {
            if (*tmp == '$') {
                /* Make tmp point to the value of the corresponding environment
                 * variable, if any, or the empty string otherwise */
                /*** TO BE DONE START ***/
                tmp = getenv(tmp + 1);
                tmp = (tmp == NULL) ? "" : tmp;
                /*** TO BE DONE END ***/
            }
            result->args[result->n_args++] = my_strdup(tmp);
            result->args[result->n_args] = 0;
        }
        tmp = strtok_r(0, BLANKS, &saveptr);
    }
    if (result->n_args)
        return result;
    fprintf(stderr, "Parsing error: empty command\n");
fail:
    free_command(result);
    return 0;
}

line_t *parse_line(char *const line) {
    static const char *const PIPE = "|";
    char *cmd, *saveptr;
    cmd = strtok_r(line, PIPE, &saveptr);
    if (!cmd)
        return 0;
    line_t *result = my_malloc(sizeof(*result));
    memset(result, 0, sizeof(*result));
    while (cmd) {
        command_t *const c = parse_cmd(cmd);
        if (!c) {
            free_line(result);
            return 0;
        }
        result->commands = my_realloc(
            result->commands, (result->n_commands + 1) * sizeof(command_t *));
        result->commands[result->n_commands++] = c;
        cmd = strtok_r(0, PIPE, &saveptr);
    }
    return result;
}

check_t check_redirections(const line_t *const l) {
    assert(l);
    /* This function must check that:
     * - Only the first command of a line can have input-redirection
     * - Only the last command of a line can have output-redirection
     * and return CHECK_OK if everything is ok, print a proper error
     * message and return CHECK_FAILED otherwise
     */
    /*** TO BE DONE START ***/
    for (int i = 0; i < l->n_commands; ++i) {
        if (l->commands[i]->in_pathname && i != 0) {
            fprintf(stderr,
                    "Parsing error: cannot have input-redirection except "
                    "in the first command\n");
            return CHECK_FAILED;
        }
        if (l->commands[i]->out_pathname && i != l->n_commands - 1) {
            fprintf(stderr, "Parsing error: cannot have output-redirection "
                            "except in the last command\n");
            return CHECK_FAILED;
        }
    }
    /*** TO BE DONE END ***/
    return CHECK_OK;
}

check_t check_cd(const line_t *const l) {
    assert(l);
    /* This function must check that if command "cd" is present in l, then such
     * a command 1) must be the only command of the line 2) cannot have I/O
     * redirections 3) must have only one argument and return CHECK_OK if
     * everything is ok, print a proper error message and return CHECK_FAILED
     * otherwise
     */
    /*** TO BE DONE START ***/

    // if there is a cd that is not the first command: fail
    for (int i = 1; i < l->n_commands; ++i) {
        if (strcmp(l->commands[i]->args[0], CD) == 0) {
            fprintf(stderr, "Parsing error: cannot have CD in pipe\n");
            return CHECK_FAILED;
        }
    }

    // if the cd is not the first command ok
    if (strcmp(l->commands[0]->args[0], CD)) {
        return CHECK_OK;
    }

    // otherwise do all check
    if (l->n_commands > 1) {
        fprintf(stderr,
                "Parsing error: cannot have more that one command with CD\n");
        return CHECK_FAILED;
    }
    if (l->commands[0]->in_pathname) {
        fprintf(stderr,
                "Parsing error: cannot have input-redirection with CD\n");
        return CHECK_FAILED;
    }
    if (l->commands[0]->out_pathname) {
        fprintf(stderr,
                "Parsing error: cannot have output-redirection with CD\n");
        return CHECK_FAILED;
    }
    if (l->commands[0]->n_args != 2) {
        fprintf(stderr,
                "Parsing error: cannot have more than one argument with CD\n");
        return CHECK_FAILED;
    }
    /*** TO BE DONE END ***/
    return CHECK_OK;
}

void wait_for_children(void) {
    /* This function must wait for the termination of all child processes.
     * If a child exits with an exit-status!=0, then you should print a
     * proper message containing its PID and exit-status. Similarly, if a
     * child is killed by a signal, then you should print a message
     * specifying its PID, signal number and signal name.
     */
    /*** TO BE DONE START ***/
    int status;
    pid_t pid;

    // -1 wait all child
    while ((pid = waitpid(-1, &status, 0)) > 0) {
        // True if the process terminated normally by a call to _exit(2) or
        // exit(3).
        if (WIFEXITED(status)) {
            int exit_status = WEXITSTATUS(status);
            if (exit_status != 0) {
                printf("Child with PID %d exited with status %d.\n", pid,
                       exit_status);
            }
        }
        // True if the process terminated due to receipt of a signal.
        else if (WIFSIGNALED(status)) {
            int signal_num = WTERMSIG(status);
            printf("Child with PID %d was killed by signal %d (%s).\n", pid,
                   signal_num, strsignal(signal_num));
        }
    }
    /*** TO BE DONE END ***/
}

void redirect(int from_fd, int to_fd) {
    /* If from_fd!=NO_REDIR, then the corresponding open file should be
     * "moved" to to_fd. That is, use dup/dup2/close to make to_fd
     * equivalent to the original from_fd, and then close from_fd
     */
    /*** TO BE DONE START ***/
    if (from_fd != NO_REDIR && from_fd != to_fd) {
        if (dup2(from_fd, to_fd) == -1) {
            fatal_errno("dup2 failed");
        }

        if (close(from_fd) == -1) {
            fatal_errno("close failed");
        }
    }
    /*** TO BE DONE END ***/
}

void run_child(const command_t *const c, int c_stdin, int c_stdout) {
    /* This function must:
     * 1) create a child process, then, in the child
     * 2) redirect c_stdin to STDIN_FILENO (=0)
     * 3) redirect c_stdout to STDOUT_FILENO (=1)
     * 4) execute the command specified in c->args[0] with the corresponding
     * arguments c->args (printing error messages in case of failure,
     * obviously)
     */
    /*** TO BE DONE START ***/
    pid_t pid = fork();

    if (pid < 0) {
        fatal_errno("fork failed on run_child");
    }

    if (pid == 0) {
        redirect(c_stdin, STDIN_FILENO);
        redirect(c_stdout, STDOUT_FILENO);
        execvp(c->args[0], c->args);
        // if exec return, an error occurred
        fatal_errno(c->args[0]);
    }
    /*** TO BE DONE END ***/
}

void change_current_directory(char *newdir) {
    /* Change the current working directory to newdir
     * (printing an appropriate error message if the syscall fails)
     */
    /*** TO BE DONE START ***/
    if (chdir(newdir) == -1) {
        perror("error in change directory");
    }
    /*** TO BE DONE END ***/
}

void close_if_needed(int fd) {
    if (fd == NO_REDIR)
        return; // nothing to do
    if (close(fd))
        perror("close in close_if_needed");
}

void execute_line(const line_t *const l) {
    if (strcmp(CD, l->commands[0]->args[0]) == 0) {
        assert(l->n_commands == 1 && l->commands[0]->n_args == 2);
        change_current_directory(l->commands[0]->args[1]);
        return;
    }
    int next_stdin = NO_REDIR;
    for (int a = 0; a < l->n_commands; ++a) {
        int curr_stdin = next_stdin, curr_stdout = NO_REDIR;
        const command_t *const c = l->commands[a];
        if (c->in_pathname) {
            assert(a == 0);
            /* Open c->in_pathname and assign the file-descriptor to
             * curr_stdin (handling error cases) */
            /*** TO BE DONE START ***/
            curr_stdin = open(c->in_pathname, O_RDONLY);
            if (curr_stdin < 0) {
                perror(c->in_pathname);
                return;
            }
            /*** TO BE DONE END ***/
        }
        if (c->out_pathname) {
            assert(a == (l->n_commands - 1));
            /* Open c->out_pathname and assign the file-descriptor to
             * curr_stdout (handling error cases) */
            /*** TO BE DONE START ***/
            // 0664 = read/write for owner, read/write for group, read for
            // others
            curr_stdout =
                open(c->out_pathname, O_WRONLY | O_CREAT | O_TRUNC, 0664);
            if (curr_stdout < 0) {
                perror(c->out_pathname);
                return;
            }
            /*** TO BE DONE END ***/
        } else if (a != (l->n_commands -
                         1)) { /* unless we're processing the last command,
                                  we need to connect the current command and
                                  the next one with a pipe */
            int fds[2];
            /* Create a pipe in fds, and set FD_CLOEXEC in both
             * file-descriptor flags */
            /*** TO BE DONE START ***/
            if (pipe(fds) == -1) {
                fatal_errno("pipe");
            }

            // If the FD_CLOEXEC bit is set, the file descriptor will
            // automatically be closed during a successful execve
            if (fcntl(fds[0], F_SETFD, FD_CLOEXEC) == -1) {
                fatal_errno("fcntl read-end");
            }
            if (fcntl(fds[1], F_SETFD, FD_CLOEXEC) == -1) {
                fatal_errno("fcntl write-end");
            }
            /*** TO BE DONE END ***/
            curr_stdout = fds[1];
            next_stdin = fds[0];
        }
        run_child(c, curr_stdin, curr_stdout);
        close_if_needed(curr_stdin);
        close_if_needed(curr_stdout);
    }
    wait_for_children();
}

void execute(char *const line) {
    line_t *const l = parse_line(line);
#ifdef DEBUG
    print_line(l);
#endif
    if (l) {
        if (check_redirections(l) == CHECK_OK && check_cd(l) == CHECK_OK)
            execute_line(l);
        free_line(l);
    }
}

int main(void) {
    const char *const prompt_suffix = " $ ";
    const size_t prompt_suffix_len = strlen(prompt_suffix);
    for (;;) {
        char *pwd;
        /* Make pwd point to a string containing the current working
         * directory. The memory area must be allocated (directly or
         * indirectly) via malloc.
         */
        /*** TO BE DONE START ***/
        pwd = getcwd(NULL, 0);
        /*** TO BE DONE END ***/
        pwd = my_realloc(pwd, strlen(pwd) + prompt_suffix_len + 1);
        strcat(pwd, prompt_suffix);
#ifdef NO_READLINE
        const int max_line_size = 512;
        char *line = my_malloc(max_line_size);
        printf("%s", pwd);
        if (!fgets(line, max_line_size, stdin)) {
            free(line);
            line = 0;
            putchar('\n');
        } else {
            size_t l = strlen(line);
            if (l && line[--l] == '\n')
                line[l] = 0;
        }
#else
        char *const line = readline(pwd);
#endif
        free(pwd);
        if (!line)
            break;
        execute(line);
        free(line);
    }
}
