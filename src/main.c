#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define MYPAAS_STATD_VERSION "0.1.0-dev"

static volatile sig_atomic_t g_stop_requested = 0;

static void handle_stop_signal(int signo)
{
    (void)signo;
    g_stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action = {0};
    action.sa_handler = handle_stop_signal;

    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }
    if (sigaction(SIGINT, &action, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }
    return 0;
}

int main(void)
{
    if (install_signal_handlers() != 0) {
        perror("mypaas-statd: install signal handlers");
        return EXIT_FAILURE;
    }

    printf("mypaas-statd %s bootstrap skeleton\n", MYPAAS_STATD_VERSION);
    printf("no runtime sampling is implemented yet\n");

    if (g_stop_requested != 0) {
        return EXIT_SUCCESS;
    }

    return EXIT_SUCCESS;
}
