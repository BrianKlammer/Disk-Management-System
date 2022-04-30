#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h> 
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n bytes from fd; returns true on success and false on
 * failure */
static bool nread(int fd, int len, uint8_t *buf) {
  int bytes_read = 0;
  while(bytes_read < len) //read() may not read all of the bytes in one call
  {
    int r = read(fd, buf, len - bytes_read);
    if(r < 0) //checks to make sure read() call was successful
    {
      return false;
    }
    bytes_read += r;
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on
 * failure */
static bool nwrite(int fd, int len, uint8_t *buf) {
  int bytes_written = 0;
  while(bytes_written < len) //write() call may not write all of the bytes in one call
  {
    int w = write(fd, buf, len - bytes_written);
    if(w < 0) //checks to make sure write() call was successful
    {
      return false;
    }
    bytes_written += w;
  }
  return true;
}

/* attempts to receive a packet from fd; returns true on success and false on
 * failure */
static bool recv_packet(int fd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  uint16_t len; //length of packet message received
  uint8_t header[HEADER_LEN]; //array to hold the packet message received from the server
  if(!nread(fd, HEADER_LEN, header)) //reads the packet from the server
  {
    return false;
  }
  memcpy(&len, header, sizeof(len));
  memcpy(op, header + 2, sizeof(*op));
  memcpy(ret, header + 6, sizeof(*ret));
  len = ntohs(len);
  *op = ntohl(*op);
  *ret = ntohs(*ret);
  if(len > 8) //checks to see if packet message contains a block
  {
    if(!nread(fd, JBOD_BLOCK_SIZE, block)) //reads the block from the message and copies its contents to block passed in
    {
      return false;
    }
  }
  return true;
}

/* attempts to send a packet to sd; returns true on success and false on
 * failure */
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  uint8_t message[HEADER_LEN + JBOD_BLOCK_SIZE]; //array to hold the packet message sent to the server
  uint16_t packet_len = HEADER_LEN;
  if(op >> 26 == JBOD_WRITE_BLOCK)
  {
    packet_len += JBOD_BLOCK_SIZE; //if the operation is a write operation, the message length needs to be incremented
    memcpy(message + 8, block, JBOD_BLOCK_SIZE); //adds the block to the packet message
  }
  packet_len = htons(packet_len);
  memcpy(message, &packet_len, sizeof(packet_len));
  uint32_t packet_op = htonl(op);
  memcpy(message + 2, &packet_op, sizeof(op));
  packet_len = ntohs(packet_len); //converts packet length back to host format for the nwrite function
  return nwrite(sd, packet_len, message);
}

/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. */
bool jbod_connect(const char *ip, uint16_t port) {
  //create socket
  cli_sd = socket(PF_INET, SOCK_STREAM, 0);
  if(cli_sd == -1)
  {
    return false;
  }
  struct sockaddr_in caddr;
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);
  if(inet_aton(ip, &caddr.sin_addr) == 0)
  {
    return false;
  }
  //connect to server
  if(connect(cli_sd, (const struct sockaddr *) &caddr, sizeof(caddr)) == -1)
  {
    return false;
  }
  return true;
}

/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  //close socket connection
  close(cli_sd);
  cli_sd = -1;
}

/* sends the JBOD operation to the server and receives and processes the
 * response. */
int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint16_t ret_code; //return code of a packet received
  uint32_t op_code; //operation code of a packet received
  send_packet(cli_sd, op, block);
  recv_packet(cli_sd, &op_code, &ret_code, block);
  return ret_code;
}
