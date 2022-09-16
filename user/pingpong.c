#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char **argv)
{

  int fd[2];
  char buf[16];

  pipe(fd);

  if(fork() > 0){
    write(fd[1],"ping",16);

    wait(0);

    read(fd[0],buf,16);

    printf("%d: received %s\n",getpid(),buf);
  }

  else{
    read(fd[0],buf,16);
    printf("%d: received %s\n",getpid(),buf);
    write(fd[1],"pong",16);
  }
  exit(0);
}
