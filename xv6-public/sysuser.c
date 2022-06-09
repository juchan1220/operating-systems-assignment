

#include "types.h"
#include "defs.h"

int sys_login (void) {
  char *userid, *passwd;
  
  if (argstr(0, &userid) < 0 || argstr(1, &passwd) < 0) {
    return -1;
  }

  // TODO

  return 0;
}

int sys_adduser (void) {
  char *userid, *passwd;
  
  if (argstr(0, &userid) < 0 || argstr(1, &passwd) < 0) {
    return -1;
  }

  // TODO

  return 0;
}

int sys_deleteuser (void) {
  char *userid;
  
  if (argstr(0, &userid)) {
    return -1;
  }

  // TODO

  return 0;
}