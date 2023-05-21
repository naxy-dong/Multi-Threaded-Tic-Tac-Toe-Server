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

// typedef struct jeux_packet_header {
//     uint8_t type;          // Type of the packet
//     uint8_t id;            // Invitation ID
//     uint8_t role;          // Role of player in game
//     uint16_t size;                 // Payload size (zero if no payload)
//     uint32_t timestamp_sec;        // Seconds field of time packet was sent
//     uint32_t timestamp_nsec;       // Nanoseconds field of time packet was sent
// } JEUX_PACKET_HEADER;

void print_debug_packet(char *preemble, JEUX_PACKET_HEADER *hdr, void **payloadp){
    if(payloadp){
        debug("%s packet time: %d.%d ID = %d TYPE = %d ROLE = %d SIZE = %u PAYLOAD = %s",
        preemble, 
        hdr->timestamp_sec, 
        hdr->timestamp_nsec,
        hdr->id,
        hdr->type,
        hdr->role,
        hdr->size,
        (char *) payloadp);
    }
    else{
        debug("%s packet time: %d.%d ID = %d TYPE = %d ROLE = %d SIZE = %u PAYLOAD = NO PAYLOAD",
        preemble, 
        hdr->timestamp_sec, 
        hdr->timestamp_nsec,
        hdr->id,
        hdr->type,
        hdr->role,
        hdr->size);
    }
}

// when reading, it must be in host byte order, when storing, it must be network order
int proto_send_packet(int fd, JEUX_PACKET_HEADER *hdr, void *data) {
    errno = 0;
    int header_size = sizeof(JEUX_PACKET_HEADER);
    int payload_size = ntohs(hdr->size);

    // invalid packet
    if((payload_size == 0 && data != NULL) || (payload_size != 0 && data == NULL)){
        debug("Error: payload_size 0 and there's data or there's size but no payload");
        return -1;
    }

    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC ,&time);
    hdr->timestamp_sec = time.tv_sec;
    hdr->timestamp_nsec = time.tv_nsec;
    // hdr->size = htons(hdr->size); //Why does this line not work??
    // hdr->role = htons(hdr->role);
    // hdr->type = htons(hdr->type);
    // hdr->id = htons(hdr->id);
    hdr->timestamp_nsec = htonl(hdr->timestamp_nsec);
    hdr->timestamp_sec = htonl(hdr->timestamp_sec);
    // memset(hdr, 0, sizeof(JEUX_PACKET_HEADER));
    // writing
    if(rio_writen(fd, hdr, header_size) < 0){
        debug("Error writing packet header to socket");
        return -1;
    }
    print_debug_packet("send: ", hdr, data);
    // Write data to socket, if present
    if (payload_size > 0 && data != NULL) {
        errno = 0;
        if(rio_writen(fd, data, payload_size) < 0){
            debug("Error writing packet header to socket");
            return -1;
        }
    }
    // otherwise we success in writing, return 0
    return 0;
}

int proto_recv_packet(int fd, JEUX_PACKET_HEADER *hdr, void **payloadp) {
    errno = 0;
    // Read packet header from the wire
    int result;
    if((result = Rio_readn(fd, hdr, sizeof(JEUX_PACKET_HEADER))) <= 0){
        return -1;
    }
    // print_debug_packet("recv before:", hdr, payloadp);
    // Convert multi-byte fields from network to host byte order
    hdr->size = ntohs(hdr->size);
    // hdr->id = ntohs(hdr->id);
    // hdr->role = ntohs(hdr->role);
    // hdr->type = ntohs(hdr->type);
    hdr->timestamp_sec = ntohl(hdr->timestamp_sec);
    hdr->timestamp_nsec = ntohl(hdr->timestamp_nsec);

    // If the packet has a payload, allocate memory for it and read from the wire
    if (hdr->size > 0) {
        errno = 0;
        char *payload = malloc(hdr->size + 1);
        memset(payload, 0, hdr->size + 1);          /***IMPORTANT!! need to do this to avoid mem leaks ***/ 
        if (payload == NULL) {
            // Error allocating memory for payload
            return -1;
        }
        // char *p = payload;
        // Read payload from the wire
        int num = 0;
        if( (num = Rio_readn(fd, payload, hdr->size))<=0){
            debug("something is wrong when reading payload");
            free(payload);
            return -1;
        }
        // debug("num is %d", num);
        payload[num] = '\0';
        *payloadp = payload;
    } else {
        // Packet has no payload
        *payloadp = NULL;

    }
    print_debug_packet("recv:", hdr,payloadp);

    return 0;
}