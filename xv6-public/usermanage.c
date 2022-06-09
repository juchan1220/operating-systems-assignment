
#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"

#define USER_ID_MAXLEN 16
#define USER_PW_MAXLEN 16

struct User {
    char userid[USER_ID_MAXLEN];
    char passwd[USER_PW_MAXLEN];
    uint uid;
};

struct User utable[NUSER];

uint next_uid = ROOT_UID + 1;
static int utable_initialized = 0;

static int s_strcmp (char* strA, char* strB) {
    while (*strA == *strB) {
        if (*strA == '\0' && *strB == '\0') {
            return 0;
        }

        strA++; strB++;
    }

    return *strA - *strB;
}

void create_usertable (void) {
    next_uid = 1;
    
    for (int i = 1; i < NUSER; i++) {
        utable[i].userid[0] = '\0';
        utable[i].passwd[0] = '\0';
        utable[i].uid = 0;
    }

    char* root = "root\0";
    for (int i = 0; i < 5; i++) {
        utable[0].userid[i] = root[i];
    }

    char* pw = "0000\0";
    for (int i = 0; i < 5; i++) {
        utable[0].passwd[i] = pw[i];
    }

    utable[0].uid = ROOT_UID;

    struct inode *ip = create("/passwd", T_FILE, 0, 0);
    if (ip == 0) {
        goto bad;
    }

    if (writei(ip, (char*)&next_uid, 0, sizeof(uint)) != sizeof(uint)) {
        goto bad;
    }

    if (writei(ip, (char*)utable, sizeof(uint), sizeof(utable)) != sizeof(utable)) {
        goto bad;
    }
    iunlockput(ip);

    return ;
bad:
    panic("failed to create user table!");
}

int init_usertable (void) {
    if (utable_initialized) {
        return -1;
    }

    begin_op();
    struct inode *ip = namei("/passwd");

    if (ip == 0) {
        create_usertable();
        end_op();
        utable_initialized = 1;
        return 0;
    }

    ilock(ip);
    if (readi(ip, (char*)&next_uid, 0, sizeof(uint)) != sizeof(uint)) {
        goto bad;
    }

    if (readi(ip, (char*)utable, sizeof(uint), sizeof(utable)) != sizeof(utable)) {
        goto bad;
    }
    iunlockput(ip);
    end_op();

    utable_initialized = 1;
    return 0;
bad:
    panic("failed to initialize user table!");
}

uint getuid (char* userid, char* passwd) {
    for (int i = 0; i < NUSER; i++) {
        if (utable[i].uid == 0
        || s_strcmp(userid, utable[i].userid) != 0
        || s_strcmp(passwd, utable[i].passwd) != 0
        ) {
            continue;
        }

        return utable[i].uid;
    }

    return 0;
}