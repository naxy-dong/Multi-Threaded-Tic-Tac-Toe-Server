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
#include "csapp.h"
#include "game.h"

/*
 * A GAME represents the current state of a game between participating
 * players.  So that a GAME object can be passed around 
 * without fear of dangling references, it has a reference count that
 * corresponds to the number of references that exist to the object.
 * A GAME object will not be freed until its reference count reaches zero.
 */

/*
 * The GAME type is a structure type that defines the state of a game.
 * You will have to give a complete structure definition in game.c.
 * The precise contents are up to you.  Be sure that all the operations
 * that might be called concurrently are thread-safe.
 */
/*
 * The GAME_MOVE type is a structure type that defines a move in a game.
 * The details are up to you.  A GAME_MOVE is immutable.
 */
#define MAX_BOARD_NUM 9

typedef struct game {
   	int turn_X; // boolean val indicating if it's X's turn or not. 1 if yes 0 otherwise
   	int num_turns;
    int board[MAX_BOARD_NUM]; // 0 means space, 1 if X, and 2 if O
    GAME_ROLE winner; // 0 if no winner, 1 if first person, 2 if second person
    int terminated;
    pthread_mutex_t lock;
    int ref_count;
} GAME;

typedef struct game_move {
    int player; // Player making the move (1 or 2)
    int square; // Square on the board (1-9)
} GAME_MOVE;

/*
 * The GAME_ROLE type is an enumeration type whose value identify the
 * possible roles of players in a game.  For this assignment, we are
 * considering only two-player games with alternating moves, in which
 * the player roles are distinguished by which player is the first to
 * move.  The constant NULL_ROLE does not refer to a player role.  It
 * is used when it is convenient to have a sentinel value of the
 * GAME_ROLE type.
 */
// typedef enum game_role {
//     NULL_ROLE,
//     FIRST_PLAYER_ROLE,
//     SECOND_PLAYER_ROLE
// } GAME_ROLE;


// return 1 if win, 0 otherwise
int win(GAME *game, int player){
	// Check for winning conditions
    return ((game->board[0] == player && game->board[1] == player && game->board[2] == player) ||
        (game->board[3] == player && game->board[4] == player && game->board[5] == player) ||
        (game->board[6] == player && game->board[7] == player && game->board[8] == player) ||
        (game->board[0] == player && game->board[3] == player && game->board[6] == player) ||
        (game->board[1] == player && game->board[4] == player && game->board[7] == player) ||
        (game->board[2] == player && game->board[5] == player && game->board[8] == player) ||
        (game->board[0] == player && game->board[4] == player && game->board[8] == player) ||
        (game->board[2] == player && game->board[4] == player && game->board[6] == player)) 
    ? 1 : 0;
}
/*
 * Create a new game in an initial state.  The returned game has a
 * reference count of one.
 *
 * @return the newly created GAME, if initialization was successful,
 * otherwise NULL.
 */
GAME *game_create(void){
    GAME *new_game = malloc(sizeof(GAME));
    if (new_game == NULL) {
        debug("oh no, malloc failed");
        return NULL;
    }

    if (pthread_mutex_init(&new_game->lock, NULL) != 0) {
        free(new_game);
        return NULL;
    }

    pthread_mutex_lock(&new_game->lock);

    new_game->turn_X = 1;
    new_game->num_turns = 0;
    new_game->winner = NULL_ROLE;
    new_game->terminated = 0;

    for(int i = 0; i < MAX_BOARD_NUM; i++){
        new_game->board[i] = 0;
    }

    new_game->ref_count = 1;
    debug("INCREASED reference count for game from (%d - %d) by creating a new game",
            new_game->ref_count-1, new_game->ref_count);
    pthread_mutex_unlock(&new_game->lock);

    return new_game;
}

/*
 * Increase the reference count on a game by one.
 *
 * @param game  The GAME whose reference count is to be increased.
 * @param why  A string describing the reason why the reference count is
 * being increased.  This is used for debugging printout, to help trace
 * the reference counting.
 * @return  The same GAME object that was passed as a parameter.
 */
GAME *game_ref(GAME *game, char *why){
	if(game == NULL)
		return NULL;
    pthread_mutex_lock(&game->lock);  // Acquire lock on INVITATION structure

	game->ref_count++;
    debug("INCREASED reference count for game from (%d - %d) %s",
            game->ref_count-1, game->ref_count, why);
    pthread_mutex_unlock(&game->lock); 
	return game;
}

/*
 * Decrease the reference count on a game by one.  If after
 * decrementing, the reference count has reached zero, then the
 * GAME and its contents are freed.
 *
 * @param game  The GAME whose reference count is to be decreased.
 * @param why  A string describing the reason why the reference count is
 * being decreased.  This is used for debugging printout, to help trace
 * the reference counting.
 */
void game_unref(GAME *game, char *why){
	if(game == NULL)
		return;

    pthread_mutex_lock(&game->lock);  // Acquire lock on GAME structure
    
    game->ref_count--;  // Decrease reference count
    
    if (game->ref_count == 0) {  // Reference count reached zero, free GAME contents
        // Destroy the mutex
        pthread_mutex_destroy(&game->lock);
        // Free the GAME structure itself
        debug("DECREASED reference count for game from (1 - 0) %s", why);
        free(game);
        debug("freed game");
        return;
    } else {
        debug("DECREASED reference count for game from (%d - %d) %s",
            game->ref_count+1, game->ref_count, why);
    }
    
    pthread_mutex_unlock(&game->lock); 
}

/*
 * Apply a GAME_MOVE to a GAME.
 * If the move is illegal in the current GAME state, then it is an error.
 *
 * @param game  The GAME to which the move is to be applied.
 * @param move  The GAME_MOVE to be applied to the game.
 * @return 0 if application of the move was successful, otherwise -1.
 */
int game_apply_move(GAME *game, GAME_MOVE *move) {
    // Ensure that the move is legal
    if (game == NULL || move == NULL){
        return -1;
    }
    pthread_mutex_lock(&game->lock);

    if (move->square < 1 || move->square > 9 || game->board[move->square - 1] != 0
    	|| (move->player == 1 && game->turn_X != 1) // if it's the first player and it's not their turn yet
    	|| (move->player == 2 && game->turn_X != 0)
    	|| game->terminated) {
        debug("invalid move || not the player's turn || game already terminated");
        pthread_mutex_unlock(&game->lock);
        return -1; // Illegal move
    }
    char *move_str = game_unparse_move(move);
    debug("[%s] is played by [%s]", move_str, game->turn_X ? "X": "O");
    free(move_str);
    // Apply the move to the board
    game->board[move->square - 1] = move->player;
    game->turn_X = game->turn_X ? 0 : 1;

    if(win(game, 1)){
    	game->winner = FIRST_PLAYER_ROLE;
    	game->terminated = 1;
    }
    if(win(game, 2)){
    	game->winner = SECOND_PLAYER_ROLE;
    	game->terminated = 1;
    }
    game->num_turns++;
    
    // all boards are filled
    if(game->num_turns >= 9){
    	game->terminated = 1;
    }
    pthread_mutex_unlock(&game->lock);
    return 0;
}

/*
 * Submit the resignation of the GAME by the player in a specified
 * GAME_ROLE.  It is an error if the game has already terminated.
 *
 * @param game  The GAME to be resigned.
 * @param role  The GAME_ROLE of the player making the resignation.
 * @return 0 if resignation was successful, otherwise -1.
 */
int game_resign(GAME *game, GAME_ROLE role){
	if(game == NULL || game->terminated){
		return -1;
	}
    pthread_mutex_lock(&game->lock);

    game->terminated = 1;
    game->winner = (role == FIRST_PLAYER_ROLE) ? SECOND_PLAYER_ROLE: FIRST_PLAYER_ROLE;
    pthread_mutex_unlock(&game->lock);
    return 0;
}

/*
 * Get a string that describes the current GAME state, in a format
 * appropriate for human users.  The returned string is in malloc'ed
 * storage, which the caller is responsible for freeing when the string
 * is no longer required.
 *
 * @param game  The GAME for which the state description is to be
 * obtained.
 * @return  A string that describes the current GAME state.
 */
char *game_unparse_state(GAME *game) {
    // Allocate enough memory to store the state description
    char *state_str = malloc(47 * sizeof(char) + 1);
    if (state_str == NULL) {
        debug("Error: memory allocation failed");
        return NULL;
    }
    memset(state_str, 0, 48);
    
    // Initialize state string to empty string
    state_str[0] = '\0';

    // Build the state string
    state_str = strcat(state_str, game->board[0] == 0 ? " " : game->board[0] == 1 ? "X" : "O");
    strcat(state_str, "|");
    state_str = strcat(state_str, game->board[1] == 0 ? " " : game->board[1] == 1 ? "X" : "O");
    strcat(state_str, "|");
    state_str = strcat(state_str, game->board[2] == 0 ? " " : game->board[2] == 1 ? "X" : "O");
    strcat(state_str, "\n");
    strcat(state_str, "-----\n");
    state_str = strcat(state_str, game->board[3] == 0 ? " " : game->board[3] == 1 ? "X" : "O");
    strcat(state_str, "|");
    state_str = strcat(state_str, game->board[4] == 0 ? " " : game->board[4] == 1 ? "X" : "O");
    strcat(state_str, "|");
    state_str = strcat(state_str, game->board[5] == 0 ? " " : game->board[5] == 1 ? "X" : "O");
    strcat(state_str, "\n");
    strcat(state_str, "-----\n");
    state_str = strcat(state_str, game->board[6] == 0 ? " " : game->board[6] == 1 ? "X" : "O");
    strcat(state_str, "|");
    state_str = strcat(state_str, game->board[7] == 0 ? " " : game->board[7] == 1 ? "X" : "O");
    strcat(state_str, "|");
    state_str = strcat(state_str, game->board[8] == 0 ? " " : game->board[8] == 1 ? "X" : "O");
    strcat(state_str, "\n");
    debug("the length of it is %li", strlen(state_str));

    strcat(state_str, "It's ");
    strcat(state_str, game->turn_X ? "X" : "O");
    strcat(state_str, "'s turn\n\0");
    return state_str;
}
/*
 * Determine if a specifed GAME has terminated.
 *
 * @param game  The GAME to be queried.
 * @return 1 if the game is over, 0 otherwise.
 */
int game_is_over(GAME *game){
	// if(game == NULL) // what to return. don't know
	return game->terminated ? 1 : 0;
}

/*
 * Get the GAME_ROLE of the player who has won the game.
 *
 * @param game  The GAME for which the winner is to be obtained.
 * @return  The GAME_ROLE of the winning player, if there is one.
 * If the game is not over, or there is no winner because the game
 * is drawn, then NULL_PLAYER is returned.
 */
GAME_ROLE game_get_winner(GAME *game){
	if(game == NULL)
		return NULL_ROLE;
	// winner
	if(game->terminated)// a person could resign the game and win
		return game->winner;
	if(win(game,1))
		return FIRST_PLAYER_ROLE;
	if(win(game,2))
		return SECOND_PLAYER_ROLE;
	return NULL_ROLE;
}

/*
 * Attempt to interpret a string as a move in the specified GAME.
 * If successful, a GAME_MOVE object representing the move is returned,
 * otherwise NULL is returned.  The caller is responsible for freeing
 * the returned GAME_MOVE when it is no longer needed.
 * Refer to the assignment handout for the syntax that should be used
 * to specify a move.
 *
 * @param game  The GAME for which the move is to be parsed.
 * @param role  The GAME_ROLE of the player making the move.
 * If this is not NULL_ROLE, then it must agree with the role that is
 * currently on the move in the game.
 * @param str  The string that is to be interpreted as a move.
 * @return  A GAME_MOVE described by the given string, if the string can
 * in fact be interpreted as a move, otherwise NULL.
 */
GAME_MOVE *game_parse_move(GAME *game, GAME_ROLE role, char *str){
    int square;
    int player;
    int len = strlen(str);

    // role must agree with the role that's currently on the move in the game
    if(role != NULL_ROLE && 
    ( (role == FIRST_PLAYER_ROLE && !(game->turn_X) ) || (role == SECOND_PLAYER_ROLE && game->turn_X)) ){
    	return NULL;
    }
    // Parse the move string
    if (len == 1) {
        // Single digit
        if (str[0] < '1' || str[0] > '9') {
            return NULL;
        }
        square = str[0] - '0';
        player = role == FIRST_PLAYER_ROLE ? 1 : 2;
    } else if (len == 4) {
        // Digit followed by "<-X" or "<-O"
        if (str[0] < '1' || str[0] > '9') {
            return NULL;
        }
        if (str[1] != '<' || str[2] != '-' || (str[3] != 'X' && str[3] != 'O')) {
            return NULL;
        }
        square = str[0] - '0';
        player = (str[3] == 'X') ? 1 : 2;
    } else {
        // Invalid move string
        return NULL;
    }

    // Create a new GAME_MOVE object and return it
    GAME_MOVE *move = malloc(sizeof(GAME_MOVE));
    if (move == NULL) {
        // Error allocating memory
        return NULL;
    }
    move->player = player;
    move->square = square;
    return move;
}

/*
 * Get a string that describes a specified GAME_MOVE, in a format
 * appropriate to be shown to human users.  The returned string should
 * be in a format from which the GAME_MOVE can be recovered by applying
 * game_parse_move() to it.  The returned string is in malloc'ed storage,
 * which it is the responsibility of the caller to free when it is no
 * longer needed.
 *
 * @param move  The GAME_MOVE whose description is to be obtained.
 * @return  A string describing the specified GAME_MOVE.
 */
char *game_unparse_move(GAME_MOVE *move) {
    char *player_str = (move->player == 1) ? "X" : "O";
    char *square_str = malloc(sizeof(char) * 3);
    sprintf(square_str, "%d", move->square);
    char *result = malloc(sizeof(char) * 5);
    result[0] = '\0';
    if(result == NULL){
        return NULL;
    }
    strcat(result, square_str);
    strcat(result, "<-");
    strcat(result, player_str);
    free(square_str);
    return result;
}