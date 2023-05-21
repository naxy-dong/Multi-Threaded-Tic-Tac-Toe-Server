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
#include <protocol.h>

#include "debug.h"
#include "csapp.h"
#include "player.h"

char* copy_payload_v2(const char* payload, size_t size) {
    char* new_string = (char*) malloc(size + 1);
    if (new_string == NULL) {
        return NULL; // Error: failed to allocate memory
    }
    memcpy(new_string, payload, size);
    new_string[size] = '\0';
    return new_string;
}

/*
 * A player registry maintains a mapping from usernames to PLAYER objects.
 * Entries persist for as long as the server is running.
 */

/*
 * The PLAYER_REGISTRY type is a structure type that defines the state
 * of a player registry.  You will have to give a complete structure
 * definition in player_registry.c. The precise contents are up to
 * you.  Be sure that all the operations that might be called
 * concurrently are thread-safe.
 */

#define MAX_PLAYERS 64

// Node in the player registry doubly linked list
typedef struct player_registry_node {
    char *name;                   // player name
    PLAYER *player;              // player object
    struct player_registry_node *prev;  // pointer to previous node
    struct player_registry_node *next;  // pointer to next node
} PLAYER_REGISTRY_NODE;

// Main structure for the player registry
typedef struct player_registry {
    pthread_mutex_t lock;           // Mutex for thread safety
    PLAYER_REGISTRY_NODE *head;    // head of the linked list
} PLAYER_REGISTRY;

void player_registry_insert(PLAYER_REGISTRY *pr, char *name, PLAYER *player) {
    // Allocate memory for the new node
    PLAYER_REGISTRY_NODE *new_node = malloc(sizeof(PLAYER_REGISTRY_NODE));
    if (new_node == NULL) {
        return;  // Allocation failed
    }
    // Initialize fields of new node
    new_node->name = strdup(name);
    new_node->player = player;
    new_node->prev = NULL;
    // Update pointers of existing nodes to include the new node
    if (pr->head != NULL) {
        pr->head->prev = new_node;
    }
    new_node->next = pr->head;
    pr->head = new_node;
}

// Remove the node with the given player name from the linked list
void player_registry_remove(PLAYER_REGISTRY *pr, char *name) {
    PLAYER_REGISTRY_NODE *node = pr->head;
    while (node != NULL) {
        if (strcmp(node->name, name) == 0) {
            // Found the node, update pointers of adjacent nodes to skip over it
            if (node->prev != NULL) {
                node->prev->next = node->next;
            } else {
                pr->head = node->next;
            }
            if (node->next != NULL) {
                node->next->prev = node->prev;
            }
            // Free memory of the removed node
            free(node->name);
            player_unref(node->player, "Removing player from registry");
            free(node);
            break;
        }
        node = node->next;
    }
}

// Free all nodes in the linked list and the registry itself
void player_registry_free(PLAYER_REGISTRY *pr) {
    // Acquire lock to modify the linked list
    // Traverse the linked list and free each node
    PLAYER_REGISTRY_NODE *node = pr->head;
    while (node != NULL) {
        PLAYER_REGISTRY_NODE *next_node = node->next;
        free(node->name);
        player_unref(node->player, "Freeing player registry node");
        free(node);
        node = next_node;
    }
    free(pr);
}

void player_registry_free_nodes(PLAYER_REGISTRY_NODE *head) {
    while (head != NULL) {
        PLAYER_REGISTRY_NODE *temp = head;
        head = head->next;
        free(temp->name);
        player_unref(temp->player, "player registry node");
        free(temp);
    }
}

// return the index in which the player exist, otherwise -1
int player_exists(PLAYER **players, char *name) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i] != NULL && strcmp(player_get_name(players[i]), name) == 0) {
            return i;
        }
    }
    return -1;
}
/*
 * Initialize a new player registry.
 *
 * @return the newly initialized PLAYER_REGISTRY, or NULL if initialization
 * fails.
 */
PLAYER_REGISTRY *preg_init(void){
	PLAYER_REGISTRY *preg = malloc(sizeof(PLAYER_REGISTRY));
    if (preg == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&preg->lock, NULL) != 0) {
        free(preg);
        return NULL;
    }

    preg->head = NULL;
    debug("Creating the player registry");

    return preg;
}

/*
 * Finalize a player registry, freeing all associated resources.
 *
 * @param cr  The PLAYER_REGISTRY to be finalized, which must not
 * be referenced again.
 */
void preg_fini(PLAYER_REGISTRY *preg){
    if (preg == NULL) {
        return;
    }
    pthread_mutex_lock(&preg->lock);
    player_registry_free_nodes(preg->head);
    pthread_mutex_unlock(&preg->lock);
    pthread_mutex_destroy(&preg->lock);

    // Free the PLAYER_REGISTRY itself
    free(preg);
}

/*
 * Register a player with a specified user name.  If there is already
 * a player registered under that user name, then the existing registered
 * player is returned, otherwise a new player is created.
 * If an existing player is returned, then its reference count is increased
 * by one to account for the returned pointer.  If a new player is
 * created, then the returned player has reference count equal to two:
 * one count for the pointer retained by the registry and one count for
 * the pointer returned to the caller.
 *
 * @param name  The player's user name, which is copied by this function.
 * @return A pointer to a PLAYER object, in case of success, otherwise NULL.
 *
 */

PLAYER *preg_register(PLAYER_REGISTRY *preg, char *name) {
    if(preg == NULL)
        return NULL;
    pthread_mutex_lock(&preg->lock); // acquire the lock

    // Check if player already exists
    PLAYER_REGISTRY_NODE *current = preg->head;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            debug("Player %s already exists in the registry", name);
            player_ref(current->player, "registering existing player"); // increase reference count
            pthread_mutex_unlock(&preg->lock); // release the lock
            return current->player;
        }
        current = current->next;
    }
    debug("Player %s does not exist in the registry", name);

    // Player is not registered, so create a new player object
    PLAYER *player = player_create(name);
    if (player == NULL) {
        pthread_mutex_unlock(&preg->lock);
        return NULL;
    }

    // Create new node for the player
    PLAYER_REGISTRY_NODE *new_node = malloc(sizeof(PLAYER_REGISTRY_NODE));
    if (new_node == NULL) {
        player_unref(player, "registering player"); // decrement reference count
        pthread_mutex_unlock(&preg->lock);
        return NULL;
    }

    new_node->name = copy_payload_v2(name, strlen(name));
    new_node->player = player;
    new_node->prev = NULL;
    new_node->next = preg->head;
    if (preg->head != NULL) {
        preg->head->prev = new_node;
    }
    preg->head = new_node;

    player_ref(player, "registering player"); // increase reference count
    pthread_mutex_unlock(&preg->lock); // release the lock

    // Return the newly created player
    return player;
}

