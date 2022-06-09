

#include "types.h"
#include "defs.h"

int sys_init_usertable (void) {
  return init_usertable();
}

int sys_login (void) {
  char *userid, *passwd;
  
  if (argstr(0, &userid) < 0 || argstr(1, &passwd) < 0) {
    return -1;
  }

  // TODO

  return 0;
}

int sys_addUser (void) {
  char *userid, *passwd;
  
  if (argstr(0, &userid) < 0 || argstr(1, &passwd) < 0) {
    return -1;
  }

  // TODO

  return 0;
}

int sys_deleteUser (void) {
  char *userid;
  
  if (argstr(0, &userid)) {
    return -1;
  }

  // TODO

  return 0;
}