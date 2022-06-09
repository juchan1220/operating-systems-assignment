// Login

#include "types.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };


int getuserid (char *buf, int nbuf)
{
  printf(2, "login: ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);

  // EOF
  if (buf[0] == 0) {
    return -1;
  }

  return 0;
}

int getuserpw (char *buf, int nbuf)
{
  printf(2, "Password: ");
  memset(buf, 0, nbuf);
  gets(buf, nbuf);

  // EOF
  if (buf[0] == 0) {
    return -1;
  }

  return 0;
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1) {
    printf(2, "login: fork sh failed");
    exit();
  }
  return pid;
}

int main (void) {
    static char userid_buf[100];
    static char passwd_buf[100];
    int fd;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  // Login Infinite Loop
  while (1) {
    getuserid(userid_buf, sizeof(userid_buf));
    getuserpw(passwd_buf, sizeof(passwd_buf));

    // TODO: login correct check
    if (1) {
      if (fork1() == 0) {
        // TODO: user setting
        exec("sh", argv);
        printf(2, "exec sh failed\n");
        exit();
      }
      wait();
    } else {
      printf(2, "Login incorrect\n");
    }
  }

  exit(); 
}