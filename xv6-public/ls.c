#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

char*
fmtpermission (char perm) {
  static char buf[7];
  strcpy(buf, "rwxrwx");

  for (int i = 0; i < 6; i++) {
    if ((perm & (1 << (5 - i))) == 0) {
      buf[i] = '-';
    }
  }

  return buf;
}

char* fmtusername (char* owner_name, uint uid) {
  static char buf[16];

  if (owner_name[0] != '\0') {
    int l = strlen(owner_name);
    memmove(buf, owner_name, l);
    memset(buf+l, ' ', 15 - l);
    return buf;
  } else {
    buf[15] = '\0';
    int i = 14;

    do {
      buf[i--] = (uid % 10) + '0';
      uid /= 10;
    } while (uid > 0);

    memmove(buf, buf + i + 1, 14 - i);
    memset(buf + 14 - i, ' ', i + 1);
  }

  return buf;
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, 0)) < 0){
    printf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    printf(1, "-%s  %s  %s %d %d %d\n",
      fmtpermission(st.perm),
      fmtusername(st.owner_name, st.owner),
      fmtname(path),
      st.type, st.ino, st.size);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf(1, "ls: cannot stat %s\n", buf);
        continue;
      }
      printf(1, "%s%s  %s  %s %d %d %d\n",
        (st.type == T_DIR ? "d" : "-"),
        fmtpermission(st.perm),
        fmtusername(st.owner_name, st.owner),
        fmtname(buf),
        st.type, st.ino, st.size);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit();
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit();
}
