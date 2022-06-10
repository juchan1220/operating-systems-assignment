
#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"

struct User {
    char username[USERNAME_MAXLEN];
    char passwd[USER_PW_MAXLEN];
    uint uid;
};

struct User utable[NUSER];

uint next_uid = ROOT_UID + 1;
static int utable_initialized = 0;
static struct inode* utable_ip = 0;
static struct sleeplock utable_lock;

// TODO: sleeplock for prevent race condition

void write_usertable (struct inode* ip) {
    if (writei(ip, (char*)&next_uid, 0, sizeof(uint)) != sizeof(uint)) {
        goto bad;
    }

    if (writei(ip, (char*)utable, sizeof(uint), sizeof(utable)) != sizeof(utable)) {
        goto bad;
    }

    return ;
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
        memset(utable[i].username, '\0', USERNAME_MAXLEN);
        memset(utable[i].passwd, '\0', USER_PW_MAXLEN);
        utable[i].uid = 0;
    }

    strncpy(utable[0].username, "root", USERNAME_MAXLEN);
    strncpy(utable[0].passwd, "0000", USER_PW_MAXLEN);
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

    initsleeplock(&utable_lock, "utable");

    return 0;

bad:
    panic("failed to initialize user table!");
}

int is_valid_username (const char* username) {
    int l = strlen(username);
    return 2 <= l && l < USERNAME_MAXLEN;
}

int is_valid_passwd (const char* passwd) {
    int l = strlen(passwd);
    return 2 <= l && l < USER_PW_MAXLEN;
}

struct User* find_user_with_username (const char* username) {
    for (int i = 0; i < NUSER; i++) {
        if (utable[i].uid == 0 || strncmp(utable[i].username, username, USERNAME_MAXLEN) != 0) { continue; }
        return &utable[i];
    }

    return 0;
}

struct User* find_user_with_uid (uint uid) {
    for (int i = 0; i < NUSER; i++) {
        if (utable[i].uid == 0 || utable[i].uid != uid) { continue; }
        return &utable[i];
    }

    return 0;
}

uint getuid (char* username, char* passwd) {
    if (is_valid_username(username) == 0 || is_valid_passwd(passwd) == 0) {
        releasesleep(&utable_lock);
        return 0;
    }

    acquiresleep(&utable_lock);

    struct User* user = find_user_with_username(username);

    if (user == 0 || strncmp(user->passwd, passwd, USER_PW_MAXLEN) != 0) {
        releasesleep(&utable_lock);
        return 0;
    }

    releasesleep(&utable_lock);
    return user->uid;
}

uint add_user (char* username, char* passwd) {    
    if (is_valid_username(username) == 0 || is_valid_passwd(passwd) == 0) {
        return 0;
    }

    acquiresleep(&utable_lock);

    struct User* empty = 0;

    for (int i = 0; i < NUSER; i++) {
        if (utable[i].uid == 0) {
            empty = &utable[i];
            continue;
        }

        if (strncmp(utable[i].username, username, USERNAME_MAXLEN) == 0) {
            releasesleep(&utable_lock);
            return 0;
        }
    }

    if (empty == 0) {
        releasesleep(&utable_lock);
        return 0;
    }

    strncpy(empty->username, username, USERNAME_MAXLEN);
    strncpy(empty->passwd, passwd, USER_PW_MAXLEN);
    empty->uid = next_uid++;

    export_usertable();
    releasesleep(&utable_lock);

    return empty->uid;
}

int delete_user (char* username) {
    if (is_valid_username(username) == 0) {
        return -1;
    }

    if (strncmp("root", username, USERNAME_MAXLEN) == 0) {
        return -1;
    }

    acquiresleep(&utable_lock);

    struct User* user = find_user_with_username(username);

    if (user == 0) {
        releasesleep(&utable_lock);
        return -1;
    }

    memset(user->username, '\0', USERNAME_MAXLEN);
    memset(user->passwd, '\0', USER_PW_MAXLEN);
    user->uid = 0;

    export_usertable();
    releasesleep(&utable_lock);

    return 0;
}

int get_username_with_uid (uint uid, char* username) {
    acquiresleep(&utable_lock);
    struct User* user = find_user_with_uid(uid);

    if (user == 0) {
        releasesleep(&utable_lock);
        return -1;
    }

    strncpy(username, user->username, USERNAME_MAXLEN);

    releasesleep(&utable_lock);
    return 0;
}