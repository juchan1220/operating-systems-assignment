#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

int sys_init_usertable (void) {
  return init_usertable();
}

static void create_home_directory (char* userid) {
  char path[USER_ID_MAXLEN + 1];

  path[0] = '/';
  strncpy(&path[1], userid, USER_ID_MAXLEN);

  begin_op();
  struct inode *ip = create(path, T_DIR, 0, 0);

  iunlockput(ip);
  end_op();
}

static void set_cwd_as_home_directory (char *userid) {
  char path[USER_ID_MAXLEN + 1];

  path[0] = '/';
  if (strncmp("root", userid, USER_ID_MAXLEN) != 0) {
    strncpy(&path[1], userid, USER_ID_MAXLEN);
  } else {
    path[1] = '\0';
  }

  begin_op();
  struct inode *ip = namei(path);
  struct proc* curproc = myproc();

  if (ip == 0) {
    end_op();
    return ;
  }

  ilock(ip);
  if (ip->type != T_DIR) {
    iunlockput(ip);
    end_op();
    return ;
  }

  iunlock(ip);
  iput(curproc->cwd);
  end_op();

  curproc->cwd = ip;
}

int sys_login (void) {
  char *userid, *passwd;
  
  if (argstr(0, &userid) < 0 || argstr(1, &passwd) < 0) {
    return -1;
  }

  uint uid = getuid(userid, passwd);
  if (uid == 0) {
    return -1;
  }

  set_cwd_as_home_directory(userid);
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

  uint new_uid = add_user(userid, passwd);

  if (new_uid == 0) {
    return -1;
  }

  create_home_directory(userid);

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

  return delete_user(userid);
}