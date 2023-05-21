#include <stdbool.h>
#include <pthread.h>

// #include "invitation.h"
#include "debug.h"
#include "client_registry.h"
#include "csapp.h"

typedef struct invitation {
    int ref_count;
    CLIENT *sender;
    CLIENT *recipient;
    GAME_ROLE source_role;
    GAME_ROLE target_role;
    GAME *game;
    INVITATION_STATE state;
    pthread_mutex_t lock;
} INVITATION;
/*
 * An INVITATION records the status of an offer, made by one CLIENT
 * to another, to participate in a GAME.  The CLIENT that initiates
 * the offer is called the "source" of the invitation, and the CLIENT
 * that is the recipient of the offer is called the "target" of the
 * invitation.  An INVITATION stores references to its source and
 * target CLIENTs, whose reference counts reflect the existence of
 * these references.  At any time, an INVITATION may be in one of
 * three states: OPEN, ACCEPTED, or CLOSED.  A newly created INVITATION
 * starts out in the OPEN state.  An OPEN invitation may be "accepted"
 * or "declined" by its target.  It may also be "revoked" by its
 * source.  An invitation that has been accepted by its target transitions
 * to the ACCEPTED state.  In association with such a transition a new
 * GAME is created and a reference to it is stored in the INVITATION.
 * An invitation that is declined by its target or revoked by its
 * source transitions to the CLOSED state.  An invitation in the
 * ACCEPTED state will also transition to the CLOSED state when the
 * game in progress has ended.
 */

/*
 * The INVITATION type is a structure type that defines the state of
 * an invitation.  You will have to give a complete structure
 * definition in invitation.c.  The precise contents are up to you.
 * Be sure that all the operations that might be called concurrently
 * are thread-safe.
 */
// typedef enum invitation_state {
//     INV_OPEN_STATE,
//     INV_ACCEPTED_STATE,
//     INV_CLOSED_STATE
// } INVITATION_STATE;

/*
 * Create an INVITATION in the OPEN state, containing reference to
 * specified source and target CLIENTs, which cannot be the same CLIENT.
 * The reference counts of the source and target are incremented to reflect
 * the stored references.
 *
 * @param source  The CLIENT that is the source of this INVITATION.
 * @param target  The CLIENT that is the target of this INVITATION.
 * @param source_role  The GAME_ROLE to be played by the source of this INVITATION.
 * @param target_role  The GAME_ROLE to be played by the target of this INVITATION.
 * @return a reference to the newly created INVITATION, if initialization
 * was successful, otherwise NULL.
 */
INVITATION *inv_create(CLIENT *source, CLIENT *target,
		       GAME_ROLE source_role, GAME_ROLE target_role){
	// Allocate memory for the new INVITATION structure
    INVITATION *inv = malloc(sizeof(INVITATION));
    if (inv == NULL) {
        return NULL;  // Allocation failed
    }

    // Initialize INVITATION fields

    inv->sender = source;
    inv->recipient = target;
    inv->source_role = source_role;
    inv->target_role = target_role;
    inv->ref_count = 1;
    inv->game = NULL;
    inv->state = INV_OPEN_STATE;

    debug("INCREASED reference count for invitation from (%d - %d) by creating new invitation",
            inv->ref_count-1, inv->ref_count);
	if (pthread_mutex_init(&(inv->lock), NULL) != 0) {
        debug("create mutex lock counter failed");
        free(inv);
        return NULL;
    }
    // Increment reference counts of source and target CLIENTs
    client_ref(source, "Creating new invitation");
    client_ref(target, "Creating new invitation");

    return inv;  // Return pointer to new INVITATION structure
}

/*
 * Increase the reference count on an invitation by one.
 *
 * @param inv  The INVITATION whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same INVITATION object that was passed as a parameter.
 */
INVITATION *inv_ref(INVITATION *inv, char *why){
	if(inv == NULL)
		return NULL;
    pthread_mutex_lock(&inv->lock);  // Acquire lock on INVITATION structure

	inv->ref_count++;
    debug("INCREASED reference count for invitation from (%d - %d) %s",
            inv->ref_count-1, inv->ref_count, why);
    pthread_mutex_unlock(&inv->lock); 
	return inv;

}

/*
 * Decrease the reference count on an invitation by one.
 * If after decrementing, the reference count has reached zero, then the
 * invitation and its contents are freed.
 *
 * @param inv  The INVITATION whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 *
 */
void inv_unref(INVITATION *inv, char *why){
	if(inv == NULL)
		return;
    pthread_mutex_lock(&inv->lock);  // Acquire lock on INVITATION structure
    inv->ref_count--;  // Decrease reference count
    if (inv->ref_count == 0) {  // Reference count reached zero, free INVITATION contents
        // Destroy the mutex
        // Free the INVITATION structure itself
        debug("DECREASED reference count for invitation from (1 - 0) %s", why);
        // Release the client references
        if (inv->sender != NULL) {
            client_unref(inv->sender, "invitation is freed");
        }
        if (inv->recipient != NULL) {
            client_unref(inv->recipient, "invitation is freed");
        }
        if (inv->game != NULL) {
            game_unref(inv->game, "invitation is freed");
        }
        pthread_mutex_destroy(&inv->lock);
        free(inv);
        debug("freed inv");
        return;
    } else {
        debug("DECREASED reference count for invitation from (%d - %d) %s",
            inv->ref_count+1, inv->ref_count, why);
    }
    pthread_mutex_unlock(&inv->lock); 
}

/*
 * Get the CLIENT that is the source of an INVITATION.
 * The reference count of the returned CLIENT is NOT incremented,
 * so the CLIENT reference should only be regarded as valid as
 * long as the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the CLIENT that is the source of the INVITATION.
 */
CLIENT *inv_get_source(INVITATION *inv){
	if(inv == NULL)
		return NULL;
	return inv->sender;
}

/*
 * Get the CLIENT that is the target of an INVITATION.
 * The reference count of the returned CLIENT is NOT incremented,
 * so the CLIENT reference should only be regarded as valid if
 * the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the CLIENT that is the target of the INVITATION.
 */
CLIENT *inv_get_target(INVITATION *inv){
	if(inv == NULL)
		return NULL;
	return inv->recipient;
}

/*
 * Get the GAME_ROLE to be played by the source of an INVITATION.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME_ROLE played by the source of the INVITATION.
 */
GAME_ROLE inv_get_source_role(INVITATION *inv){
	if(inv == NULL)
		return NULL_ROLE;
	return inv->source_role;
}

/*
 * Get the GAME_ROLE to be played by the target of an INVITATION.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME_ROLE played by the target of the INVITATION.
 */
GAME_ROLE inv_get_target_role(INVITATION *inv){
	if(inv == NULL)
		return NULL_ROLE;
	return inv->target_role;
}

/*
 * Get the GAME (if any) associated with an INVITATION.
 * The reference count of the returned GAME is NOT incremented,
 * so the GAME reference should only be regarded as valid as long
 * as the INVITATION has not been freed.
 *
 * @param inv  The INVITATION to be queried.
 * @return the GAME associated with the INVITATION, if there is one,
 * otherwise NULL.
 */
GAME *inv_get_game(INVITATION *inv){
	if(inv == NULL)
		return NULL;
	return inv->game;
}

/*
 * Accept an INVITATION, changing it from the OPEN to the
 * ACCEPTED state, and creating a new GAME.  If the INVITATION was
 * not previously in the the OPEN state then it is an error.
 *
 * @param inv  The INVITATION to be accepted.
 * @return 0 if the INVITATION was successfully accepted, otherwise -1.
 */
int inv_accept(INVITATION *inv){
	if (inv == NULL) {
        return -1;
    }
    pthread_mutex_lock(&inv->lock);  // Acquire lock on INVITATION structure

    if (inv->state != INV_OPEN_STATE) {
        pthread_mutex_unlock(&inv->lock);  // Release lock on INVITATION structure
        return -1;  // INVITATION is not in the OPEN state, return error
    }
    inv->state = INV_ACCEPTED_STATE;  // Change INVITATION state to ACCEPTED
    inv->game = game_create();  // Create new GAME
	if (inv->game == NULL) {
    	pthread_mutex_unlock(&inv->lock);  // Release lock on INVITATION structure
        return -1;  // Failed to create new GAME, return error
    }
    pthread_mutex_unlock(&inv->lock);  // Release lock on INVITATION structure

    return 0;  // Success
}

/*
 * Close an INVITATION, changing it from either the OPEN state or the
 * ACCEPTED state to the CLOSED state.  If the INVITATION was not previously
 * in either the OPEN state or the ACCEPTED state, then it is an error.
 * If INVITATION that has a GAME in progress is closed, then the GAME
 * will be resigned by a specified player.
 *
 * @param inv  The INVITATION to be closed.
 * @param role  This parameter identifies the GAME_ROLE of the player that
 * should resign as a result of closing an INVITATION that has a game in
 * progress.  If NULL_ROLE is passed, then the invitation can only be
 * closed if there is no game in progress.
 * @return 0 if the INVITATION was successfully closed, otherwise -1.
 */
int inv_close(INVITATION *inv, GAME_ROLE role){
	 if (inv == NULL || (role == NULL_ROLE && inv->game != NULL)) {
        return -1;
    }
    pthread_mutex_lock(&inv->lock);
    if (inv->state != INV_OPEN_STATE && inv->state != INV_ACCEPTED_STATE) {
        pthread_mutex_unlock(&inv->lock);
        return -1;
    }
    if (inv->state == INV_ACCEPTED_STATE && inv->game != NULL) {
        // A game is in progress, so resign the appropriate player
        game_resign(inv->game, role);
    }

    inv->state = INV_CLOSED_STATE;

    pthread_mutex_unlock(&inv->lock);
    return 0; // success
}

