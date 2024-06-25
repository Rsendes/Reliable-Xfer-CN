#include "packet-format.h"
#include <limits.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  char *file_name = argv[1];
  char *host = argv[2];
  int port = atoi(argv[3]);

  FILE *file = fopen(file_name, "r");
  if (!file) {
    perror("fopen");
    exit(EXIT_FAILURE);
  }

  // Prepare server host address.
  struct hostent *he;
  if (!(he = gethostbyname(host))) {
    perror("gethostbyname");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in srv_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = *((struct in_addr *)he->h_addr),
  };

  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  uint32_t seq_num = 0;
  data_pkt_t data_pkt;
  size_t data_len;
  int retries = 0;

  do { // Generate segments from file, until the the end of the file.
    // Prepare data segment.
    ack_pkt_t ack_pkt;
    data_pkt.seq_num = htonl(seq_num++);

    // Load data from file.
    data_len = fread(data_pkt.data, 1, sizeof(data_pkt.data), file);

    if (data_len == 0) break;

    // Send segment.
    ssize_t sent_len =
        sendto(sockfd, &data_pkt, offsetof(data_pkt_t, data) + data_len, 0,
               (struct sockaddr *)&srv_addr, sizeof(srv_addr));
    printf("Sender: sending segment %d, size %ld.\n", ntohl(data_pkt.seq_num),
           offsetof(data_pkt_t, data) + data_len);
    if (sent_len != offsetof(data_pkt_t, data) + data_len) {
      fprintf(stderr, "Truncated packet.\n");
      exit(EXIT_FAILURE);
    }

    //Receives ack
    ssize_t len =
        recvfrom(sockfd, &ack_pkt, sizeof(ack_pkt_t), 0,
                 (struct sockaddr *)&srv_addr, &(socklen_t){sizeof(srv_addr)});
    if (len == -1 ) {
      if (retries == 3) {
        printf("Timeout after 3 retries.\n");
        close(sockfd);
        fclose(file);
        exit(EXIT_FAILURE);
      }
      printf("Retrying ack, retry %d.\n", retries);
      fseek(file, -1*sizeof(data_pkt.data), SEEK_CUR);
      seq_num--;
      retries++;
      continue;
    }
    printf("Sender: recieving Ack segment %d.\n", ntohl(ack_pkt.seq_num));

    retries = 0;  

  } while (!(feof(file) && data_len < sizeof(data_pkt.data)));

  // Clean up and exit.
  close(sockfd);
  fclose(file);

  return EXIT_SUCCESS;
}
