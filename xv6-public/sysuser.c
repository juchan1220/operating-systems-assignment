#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int sys_init_usertable (void) {
  return init_usertable();
}

int sys_login (void) {
  char *userid, *passwd;
  
  if (argstr(0, &userid) < 0 || argstr(1, &passwd) < 0) {
    return -1;
  }

  int id_len = strlen(userid), pw_len = strlen(passwd);

  if (id_len < 2 || 15 < id_len || pw_len < 2 || 15 < pw_len) {
    return -1;
  }

  uint uid = getuid(userid, passwd);
  if (uid == 0) {
    return -1;
  }
  
  // TODO: change cwd to home directory

  change_user(uid);

  return 0;
}

int sys_addUser (void) {
  if (myproc()->uid != ROOT_UID) {
    return -1;
  }

  char *userid, *passwd;
  if (argstr(0, &userid) < 0 || argstr(1, &passwd) < 0) {
    return -1;
  }

  int id_len = strlen(userid), pw_len = strlen(passwd);

  if (id_len < 2 || 15 < id_len || pw_len < 2 || 15 < pw_len) {
    return -1;
  }

  uint new_uid = add_user(userid, passwd);

  if (new_uid == 0) {
    return -1;
  }

  // TODO: create user's home directory

  return 0;
}

int sys_deleteUser (void) {
  if (myproc()->uid != ROOT_UID) {
    return -1;
  }
  
  char *userid;
  if (argstr(0, &userid) < 0) {
    return -1;
  }

  int id_len = strlen(userid);
  
  if (id_len < 2 || 15 < id_len) {
    return -1;
  }

  return delete_user(userid);
}