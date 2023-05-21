#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <getopt.h>
#include <ctype.h>

#include "debug.h"
#include "protocol.h"
#include "server.h"
#include "client_registry.h"
#include "player_registry.h"
#include "jeux_globals.h"
#include "csapp.h"

#ifdef DEBUG
int _debug_packets_ = 1;
#endif

#define PORT_OPTION                 0x1
int global_options = 0;

static void terminate(int status);
int *connfdp;

void sighup_handler(int signal) {
    debug("Received SIGHUP signal!\n");
    free(connfdp);
    terminate(EXIT_SUCCESS);
}

void sigpipe_handler(int signal){
    debug("proto_send_packet: [broken pipe]");
}

int my_atoi(char *str){
    int i = 0;
    while (str[i] != '\0') {
        if (!isdigit(str[i])) {
            return -1;
        }
        i++;
    }
    return atoi(str);
}

int port = 0;
char *host = "localhost";
/*
 * "Jeux" game server.
 *
 * Usage: jeux <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    int opt;
    char *portstr;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                global_options |= PORT_OPTION;
                if( (port = my_atoi(optarg)) == -1){
                    debug("Invalid port number");
                    exit(EXIT_FAILURE);
                }
                portstr = optarg;
                break;
            default:
                break;
        }
    }

    // Check if the required options are set
    if (!(global_options & PORT_OPTION)) {
        fprintf(stderr, "Missing required port option\n");
        exit(EXIT_FAILURE);
    }

    // Perform required initializations of the client_registry and
    // player_registry.
    client_registry = creg_init();
    player_registry = preg_init();

    // In addition, you should install a SIGHUP handler, so that receipt of SIGHUP will perform a clean shutdown of the server.
    struct sigaction sa;
    sa.sa_handler = sighup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        debug("Error setting SIGHUP signal handler");
        exit(EXIT_FAILURE);
    }

    struct sigaction sp;
    sp.sa_handler = sigpipe_handler;
    sigemptyset(&sp.sa_mask);
    sp.sa_flags = 0;

    if (sigaction(SIGPIPE, &sp, NULL) == -1) {
        debug("Error setting SIGPIPE signal handler");
        exit(EXIT_FAILURE);
    }

    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    listenfd = Open_listenfd(portstr);
    pthread_t tid;

    while(1){
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = malloc(sizeof(int));
        if(connfdp == NULL){
            terminate(EXIT_FAILURE);
        }
        memset(connfdp, 0, sizeof(int));
        *connfdp = Accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if(*connfdp < 0){
            free(connfdp);
            terminate(EXIT_FAILURE);
        }
        pthread_create(&tid,NULL,jeux_client_service,connfdp);
        // break;
    }

    fprintf(stderr, "You have to finish implementing main() "
	    "before the Jeux server will function.\n");

    // something wrong happened
    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
static void terminate(int status) {
    // Shutdown all client connections.
    // This will trigger the eventual termination of service threads.
    creg_shutdown_all(client_registry);

    debug("%ld: Waiting for service threads to terminate...", pthread_self());
    creg_wait_for_empty(client_registry);
    debug("%ld: All service threads terminated.", pthread_self());

    // Finalize modules.
    creg_fini(client_registry);
    preg_fini(player_registry);
    debug("%ld: Jeux server terminating", pthread_self());
    exit(status);
}
