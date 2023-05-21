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

#include "protocol.h"
#include "player.h"
#include "client_registry.h"
#include "jeux_globals.h"
#include "invitation.h"
#include "csapp.h"
#include "debug.h"

/*
 * A CLIENT represents the state of a network client connected to the
 * system.  It contains the file descriptor of the connection to the
 * client and it provides functions for sending packets to the client.
 * If the client is logged in as a particular player, it contains a
 * reference to a PLAYER object and it contains a list of invitations
 * for which the client is either the source or the target.  CLIENT
 * objects are managed by the client registry.  So that a CLIENT
 * object can be passed around externally to the client registry
 * without fear of dangling references, it has a reference count that
 * corresponds to the number of references that exist to the object.
 * A CLIENT object will not be freed until its reference count reaches zero.
 */

/*
 * The CLIENT type is a structure type that defines the state of a client.
 * You will have to give a complete structure definition in client.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.
 */

typedef struct invitation_node {
    INVITATION *invitation;
    int id;
    struct invitation_node *next;
    struct invitation_node *prev;
} INVITATION_NODE;

typedef struct client {
    int fd;                     // file descriptor of the client connection
    int ref_count;              // reference count for managing the object's lifetime
    int logged_in;              // boolean variable indicating if it's logged in or not
    PLAYER *player;             // pointer to the player associated with the client, if any
    INVITATION_NODE *invitations_head;   // pointer to the head of the invitation linked list
    pthread_mutex_t lock;       // mutex for incrementing the reference count
} CLIENT;

// return 1 if exists, 0 otherwise
int id_exists_in_client_invitation(CLIENT *client, int id){
    // Acquire the lock associated with the client object
    // Traverse the linked list of invitation nodes
    INVITATION_NODE *curr_node = client->invitations_head;
    while (curr_node != NULL) {
        if (curr_node->id == id) {
            // Release the lock associated with the client object
            return 1;
        }
        curr_node = curr_node->next;
    }
    // Release the lock associated with the client object
    return 0;
}

int find_lowest_id_availalble(CLIENT *client){
    // Acquire the lock associated with the client object
    int i = 0;
    while(1){
        if(!id_exists_in_client_invitation(client, i)){
            break;
        }
        i++;
    }
    return i;
}

/*
 * Create a new CLIENT object with a specified file descriptor with which
 * to communicate with the client.  The returned CLIENT has a reference
 * count of one and is in the logged-out state.
 *
 * @param creg  The client registry in which to create the client.
 * @param fd  File descriptor of a socket to be used for communicating
 * with the client.
 * @return  The newly created CLIENT object, if creation is successful,
 * otherwise NULL.
 */
CLIENT *client_create(CLIENT_REGISTRY *creg, int fd){
    CLIENT *client = malloc(sizeof(CLIENT));
    if (client == NULL) {
        return NULL;  // memory allocation failed
    }

    client->fd = fd;
    client->logged_in = 0;
    client->ref_count = 1;
    debug("increase client [%d -> %d] because newly created client", client->ref_count-1 ,client->ref_count);

    client->player = NULL;
    client->invitations_head = NULL;

    if (pthread_mutex_init(&(client->lock), NULL) != 0) {
        free(client);
        return NULL;  // mutex initialization failed
    }

    debug("created client!");
    return client;
}

/*
 * Increase the reference count on a CLIENT by one.
 *
 * @param client  The CLIENT whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same CLIENT that was passed as a parameter.
 */
CLIENT *client_ref(CLIENT *client, char *why){
    if(client == NULL)
        return NULL;
    pthread_mutex_lock(&client->lock);  // Acquire lock on INVITATION structure

    client->ref_count++;
    debug("increase client [%d -> %d] because %s", client->ref_count-1 ,client->ref_count, why);
    pthread_mutex_unlock(&client->lock);
    return client;
}

/*
 * Decrease the reference count on a CLIENT by one.  If after
 * decrementing, the reference count has reached zero, then the CLIENT
 * and its contents are freed.
 *
 * @param client  The CLIENT whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 */
void client_unref(CLIENT *client, char *why){
    if(client == NULL)
        return;

    pthread_mutex_lock(&client->lock);  // Acquire lock on GAME structure
    client->ref_count--;  // Decrease reference count
    if (client->ref_count == 0) {  // Reference count reached zero, free client contents
        // Destroy the mutex
        debug("DECREASED reference count for client from (1 - 0) %s", why);
        debug("freed client");

        if(client->player){
            player_unref(client->player, "being client is being freed");
        }
        // Free the client structure itself
        pthread_mutex_destroy(&client->lock);
        free(client);
        return;
    } else {
        debug("DECREASED reference count for client from (%d - %d) %s",
            client->ref_count+1, client->ref_count, why);
    }
    pthread_mutex_unlock(&client->lock);
}


int check_if_other_clients_logged_in_with_same_player(char *name){
    PLAYER** player_list = creg_all_players(client_registry);
    int result = 0;
    for (int i = 0; player_list[i] != NULL; i++) {
        player_unref(player_list[i], "Player remove from the player list");
        if (strcmp(player_get_name(player_list[i]),name) == 0) {
            result = 1;
        }
    }
    free(player_list);
    return result;
}
/*
 * Log in this CLIENT as a specified PLAYER.
 * The login fails if the CLIENT is already logged in or there is already
 * some other CLIENT that is logged in as the specified PLAYER.
 * Otherwise, the login is successful, the CLIENT is marked as "logged in"
 * and a reference to the PLAYER is retained by it.  In this case,
 * the reference count of the PLAYER is incremented to account for the
 * retained reference.
 *
 * @param CLIENT  The CLIENT that is to be logged in.
 * @param PLAYER  The PLAYER that the CLIENT is to be logged in as.
 * @return 0 if the login operation is successful, otherwise -1.
 */
int client_login(CLIENT *client, PLAYER *player){
    if(client == NULL)
        return -1;
    pthread_mutex_lock(&client->lock);
    if(client->logged_in || check_if_other_clients_logged_in_with_same_player(player_get_name(player))){
        debug("client already logged in or client is already loggined in with the same player name");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    client->logged_in = 1;
    client->player = player;

    // Increment the reference count of the PLAYER
    player_ref(player, "client retained reference to the player");
    pthread_mutex_unlock(&client->lock);
    return 0;
}

/*
 * Log out this CLIENT.  If the client was not logged in, then it is
 * an error.  The reference to the PLAYER that the CLIENT was logged
 * in as is discarded, and its reference count is decremented.  Any
 * INVITATIONs in the client's list are revoked or declined, if
 * possible, any games in progress are resigned, and the invitations
 * are removed from the list of this CLIENT as well as its opponents'.
 *
 * @param client  The CLIENT that is to be logged out.
 * @return 0 if the client was logged in and has been successfully
 * logged out, otherwise -1.
 */
int client_logout(CLIENT *client){
    pthread_mutex_lock(&(client->lock));        // this could cause a trouble

    if(!(client->logged_in)){
        debug("Failed to logout when client is not logged in");
        pthread_mutex_unlock(&(client->lock));
        return -1;
    }
    pthread_mutex_unlock(&(client->lock));

    player_unref(client->player, "client loggout, so the player is discarded");
    INVITATION_NODE *inv = client->invitations_head;
    while(inv != NULL){
        GAME *game;
        INVITATION_NODE *next = inv->next;

        if( (game = inv_get_game(inv->invitation)) != NULL){          // if there's a game in progress
            debug("Resigning game due to logout");
            client_resign_game(client, inv->id);
        }
        else{
            if(inv_get_source(inv->invitation) == client){
                debug("Revoking invitation due to logout");
                client_revoke_invitation(client, inv->id);
            }
            else{
                debug("Declining invitation due to logout");
                client_decline_invitation(client, inv->id);
            }
        }
        inv = next;
    }
    return 0;
}


/*
 * Get the PLAYER for the specified logged-in CLIENT.
 * The reference count on the returned PLAYER is NOT incremented,
 * so the returned reference should only be regarded as valid as long
 * as the CLIENT has not been freed.
 *
 * @param client  The CLIENT from which to get the PLAYER.
 * @return  The PLAYER that the CLIENT is currently logged in as,
 * otherwise NULL if the player is not currently logged in.
 */
PLAYER *client_get_player(CLIENT *client){
    if(client == NULL)
        return NULL;
    if(client->logged_in)
        return client->player;
    return NULL;
}

/*
 * Get the file descriptor for the network connection associated with
 * this CLIENT.
 *
 * @param client  The CLIENT for which the file descriptor is to be
 * obtained. *             Header: invitation ID
 *             Payload: user name of source
 * @return the file descriptor.
 */
int client_get_fd(CLIENT *client){
    return client->fd;
}

/*
 * Send a packet to a client.  Exclusive access to the network connection
 * is obtained for the duration of this operation, to prevent concurrent
 * invocations from corrupting each other's transmissions.  To prevent
 * such interference, only this function should be used to send packets to
 * the client, rather than the lower-level proto_send_packet() function.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @param pkt  The header of the packet to be sent.
 * @param data  Data payload to be sent, or NULL if none.
 * @return 0 if transmission succeeds, -1 otherwise.
 */
int client_send_packet(CLIENT *client, JEUX_PACKET_HEADER *pkt, void *data) {
    if(client == NULL)
        return -1;
    int res = -1;
    pthread_mutex_lock(&client->lock);
    if (proto_send_packet(client_get_fd(client), pkt, data) == 0) {
        res = 0;
    }
    else{

    }

    pthread_mutex_unlock(&client->lock);
    return res;
}

// return NULL if failed
JEUX_PACKET_HEADER *make_packet(JEUX_PACKET_TYPE type, size_t size){
    JEUX_PACKET_HEADER *pkt = malloc(sizeof(JEUX_PACKET_HEADER));
    if(pkt == NULL){
        return NULL;
    }
    memset(pkt, 0, sizeof(JEUX_PACKET_HEADER));
    pkt->type = type;
    pkt->size = htons(size);
    return pkt;
}

/*
 * Send an ACK packet to a client.  This is a convenience function that
 * streamlines a common case.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @param data  Pointer to the optional data payload for this packet,
 * or NULL if there is to be no payload.
 * @param datalen  Length of the data payload, or 0 if there is none.
 * @return 0 if transmission succeeds, -1 otherwise.
 */
int client_send_ack(CLIENT *client, void *data, size_t datalen){
    JEUX_PACKET_HEADER *ack_pkt = make_packet(JEUX_ACK_PKT, datalen);
    if(ack_pkt == NULL){
        return -1;
    }
    debug("Send out ACK packet: fd number is %d", client_get_fd(client));
    if(client_send_packet(client, ack_pkt, data) == -1){
        free(ack_pkt);
        return -1;
    }
    free(ack_pkt);
    return 0;
}

/*
 * Send an NACK packet to a client.  This is a convenience function that
 * streamlines a common case.
 *
 * @param client  The CLIENT who should be sent the packet.
 * @return 0 if transmission succeeds, -1 otherwise.
 */
int client_send_nack(CLIENT *client){
    JEUX_PACKET_HEADER * nack_pkt = make_packet(JEUX_NACK_PKT, 0);
    if(nack_pkt == NULL){
        return -1;
    }
    debug("Send out NACK packet: fd number is %d", client_get_fd(client));
    if(client_send_packet(client, nack_pkt, NULL) == -1){
        free(nack_pkt);
        return -1;
    }
    free(nack_pkt);
    return 0;
}

/*
 * Add an INVITATION to the list of outstanding invitations for a
 * specified CLIENT.  A reference to the INVITATION is retained by
 * the CLIENT and the reference count of the INVITATION is
 * incremented.  The invitation is assigned an integer ID,
 * which the client subsequently uses to identify the invitation.
 *
 * @param client  The CLIENT to which the invitation is to be added.
 * @param inv  The INVITATION that is to be added.
 * @return  The ID assigned to the invitation, if the invitation
 * was successfully added, otherwise -1.
 */
int client_add_invitation(CLIENT *client, INVITATION *inv){
    if(client == NULL || inv == NULL)
        return -1;
    // Acquire the lock associated with the client object
    pthread_mutex_lock(&client->lock);

    // Create a new INVITATION_NODE and set its invitation field to inv
    INVITATION_NODE *inv_node = malloc(sizeof(INVITATION_NODE));
    if (!inv_node) {
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    inv_node->invitation = inv;
    inv_node->next = NULL;
    inv_node->prev = NULL;
    
    // Assign a unique integer ID to the invitation node
    inv_node->id = find_lowest_id_availalble(client);

    // Add the new invitation node to the front of the linked list
    inv_node->next = client->invitations_head;
    if (client->invitations_head) {
        client->invitations_head->prev = inv_node;
    }
    client->invitations_head = inv_node;
    // Increment the reference count of the INVITATION object pointed to by inv
    inv_ref(inv, "add invitation to the client's list");

    // Release the lock associated with the client object
    pthread_mutex_unlock(&client->lock);

    // Return the ID assigned to the invitation
    return inv_node->id;
}

/*
 * Remove an invitation from the list of outstanding invitations
 * for a specified CLIENT.  The reference count of the invitation is
 * decremented to account for the discarded reference.
 *
 * @param client  The client from which the invitation is to be removed.
 * @param inv  The invitation that is to be removed.
 * @return the CLIENT's id for the INVITATION, if it was successfully
 * removed, otherwise -1.
 */
int client_remove_invitation(CLIENT *client, INVITATION *inv) {
    // Ensure the client and invitation pointers are not null
    if (client == NULL || inv == NULL) {
        return -1;
    }


    // Acquire the client's lock
    pthread_mutex_lock(&client->lock);

    // Search for the invitation node in the list
    INVITATION_NODE *curr = client->invitations_head;
    while (curr != NULL) {

        if (curr->invitation == inv) {
            // Remove the invitation node from the list
            if (curr->prev != NULL) {
                curr->prev->next = curr->next;
            } else {
                client->invitations_head = curr->next;
            }

            if (curr->next != NULL) {
                curr->next->prev = curr->prev;
            }
            pthread_mutex_unlock(&client->lock);

            int inv_id = curr->id;
            // Decrement the invitation's reference count
            inv_unref(inv, "removing from the client's invitation list");

            // Save the invitation ID before freeing the node

            // Free the invitation node
            free(curr);

            // Release the client's lock and return the invitation ID
            return inv_id;
        }

        curr = curr->next;
    }

    // If the invitation was not found, release the client's lock and return -1
    pthread_mutex_unlock(&client->lock);
    return -1;
}

/*
 * Make a new invitation from a specified "source" CLIENT to a specified
 * target CLIENT.  The invitation represents an offer to the target to
 * engage in a game with the source.  The invitation is added to both the
 * source's list of invitations and the target's list of invitations and
 * the invitation's reference count is appropriately increased.
 * An `INVITED` packet is sent to the target of the invitation.
 *
 * @param source  The CLIENT that is the source of the INVITATION.
 * @param target  The CLIENT that is the target of the INVITATION.
 * @param source_role  The GAME_ROLE to be played by the source of the INVITATION.
 * @param target_role  The GAME_ROLE to be played by the target of the INVITATION.
 * @return the ID assigned by the source to the INVITATION, if the operation
 * is successful, otherwise -1.
 */
int client_make_invitation(CLIENT *source, CLIENT *target, GAME_ROLE source_role, GAME_ROLE target_role){
        // Allocate memory for a new INVITATION object
    INVITATION *invitation;

    if( (invitation = inv_create(source, target, source_role, target_role)) == NULL){
        debug("Failed to make invitation");
        return -1;
    }
    // Add the invitation to the source and target client's lists of invitations
    int client_id;
    if( (client_id = client_add_invitation(source, invitation)) == -1){
        debug("added invitation to client failed");
        inv_unref(invitation, "The point to the invitation is now discarded");
        return -1;
    }
    int target_id;
    if( (target_id = client_add_invitation(target, invitation)) == -1){
        debug("added invitation to target failed");
        inv_unref(invitation, "The point to the invitation is now discarded");
        return -1;
    }

    pthread_mutex_lock(&source->lock);
 // *             Header: invitation ID
 // *             Payload: user name of source
    // construct INVITATION PACKET AND SEND IT to the TARGET CLIENT
    char *source_name = player_get_name(client_get_player(source));
    JEUX_PACKET_HEADER *invited_pkt = make_packet(JEUX_INVITED_PKT, strlen(source_name));
    invited_pkt->id = target_id;
    invited_pkt->role = target_role;
    if(client_send_packet(target, invited_pkt, source_name) == -1){
        debug("Failed to send the invite packet ot client");
        free(invited_pkt);
        pthread_mutex_unlock(&source->lock);
        return -1;
    }
    free(invited_pkt);
    inv_unref(invitation, "The point to the invitation is now discarded");
    pthread_mutex_unlock(&source->lock);
    return client_id;
}

/*
 * Revoke an invitation for which the specified CLIENT is the source.
 * The invitation is removed from the lists of invitations of its source
 * and target CLIENT's and the reference counts are appropriately
 * decreased.  It is an error if the specified CLIENT is not the source
 * of the INVITATION, or the INVITATION does not exist in the source or
 * target CLIENT's list.  It is also an error if the INVITATION being
 * revoked is in a state other than the "open" state.  If the invitation
 * is successfully revoked, then the target is sent a REVOKED packet
 * containing the target's ID of the revoked invitation.
 *
 * @param client  The CLIENT that is the source of the invitation to be
 * revoked.
 * @param id  The ID assigned by the CLIENT to the invitation to be
 * revoked.
 * @return 0 if the invitation is successfully revoked, otherwise -1.
 */
int client_revoke_invitation(CLIENT *client, int id) {
    // Acquire the lock of the source CLIENT.
    pthread_mutex_lock(&client->lock);

    // Search for the INVITATION_NODE in the source CLIENT's list of outstanding invitations, corresponding to the given ID.
    INVITATION_NODE *source_node = client->invitations_head;
    while (source_node != NULL && source_node->id != id) {
        source_node = source_node->next;
    }

    if (source_node == NULL) {
        // The INVITATION_NODE is not found in the source CLIENT's list of outstanding invitations.
        debug("FAILED to find the invitation associated with the ID in the invitation list");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }

    INVITATION *inv = source_node->invitation;
    if (inv_get_game(inv) != NULL) {
        // The INVITATION is not in the "open" state, meaning there's a game currently going on and it can't be revoked.
        debug("there's a game currently in progress and invitation can't be revoked");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }

    CLIENT *source = inv_get_source(inv);
    // if the client is not the source of the invitation
    if(source != client){
        pthread_mutex_unlock(&client->lock);
        debug("The client is not the source of the invitation, thus it can't be revoked");
        return -1;
    }
    pthread_mutex_unlock(&client->lock);

    // Remove the INVITATION_NODE from the source CLIENT's list of outstanding invitations and decrement the INVITATION's reference count.
    if(client_remove_invitation(client, inv) == -1){
        debug("FAILED to remove invitation from the source's invitation list");
        return -1;
    }

    // Remove the INVITATION_NODE from the target CLIENT's list of outstanding invitations and decrement the INVITATION's reference count.
    int target_id;
    CLIENT *target = inv_get_target(inv);
    if( (target_id =  client_remove_invitation(target, inv)) == -1){
        debug("FAILED to remove invitation from the target's invitation list");
        return -1;
    }


 //  *   REVOKED   Sent when an invitation has been revoked by source
 //  *             Header: invitation ID assigned by target
    // Construct a REVOKED packet containing the target's ID of the revoked invitation and send it to the target CLIENT.
    JEUX_PACKET_HEADER *revoked_pkt = make_packet(JEUX_REVOKED_PKT, 0);
    revoked_pkt->id = target_id;
    if(client_send_packet(target, revoked_pkt, NULL) == -1){
        free(revoked_pkt);
        debug("failed to send the client's packet");
        return -1;
    }
    free(revoked_pkt);
    debug("Revoke packet sent!");

    // Release the locks of both source and target CLIENTs.
    return 0;
}
/*
 * Decline an invitation previously made with the specified CLIENT as target.  
 * The invitation is removed from the lists of invitations of its source
 * and target CLIENT's and the reference counts are appropriately
 * decreased.  It is an error if the specified CLIENT is not the target
 * of the INVITATION, or the INVITATION does not exist in the source or
 * target CLIENT's list.  It is also an error if the INVITATION being
 * declined is in a state other than the "open" state.  If the invitation
 * is successfully declined, then the source is sent a DECLINED packet
 * containing the source's ID of the declined invitation.
 *
 * @param client  The CLIENT that is the target of the invitation to be
 * declined.
 * @param id  The ID assigned by the CLIENT to the invitation to be
 * declined.
 * @return 0 if the invitation is successfully declined, otherwise -1.
 */
int client_decline_invitation(CLIENT *client, int id){
    // Acquire the lock of the source CLIENT.
    pthread_mutex_lock(&client->lock);

    // Search for the INVITATION_NODE in the source CLIENT's list of outstanding invitations, corresponding to the given ID.
    INVITATION_NODE *target_node = client->invitations_head;
    while (target_node != NULL && target_node->id != id) {
        target_node = target_node->next;
    }

    if (target_node == NULL) {
        debug("FAILED to find the invitation associated with the ID in the invitation list");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }

    INVITATION *inv = target_node->invitation;

    if (inv_get_game(inv) != NULL) {
        // The INVITATION is not in the "open" state, meaning there's a game currently going on and it can't be declined.
        debug("there's a game currently in progress and invitation can't be declined");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }


    // Search for the INVITATION_NODE in the target CLIENT's list of outstanding invitations, corresponding to the given ID.
    CLIENT *target = inv_get_target(inv);
    // if the client is not the source of the invitation
    if(target != client){
        debug("The client is not the target of the invitation, thus it can't be declined");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    pthread_mutex_unlock(&client->lock);

    // Remove the INVITATION_NODE from the source CLIENT's list of outstanding invitations and decrement the INVITATION's reference count.
    if(client_remove_invitation(target, inv) == -1){
        debug("FAILED to remove target's invitation from the invitation list");
        return -1;
    }

    CLIENT *source = inv_get_source(inv);
    // Remove the INVITATION_NODE from the target CLIENT's list of outstanding invitations and decrement the INVITATION's reference count.
    int source_id;
    if( (source_id = client_remove_invitation(source, inv)) == -1){
        debug("FAILED to remove source's invitation from the invitation list");
        return -1;
    }
    pthread_mutex_lock(&client->lock);


 // *   DECLINED  Sent when an invitation has been declined by target
 // *             Header: invitation ID assigned by source

    // Construct a declined packet containing the target's ID of the declined invitation and send it to the target CLIENT.
    JEUX_PACKET_HEADER *declined_pkt = make_packet(JEUX_DECLINED_PKT, 0);
    declined_pkt->id = source_id;
    if(client_send_packet(source, declined_pkt, NULL) ){
        free(declined_pkt);
        debug("failed to send the client's packet");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    free(declined_pkt);

    pthread_mutex_unlock(&client->lock);
    return 0;
}

/*
 * Accept an INVITATION previously made with the specified CLIENT as
 * the target.  A new GAME is created and a reference to it is saved
 * in the INVITATION.  If the invitation is successfully accepted,
 * the source is sent an ACCEPTED packet containing the source's ID
 * of the accepted INVITATION.  If the source is to play the role of
 * the first player, then the payload of the ACCEPTED packet contains
 * a string describing the initial game state.  A reference to the
 * new GAME (with its reference count incremented) is returned to the
 * caller.
 *
 * @param client  The CLIENT that is the target of the INVITATION to be
 * accepted.
 * @param id  The ID assigned by the target to the INVITATION.
 * @param strp  Pointer to a variable into which will be stored either
 * NULL, if the accepting client is not the first player to move,
 * or a malloc'ed string that describes the initial game state,
 * if the accepting client is the first player to move.
 * If non-NULL, this string should be used as the payload of the `ACK`
 * message to be sent to the accepting client.  The caller must free
 * the string after use.
 * @return 0 if the INVITATION is successfully accepted, otherwise -1.
 */
int client_accept_invitation(CLIENT *client, int id, char **strp) {
    INVITATION *inv;
    char *state_str;
    pthread_mutex_lock(&client->lock);

    /***STEP 1: FIND the invitation in the client's invitation list**/
    INVITATION_NODE *target_node = client->invitations_head;
    while (target_node != NULL && target_node->id != id) {
        target_node = target_node->next;
    }
    if (target_node == NULL) {
        debug("FAILED to find the invitation associated with the ID in the invitation list");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    inv = target_node->invitation;

    CLIENT *target = inv_get_target(inv);
    CLIENT *source = inv_get_source(inv);

    if(target != client){
        debug("The client is not the target of the invitation, so it can't be accepted");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }

    if (inv_get_game(inv) != NULL) {
        debug("there's a game currently in progress and invitation can't be accepted");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    debug("stage 1");
    /***STEP 2: ACCEPT THE PACKET CREATE A NEW GAME AND SAVE IT TO THE INVITATION**/
    if( inv_accept(inv) == -1 ){
        debug("failed to accept the invitation");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    debug("stage 2");

    /***STEP 3: DEALING WITH SENDING PACKETS & stuff**/

    // Get the source client ID and initial state string
    INVITATION_NODE *source_node = source->invitations_head;
    while (source_node != NULL && source_node->invitation != inv) {
        source_node = source_node->next;
    }
    if (source_node == NULL) {
        debug("FAILED to find the invitation in the invitation list");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    int source_id = source_node->id;

// *     ACCEPTED  Sent when an invitation has been accepted by target
//  *             Header: invitation ID assigned by source
//  *             Payload: string showing initial game state
    // Construct a accepted packet containing the target's ID send it to the source CLIENT.
    JEUX_PACKET_HEADER *accepted_pkt = make_packet(JEUX_ACCEPTED_PKT, 0);
    accepted_pkt->id = source_id;
    debug("stage 3");

    if (inv_get_source_role(inv) == FIRST_PLAYER_ROLE){
        strp = NULL;
        state_str = game_unparse_state(inv_get_game(inv));
        accepted_pkt->size = htons(strlen(state_str));
        if(client_send_packet(source, accepted_pkt, state_str) ){
            free(accepted_pkt);
            free(state_str);
            debug("failed to send the client's packet");
            pthread_mutex_unlock(&client->lock);
            return -1;
        }
        free(accepted_pkt);
        free(state_str);
    }
    else{
        *strp = game_unparse_state(inv_get_game(inv));
        if(client_send_packet(source, accepted_pkt, NULL) ){
            free(accepted_pkt);
            debug("failed to send the client's packet");
            pthread_mutex_unlock(&client->lock);
            return -1;
        }
        free(accepted_pkt);
    }
    pthread_mutex_unlock(&client->lock);
    debug("stage 4");


    // // Return a reference to the new game with its reference count incremented
    // game_ref()  // I might have to do this
    return 0;
}

/*
 * Resign a game in progress.  This function may be called by a CLIENT
 * that is either source or the target of the INVITATION containing the
 * GAME that is to be resigned.  It is an error if the INVITATION containing
 * the GAME is not in the ACCEPTED state.  If the game is successfully
 * resigned, the INVITATION is set to the CLOSED state, it is removed
 * from the lists of both the source and target, and a RESIGNED packet
 * containing the opponent's ID for the INVITATION is sent to the opponent
 * of the CLIENT that has resigned.
 *
 * @param client  The CLIENT that is resigning.
 * @param id  The ID assigned by the CLIENT to the INVITATION that contains
 * the GAME to be resigned.
 * @return 0 if the game is successfully resigned, otherwise -1.
 */
int client_resign_game(CLIENT *client, int id) {
    INVITATION *inv;
    pthread_mutex_lock(&client->lock);

    // Find the invitation in the client's invitation list
    INVITATION_NODE *inv_node = client->invitations_head;
    while (inv_node != NULL && inv_node->id != id) {
        inv_node = inv_node->next;
    }
    if (inv_node == NULL) {
        debug("FAILED to find the invitation in the invitation list");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    inv = inv_node->invitation;

    CLIENT *source = inv_get_source(inv);
    CLIENT *target = inv_get_target(inv);
    GAME *game = inv_get_game(inv);
    // Check that the game is in progress and that the client is either the source or target
    if (game == NULL || (source != client && target != client)) {
        debug("The invitation is not in the correct state or the client is not a player in the game");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }

    GAME_ROLE client_role = (client == source) ? inv_get_source_role(inv): inv_get_target_role(inv);

    if(inv_close(inv, client_role) == -1){
        debug("failed to resign the game");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }

    // Construct and send the RESIGNED packet to the opponent
    CLIENT *opponent = (client == source) ? target : source;
    INVITATION_NODE *opponent_node = opponent->invitations_head;
    while (opponent_node != NULL && opponent_node ->invitation != inv) {
        opponent_node  = opponent_node ->next;
    }
    if (opponent_node == NULL) {
        debug("FAILED to find the invitation in the invitation list");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    int opponent_id = opponent_node->id;

    pthread_mutex_unlock(&client->lock);

 // *   RESIGNED  Sent when the opponent has resigned
 // *             Header: invitation ID assigned by recipient
    JEUX_PACKET_HEADER *resigned_pkt = make_packet(JEUX_RESIGNED_PKT, 0);
    resigned_pkt->id = opponent_id;
    if (client_send_packet(opponent, resigned_pkt, NULL)) {
        free(resigned_pkt);
        debug("failed to send the resigned packet to the opponent");
        return -1;
    }
    free(resigned_pkt);

     /* Notify both players */
     // *   ENDED     Sent when a game has ended
     // *             Header: invitation ID assigned by recipient
     // *                     GAME_ROLE (none, first, second) of winner
    JEUX_PACKET_HEADER *opp_ended_pkt = make_packet(JEUX_ENDED_PKT, 0);
    opp_ended_pkt->id = opponent_id;
    GAME_ROLE winner = game_get_winner(game);
    opp_ended_pkt->role = winner;

    if (client_send_packet(opponent, opp_ended_pkt, NULL)) {
        free(opp_ended_pkt);
        debug("failed to send the ended packet to the opponent");
        return -1;
    }
    free(opp_ended_pkt);

    JEUX_PACKET_HEADER *cli_ended_pkt = make_packet(JEUX_ENDED_PKT, 0);
    cli_ended_pkt->id = id;
    cli_ended_pkt->role = winner;

    if (client_send_packet(client, cli_ended_pkt, NULL) && errno != EPIPE) {
        free(cli_ended_pkt);
        debug("failed to send the ended packet to the player(AKA not opponent)");
        return -1;
    }
    if(errno == EPIPE){
        debug("SIGPIPE received");
    }
    free(cli_ended_pkt);

    /* Remove the INVITATION */
    if(client_remove_invitation(source, inv) == -1){
        debug("FAILED to remove source's invitation from the invitation list");
        return -1;
    }
    if(client_remove_invitation(target, inv) == -1){
        debug("FAILED to remove target's invitation from the invitation list");
        return -1;
    }

    pthread_mutex_lock(&client->lock);
    /* Update the ratings of both players */
    player_post_result(client_get_player(source), client_get_player(target), client == source ? 2 : 1);

    pthread_mutex_unlock(&client->lock);
    return 0;
}

/*
 * Make a move in a game currently in progress, in which the specified
 * CLIENT is a participant.  The GAME in which the move is to be made is
 * specified by passing the ID assigned by the CLIENT to the INVITATION
 * that contains the game.  The move to be made is specified as a string
 * that describes the move in a game-dependent format.  It is an error
 * if the ID does not refer to an INVITATION containing a GAME in progress,
 * if the move cannot be parsed, or if the move is not legal in the current
 * GAME state.  If the move is successfully made, then a MOVED packet is
 * sent to the opponent of the CLIENT making the move.  In addition, if
 * the move that has been made results in the game being over, then an
 * ENDED packet containing the appropriate game ID and the game result
 * is sent to each of the players participating in the game, and the
 * INVITATION containing the now-terminated game is removed from the lists
 * of both the source and target.  The result of the game is posted in
 * order to update both players' ratings.
 *
 * @param client  The CLIENT that is making the move.
 * @param id  The ID assigned by the CLIENT to the GAME in which the move
 * is to be made.
 * @param move  A string that describes the move to be made.
 * @return 0 if the move was made successfully, -1 otherwise.
 */

int client_make_move(CLIENT *client, int id, char *move) {
    pthread_mutex_lock(&client->lock);
    /* Find the INVITATION corresponding to the specified game ID */
    INVITATION *inv;
    // Find the invitation in the client's invitation list
    INVITATION_NODE *inv_node = client->invitations_head;
    while (inv_node != NULL && inv_node->id != id) {
        inv_node = inv_node->next;
    }
    if (inv_node == NULL) {
        debug("FAILED to find the invitation in the invitation list");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    inv = inv_node->invitation;
    CLIENT *source = inv_get_source(inv);
    CLIENT *target = inv_get_target(inv);
    GAME *game = inv_get_game(inv);
    if (game == NULL) {
        debug("The game is not in progress");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }

    GAME_ROLE client_role = client == source ? inv_get_source_role(inv) : inv_get_target_role(inv);
    /* Parse the move string */
    GAME_MOVE * game_move;
    if ( (game_move = game_parse_move(game, client_role, move)) == NULL) {
        debug("The move could not be parsed");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    
    /* Check if the move is legal in the current game state */
    if (game_apply_move(game, game_move) == -1) {
        free(game_move);
        debug("The move is not legal");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    free(game_move);


    /* Notify the opponent of the CLIENT making the move */
    CLIENT *opponent = (client == source) ? target : source;
    INVITATION_NODE *opponent_node = opponent->invitations_head;

    while (opponent_node != NULL && opponent_node ->invitation != inv) {
        opponent_node  = opponent_node ->next;
    }
    if (opponent_node == NULL) {
        debug("FAILED to find the invitation in the invitation list");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    int opponent_id = opponent_node->id;

     // *   MOVED     Sent when the opponent has made a move
     // *             Header: invitation ID
     // *             Payload: string showing game state after the move
    char *state_str = game_unparse_state(game);
    JEUX_PACKET_HEADER *moved_pkt = make_packet(JEUX_MOVED_PKT, strlen(state_str));
    moved_pkt->id = opponent_id;

    if (client_send_packet(opponent, moved_pkt, state_str)) {
        free(moved_pkt);
        free(state_str);
        debug("failed to send the ended packet to the opponent");
        pthread_mutex_unlock(&client->lock);
        return -1;
    }
    free(moved_pkt);
    free(state_str);


    /* If the move results in the game ending, notify both players and remove the INVITATION */
    if (game_is_over(game)) {
        /* Notify both players */
         // *   ENDED     Sent when a game has ended
         // *             Header: invitation ID assigned by recipient
         // *                     GAME_ROLE (none, first, second) of winner
        JEUX_PACKET_HEADER *opp_ended_pkt = make_packet(JEUX_ENDED_PKT, 0);
        opp_ended_pkt->id = opponent_id;
        GAME_ROLE winner = game_get_winner(game);
        opp_ended_pkt->role = winner;

        if (client_send_packet(opponent, opp_ended_pkt, NULL)) {
            free(opp_ended_pkt);
            debug("failed to send the ended packet to the opponent");
            pthread_mutex_unlock(&client->lock);
            return -1;
        }
        free(opp_ended_pkt);

        JEUX_PACKET_HEADER *cli_ended_pkt = make_packet(JEUX_ENDED_PKT, 0);
        cli_ended_pkt->id = id;
        cli_ended_pkt->role = winner;

        pthread_mutex_unlock(&client->lock);


        if (client_send_packet(client, cli_ended_pkt, NULL)) {
            free(cli_ended_pkt);
            debug("failed to send the ended packet to the opponent");
            return -1;
        }
        free(cli_ended_pkt);

        /* Remove the INVITATION */
        if(client_remove_invitation(source, inv) == -1){
            debug("FAILED to remove source's invitation from the invitation list");
            return -1;
        }
        if(client_remove_invitation(target, inv) == -1){
            debug("FAILED to remove target's invitation from the invitation list");
            return -1;
        }
        pthread_mutex_lock(&client->lock);

        /* Update the ratings of both players */
        // could be a win, a lose, or a draw
        int result = winner == NULL_ROLE ? 0.5 : winner == FIRST_PLAYER_ROLE ? 1 : 2;
        player_post_result(client == source ? client_get_player(source) : client_get_player(target),
                           client == source ? client_get_player(target) : client_get_player(source),
                           result);
    }
    pthread_mutex_unlock(&client->lock);
    
    return 0;
}
