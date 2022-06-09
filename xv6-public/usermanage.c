
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
static struct inode* utable_ip = 0;

void write_usertable (struct inode* ip) {
    if (writei(ip, (char*)&next_uid, 0, sizeof(uint)) != sizeof(uint)) {
        goto bad;
    }

    if (writei(ip, (char*)utable, sizeof(uint), sizeof(utable)) != sizeof(utable)) {
        goto bad;
    }

bad:
    panic("failed to write user table!");    
}

void export_usertable (void) {
    begin_op();
    ilock(utable_ip);
    write_usertable(utable_ip);
    iunlock(utable_ip);
    end_op();
}

struct inode* create_usertable (void) {
    next_uid = ROOT_UID + 1;
    
    for (int i = 1; i < NUSER; i++) {
        memset(utable[i].userid, '\0', USER_ID_MAXLEN);
        memset(utable[i].passwd, '\0', USER_PW_MAXLEN);
        utable[i].uid = 0;
    }

    safestrcpy(utable[0].userid, "root", 4);
    safestrcpy(utable[0].passwd, "0000", 4);
    utable[0].uid = ROOT_UID;

    struct inode* ip = create("/passwd", T_FILE, 0, 0);
    if (ip == 0) {
        goto bad;
    }

    write_usertable(ip);
    
    return ip;
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
        ip = create_usertable();
        goto finish;
    }

    ilock(ip);
    if (readi(ip, (char*)&next_uid, 0, sizeof(uint)) != sizeof(uint)) {
        goto bad;
    }

    if (readi(ip, (char*)utable, sizeof(uint), sizeof(utable)) != sizeof(utable)) {
        goto bad;
    }

finish:
    iunlock(ip);
    end_op();

    utable_ip = ip;
    utable_initialized = 1;
    return 0;

bad:
    panic("failed to initialize user table!");
}

struct User* find_user_with_userid (const char* userid) {
    for (int i = 0; i < NUSER; i++) {
        if (utable[i].uid == 0 || strncmp(utable[i].userid, userid, USER_ID_MAXLEN) != 0) { continue; }
        return &utable[i];
    }

    return 0;
}

uint getuid (char* userid, char* passwd) {
    struct User* user = find_user_with_userid(userid);

    if (user == 0 ||  strncmp(user->passwd, passwd, USER_PW_MAXLEN) != 0) {
        return 0;
    }

    return user->uid;
}


uint add_user (char* userid, char* passwd) {

}

int delete_user (char* userid) {
    if (strncmp("root", userid, USER_ID_MAXLEN) == 0) {
        return -1;
    }

    struct User* user = find_user_with_userid(userid);

    if (user == 0) {
        return -1;
    }

    memset(user->userid, '\0', USER_ID_MAXLEN);
    memset(user->passwd, '\0', USER_PW_MAXLEN);
    user->uid = 0;

    export_usertable();

    return 0;
}
