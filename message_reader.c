#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/fcntl.h>


#include "message_slot.h"


typedef enum errors {
  LEN,
  OPEN,
  ID,
  READ, 
} e;

void checker(int val1, int val2, e op);

// general function for validations
void checker(int val1, int val2, e op){
    if (op == LEN && val1 != val2){
        perror("Error - Invalid number of command line arguments");
        exit(1);
    }
    if (op == OPEN && val1 < val2){
        perror("Error - failed to open file");
        exit(1);
    }
    if (op == ID && val1 != val2){
        perror("Error - failed to set channel id");
        exit(1);
    }
    if (op == READ && val1 < val2){
        perror("Error - failed reading file");
        exit(1);
    }
}

int main(int argc, char* argv[]){
    int fd = 0;
    unsigned long channel_id = 0;
    int n_bytes = 0;
    char buffer[128];
    // validate num of args
    checker(argc, 3, LEN);
    fd = open(argv[1], O_RDONLY);
    // check file open success 
    checker(fd, 0, OPEN);
    channel_id = atol(argv[2]);
    // check ioctl sucsses
    checker(ioctl(fd, MSG_SLOT_CHANNEL, channel_id), 0, ID);
    n_bytes = read(fd, buffer, MAX_MSG_LEN);
    // check reading sucsses
    checker(n_bytes, 0, READ);
    // check printing sucsses
    if (write(STDOUT_FILENO, buffer, n_bytes) == -1){
        perror("Error - failed printing");
        exit(1);
    }
    close(fd);
    
    exit(0);
}
