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
#include "protocol.h"

/*
 * A PLAYER represents a user of the system.  A player has a username,
 * which does not change, and also has a "rating", which is a value
 * that reflects the player's skill level among all players known to
 * the system.  The player's rating changes as a result of each game
 * in which the player participates.  PLAYER objects are managed by
 * the player registry.  So that a PLAYER object can be passed around
 * externally to the player registry without fear of dangling
 * references, it has a reference count that corresponds to the number
 * of references that exist to the object.  A PLAYER object will not
 * be freed until its reference count reaches zero.
 */

/*
 * The PLAYER type is a structure type that defines the state of a player.
 * You will have to give a complete structure definition in player.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.
 */
typedef struct player {
    char* username;
    double rating;
    int ref_count;
    pthread_mutex_t lock;
} PLAYER;

/*
 * Create a new PLAYER with a specified username.  A private copy is
 * made of the username that is passed.  The newly created PLAYER has
 * a reference count of one, corresponding to the reference that is
 * returned from this function.
 *
 * @param name  The username of the PLAYER.
 * @return  A reference to the newly created PLAYER, if initialization
 * was successful, otherwise NULL.
 */
PLAYER *player_create(char *name){
    // Allocate memory for the new PLAYER object.
    PLAYER *new_player = (PLAYER *) Malloc(sizeof(PLAYER));
    if (new_player == NULL) {
        return NULL; // Memory allocation failed.
    }

    // Allocate memory for a private copy of the username string.
    char *username = (char *) Malloc((strlen(name) + 1) * sizeof(char));
    if (username == NULL) {
        free(new_player); // Free the memory allocated for the new PLAYER object.
        return NULL; // Memory allocation failed.
    }

    // Copy the username string into the newly allocated memory.
    strcpy(username, name);
    username[strlen(name)] = '\0';


    // Initialize the fields of the PLAYER object.
    new_player->username = username;
    new_player->rating = PLAYER_INITIAL_RATING;
    new_player->ref_count = 1; // Set the reference count to 1.
    debug("INCREASED reference count for player [%s] from (0 - 1) because the player is created", new_player->username);
    if (pthread_mutex_init(&new_player->lock, NULL) != 0) {
        debug("create mutex lock counter failed");
        return NULL;
    }
    return new_player;
}

/*
 * Increase the reference count on a player by one.
 *
 * @param player  The PLAYER whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same PLAYER object that was passed as a parameter.
 */
PLAYER *player_ref(PLAYER *player, char *why){
    if(player == NULL)
        return NULL;
    pthread_mutex_lock(&player->lock);
    player->ref_count++;
    debug("INCREASED reference count for player [%s] from (%d - %d) %s",
           player->username, player->ref_count-1, player->ref_count, why);
    pthread_mutex_unlock(&player->lock);
    return player;
}

/*
 * Decrease the reference count on a PLAYER by one.
 * If after decrementing, the reference count has reached zero, then the
 * PLAYER and its contents are freed.
 *
 * @param player  The PLAYER whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 *
 */
void player_unref(PLAYER *player, char *why){
    if(player == NULL)
        return;
    pthread_mutex_lock(&player->lock);
    player->ref_count--;
    debug("DECREASED reference count for **player [%s] from (%d - %d) %s",
           player->username, player->ref_count+1, player->ref_count, why);
    if (player->ref_count == 0) {
        debug("Freeing player %s (%s)\n", player->username, why);
        free(player->username);
        pthread_mutex_unlock(&player->lock);
        pthread_mutex_destroy(&player->lock);
        free(player);
        return;
    }
    pthread_mutex_unlock(&player->lock);
}

/*
 * Get the username of a player.
 *
 * @param player  The PLAYER that is to be queried.
 * @return the username of the player.
 */
char *player_get_name(PLAYER *player){
    if(player == NULL)
        return NULL;
    return player->username;
}

/*
 * Get the rating of a player.
 *
 * @param player  The PLAYER that is to be queried.
 * @return the rating of the player.
 */
int player_get_rating(PLAYER *player){
    if(player == NULL)
        return -1;
    return player->rating;
}

/*
 * Post the result of a game between two players.
 * To update ratings, we use a system of a type devised by Arpad Elo,
 * similar to that used by the US Chess Federation.
 * The player's ratings are updated as follows:
 * Assign each player a score of 0, 0.5, or 1, according to whether that
 * player lost, drew, or won the game.
 * Let S1 and S2 be the scores achieved by player1 and player2, respectively.
 * Let R1 and R2 be the current ratings of player1 and player2, respectively.
 * Let E1 = 1/(1 + 10**((R2-R1)/400)), and
 *     E2 = 1/(1 + 10**((R1-R2)/400))
 * Update the players ratings to R1' and R2' using the formula:
 *     R1' = R1 + 32*(S1-E1)
 *     R2' = R2 + 32*(S2-E2)
 *
 * @param player1  One of the PLAYERs that is to be updated.
 * @param player2  The other PLAYER that is to be updated.
 * @param result   0 if draw, 1 if player1 won, 2 if player2 won.
 */
void player_post_result(PLAYER *player1, PLAYER *player2, int result){
    // Compute the scores achieved by the two players.
    double score1, score2;
    if (result == 0) {
        score1 = 0.5;
        score2 = 0.5;
    } else if (result == 1) {
        score1 = 1.0;
        score2 = 0.0;
    } else if (result == 2) {
        score1 = 0.0;
        score2 = 1.0;
    } else {
        // Invalid result, do nothing.
        return;
    }

    // Compute the expected scores of the two players.
    double rating_diff = player2->rating - player1->rating;
    double exponent = rating_diff / 400.0;
    double expected1 = 1.0 / (1.0 + pow(10.0, exponent));
    double expected2 = 1.0 / (1.0 + pow(10.0, -exponent));

    // Update the ratings of the two players.
    double rating_change1 = 32.0 * (score1 - expected1);
    double rating_change2 = 32.0 * (score2 - expected2);
    pthread_mutex_lock(&player1->lock);
    player1->rating += rating_change1;
    pthread_mutex_unlock(&player1->lock);
    pthread_mutex_lock(&player2->lock);
    player2->rating += rating_change2;
    pthread_mutex_unlock(&player2->lock);
}
