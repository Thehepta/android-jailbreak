//
// Created by chic on 2022/8/25.
//
#include <termios.h>
#include "pts.h"



struct termios old_stdin;
int stdin_is_raw = 0;


int pts_open(char *slave_name, size_t slave_name_size) {
    int fdm;
    char *sn_tmp;

    // Open master ptmx device
    fdm = open("/dev/ptmx", O_RDWR);
    if (fdm == -1) return -1;

    // Get the slave name
    sn_tmp = ptsname(fdm);
    if (!sn_tmp) {
        close(fdm);
        return -2;
    }

    strncpy(slave_name, sn_tmp, slave_name_size);
    slave_name[slave_name_size - 1] = '\0';

    // Grant, then unlock
    if (grantpt(fdm) == -1) {
        close(fdm);
        return -1;
    }
    if (unlockpt(fdm) == -1) {
        close(fdm);
        return -1;
    }

    return fdm;
}


// Ensures all the data is written out
int write_blocking(int fd, char *buf, size_t bufsz) {
    ssize_t ret, written;

    written = 0;
    do {
        ret = write(fd, buf + written, bufsz - written);
        if (ret == -1) return -1;
        written += ret;
    } while (written < bufsz);

    return 0;
}


void pump_ex(int input, int output, int close_output) {
    char buf[4096];
    int len;
    while ((len = read(input, buf, 4096)) > 0) {
        if (write_blocking(output, buf, len) == -1)
            break;
    }
    close(input);
    if (close_output) close(output);
}


void pump(int input, int output) {
    pump_ex(input, output, 1);
}



void* pump_thread(void* data) {
    int* files = (int*)data;
    int input = files[0];
    int output = files[1];
    pump(input, output);
    free(data);
    return NULL;
}


void pump_async(int input, int output) {
    pthread_t writer;
    int* files = (int*)malloc(sizeof(int) * 2);
    if (files == NULL) {
        exit(-1);
    }
    files[0] = input;
    files[1] = output;
    pthread_create(&writer, NULL, pump_thread, files);
}



int set_stdin_raw(void) {
    struct termios new_termios;

    // Save the current stdin termios
    if (tcgetattr(STDIN_FILENO, &old_stdin) < 0) {
        return -1;
    }

    // Start from the current settings
    new_termios = old_stdin;

    // Make the terminal like an SSH or telnet client
    new_termios.c_iflag |= IGNPAR;
    new_termios.c_iflag &= ~(ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXANY | IXOFF);
    new_termios.c_lflag &= ~(ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL);
    new_termios.c_oflag &= ~OPOST;
    new_termios.c_cc[VMIN] = 1;
    new_termios.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios) < 0) {
        return -1;
    }

    stdin_is_raw = 1;

    return 0;
}


int restore_stdin(void) {
    if (!stdin_is_raw) return 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_stdin) < 0) {
        return -1;
    }

    stdin_is_raw = 0;

    return 0;
}


void watch_sigwinch_cleanup(void) {
    closing_time = 1;
    raise(SIGWINCH);
}


void pump_stdout_blocking(int infd) {
    // Pump data from stdout to PTY
    pump_ex(infd, STDOUT_FILENO, 0 /* Don't close output when done */);

    // Cleanup
    restore_stdin();
    watch_sigwinch_cleanup();
}
