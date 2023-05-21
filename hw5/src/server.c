#include "client_registry.h"
#include "jeux_globals.h"
#include "debug.h"
#include "csapp.h"
#include "player.h"
#include "game.h"
#include "client_registry.h"


 /* Client-to-server requests:
 *   LOGIN:    Log a username into the server
 *             Payload: username
 *   USERS:    Request a list of currently logged-in users
 *   INVITE:   Invite another user to a game
 *             Header: role to which the target is invited.
 *             Payload: username of target
 *   REVOKE:   Revoke an invitation previously made
 *             Header: invitation ID assigned by the source
 *   ACCEPT:   Accept an invitation made by another player
 *             Header: invitation ID assigned by the target
 *   DECLINE:  Decline an invitation made by another player
 *             Header: invitation ID assigned by the target
 *   MOVE:     Make a move in an ongoing game
 *             Header: invitation ID assigned by player making the move
 *             Payload: string that describes the move
 *   RESIGN:   Resign an ongoing game
 *             Header: invitation ID assigned by player who is resigning
 *
 * Server-to-client responses (synchronous):
 *   ACK:      Sent in response to a successful request
 *             Header (depends on request):
 *          invitation ID (for INVITE request)
 *             Payload (depends on request):
 *                      list of usernames (for USERS request)
 *                      initial game state
 *                        (for ACCEPT request sent by first player)
 *   NACK:     Sent in response to a failed request
 *
 * Server-to-client notifications (asynchronous):
 * Specific to client:
 *   INVITED   Sent when an invitation has been made
 *             Header: invitation ID
 *             Payload: user name of source
 *   REVOKED   Sent when an invitation has been revoked by source
 *             Header: invitation ID assigned by target
 *   ACCEPTED  Sent when an invitation has been accepted by target
 *             Header: invitation ID assigned by source
 *             Payload: string showing initial game state
 *   DECLINED  Sent when an invitation has been declined by target
 *             Header: invitation ID assigned by source
 *   MOVED     Sent when the opponent has made a move
 *             Header: invitation ID
 *             Payload: string showing game state after the move
 *   RESIGNED  Sent when the opponent has resigned
 *             Header: invitation ID assigned by recipient
 *   ENDED     Sent when a game has ended
 *             Header: invitation ID assigned by recipient
 *                     GAME_ROLE (none, first, second) of winner
 */

/*
 * Thread function for the thread that handles a particular client.
 *
 * @param  Pointer to a variable that holds the file descriptor for
 * the client connection.  This pointer must be freed once the file
 * descriptor has been retrieved.
 * @return  NULL
 *
 * This function executes a "service loop" that receives packets from
 * the client and dispatches to appropriate functions to carry out
 * the client's requests.  
    basically PACKET TYPE -> STUB FUNCTION

 It also maintains information about whether
 * the client has logged in or not.  Until the client has logged in,
 * only LOGIN packets will be honored.  Once a client has logged in,
 * LOGIN packets will no longer be honored, but other packets will be.
 * The service loop ends when the network connection shuts down and
 * EOF is seen.  This could occur either as a result of the client
 * explicitly closing the connection, a timeout in the network causing
 * the connection to be closed, or the main thread of the server shutting
 * down the connection as part of graceful termination.
 */

/*
 * Client registry that should be used to track the set of
 * file descriptors of currently connected clients.
 */
extern CLIENT_REGISTRY *client_registry;
extern PLAYER_REGISTRY *player_registry;


JEUX_PACKET_HEADER *construct_packet(JEUX_PACKET_TYPE type, size_t size){
    JEUX_PACKET_HEADER *pkt = Malloc(sizeof(JEUX_PACKET_HEADER));
    memset(pkt, 0, sizeof(JEUX_PACKET_HEADER));
    pkt->type = type;
    pkt->size = size;
    return pkt;
}

char* copy_payload(const char* payload, size_t size) {
    // Allocate memory for the new string, including space for the null terminator
    char* new_string = (char*) malloc(size + 1);
    if (new_string == NULL) {
        return NULL; // Error: failed to allocate memory
    }
    memset(new_string, 0, size);
    // Copy the payload into the new string
    memcpy(new_string, payload, size);
    // Add the null terminator
    new_string[size] = '\0';
    // Return the new string
    return new_string;
}

int check_if_other_clients_logged_in_with_same_name(char *name){
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

void *jeux_client_service(void *arg){
    int logged_in = 0;

    int fd = *(int *) arg;
    debug("the client fd is %d", fd);
    free(arg);

    pthread_detach(pthread_self());  // detach
    debug("[%d] starting client service", fd);
    CLIENT *target_client, *client;
    PLAYER *player = NULL;
    if( (client = creg_register(client_registry,fd)) == NULL){
        return NULL;
    }

    JEUX_PACKET_HEADER* hdr = Malloc(sizeof(JEUX_PACKET_HEADER));
    if (hdr == NULL){
        return NULL;
    }
    void* payload = NULL;
    while(proto_recv_packet(fd, hdr, &payload) == 0){
        switch(hdr->type){
            /* Unused */
            case JEUX_NO_PKT:
                debug("Received JEUX_NO_PKT packet: fd number is %d", fd);
                break;

            /************************************* Client-to-server  **********************************/
            case JEUX_LOGIN_PKT:
                debug("Received LOGIN packet: fd number is %d", fd);
                // they should only honor the LOGIN packet if the user is not logged in
                if(!logged_in){ // if the client is already logged in
                    // copy the payload, which is the pointer
                    char* p = malloc( (hdr->size) +1);
                    // char* temp_ptr = payload;
                    memcpy(p, payload, hdr->size);
                    // payload = temp_ptr;
                    *(p + hdr->size) = '\0';

                    if(check_if_other_clients_logged_in_with_same_name(p)){
                        free(p);
                        client_send_nack(client);
                        break;
                    }
                    PLAYER *player = preg_register(player_registry, p);
                    int val = client_login(client,player);
                    debug("client login is SUCC[0]/FAIL[-1] = %d", val);
                    free(p);

                    if(!val){   // successful -> ACK packet
                        client_send_ack(client, NULL, 0);
                        logged_in = 1;
                    }else{     // unsuccessful -> NACK packet
                        client_send_nack(client);
                        break;
                    }
                }else{
                    client_send_nack(client);
                }
                break;
            case JEUX_USERS_PKT:
                debug("Received USERS packet: fd number is %d", fd);

                if(!logged_in){// if the client is NOT logged in
                    client_send_nack(client);
                }
                else{
                    // The server responds by sending an ACK packet whose payload consists of a text string in which
                    // each line gives the username of a currently logged in player, followed by
                    // a single TAB character, followed by the player's current rating
                    PLAYER ** player_list = creg_all_players(client_registry);
                    debug("[%d] USERS", fd);
                    size_t total_size = 0; // assuming null terminator is in there
                    char * str_payload = NULL;
                    int i = 0;
                    for(PLAYER **player = player_list; *player; player++){
                        player_unref(*player, "Player remove from the player list");
                        char *name = player_get_name(*player);
                        int rating = player_get_rating(*player);
                        int line_size = strlen(name) + snprintf(NULL, 0, "%d", rating) + 3;
                        total_size += line_size; // Calculate size of each line

                        char *line = malloc(line_size);
                        snprintf(line, line_size, "%s\t%d\n", name, rating);
                        if (str_payload == NULL) {
                            str_payload = strdup(line);
                        } else {
                            str_payload = realloc(str_payload, total_size);
                            if(str_payload == NULL){
                                client_send_nack(client);
                            }
                            strcat(str_payload, line);
                        }
                        free(line);  // free the line buffer
                        // free(name);  // free the name buffer
                        i++;
                    }
                    str_payload = realloc(str_payload, total_size - i + 5);
                    if (str_payload == NULL) {
                        free(player_list);// needs to free this
                        client_send_nack(client);
                        break;
                    }
                    str_payload[total_size] = '\0'; // Include null terminator
                    if(client_send_ack(client, str_payload, total_size - i) == -1){
                        debug("There's something wrong sending the ack packet");
                    }
                    free(str_payload);
                    free(player_list);// needs to free this
                }
                break;
            case JEUX_INVITE_PKT:
                debug("Received INVITE packet: fd number is %d", fd);
                // look up the player who send the invitation with fd and look up the player who will receive it name
                if(!logged_in) {    // if the target client OR the source client is NOT logged in
                    client_send_nack(client);
                }
                else{
                    // char* p = malloc( (hdr->size) +1);
                    // // char* temp_ptr = payload;
                    // memcpy(p, payload, hdr->size);
                    // // payload = temp_ptr;
                    // *(p + hdr->size) = '\0';
                    debug("The payload is %s", (char *) payload);

                    if((target_client = creg_lookup(client_registry, payload)) == NULL || target_client == client){
                        debug("Target client can't be found using name %s", (char *) payload);
                        client_unref(target_client, "client can't be looked up when invited");
                        client_send_nack(client);
                        break;
                    }

                    // Role: (1 for first player to move, 2 for second player to move)
                    GAME_ROLE source_role;
                    GAME_ROLE target_role;
                    if(hdr->role == 1){
                        source_role = SECOND_PLAYER_ROLE;
                        target_role = FIRST_PLAYER_ROLE;
                    }
                    else if(hdr->role == 2){
                        source_role = FIRST_PLAYER_ROLE;
                        target_role = SECOND_PLAYER_ROLE;
                    }
                    else{
                        debug("game role invalid");
                        // if the header is not 1 or 2, then we don't know what to do
                        client_send_nack(client);
                        break;
                    }

                    int source_id;
                    if( (source_id = client_make_invitation(client, target_client, source_role, target_role)) == -1){
                        debug("Invitation failed");
                        client_send_nack(client);
                        break;
                    }
                    client_unref(target_client, "after invitation attempt");
                    // construct ACT PACKET AND SEND IT to the SOURCE CLIENT
                    JEUX_PACKET_HEADER *ack_pkt = construct_packet(JEUX_ACK_PKT, 0);
                    ack_pkt->id = source_id;
                    client_send_packet(client, ack_pkt, 0);
                    free(ack_pkt);
                }
                break;
            case JEUX_REVOKE_PKT:
                debug("Received REVOKE packet: fd number is %d", fd);
                if( !logged_in){// if the client is NOT logged in
                    client_send_nack(client);
                }
                else{
                    int invite_id = hdr->id;
                    int result;
                    if( (result = client_revoke_invitation(client, invite_id)) == -1){
                        client_send_nack(client);
                        break;
                    }
                    client_send_ack(client, NULL, 0);
                }
                break;
            case JEUX_DECLINE_PKT:
                debug("Received DECLINE packet: fd number is %d", fd);
                if(!logged_in){// if the client is NOT logged in
                    client_send_nack(client);
                }
                else{
                    int invite_id = hdr->id;
                    int result;
                    if( (result = client_decline_invitation(client, invite_id)) == -1){
                        client_send_nack(client);
                        break;
                    }
                    client_send_ack(client, NULL, 0);
                }
                break;
            case JEUX_ACCEPT_PKT:
                debug("Received ACCEPT packet: fd number is %d", fd);
                if(!logged_in){// if the client is NOT logged in
                    client_send_nack(client);
                }
                else{
                    int invite_id = hdr->id;
                    debug("the id is %d", hdr->id);

                    char *str = NULL;
                    int result;
                    if( (result = client_accept_invitation(client, invite_id, &str)) == -1){
                        if(str != NULL){
                            free(str);
                        }
                        client_send_nack(client);
                        break;
                    }

                    JEUX_PACKET_HEADER *ack_pkt = construct_packet(JEUX_ACK_PKT, 0);
                    // only construct pkt did the htonsm we need to do it here
                    ack_pkt->id = invite_id;
                    if(str != NULL){
                        ack_pkt->size = htons(strlen(str));
                    }
                    client_send_packet(client, ack_pkt, str);
                    debug("Send out ACK packet: fd number is %d", fd);
                    if(str != NULL){
                        debug("free the str");
                        free(str); //throws segfault probably
                    }
                    free(ack_pkt);
                }
                break;
            case JEUX_MOVE_PKT:
                debug("Received MOVE packet: fd number is %d", fd);
                if(!logged_in){// if the client is NOT logged in
                    client_send_nack(client);
                }
                else{
                    int client_id = hdr->id;
                    int result;
                    void *p = copy_payload(payload, hdr->size);
                    if((result = client_make_move(client, client_id, p)) == -1){
                        if(p != NULL){
                            free(p);
                        }
                        client_send_nack(client);
                        break;
                    }
                    if(p != NULL){
                        free(p);
                    }
                    debug("MOVE success");
                    client_send_ack(client, NULL, 0);
                }
                break;
            case JEUX_RESIGN_PKT:
                debug("Received RESIGN packet: fd number is %d", fd);
                if(!logged_in){// if the client is NOT logged in
                    client_send_nack(client);
                }
                else{
                    int client_id = hdr->id;
                    int result;

                    if((result = client_resign_game(client, client_id)) == -1){
                        client_send_nack(client);
                        break;
                    }
                    client_send_ack(client, NULL, 0);
                }
                break;
        }

        if(payload != NULL){
            free(payload);
        }
    }

    debug("[%d]Ending client service", fd);
    if(logged_in){
        player_unref(player, "Server thread is closing while the client is logged in");
    }
    client_logout(client);
    creg_unregister(client_registry, client);
    free(hdr);
    return NULL;
}
