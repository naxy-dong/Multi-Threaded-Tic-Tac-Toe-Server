#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>		// IMPORTANT for importing pthreads
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <getopt.h>
#include <ctype.h>
#include <protocol.h>
#include <semaphore.h>      // IMPORTANT for importing semaphores

#include "debug.h"
#include "client_registry.h"
#include "csapp.h"


typedef struct client_node {
    CLIENT *client;
    struct client_node *prev;
    struct client_node *next;
} CLIENT_NODE;

// define the mutex
typedef struct client_registry {
    int P_flag;                        // this flag is used for semaphores
    CLIENT_NODE *head;                 // head of the doubly linked list
    int client_count;                  // # of current clients
    pthread_mutex_t mutex;             // mutex for incrementing the count
    sem_t semaphore;
} CLIENT_REGISTRY;

void insert_client(CLIENT_REGISTRY *cr, CLIENT *client) {
    CLIENT_NODE *node = malloc(sizeof(CLIENT_NODE));
    node->client = client;
    node->prev = NULL;
    node->next = cr->head;

    if (cr->head != NULL) {
        cr->head->prev = node;
    }

    cr->head = node;
    cr->client_count++;
}

void remove_client(CLIENT_REGISTRY *cr, CLIENT *client) {
    CLIENT_NODE *node = cr->head;

    while (node != NULL && node->client != client) {
        node = node->next;
    }

    if (node == NULL) {
        return;
    }

    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        cr->head = node->next;
    }

    if (node->next != NULL) {
        node->next->prev = node->prev;
    }

    cr->client_count--;
    free(node);
    debug("removed clients");
}

void free_clients(CLIENT_NODE *node) {
    CLIENT_NODE *next;

    while (node != NULL) {
        next = node->next;
        free(node->client);
        free(node);
        node = next;
    }
}
/*
 * Initialize a new client registry.
 *
 * @return  the newly initialized client registry, or NULL if initialization
 * fails.
 */
CLIENT_REGISTRY *creg_init(){
	CLIENT_REGISTRY *cr_ptr = malloc(sizeof(CLIENT_REGISTRY));
	if(cr_ptr < 0){
		debug("malloc failed");
		return NULL;
	}
	cr_ptr->client_count= 0;
    cr_ptr->P_flag = 0;
    cr_ptr->head = NULL;

	if (pthread_mutex_init(&(cr_ptr->mutex), NULL) != 0) {
        debug("create mutex lock counter failed");
        return NULL;
    }
    if (sem_init(&cr_ptr->semaphore, 0, 0) < 0){
        debug("create mutex lock counter failed");
        return NULL;
    }
    debug("Creating the client registry");
	return cr_ptr;
}


/*
 * Finalize a client registry, freeing all associated resources.
 * This method should not be called unless there are no currently
 * registered clients.
 *
 * @param cr  The client registry to be finalized, which must not
 * be referenced again.
 */
void creg_fini(CLIENT_REGISTRY *cr){
	if(cr == NULL){
        debug("Passed in a NULL cr");
		return;
	}
    // if currently there are registered clients
    if(cr->client_count != 0){
        debug("You should not finalize when there are registered clients");
        return;
    }
    free_clients(cr->head);
    pthread_mutex_destroy(&(cr->mutex));
    sem_destroy(&(cr->semaphore));
    free(cr);
    debug("destroying the mutex, semaphore, and freeing the client registry");
}

/*
 * Register a client file descriptor.
 * If successful, returns a reference to the the newly registered CLIENT,
 * otherwise NULL.  The returned CLIENT has a reference count of one.
 *
 * @param cr  The client registry.
 * @param fd  The file descriptor to be registered.
 * @return a reference to the newly registered CLIENT, if registration
 * is successful, otherwise NULL.
 */
// 0 if already used, -1 if have not used
int fd_already_used(CLIENT_REGISTRY *cr, int fd){
    CLIENT_NODE *current = cr->head;
    while(current != NULL){
        if(client_get_fd(current->client) == fd){
            return 0;
        }
        current = current->next;
    }
    return -1;
}

CLIENT *creg_register(CLIENT_REGISTRY *cr, int fd){
    if(cr == NULL)
        return NULL;
    pthread_mutex_lock(&(cr->mutex));
    // if the client is already registered or you can't register any more clients
    if(fd_already_used(cr, fd) == 0 || cr->client_count >= MAX_CLIENTS){
        pthread_mutex_unlock(&(cr->mutex));
        return NULL;
    }

    CLIENT *client_ptr = client_create(cr, fd);
    if(client_ptr == NULL){
        pthread_mutex_unlock(&(cr->mutex));
        debug("create client failed");
        return NULL;
    }
    insert_client(cr, client_ptr);

    debug("creg_register: client file descriptor %d (count number: %d), the default has a reference count of 1", 
        fd,
        cr->client_count);

    pthread_mutex_unlock(&(cr->mutex));
    return client_ptr;
}

/*
 * Unregister a CLIENT, removing it from the registry.
 * The client reference count is decreased by one to account for the
 * pointer discarded by the client registry.  If the number of registered
 * clients is now zero, then any threads that are blocked in
 * creg_wait_for_empty() waiting for this situation to occur are allowed
 * to proceed.  It is an error if the CLIENT is not currently registered
 * when this function is called.
 *
 * @param cr  The client registry.
 * @param client  The CLIENT to be unregistered.
 * @return 0  if unregistration succeeds, otherwise -1.
 */

int creg_unregister(CLIENT_REGISTRY *cr, CLIENT *client){

    if(cr == NULL || client == NULL){
        return -1;
    }
    pthread_mutex_lock(&(cr->mutex));
    int client_fd = client_get_fd(client);

    // if CLIENT is not currently registered when this function is called
    if(fd_already_used(cr, client_fd) == -1){
        return -1;
    }
    remove_client(cr, client);

    debug("unregister: client file descriptor %d (count number: %d)", client_fd, cr->client_count);
    client_unref(client, "Client is being unregistered.");       // becareful. The client will be freed!

    if(cr->client_count == 0 && cr->P_flag == 1) {
//        cr->P_flag = 0;
        debug("unregister: increment semaphore");
        V(&cr->semaphore);
    }
    pthread_mutex_unlock(&(cr->mutex));
    return 0;
}

/*
 * Given a username, return the CLIENT that is logged in under that
 * username.  The reference count of the returned CLIENT is
 * incremented by one to account for the reference returned.
 *
 * @param cr  The registry in which the lookup is to be performed.
 * @param user  The username that is to be looked up.
 * @return the CLIENT currently registered under the specified
 * username, if there is one, otherwise NULL.
 */
CLIENT *creg_lookup(CLIENT_REGISTRY *cr, char *user) {
    if (cr == NULL || user == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&cr->mutex);
    CLIENT *result = NULL;

    CLIENT_NODE *node = cr->head;
    while (node != NULL) {
        if (node->client
            && client_get_player(node->client)
            && strcmp(player_get_name(client_get_player(node->client)), user) == 0) {
            result = node->client;
            client_ref(result, "for lookup"); // Increment reference count
            break;
        }
        node = node->next;
    }

    pthread_mutex_unlock(&cr->mutex);
    return result;
}

/*
 * Return a list of all currently logged in players.  The result is
 * returned as a malloc'ed array of PLAYER pointers, with a NULL
 * pointer marking the end of the array.  It is the caller's
 * responsibility to decrement the reference count of each of the
 * entries and to free the array when it is no longer needed.
 *
 * @param cr  The registry for which the set of usernames is to be
 * obtained.
 * @return the list of players as a NULL-terminated array of pointers.
 */

// If the client is logged in as a particular player, it contains a
//  * reference to a PLAYER object and it contains a list of invitations
//  * for which the client is either the source or the target.
PLAYER **creg_all_players(CLIENT_REGISTRY *cr){
    // Traverse the list of clients, adding any logged in players to the array
    if(cr == NULL){
        return NULL;
    }
    pthread_mutex_lock(&cr->mutex);

    // Allocate space for the maximum possible number of players, the # of player = # of clients
    PLAYER **player_list = malloc(sizeof(PLAYER*) * (MAX_CLIENTS + 1)); // add 1 for the null ptr in the end
    if (player_list == NULL) {
        pthread_mutex_unlock(&cr->mutex);
        debug("Malloc for player list failed");
        return NULL;
    }

    // Copy the player pointers into the array
    CLIENT_NODE *node = cr->head;
    int i = 0;
    while (node != NULL && i < MAX_CLIENTS) {
        if (node->client && client_get_player(node->client)) {
            player_list[i] = client_get_player(node->client);
            player_ref(player_list[i], "Player added to the player list");
            i++;
        }
        node = node->next;
    }
    // Null-terminate the array
    player_list[i] = NULL;
    pthread_mutex_unlock(&cr->mutex);
    return player_list;
}

/*
 * A thread calling this function will block in the call until
 * the number of registered clients has reached zero, at which
 * point the function will return.  Note that this function may be
 * called concurrently by an arbitrary number of threads.
 *
 * @param cr  The client registry.
 */
void creg_wait_for_empty(CLIENT_REGISTRY *cr){
    if(cr == NULL){
        return;
    }
    cr->P_flag = 1;
    if(cr->client_count == 0){
        V(&cr->semaphore);
    }
    debug("wait client for empty, count number is %d",  cr->client_count);
    P(&cr->semaphore);
}

/*
 * Shut down (using shutdown(2)) all the sockets for connections
 * to currently registered clients.  The clients are not unregistered
 * by this function.  It is intended that the clients will be
 * unregistered by the threads servicing their connections, once
 * those server threads have recognized the EOF on the connection
 * that has resulted from the socket shutdown.
 *
 * @param cr  The client registry.
 */

void creg_shutdown_all(CLIENT_REGISTRY *cr){
    if(cr == NULL){
        return;
    }
    // logic: loops through all the fd and shutdown registered clients
    CLIENT_NODE *current = cr->head;
    while(current != NULL){
        shutdown(client_get_fd(current->client), SHUT_RD);
        CLIENT_NODE *temp = current;
        current = current->next;
        client_logout(temp->client);
        creg_unregister(cr, temp->client);
    }
    debug("creg shutdown all, count number is %d", cr->client_count);
}
