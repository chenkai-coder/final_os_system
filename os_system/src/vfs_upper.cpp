#include "physical_api.h"
#include "config.h"
#include "error.h"
#include "journal.h"
#include "superblock.h"
#include "block_alloc.h"
#include "inode.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// VFS constants aligned to the prompt requirements.
#define SYSOPENFILE 40
#define NOFILE 20
#define DIRSIZ 14
#define PWDSIZ 12
#define PWDNUM 32
#define USERNUM 10
#define NHINO 128
#define NADDR 10

#define DIEMPTY 00000
#define DIFILE 01000
#define DIDIR 02000
#define UDIREAD 00001
#define UDIWRITE 00002
#define UDIEXICUTE 00004
#define GDIREAD 00010
#define GDIWRITE 00020
#define GDIEXICUTE 00040
#define ODIREAD 00100
#define ODIWRITE 00200
#define ODIEXICUTE 00400
#define DEFAULTMODE 00777
#define IUPDATE 00002
#define SUPDATE 00001
#define FREAD 00001
#define FWRITE 00002
#define FAPPEND 00004

static const unsigned short VFS_INVALID_OFILE = 0xFFFF;

#pragma pack(push, 1)
struct vfs_dirent {
    unsigned short d_ino;
    char d_name[DIRSIZ];
};
#pragma pack(pop)

struct inode {
    unsigned short i_mode;
    unsigned int i_uid;
    unsigned int i_gid;
    unsigned int i_nlink;
    unsigned long long i_size;
    unsigned short i_addr[NADDR];
    struct inode *i_forw;
    struct inode *i_back;
    unsigned short i_count;
    unsigned int i_num;
    char i_flag;
};

struct hinode {
    struct inode *i_forw;
};

struct file {
    char f_flag;
    unsigned short f_count;
    struct inode *f_inode;
    unsigned long long f_offset;
};

struct user {
    unsigned int u_uid;
    unsigned int u_gid;
    int used;
    unsigned short u_ofile[NOFILE];
};

struct pwd {
    unsigned short p_uid;
    unsigned short p_gid;
    char password[PWDSIZ];
};

static struct hinode hinode[NHINO];
static struct file sys_ofile[SYSOPENFILE];
static struct pwd pwd_table[PWDNUM];
static char pwd_name[PWDNUM][PWDSIZ];
static struct user user_table[USERNUM];
static struct inode *cur_path_inode = nullptr;
static std::string cur_path = "/";
static int current_user_id = -1;
static int current_user_slot = -1;
static int pwd_count = 0;
static myfs_ino_t users_store_inode = 0;
static int users_store_ready = 0;

#pragma pack(push, 1)
struct vfs_users_header {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
};

struct vfs_users_entry {
    char name[PWDSIZ];
    char pass[PWDSIZ];
    uint16_t uid;
    uint16_t gid;
};
#pragma pack(pop)

static const uint32_t VFS_USERS_MAGIC = 0x55534552; // "USER"
static const uint32_t VFS_USERS_VERSION = 1;
static const char *VFS_USERS_PATH = "/.users";
static const char *VFS_USERS_NAME = ".users";

struct inode *namei(const char *path);
struct inode *iget(unsigned short ino);
void iput(struct inode *p);
int access(struct inode *ip, unsigned short mode);
int sys_login(const char *username, const char *password);
int sys_logout();
int sys_mkdir(const char *path);
int sys_chdir(const char *path);
void sys_dir();
int sys_create(const char *path, unsigned short mode);
int sys_open(const char *path, int mode);
int sys_close(int fd);
int sys_read(int fd, char *buf, int size);
int sys_write(int fd, const char *buf, int size);
int sys_delete(const char *path);
int sys_link(const char *oldpath, const char *newpath);
static int ensure_mounted(void);

static int vfs_users_ensure_store_inode(myfs_ino_t *out_inode);
static int vfs_users_save(void);
static int vfs_users_load_or_init(void);
static int vfs_add_dirent(myfs_ino_t dir_inode, myfs_ino_t child_ino, const std::string &name);

int sys_chmod(const char *path, unsigned short mode) {
    if (path == nullptr) return -1;
    if (ensure_mounted() != 0 || current_user_id < 0) return -1;

    struct inode *target = namei(path);
    if (target == nullptr) return -1;

    // Only root or owner can chmod
    if (current_user_id != 0 && target->i_uid != static_cast<unsigned int>(current_user_id)) {
        iput(target);
        return -1;
    }

    target->i_mode = (target->i_mode & ~0777) | (mode & 0777);
    target->i_flag |= IUPDATE;
    iput(target);
    return 0;
}

int sys_chown(const char *path, unsigned int uid, unsigned int gid) {
    if (path == nullptr) return -1;
    if (ensure_mounted() != 0 || current_user_id < 0) {
        std::cout << "[ERR] chown: not mounted or not logged in" << std::endl;
        return -1;
    }

    struct inode *target = namei(path);
    if (target == nullptr) {
        std::cout << "[ERR] chown: path not found" << std::endl;
        return -1;
    }

    // Only root can chown
    if (current_user_id != 0) {
        std::cout << "[ERR] chown: only root can change ownership (current uid="
                  << current_user_id << ")" << std::endl;
        iput(target);
        return -1;
    }

    target->i_uid = uid;
    target->i_gid = gid;
    target->i_flag |= IUPDATE;
    iput(target);
    return 0;
}

int sys_useradd(const char *username, const char *password, unsigned short uid, unsigned short gid) {
    if (current_user_id != 0) {
        std::cout << "[ERR] only root can add users" << std::endl;
        return -1;
    }
    if (pwd_count >= PWDNUM) {
        std::cout << "[ERR] user table full" << std::endl;
        return -1;
    }
    if (username == nullptr || password == nullptr || username[0] == '\0') {
        std::cout << "[ERR] invalid username/password" << std::endl;
        return -1;
    }
    if (strncmp(username, "root", PWDSIZ) == 0) {
        std::cout << "[ERR] cannot add root" << std::endl;
        return -1;
    }
    for (int i = 0; i < pwd_count; i++) {
        if (strncmp(username, pwd_name[i], PWDSIZ) == 0) {
            std::cout << "[ERR] user already exists" << std::endl;
            return -1;
        }
        if (pwd_table[i].p_uid == uid) {
            std::cout << "[ERR] uid already exists" << std::endl;
            return -1;
        }
    }
    strncpy(pwd_name[pwd_count], username, PWDSIZ - 1);
    pwd_name[pwd_count][PWDSIZ - 1] = '\0';
    strncpy(pwd_table[pwd_count].password, password, PWDSIZ - 1);
    pwd_table[pwd_count].password[PWDSIZ - 1] = '\0';
    pwd_table[pwd_count].p_uid = uid;
    pwd_table[pwd_count].p_gid = gid;
    pwd_count++;
    vfs_users_save();
    std::cout << "[OK] user added" << std::endl;
    return 0;
}

int sys_userdel(const char *username) {
    if (current_user_id != 0) {
        std::cout << "[ERR] only root can delete users" << std::endl;
        return -1;
    }
    if (strcmp(username, "root") == 0) {
        std::cout << "[ERR] cannot delete root" << std::endl;
        return -1;
    }
    int found = -1;
    for (int i = 0; i < pwd_count; i++) {
        if (strncmp(username, pwd_name[i], PWDSIZ) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        std::cout << "[ERR] user not found" << std::endl;
        return -1;
    }
    for (int i = found; i < pwd_count - 1; i++) {
        memcpy(pwd_name[i], pwd_name[i + 1], PWDSIZ);
        pwd_table[i] = pwd_table[i + 1];
    }
    pwd_count--;
    vfs_users_save();
    std::cout << "[OK] user deleted" << std::endl;
    return 0;
}

static void sync_print(const std::string &msg) {
    std::cout << msg << std::endl;
    fflush(stdout);
}

static void vfs_sync_super_info(void) {
    myfs_phys_statfs_t st;
    if (myfs_phys_statfs(&st) != MYFS_OK) {
        return;
    }

    std::cout << "[SYNC] SUPER "
              << st.data_block_start << " "
              << st.data_blocks << " "
              << MYFS_FREE_GROUP_SIZE << " "
              << st.block_size << " "
              << st.total_blocks
              << std::endl;

    uint32_t count = 0;
    uint32_t blocks[MYFS_FREE_GROUP_SIZE];
    if (myfs_phys_get_free_group(&count, blocks, MYFS_FREE_GROUP_SIZE) == MYFS_OK) {
        std::cout << "[SYNC] GROUP_ITEMS ";
        for (uint32_t i = 0; i < count; i++) {
            std::cout << blocks[i] << " ";
        }
        std::cout << std::endl;
    }
    
    uint32_t lru_blocks[128];
    uint32_t lru_count = 0;
    if (myfs_phys_cache_get_lru_list(lru_blocks, 128, &lru_count) == MYFS_OK) {
        std::cout << "[SYNC] CACHE_LRU " << lru_count << " ";
        for (uint32_t i = 0; i < lru_count; i++) {
            std::cout << lru_blocks[i] << " ";
        }
        std::cout << std::endl;
    }
    
    fflush(stdout);
}

static void vfs_sync_cache_lru(void) {
    if (myfs_phys_is_mounted() != 1) {
        return;
    }

    uint32_t lru_blocks[128];
    uint32_t lru_count = 0;
    if (myfs_phys_cache_get_lru_list(lru_blocks, 128, &lru_count) == MYFS_OK) {
        std::cout << "[SYNC] CACHE_LRU " << lru_count << " ";
        for (uint32_t i = 0; i < lru_count; i++) {
            std::cout << lru_blocks[i] << " ";
        }
        std::cout << std::endl;
    }

    fflush(stdout);
}

static void vfs_sync_used_blocks(myfs_block_t start_block, uint32_t count) {
    if (myfs_phys_is_mounted() != 1) {
        return;
    }
    if (count == 0) {
        std::cout << "[SYNC] USED_BLOCKS " << start_block << " " << count << " 0" << std::endl;
        fflush(stdout);
        return;
    }

    // Flush dirty cache blocks to disk before reading free-group chain,
    // otherwise myfs_disk_read_block in the used-blocks query would
    // bypass the cache and see stale data on disk.
    myfs_phys_sync();

    std::vector<uint32_t> used(count);
    uint32_t actual = 0;
    int ret = myfs_phys_get_used_blocks_in_range(start_block, count, used.data(), count, &actual);
    if (ret != MYFS_OK) {
        return;
    }

    std::cout << "[SYNC] USED_BLOCKS " << start_block << " " << count << " " << actual;
    for (uint32_t i = 0; i < actual; i++) {
        std::cout << " " << used[i];
    }
    std::cout << std::endl;
    fflush(stdout);
}

static int ensure_mounted(void) {
    if (myfs_phys_is_mounted() != 1) {
        std::cout << "[ERR] file system not mounted" << std::endl;
        return -1;
    }
    return 0;
}

static void vfs_init_hash(void) {
    for (int i = 0; i < NHINO; i++) {
        hinode[i].i_forw = nullptr;
    }
}

static void vfs_init_users(void) {
    pwd_count = 0;
    memset(pwd_table, 0, sizeof(pwd_table));
    memset(pwd_name, 0, sizeof(pwd_name));
    users_store_inode = 0;
    users_store_ready = 0;

    struct {
        const char *name;
        const char *pass;
        unsigned short uid;
        unsigned short gid;
    } defaults[] = {
            {"root", "root", 0, 0},
            {"user", "user", 1000, 1000}
    };

    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        strncpy(pwd_name[pwd_count], defaults[i].name, PWDSIZ - 1);
        pwd_name[pwd_count][PWDSIZ - 1] = '\0';

        strncpy(pwd_table[pwd_count].password, defaults[i].pass, PWDSIZ - 1);
        pwd_table[pwd_count].password[PWDSIZ - 1] = '\0';

        pwd_table[pwd_count].p_uid = defaults[i].uid;
        pwd_table[pwd_count].p_gid = defaults[i].gid;
        pwd_count++;
    }

    for (int i = 0; i < USERNUM; i++) {
        user_table[i].used = 0;
        user_table[i].u_uid = 0;
        user_table[i].u_gid = 0;
        for (int j = 0; j < NOFILE; j++) {
            user_table[i].u_ofile[j] = VFS_INVALID_OFILE;
        }
    }

    for (int i = 0; i < SYSOPENFILE; i++) {
        sys_ofile[i].f_flag = 0;
        sys_ofile[i].f_count = 0;
        sys_ofile[i].f_inode = nullptr;
        sys_ofile[i].f_offset = 0;
    }
}

static void vfs_release_all_cached_inodes(void) {
    for (int i = 0; i < NHINO; i++) {
        inode *p = hinode[i].i_forw;
        while (p != nullptr) {
            inode *next = p->i_forw;
            delete p;
            p = next;
        }
        hinode[i].i_forw = nullptr;
    }
}

static void vfs_reset_for_new_filesystem(void) {
    if (cur_path_inode != nullptr) {
        iput(cur_path_inode);
        cur_path_inode = nullptr;
    }

    for (int i = 0; i < SYSOPENFILE; i++) {
        if (sys_ofile[i].f_inode != nullptr) {
            iput(sys_ofile[i].f_inode);
            sys_ofile[i].f_inode = nullptr;
        }
        sys_ofile[i].f_flag = 0;
        sys_ofile[i].f_count = 0;
        sys_ofile[i].f_offset = 0;
    }

    vfs_release_all_cached_inodes();
    vfs_init_hash();
    vfs_init_users();
    current_user_id = -1;
    current_user_slot = -1;
    cur_path = "/";
}

static int vfs_users_ensure_store_inode(myfs_ino_t *out_inode) {
    if (out_inode == nullptr) {
        return -1;
    }
    if (ensure_mounted() != 0) {
        return -1;
    }

    if (users_store_ready && users_store_inode != 0) {
        *out_inode = users_store_inode;
        return 0;
    }

    struct inode *existing = namei(VFS_USERS_PATH);
    if (existing != nullptr) {
        users_store_inode = existing->i_num;
        users_store_ready = 1;
        iput(existing);
        *out_inode = users_store_inode;
        return 0;
    }

    myfs_ino_t root_ino = 0;
    if (myfs_phys_get_root_inode(&root_ino) != MYFS_OK) {
        return -1;
    }

    myfs_ino_t new_inode = 0;
    int ret = myfs_phys_inode_create(
            MYFS_PHYS_INODE_FILE,
            0600,
            0,
            0,
            &new_inode
    );
    if (ret != MYFS_OK) {
        return -1;
    }

    if (vfs_add_dirent(root_ino, new_inode, VFS_USERS_NAME) != 0) {
        myfs_phys_inode_free(new_inode);
        return -1;
    }

    users_store_inode = new_inode;
    users_store_ready = 1;
    *out_inode = new_inode;
    return 0;
}

static int vfs_users_save(void) {
    if (ensure_mounted() != 0) {
        return -1;
    }

    myfs_ino_t ino = 0;
    if (vfs_users_ensure_store_inode(&ino) != 0) {
        return -1;
    }

    vfs_users_header hdr;
    hdr.magic = VFS_USERS_MAGIC;
    hdr.version = VFS_USERS_VERSION;
    hdr.count = static_cast<uint32_t>(pwd_count);

    std::vector<unsigned char> buf(sizeof(hdr) + pwd_count * sizeof(vfs_users_entry));
    memcpy(buf.data(), &hdr, sizeof(hdr));

    for (int i = 0; i < pwd_count; i++) {
        vfs_users_entry ent;
        memset(&ent, 0, sizeof(ent));
        strncpy(ent.name, pwd_name[i], PWDSIZ - 1);
        strncpy(ent.pass, pwd_table[i].password, PWDSIZ - 1);
        ent.uid = pwd_table[i].p_uid;
        ent.gid = pwd_table[i].p_gid;
        memcpy(buf.data() + sizeof(hdr) + i * sizeof(vfs_users_entry), &ent, sizeof(ent));
    }

    if (myfs_phys_truncate(ino, 0) != MYFS_OK) {
        return -1;
    }

    uint32_t written = 0;
    if (myfs_phys_write(ino, 0, buf.data(), static_cast<uint32_t>(buf.size()), &written) != MYFS_OK) {
        return -1;
    }

    return 0;
}

static int vfs_users_load_or_init(void) {
    if (ensure_mounted() != 0) {
        return -1;
    }

    myfs_ino_t ino = 0;
    if (vfs_users_ensure_store_inode(&ino) != 0) {
        return -1;
    }

    /*
     * 尝试从磁盘文件加载用户列表。
     * 如果文件不存在、太小、格式错误或数据不足，
     * 则保留内存中的默认用户（root + user），不保存。
     */
    bool load_ok = false;
    {
        myfs_phys_inode_info_t info;
        if (myfs_phys_inode_get_info(ino, &info) != MYFS_OK ||
            info.size < sizeof(vfs_users_header)) {
            load_ok = false;
        } else {
            std::vector<unsigned char> raw(static_cast<size_t>(info.size));
            uint32_t bytes_read = 0;
            if (myfs_phys_read(ino, 0, raw.data(),
                               static_cast<uint32_t>(raw.size()),
                               &bytes_read) != MYFS_OK ||
                bytes_read < sizeof(vfs_users_header)) {
                load_ok = false;
            } else {
                vfs_users_header hdr;
                memcpy(&hdr, raw.data(), sizeof(hdr));
                if (hdr.magic != VFS_USERS_MAGIC ||
                    hdr.version != VFS_USERS_VERSION) {
                    load_ok = false;
                } else {
                    uint32_t count = hdr.count;
                    if (count > PWDNUM) {
                        count = PWDNUM;
                    }
                    size_t need = sizeof(hdr) + count * sizeof(vfs_users_entry);
                    if (raw.size() < need) {
                        load_ok = false;
                    } else {
                        /* 加载成功，开始合并到内存表 */
                        std::vector<vfs_users_entry> entries(count);
                        memcpy(entries.data(), raw.data() + sizeof(hdr),
                               count * sizeof(vfs_users_entry));

                        memset(pwd_table, 0, sizeof(pwd_table));
                        memset(pwd_name, 0, sizeof(pwd_name));
                        pwd_count = 0;

                        auto uid_exists = [&](unsigned short uid) -> bool {
                            for (int i = 0; i < pwd_count; i++) {
                                if (pwd_table[i].p_uid == uid) return true;
                            }
                            return false;
                        };

                        /* 先检查文件中是否有 root */
                        bool has_root = false;
                        for (uint32_t i = 0; i < count; i++) {
                            if (entries[i].name[0] == '\0') continue;
                            if (strncmp(entries[i].name, "root", PWDSIZ) == 0) {
                                has_root = true;
                            }
                        }

                        /* 如果文件中没有 root，添加默认 root */
                        if (!has_root) {
                            strncpy(pwd_name[pwd_count], "root", PWDSIZ - 1);
                            pwd_name[pwd_count][PWDSIZ - 1] = '\0';
                            strncpy(pwd_table[pwd_count].password, "root",
                                    PWDSIZ - 1);
                            pwd_table[pwd_count].password[PWDSIZ - 1] = '\0';
                            pwd_table[pwd_count].p_uid = 0;
                            pwd_table[pwd_count].p_gid = 0;
                            pwd_count++;
                        }

                        /* 遍历文件条目，添加到内存表 */
                        for (uint32_t i = 0;
                             i < count && pwd_count < PWDNUM; i++) {
                            if (entries[i].name[0] == '\0') continue;

                            if (strncmp(entries[i].name, "root", PWDSIZ) == 0) {
                                /*
                                 * 文件中的 root 条目。
                                 * 只有在内存表中还没有 uid=0 的用户时才添加。
                                 */
                                bool uid0_exists = false;
                                for (int j = 0; j < pwd_count; j++) {
                                    if (pwd_table[j].p_uid == 0) {
                                        uid0_exists = true;
                                        break;
                                    }
                                }
                                if (uid0_exists) continue;
                                strncpy(pwd_name[pwd_count], entries[i].name,
                                        PWDSIZ - 1);
                                pwd_name[pwd_count][PWDSIZ - 1] = '\0';
                                strncpy(pwd_table[pwd_count].password,
                                        entries[i].pass, PWDSIZ - 1);
                                pwd_table[pwd_count].password[PWDSIZ - 1] =
                                    '\0';
                                pwd_table[pwd_count].p_uid = entries[i].uid;
                                pwd_table[pwd_count].p_gid = entries[i].gid;
                                pwd_count++;
                                continue;
                            }

                            if (uid_exists(entries[i].uid)) continue;

                            bool name_dup = false;
                            for (int j = 0; j < pwd_count; j++) {
                                if (strncmp(entries[i].name, pwd_name[j],
                                            PWDSIZ) == 0) {
                                    name_dup = true;
                                    break;
                                }
                            }
                            if (name_dup) continue;

                            strncpy(pwd_name[pwd_count], entries[i].name,
                                    PWDSIZ - 1);
                            pwd_name[pwd_count][PWDSIZ - 1] = '\0';
                            strncpy(pwd_table[pwd_count].password,
                                    entries[i].pass, PWDSIZ - 1);
                            pwd_table[pwd_count].password[PWDSIZ - 1] = '\0';
                            pwd_table[pwd_count].p_uid = entries[i].uid;
                            pwd_table[pwd_count].p_gid = entries[i].gid;
                            pwd_count++;
                        }

                        vfs_users_save();
                        load_ok = true;
                    }
                }
            }
        }
    }

    /*
     * 如果加载失败，保留内存中的默认用户并保存到磁盘。
     * 这样 .users 文件会被正确初始化，后续挂载时可以直接加载。
     */
    if (!load_ok) {
        /* pwd_table 仍然持有 vfs_init_users() 设置的默认 root + user */
        vfs_users_save();
    }

    return 0;
}

static unsigned int vfs_hash_ino(unsigned int ino) {
    return ino % NHINO;
}

static int vfs_load_inode(unsigned int ino, struct inode *out) {
    if (out == nullptr) {
        return -1;
    }

    myfs_phys_inode_info_t info;
    int ret = myfs_phys_inode_get_info(ino, &info);
    if (ret != MYFS_OK) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    unsigned short type_bits = (info.type == MYFS_PHYS_INODE_DIR) ? DIDIR : DIFILE;
    out->i_mode = static_cast<unsigned short>(type_bits | (info.mode & 0777));
    out->i_uid = info.uid;
    out->i_gid = info.gid;
    out->i_nlink = info.link_count;
    out->i_size = info.size;
    out->i_num = info.inode_id;
    out->i_flag = 0;
    out->i_count = 1;

    return 0;
}

struct inode *iget(unsigned short ino) {
    unsigned int hash = vfs_hash_ino(ino);
    struct inode *p = hinode[hash].i_forw;

    while (p != nullptr) {
        if (p->i_num == ino) {
            p->i_count++;
            return p;
        }
        p = p->i_forw;
    }

    struct inode *node = new (std::nothrow) inode();
    if (node == nullptr) {
        return nullptr;
    }

    if (vfs_load_inode(ino, node) != 0) {
        delete node;
        return nullptr;
    }

    node->i_forw = hinode[hash].i_forw;
    node->i_back = nullptr;
    if (hinode[hash].i_forw != nullptr) {
        hinode[hash].i_forw->i_back = node;
    }
    hinode[hash].i_forw = node;

    return node;
}

static void vfs_unlink_inode_from_hash(struct inode *p) {
    if (p == nullptr) {
        return;
    }

    unsigned int hash = vfs_hash_ino(p->i_num);

    if (p->i_back != nullptr) {
        p->i_back->i_forw = p->i_forw;
    } else {
        hinode[hash].i_forw = p->i_forw;
    }

    if (p->i_forw != nullptr) {
        p->i_forw->i_back = p->i_back;
    }

    p->i_forw = nullptr;
    p->i_back = nullptr;
}

void iput(struct inode *p) {
    if (p == nullptr) {
        return;
    }

    if (p->i_count > 0) {
        p->i_count--;
    }

    if (p->i_count > 0) {
        return;
    }

    if (p->i_flag & IUPDATE) {
        myfs_phys_inode_set_attr(
                p->i_num,
                static_cast<unsigned short>(p->i_mode & 0777),
                p->i_uid,
                p->i_gid
        );
    }

    vfs_unlink_inode_from_hash(p);
    delete p;
}

static std::string vfs_trim(const std::string &s) {
    size_t start = 0;
    // Skip UTF-8 BOM if present
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        start += 3;
    }
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }
    return s.substr(start, end - start);
}

static std::vector<std::string> vfs_split_path(const std::string &path) {
    std::vector<std::string> parts;
    std::string temp;
    std::stringstream ss(path);

    while (std::getline(ss, temp, '/')) {
        if (!temp.empty()) {
            parts.push_back(temp);
        }
    }

    return parts;
}

static std::string vfs_normalize_path(const std::string &path) {
    std::vector<std::string> stack;

    if (path.empty()) {
        return cur_path;
    }

    if (path[0] != '/') {
        std::vector<std::string> base = vfs_split_path(cur_path);
        stack.insert(stack.end(), base.begin(), base.end());
    }

    std::vector<std::string> parts = vfs_split_path(path);
    for (const auto &part : parts) {
        if (part == "." || part.empty()) {
            continue;
        }
        if (part == "..") {
            if (!stack.empty()) {
                stack.pop_back();
            }
            continue;
        }
        stack.push_back(part);
    }

    std::string result = "/";
    for (size_t i = 0; i < stack.size(); i++) {
        result += stack[i];
        if (i + 1 < stack.size()) {
            result += "/";
        }
    }

    return result;
}

static std::string vfs_join_path(const std::string &base, const std::string &name) {
    if (base.empty()) {
        return name;
    }
    if (base == "/") {
        return "/" + name;
    }
    if (base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

static int vfs_dirent_name_equals(const char *entry_name, const std::string &name) {
    char temp[DIRSIZ + 1];
    memset(temp, 0, sizeof(temp));
    strncpy(temp, entry_name, DIRSIZ);
    temp[DIRSIZ] = '\0';
    return name == temp;
}

static bool vfs_dirent_is_free(const vfs_dirent &entry) {
    return entry.d_ino == 0 && entry.d_name[0] == '\0';
}

static int vfs_read_dir_block(myfs_ino_t dir_inode,
                              uint64_t offset,
                              vfs_dirent *entries,
                              uint32_t entries_per_block) {
    unsigned char buf[MYFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    uint32_t bytes_read = 0;
    int ret = myfs_phys_read(dir_inode, offset, buf, MYFS_BLOCK_SIZE, &bytes_read);
    if (ret != MYFS_OK) {
        return -1;
    }

    if (bytes_read < MYFS_BLOCK_SIZE) {
        memset(buf + bytes_read, 0, MYFS_BLOCK_SIZE - bytes_read);
    }

    memcpy(entries, buf, entries_per_block * sizeof(vfs_dirent));
    return 0;
}

static int vfs_write_dir_block(myfs_ino_t dir_inode,
                               uint64_t offset,
                               const vfs_dirent *entries,
                               uint32_t entries_per_block) {
    unsigned char buf[MYFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, entries, entries_per_block * sizeof(vfs_dirent));

    uint32_t bytes_written = 0;
    int ret = myfs_phys_write(dir_inode, offset, buf, MYFS_BLOCK_SIZE, &bytes_written);
    if (ret != MYFS_OK || bytes_written != MYFS_BLOCK_SIZE) {
        return -1;
    }

    /*
     * 关键：目录项是文件系统结构的一部分，必须立即刷盘。
     * 如果走 write-back 缓存，crash 时目录项会丢失，
     * 导致新建的文件在重启后"消失"。
     */
    myfs_phys_sync();

    return 0;
}

static int vfs_find_dirent(myfs_ino_t dir_inode,
                           const std::string &name,
                           vfs_dirent *out_entry,
                           uint64_t *out_offset) {
    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(dir_inode, &info) != MYFS_OK) {
        return -1;
    }

    uint32_t entries_per_block = MYFS_BLOCK_SIZE / sizeof(vfs_dirent);
    std::vector<vfs_dirent> entries(entries_per_block);

    for (uint64_t offset = 0; offset < info.size; offset += MYFS_BLOCK_SIZE) {
        if (vfs_read_dir_block(dir_inode, offset, entries.data(), entries_per_block) != 0) {
            return -1;
        }

        for (uint32_t i = 0; i < entries_per_block; i++) {
            if (vfs_dirent_is_free(entries[i])) {
                continue;
            }

            if (vfs_dirent_name_equals(entries[i].d_name, name)) {
                if (out_entry != nullptr) {
                    *out_entry = entries[i];
                }
                if (out_offset != nullptr) {
                    *out_offset = offset + i * sizeof(vfs_dirent);
                }
                return 0;
            }
        }
    }

    return -1;
}

static int vfs_add_dirent(myfs_ino_t dir_inode, myfs_ino_t child_ino, const std::string &name) {
    if (child_ino > 0xFFFF) {
        return -1;
    }

    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(dir_inode, &info) != MYFS_OK) {
        return -1;
    }

    uint32_t entries_per_block = MYFS_BLOCK_SIZE / sizeof(vfs_dirent);
    std::vector<vfs_dirent> entries(entries_per_block);

    for (uint64_t offset = 0; offset < info.size; offset += MYFS_BLOCK_SIZE) {
        if (vfs_read_dir_block(dir_inode, offset, entries.data(), entries_per_block) != 0) {
            return -1;
        }

        for (uint32_t i = 0; i < entries_per_block; i++) {
            if (vfs_dirent_is_free(entries[i])) {
                entries[i].d_ino = static_cast<unsigned short>(child_ino);
                memset(entries[i].d_name, 0, DIRSIZ);
                strncpy(entries[i].d_name, name.c_str(), DIRSIZ - 1);
                entries[i].d_name[DIRSIZ - 1] = '\0';

                return vfs_write_dir_block(dir_inode, offset, entries.data(), entries_per_block);
            }
        }
    }

    std::vector<vfs_dirent> new_entries(entries_per_block);
    memset(new_entries.data(), 0, entries_per_block * sizeof(vfs_dirent));

    new_entries[0].d_ino = static_cast<unsigned short>(child_ino);
    strncpy(new_entries[0].d_name, name.c_str(), DIRSIZ - 1);
    new_entries[0].d_name[DIRSIZ - 1] = '\0';

    uint64_t new_offset = (info.size + MYFS_BLOCK_SIZE - 1) / MYFS_BLOCK_SIZE * MYFS_BLOCK_SIZE;
    return vfs_write_dir_block(dir_inode, new_offset, new_entries.data(), entries_per_block);
}

static int vfs_remove_dirent(myfs_ino_t dir_inode, const std::string &name) {
    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(dir_inode, &info) != MYFS_OK) {
        return -1;
    }

    uint32_t entries_per_block = MYFS_BLOCK_SIZE / sizeof(vfs_dirent);
    std::vector<vfs_dirent> entries(entries_per_block);

    for (uint64_t offset = 0; offset < info.size; offset += MYFS_BLOCK_SIZE) {
        if (vfs_read_dir_block(dir_inode, offset, entries.data(), entries_per_block) != 0) {
            return -1;
        }

        for (uint32_t i = 0; i < entries_per_block; i++) {
            if (vfs_dirent_is_free(entries[i])) {
                continue;
            }

            if (vfs_dirent_name_equals(entries[i].d_name, name)) {
                entries[i].d_ino = 0;
                memset(entries[i].d_name, 0, DIRSIZ);
                return vfs_write_dir_block(dir_inode, offset, entries.data(), entries_per_block);
            }
        }
    }

    return -1;
}

static int vfs_is_dir_empty(myfs_ino_t dir_inode) {
    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(dir_inode, &info) != MYFS_OK) {
        return -1;
    }

    uint32_t entries_per_block = MYFS_BLOCK_SIZE / sizeof(vfs_dirent);
    std::vector<vfs_dirent> entries(entries_per_block);

    for (uint64_t offset = 0; offset < info.size; offset += MYFS_BLOCK_SIZE) {
        if (vfs_read_dir_block(dir_inode, offset, entries.data(), entries_per_block) != 0) {
            return -1;
        }

        for (uint32_t i = 0; i < entries_per_block; i++) {
            if (vfs_dirent_is_free(entries[i])) {
                continue;
            }

            char temp[DIRSIZ + 1];
            memset(temp, 0, sizeof(temp));
            strncpy(temp, entries[i].d_name, DIRSIZ);

            if (strcmp(temp, ".") != 0 && strcmp(temp, "..") != 0) {
                return 0;
            }
        }
    }

    return 1;
}

static int vfs_init_dir(myfs_ino_t dir_inode, myfs_ino_t parent_inode) {
    uint32_t entries_per_block = MYFS_BLOCK_SIZE / sizeof(vfs_dirent);
    std::vector<vfs_dirent> entries(entries_per_block);
    memset(entries.data(), 0, entries_per_block * sizeof(vfs_dirent));

    entries[0].d_ino = static_cast<unsigned short>(dir_inode);
    strncpy(entries[0].d_name, ".", DIRSIZ - 1);
    entries[0].d_name[DIRSIZ - 1] = '\0';

    entries[1].d_ino = static_cast<unsigned short>(parent_inode);
    strncpy(entries[1].d_name, "..", DIRSIZ - 1);
    entries[1].d_name[DIRSIZ - 1] = '\0';

    return vfs_write_dir_block(dir_inode, 0, entries.data(), entries_per_block);
}

static int vfs_ensure_root_dir(void) {
    myfs_ino_t root_ino = 0;
    if (myfs_phys_get_root_inode(&root_ino) != MYFS_OK) {
        return -1;
    }

    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(root_ino, &info) != MYFS_OK) {
        return -1;
    }

    if (info.type != MYFS_PHYS_INODE_DIR) {
        return -1;
    }

    if (info.size == 0) {
        return vfs_init_dir(root_ino, root_ino);
    }

    vfs_dirent entry;
    if (vfs_find_dirent(root_ino, ".", &entry, nullptr) != 0) {
        vfs_add_dirent(root_ino, root_ino, ".");
    }
    if (vfs_find_dirent(root_ino, "..", &entry, nullptr) != 0) {
        vfs_add_dirent(root_ino, root_ino, "..");
    }

    return 0;
}

static int vfs_split_parent_child(const std::string &path,
                                  std::string &parent,
                                  std::string &name) {
    if (path == "/") {
        return -1;
    }

    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return -1;
    }

    parent = (pos == 0) ? "/" : path.substr(0, pos);
    name = path.substr(pos + 1);

    if (name.empty()) {
        return -1;
    }

    return 0;
}

struct inode *namei(const char *path) {
    if (path == nullptr) {
        return nullptr;
    }

    if (ensure_mounted() != 0) {
        return nullptr;
    }

    std::string abs_path = vfs_normalize_path(path);
    if (abs_path == "/") {
        myfs_ino_t root_ino = 0;
        if (myfs_phys_get_root_inode(&root_ino) != MYFS_OK) {
            return nullptr;
        }
        return iget(static_cast<unsigned short>(root_ino));
    }

    std::vector<std::string> parts = vfs_split_path(abs_path);

    myfs_ino_t root_ino = 0;
    if (myfs_phys_get_root_inode(&root_ino) != MYFS_OK) {
        return nullptr;
    }

    struct inode *current = iget(static_cast<unsigned short>(root_ino));
    if (current == nullptr) {
        return nullptr;
    }

    for (size_t i = 0; i < parts.size(); i++) {
        vfs_dirent entry;
        if (vfs_find_dirent(current->i_num, parts[i], &entry, nullptr) != 0) {
            iput(current);
            return nullptr;
        }

        if (i + 1 < parts.size()) {
            myfs_phys_inode_info_t info;
            if (myfs_phys_inode_get_info(entry.d_ino, &info) != MYFS_OK ||
                info.type != MYFS_PHYS_INODE_DIR) {
                iput(current);
                return nullptr;
            }
        }

        iput(current);
        current = iget(entry.d_ino);
        if (current == nullptr) {
            return nullptr;
        }
    }

    return current;
}

int access(struct inode *ip, unsigned short mode) {
    if (ip == nullptr) {
        return -1;
    }

    if (current_user_id < 0 || current_user_slot < 0) {
        return -1;
    }

    unsigned short perm_bits = 0;
    if (ip->i_uid == static_cast<unsigned int>(current_user_id)) {
        perm_bits = static_cast<unsigned short>((ip->i_mode & 0700) >> 6);
    } else if (user_table[current_user_slot].u_gid == ip->i_gid) {
        perm_bits = static_cast<unsigned short>((ip->i_mode & 0070) >> 3);
    } else {
        perm_bits = static_cast<unsigned short>(ip->i_mode & 0007);
    }

    if ((mode & FREAD) && ((perm_bits & 0x4) == 0)) {
        return -1;
    }

    if ((mode & FWRITE) && ((perm_bits & 0x2) == 0)) {
        return -1;
    }

    return 0;
}

int sys_login(const char *username, const char *password) {
    if (username == nullptr || password == nullptr) {
        return -1;
    }

    if (ensure_mounted() != 0) {
        return -1;
    }

    if (current_user_id >= 0) {
        std::cout << "[ERR] already logged in" << std::endl;
        return -1;
    }

    int found = -1;
    for (int i = 0; i < pwd_count; i++) {
        if (strncmp(username, pwd_name[i], PWDSIZ) == 0 &&
            strncmp(password, pwd_table[i].password, PWDSIZ) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        std::cout << "[ERR] login failed" << std::endl;
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < USERNUM; i++) {
        if (!user_table[i].used) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        std::cout << "[ERR] no free user slot" << std::endl;
        return -1;
    }

    user_table[slot].used = 1;
    user_table[slot].u_uid = pwd_table[found].p_uid;
    user_table[slot].u_gid = pwd_table[found].p_gid;
    for (int i = 0; i < NOFILE; i++) {
        user_table[slot].u_ofile[i] = VFS_INVALID_OFILE;
    }

    current_user_id = pwd_table[found].p_uid;
    current_user_slot = slot;

    sync_print("[SYNC] LOGIN " + std::to_string(current_user_id));

    return 0;
}

int sys_logout() {
    if (current_user_id < 0 || current_user_slot < 0) {
        return -1;
    }

    for (int i = 0; i < NOFILE; i++) {
        if (user_table[current_user_slot].u_ofile[i] != VFS_INVALID_OFILE) {
            sys_close(i);
        }
    }

    int uid = current_user_id;

    user_table[current_user_slot].used = 0;
    current_user_id = -1;
    current_user_slot = -1;

    sync_print("[SYNC] LOGOUT " + std::to_string(uid));

    return 0;
}

int sys_mkdir(const char *path) {
    if (path == nullptr) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    std::string abs_path = vfs_normalize_path(path);
    std::string parent_path;
    std::string name;

    if (vfs_split_parent_child(abs_path, parent_path, name) != 0) {
        return -1;
    }

    struct inode *existing = namei(abs_path.c_str());
    if (existing != nullptr) {
        iput(existing);
        return -1;
    }

    struct inode *parent = namei(parent_path.c_str());
    if (parent == nullptr) {
        return -1;
    }

    myfs_phys_inode_info_t parent_info;
    if (myfs_phys_inode_get_info(parent->i_num, &parent_info) != MYFS_OK ||
        parent_info.type != MYFS_PHYS_INODE_DIR) {
        iput(parent);
        return -1;
    }

    if (access(parent, FWRITE) != 0) {
        iput(parent);
        return -1;
    }

    myfs_ino_t new_inode = 0;
    int ret = myfs_phys_inode_create(
            MYFS_PHYS_INODE_DIR,
            DEFAULTMODE,
            user_table[current_user_slot].u_uid,
            user_table[current_user_slot].u_gid,
            &new_inode
    );

    if (ret != MYFS_OK) {
        iput(parent);
        return -1;
    }

    if (vfs_init_dir(new_inode, parent->i_num) != 0) {
        iput(parent);
        return -1;
    }

    if (vfs_add_dirent(parent->i_num, new_inode, name) != 0) {
        iput(parent);
        return -1;
    }

    iput(parent);

    sync_print("[SYNC] TREE_UPDATE " + abs_path);

    return 0;
}

int sys_chdir(const char *path) {
    if (path == nullptr) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    struct inode *target = namei(path);
    if (target == nullptr) {
        return -1;
    }

    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(target->i_num, &info) != MYFS_OK ||
        info.type != MYFS_PHYS_INODE_DIR) {
        iput(target);
        return -1;
    }

    if (access(target, FREAD) != 0) {
        iput(target);
        return -1;
    }

    if (cur_path_inode != nullptr) {
        iput(cur_path_inode);
    }
    cur_path_inode = target;
    cur_path = vfs_normalize_path(path);

    sync_print("[SYNC] CHDIR " + cur_path);

    return 0;
}

void sys_dir() {
    if (ensure_mounted() != 0 || current_user_id < 0 || cur_path_inode == nullptr) {
        return;
    }

    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(cur_path_inode->i_num, &info) != MYFS_OK) {
        return;
    }

    uint32_t entries_per_block = MYFS_BLOCK_SIZE / sizeof(vfs_dirent);
    std::vector<vfs_dirent> entries(entries_per_block);

    for (uint64_t offset = 0; offset < info.size; offset += MYFS_BLOCK_SIZE) {
        if (vfs_read_dir_block(cur_path_inode->i_num, offset, entries.data(), entries_per_block) != 0) {
            return;
        }

        for (uint32_t i = 0; i < entries_per_block; i++) {
            if (vfs_dirent_is_free(entries[i])) {
                continue;
            }

            char name_buf[DIRSIZ + 1];
            memset(name_buf, 0, sizeof(name_buf));
            strncpy(name_buf, entries[i].d_name, DIRSIZ);

            myfs_phys_inode_info_t entry_info;
            if (myfs_phys_inode_get_info(entries[i].d_ino, &entry_info) != MYFS_OK) {
                continue;
            }

            std::string type = (entry_info.type == MYFS_PHYS_INODE_DIR) ? "<DIR>" : "<FILE>";

            std::cout << type << " " << entry_info.size << " " << name_buf << std::endl;
        }
    }
}

static const char *inode_type_str(uint16_t type) {
    switch (type) {
        case MYFS_PHYS_INODE_DIR: return "DIR";
        case MYFS_PHYS_INODE_FILE: return "FILE";
        case MYFS_PHYS_INODE_SYMLINK: return "SYMLINK";
        default: return "FREE";
    }
}

static void print_inode_header(const std::string &path,
                               const myfs_phys_inode_info_t &info) {
    std::cout << std::endl;
    std::cout << "========== Inode Info ==========" << std::endl;
    std::cout << "Path:     " << path << std::endl;
    std::cout << "Inode:    " << info.inode_id
              << " (" << inode_type_str(info.type)
              << ", mode=" << std::oct << info.mode << std::dec << ")"
              << std::endl;
    std::cout << "Size:     " << info.size << " bytes" << std::endl;
    std::cout << "Blocks:   " << info.block_count << std::endl;
    std::cout << "Uid/Gid:  " << info.uid << "/" << info.gid << std::endl;
    std::cout << "Links:    " << info.link_count
              << (info.type == MYFS_PHYS_INODE_DIR ? "  (normal: parent + . + subdirs' ..)" : "")
              << std::endl;
}

static void sys_dir_inode(const char *path) {
    if (ensure_mounted() != 0 || current_user_id < 0) {
        return;
    }

    struct inode *target_node = nullptr;
    std::string display_path;
    bool need_iput = false;

    if (path == nullptr || path[0] == '\0') {
        /* Use current directory */
        if (cur_path_inode == nullptr) {
            std::cout << "[ERR] no current directory" << std::endl;
            return;
        }
        target_node = cur_path_inode;
        display_path = cur_path;
    } else {
        std::string abs_path = vfs_normalize_path(path);
        target_node = namei(abs_path.c_str());
        if (target_node == nullptr) {
            std::cout << "[ERR] inode: path not found" << std::endl;
            return;
        }
        need_iput = true;
        display_path = abs_path;
    }

    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(target_node->i_num, &info) != MYFS_OK) {
        std::cout << "[ERR] inode: cannot get inode info" << std::endl;
        if (need_iput) iput(target_node);
        return;
    }

    if (info.type == MYFS_PHYS_INODE_DIR) {
        /* ---- Directory: show dir info + entry list ---- */
        print_inode_header(display_path, info);
        std::cout << "---------------------------------" << std::endl;

        uint32_t entries_per_block = MYFS_BLOCK_SIZE / sizeof(vfs_dirent);
        std::vector<vfs_dirent> entries(entries_per_block);
        int entry_count = 0;

        for (uint64_t offset = 0; offset < info.size; offset += MYFS_BLOCK_SIZE) {
            if (vfs_read_dir_block(target_node->i_num, offset, entries.data(), entries_per_block) != 0) {
                break;
            }

            for (uint32_t i = 0; i < entries_per_block; i++) {
                if (vfs_dirent_is_free(entries[i])) {
                    continue;
                }

                char name_buf[DIRSIZ + 1];
                memset(name_buf, 0, sizeof(name_buf));
                strncpy(name_buf, entries[i].d_name, DIRSIZ);

                myfs_phys_inode_info_t entry_info;
                if (myfs_phys_inode_get_info(entries[i].d_ino, &entry_info) != MYFS_OK) {
                    std::cout << "  " << name_buf
                              << "  -> inode " << entries[i].d_ino
                              << "  (error)" << std::endl;
                    entry_count++;
                    continue;
                }

                std::cout << "  " << name_buf
                          << "  -> inode " << entries[i].d_ino
                          << "  " << inode_type_str(entry_info.type)
                          << "  size=" << entry_info.size
                          << "  blocks=" << entry_info.block_count
                          << "  links=" << entry_info.link_count
                          << std::endl;
                entry_count++;
            }
        }

        std::cout << "---------------------------------" << std::endl;
        std::cout << "Total entries: " << entry_count << std::endl;
        std::cout << "=================================" << std::endl;
        std::cout << std::endl;

        /* Emit SYNC so the Python UI tree panel refreshes */
        sync_print("[SYNC] TREE_UPDATE " + display_path);

    } else {
        /* ---- File / Symlink: show detailed inode info ---- */
        print_inode_header(display_path, info);

        /* Also dump full inode details via the debug layer */
        myfs_phys_debug_inode(target_node->i_num);

        std::cout << std::endl;
    }

    if (need_iput) iput(target_node);
}

int sys_create(const char *path, unsigned short mode) {
    if (path == nullptr) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    std::string abs_path = vfs_normalize_path(path);
    std::string parent_path;
    std::string name;

    if (vfs_split_parent_child(abs_path, parent_path, name) != 0) {
        return -1;
    }

    struct inode *existing = namei(abs_path.c_str());
    if (existing != nullptr) {
        if (access(existing, FWRITE) != 0) {
            iput(existing);
            return -1;
        }
        myfs_phys_truncate(existing->i_num, 0);
        iput(existing);
        sync_print("[SYNC] TREE_UPDATE " + abs_path);
        return sys_open(abs_path.c_str(), FWRITE);
    }

    struct inode *parent = namei(parent_path.c_str());
    if (parent == nullptr) {
        return -1;
    }

    myfs_phys_inode_info_t parent_info;
    if (myfs_phys_inode_get_info(parent->i_num, &parent_info) != MYFS_OK ||
        parent_info.type != MYFS_PHYS_INODE_DIR) {
        iput(parent);
        return -1;
    }

    if (access(parent, FWRITE) != 0) {
        iput(parent);
        return -1;
    }

    myfs_ino_t new_inode = 0;
    int ret = myfs_phys_inode_create(
            MYFS_PHYS_INODE_FILE,
            static_cast<unsigned short>(mode & 0777),
            user_table[current_user_slot].u_uid,
            user_table[current_user_slot].u_gid,
            &new_inode
    );

    if (ret != MYFS_OK) {
        iput(parent);
        return -1;
    }

    if (vfs_add_dirent(parent->i_num, new_inode, name) != 0) {
        iput(parent);
        return -1;
    }

    iput(parent);

    sync_print("[SYNC] TREE_UPDATE " + abs_path);

    return sys_open(abs_path.c_str(), FWRITE);
}

int sys_open(const char *path, int mode) {
    if (path == nullptr) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    if (mode == 0) {
        return -1;
    }

    struct inode *node = namei(path);
    if (node == nullptr) {
        return -1;
    }

    if (access(node, static_cast<unsigned short>(mode)) != 0) {
        iput(node);
        return -1;
    }

    int sys_index = -1;
    for (int i = 0; i < SYSOPENFILE; i++) {
        if (sys_ofile[i].f_count == 0) {
            sys_index = i;
            break;
        }
    }

    if (sys_index < 0) {
        iput(node);
        return -1;
    }

    int fd = -1;
    for (int i = 0; i < NOFILE; i++) {
        if (user_table[current_user_slot].u_ofile[i] == VFS_INVALID_OFILE) {
            fd = i;
            break;
        }
    }

    if (fd < 0) {
        iput(node);
        return -1;
    }

    sys_ofile[sys_index].f_flag = static_cast<char>(mode);
    sys_ofile[sys_index].f_count = 1;
    sys_ofile[sys_index].f_inode = node;
    sys_ofile[sys_index].f_offset = 0;

    if (mode & FAPPEND) {
        myfs_phys_inode_info_t info;
        if (myfs_phys_inode_get_info(node->i_num, &info) == MYFS_OK) {
            sys_ofile[sys_index].f_offset = info.size;
        }
    }

    user_table[current_user_slot].u_ofile[fd] = static_cast<unsigned short>(sys_index);

    myfs_phys_inode_inc_open(node->i_num);

    return fd;
}

int sys_close(int fd) {
    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    if (fd < 0 || fd >= NOFILE) {
        return -1;
    }

    unsigned short sys_index = user_table[current_user_slot].u_ofile[fd];
    if (sys_index == VFS_INVALID_OFILE || sys_index >= SYSOPENFILE) {
        return -1;
    }

    struct file *fp = &sys_ofile[sys_index];

    if (fp->f_count > 0) {
        fp->f_count--;
    }

    if (fp->f_count == 0) {
        if (fp->f_inode != nullptr) {
            myfs_phys_inode_dec_open(fp->f_inode->i_num);
            
            myfs_phys_inode_info_t info;
            if (myfs_phys_inode_get_info(fp->f_inode->i_num, &info) == MYFS_OK) {
                if (info.link_count == 0 && info.open_count == 0) {
                    myfs_phys_truncate(fp->f_inode->i_num, 0);
                    std::cout << "[OK] file fully closed and deleted (link_count=0, open_count=0)" << std::endl;
                }
            }
            
            iput(fp->f_inode);
            fp->f_inode = nullptr;
        }
        fp->f_flag = 0;
        fp->f_offset = 0;
    }

    user_table[current_user_slot].u_ofile[fd] = VFS_INVALID_OFILE;

    return 0;
}

int sys_read(int fd, char *buf, int size) {
    if (buf == nullptr || size < 0) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    if (fd < 0 || fd >= NOFILE) {
        return -1;
    }

    unsigned short sys_index = user_table[current_user_slot].u_ofile[fd];
    if (sys_index == VFS_INVALID_OFILE || sys_index >= SYSOPENFILE) {
        return -1;
    }

    struct file *fp = &sys_ofile[sys_index];
    if ((fp->f_flag & FREAD) == 0) {
        return -1;
    }

    uint32_t bytes_read = 0;
    int ret = myfs_phys_read(
            fp->f_inode->i_num,
            fp->f_offset,
            buf,
            static_cast<uint32_t>(size),
            &bytes_read
    );

    if (ret != MYFS_OK) {
        return -1;
    }

    fp->f_offset += bytes_read;

    return static_cast<int>(bytes_read);
}

int sys_write(int fd, const char *buf, int size) {
    if (buf == nullptr || size < 0) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    if (fd < 0 || fd >= NOFILE) {
        return -1;
    }

    unsigned short sys_index = user_table[current_user_slot].u_ofile[fd];
    if (sys_index == VFS_INVALID_OFILE || sys_index >= SYSOPENFILE) {
        return -1;
    }

    struct file *fp = &sys_ofile[sys_index];
    if ((fp->f_flag & FWRITE) == 0) {
        return -1;
    }

    uint32_t bytes_written = 0;
    int ret = myfs_phys_write(
            fp->f_inode->i_num,
            fp->f_offset,
            buf,
            static_cast<uint32_t>(size),
            &bytes_written
    );

    if (ret != MYFS_OK) {
        return -1;
    }

    fp->f_offset += bytes_written;

    return static_cast<int>(bytes_written);
}

int sys_delete(const char *path) {
    if (path == nullptr) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    std::string abs_path = vfs_normalize_path(path);
    std::string parent_path;
    std::string name;

    if (vfs_split_parent_child(abs_path, parent_path, name) != 0) {
        return -1;
    }

    struct inode *target = namei(abs_path.c_str());
    if (target == nullptr) {
        return -1;
    }

    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(target->i_num, &info) != MYFS_OK) {
        iput(target);
        return -1;
    }

    if (info.type == MYFS_PHYS_INODE_DIR) {
        iput(target);
        return -1;
    }

    if (access(target, FWRITE) != 0) {
        iput(target);
        return -1;
    }

    struct inode *parent = namei(parent_path.c_str());
    if (parent == nullptr) {
        iput(target);
        return -1;
    }

    if (access(parent, FWRITE) != 0) {
        iput(parent);
        iput(target);
        return -1;
    }

    if (vfs_remove_dirent(parent->i_num, name) != 0) {
        iput(parent);
        iput(target);
        return -1;
    }

    myfs_phys_inode_dec_link(target->i_num);

    myfs_phys_inode_info_t info_after;
    if (myfs_phys_inode_get_info(target->i_num, &info_after) == MYFS_OK) {
        if (info_after.link_count == 0 && info_after.open_count == 0) {
            myfs_phys_truncate(target->i_num, 0);
            std::cout << "[OK] file deleted (link_count=0, open_count=0)" << std::endl;
        } else if (info_after.link_count == 0) {
            std::cout << "[OK] link removed, file data kept (open_count=" << info_after.open_count << ")" << std::endl;
        } else {
            std::cout << "[OK] link removed (link_count=" << info_after.link_count << ")" << std::endl;
        }
    }

    iput(parent);
    iput(target);

    sync_print("[SYNC] TREE_UPDATE " + abs_path);

    return 0;
}

int sys_link(const char *oldpath, const char *newpath) {
    if (oldpath == nullptr || newpath == nullptr) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    std::string abs_oldpath = vfs_normalize_path(oldpath);
    std::string abs_newpath = vfs_normalize_path(newpath);

    struct inode *old_node = namei(abs_oldpath.c_str());
    if (old_node == nullptr) {
        return -1;
    }

    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(old_node->i_num, &info) != MYFS_OK) {
        iput(old_node);
        return -1;
    }

    if (info.type == MYFS_PHYS_INODE_DIR) {
        std::cout << "[ERR] cannot link to directory" << std::endl;
        iput(old_node);
        return -1;
    }

    std::string new_parent_path;
    std::string new_name;
    if (vfs_split_parent_child(abs_newpath, new_parent_path, new_name) != 0) {
        iput(old_node);
        return -1;
    }

    struct inode *parent = namei(new_parent_path.c_str());
    if (parent == nullptr) {
        iput(old_node);
        return -1;
    }

    if (access(parent, FWRITE) != 0) {
        iput(parent);
        iput(old_node);
        return -1;
    }

    /* Unix semantics: link() fails with EEXIST if target already exists */
    struct inode *existing = namei(abs_newpath.c_str());
    if (existing != nullptr) {
        std::cout << "[ERR] link: target already exists: " << abs_newpath << std::endl;
        iput(existing);
        iput(parent);
        iput(old_node);
        return -1;
    }

    if (vfs_add_dirent(parent->i_num, old_node->i_num, new_name) != 0) {
        iput(parent);
        iput(old_node);
        return -1;
    }

    myfs_phys_inode_inc_link(old_node->i_num);

    iput(parent);
    iput(old_node);

    sync_print("[SYNC] TREE_UPDATE " + abs_newpath);

    std::cout << "[OK] link success: " << abs_oldpath << " -> " << abs_newpath << std::endl;

    return 0;
}

static int sys_rmdir(const char *path) {
    if (path == nullptr) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    std::string abs_path = vfs_normalize_path(path);
    if (abs_path == "/") {
        return -1;
    }

    std::string parent_path;
    std::string name;
    if (vfs_split_parent_child(abs_path, parent_path, name) != 0) {
        return -1;
    }

    struct inode *target = namei(abs_path.c_str());
    if (target == nullptr) {
        return -1;
    }

    myfs_phys_inode_info_t info;
    if (myfs_phys_inode_get_info(target->i_num, &info) != MYFS_OK ||
        info.type != MYFS_PHYS_INODE_DIR) {
        iput(target);
        return -1;
    }

    int empty = vfs_is_dir_empty(target->i_num);
    if (empty != 1) {
        iput(target);
        return -1;
    }

    struct inode *parent = namei(parent_path.c_str());
    if (parent == nullptr) {
        iput(target);
        return -1;
    }

    if (access(parent, FWRITE) != 0) {
        iput(parent);
        iput(target);
        return -1;
    }

    if (vfs_remove_dirent(parent->i_num, name) != 0) {
        iput(parent);
        iput(target);
        return -1;
    }

    myfs_phys_truncate(target->i_num, 0);
    myfs_phys_inode_dec_link(target->i_num);

    iput(parent);
    iput(target);

    sync_print("[SYNC] TREE_UPDATE " + abs_path);

    return 0;
}

static int vfs_mount_default_disk(void) {
    if (myfs_phys_is_mounted() == 1) {
        if (vfs_ensure_root_dir() != 0) {
            return -1;
        }
    } else {
        int ret = myfs_phys_mount("disk.img");
        if (ret != MYFS_OK) {
            std::cout << "[ERR] mount failed: " << myfs_strerror(ret) << std::endl;
            return -1;
        }

        // Initialize cache after successful mount
        myfs_phys_cache_init(128); // e.g. 128 blocks capacity

        // Reset stale open_count from previous crashed session
        myfs_inode_reset_open_counts();

        if (vfs_ensure_root_dir() != 0) {
            std::cout << "[ERR] root directory init failed" << std::endl;
            return -1;
        }
    }

    if (cur_path_inode != nullptr) {
        iput(cur_path_inode);
    }

    myfs_ino_t root_ino = 0;
    if (myfs_phys_get_root_inode(&root_ino) == MYFS_OK) {
        cur_path_inode = iget(static_cast<unsigned short>(root_ino));
    }

    cur_path = "/";
    vfs_users_load_or_init();
    vfs_sync_super_info();
    return 0;
}

static int vfs_format_default_disk(void) {
    const uint32_t blocks = 32768;
    const uint32_t inodes = MYFS_DEFAULT_TOTAL_INODES;

    if (myfs_phys_is_mounted() == 1) {
        myfs_phys_umount();
    }

    vfs_reset_for_new_filesystem();

    int ret = myfs_phys_format("disk.img", blocks, inodes);
    if (ret != MYFS_OK) {
        std::cout << "[ERR] format failed: " << myfs_strerror(ret) << std::endl;
        return -1;
    }

    return vfs_mount_default_disk();
}

static void vfs_cmd_write(int fd, int size) {
    if (size <= 0) {
        return;
    }

    std::vector<char> buf(size);
    for (int i = 0; i < size; i++) {
        buf[i] = static_cast<char>('A' + (i % 26));
    }

    int written = sys_write(fd, buf.data(), size);
    if (written < 0) {
        std::cout << "[ERR] write failed" << std::endl;
    } else {
        std::cout << "[OK] wrote " << written << " bytes" << std::endl;
    }
}

static void vfs_cmd_write_text(int fd, const std::string &text) {
    if (text.empty()) {
        return;
    }

    int written = sys_write(fd, text.c_str(), static_cast<int>(text.size()));
    if (written < 0) {
        std::cout << "[ERR] write failed" << std::endl;
    } else {
        std::cout << "[OK] wrote " << written << " bytes" << std::endl;
    }
}

static void vfs_batch_mkdir(const std::string &base, const std::string &prefix, int count) {
    if (ensure_mounted() != 0 || current_user_id < 0) {
        std::cout << "[ERR] not mounted or not logged in" << std::endl;
        return;
    }
    if (count <= 0) {
        return;
    }

    std::string base_path = vfs_normalize_path(base);

    for (int i = 0; i < count; i++) {
        std::string name = prefix + std::to_string(i);
        std::string path = vfs_join_path(base_path, name);
        if (sys_mkdir(path.c_str()) != 0) {
            std::cout << "[ERR] batch mkdir failed: " << path << std::endl;
        }
    }
}

static void vfs_batch_create(const std::string &base, const std::string &prefix, int count, int size) {
    if (ensure_mounted() != 0 || current_user_id < 0) {
        std::cout << "[ERR] not mounted or not logged in" << std::endl;
        return;
    }
    if (count <= 0) {
        return;
    }

    std::string base_path = vfs_normalize_path(base);

    for (int i = 0; i < count; i++) {
        std::string name = prefix + std::to_string(i);
        std::string path = vfs_join_path(base_path, name);
        int fd = sys_create(path.c_str(), DEFAULTMODE);
        if (fd < 0) {
            std::cout << "[ERR] batch create failed: " << path << std::endl;
            continue;
        }
        if (size > 0) {
            vfs_cmd_write(fd, size);
        }
        if (sys_close(fd) != 0) {
            std::cout << "[ERR] batch close failed: " << path << std::endl;
        }
    }
}

static void vfs_batch_delete(const std::string &base, const std::string &prefix, int count) {
    if (ensure_mounted() != 0 || current_user_id < 0) {
        std::cout << "[ERR] not mounted or not logged in" << std::endl;
        return;
    }
    if (count <= 0) {
        return;
    }

    std::string base_path = vfs_normalize_path(base);

    for (int i = 0; i < count; i++) {
        std::string name = prefix + std::to_string(i);
        std::string path = vfs_join_path(base_path, name);
        if (sys_delete(path.c_str()) != 0) {
            std::cout << "[ERR] batch delete failed: " << path << std::endl;
        }
    }
}

static void vfs_cmd_read(int fd, int size) {
    if (size <= 0) {
        return;
    }

    std::vector<char> buf(size + 1);
    memset(buf.data(), 0, buf.size());

    int read_bytes = sys_read(fd, buf.data(), size);
    if (read_bytes < 0) {
        std::cout << "[ERR] read failed" << std::endl;
        return;
    }

    if (read_bytes == 0) {
        std::cout << "[INFO] EOF" << std::endl;
        return;
    }

    buf[read_bytes] = '\0';
    std::cout << buf.data() << std::endl;
}

static int vfs_seek(int fd, long long offset) {
    if (offset < 0) {
        return -1;
    }

    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    if (fd < 0 || fd >= NOFILE) {
        return -1;
    }

    unsigned short sys_index = user_table[current_user_slot].u_ofile[fd];
    if (sys_index == VFS_INVALID_OFILE || sys_index >= SYSOPENFILE) {
        return -1;
    }

    sys_ofile[sys_index].f_offset = static_cast<unsigned long long>(offset);
    return 0;
}

static int vfs_truncate_fd(int fd) {
    if (ensure_mounted() != 0 || current_user_id < 0) {
        return -1;
    }

    if (fd < 0 || fd >= NOFILE) {
        return -1;
    }

    unsigned short sys_index = user_table[current_user_slot].u_ofile[fd];
    if (sys_index == VFS_INVALID_OFILE || sys_index >= SYSOPENFILE) {
        return -1;
    }

    struct file *fp = &sys_ofile[sys_index];
    if (fp->f_inode == nullptr) {
        return -1;
    }

    if (myfs_phys_truncate(fp->f_inode->i_num, 0) != MYFS_OK) {
        return -1;
    }

    fp->f_offset = 0;
    return 0;
}

static void vfs_cmd_cat(const std::string &path) {
    int fd = sys_open(path.c_str(), FREAD);
    if (fd < 0) {
        std::cout << "[ERR] open failed" << std::endl;
        return;
    }

    std::vector<char> buf(MYFS_BLOCK_SIZE + 1);
    while (true) {
        int read_bytes = sys_read(fd, buf.data(), MYFS_BLOCK_SIZE);
        if (read_bytes <= 0) {
            break;
        }
        buf[read_bytes] = '\0';
        std::cout << buf.data();
    }

    std::cout << std::endl;
    sys_close(fd);
}

static void vfs_print_prompt(void) {
    std::cout << "myfs:" << cur_path << "> " << std::flush;
}

int main() {
#ifdef _WIN32
    // 强制 C++ std::cout/std::cin 使用 UTF-8，与前端 Python 保持一致
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    vfs_init_hash();
    vfs_init_users();

    std::string line;

    vfs_print_prompt();
    while (std::getline(std::cin, line)) {
        line = vfs_trim(line);
        if (line.empty()) {
            vfs_print_prompt();
            continue;
        }

        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;

        if (cmd == "format") {
            if (vfs_format_default_disk() != 0) {
                std::cout << "[ERR] format failed" << std::endl;
            }
        } else if (cmd == "leak") {
            /*
             * 制造泄漏块用于演示 fsck→recover 完整流程：
             * 分配 N 个数据块，但不链接到任何 inode，
             * 然后在 crash 前不做任何清理。
             *
             * 这些块已从空闲链中移除但没有任何 inode 引用它们，
             * fsck 会检测为 leaked_blocks，recover 会回收。
             */
            int n = 20;
            ss >> n;
            if (n < 1) n = 20;
            if (n > 500) n = 500;
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            std::cout << "[WARN] creating " << n << " leaked blocks..." << std::endl;
            for (int i = 0; i < n; i++) {
                myfs_block_t blk = 0;
                if (myfs_block_alloc(&blk) == MYFS_OK) {
                    /* 分配成功——块已从空闲链移除，但不给任何 inode 用 */
                }
            }
            std::cout << "[OK] " << n << " blocks now leaked — "
                      << "use 'fsck' to detect, then 'recover' to fix" << std::endl;

        } else if (cmd == "crash") {
          
            std::cout << "[WARN] simulated crash — cache not flushed, "
                      << "filesystem left DIRTY" << std::endl;
            return 1;
        } else if (cmd == "mount") {
            if (vfs_mount_default_disk() != 0) {
                std::cout << "[ERR] mount failed" << std::endl;
            }
        } else if (cmd == "login") {
            std::string user;
            std::string pass;
            if (!(ss >> user >> pass)) {
                std::cout << "[ERR] login args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_login(user.c_str(), pass.c_str()) != 0) {
                std::cout << "[ERR] login failed" << std::endl;
            }
        } else if (cmd == "logout") {
            if (sys_logout() != 0) {
                std::cout << "[ERR] logout failed" << std::endl;
            }
        } else if (cmd == "mkdir") {
            std::string path;
            if (!(ss >> path)) {
                std::cout << "[ERR] mkdir args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_mkdir(path.c_str()) != 0) {
                std::cout << "[ERR] mkdir failed" << std::endl;
            }
        } else if (cmd == "batch_mkdir") {
            std::string path;
            std::string prefix;
            int count = 0;
            if (!(ss >> path >> prefix >> count)) {
                std::cout << "[ERR] batch_mkdir args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            vfs_batch_mkdir(path, prefix, count);
        } else if (cmd == "rmdir") {
            std::string path;
            if (!(ss >> path)) {
                std::cout << "[ERR] rmdir args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_rmdir(path.c_str()) != 0) {
                std::cout << "[ERR] rmdir failed" << std::endl;
            }
        } else if (cmd == "create") {
            std::string path;
            if (!(ss >> path)) {
                std::cout << "[ERR] create args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            int fd = sys_create(path.c_str(), DEFAULTMODE);
            if (fd < 0) {
                std::cout << "[ERR] create failed" << std::endl;
            } else {
                sys_close(fd);
            }
        } else if (cmd == "batch_create") {
            std::string path;
            std::string prefix;
            int count = 0;
            int size = 0;
            if (!(ss >> path >> prefix >> count)) {
                std::cout << "[ERR] batch_create args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (ss >> size) {
                if (size < 0) {
                    size = 0;
                }
            }
            vfs_batch_create(path, prefix, count, size);
        } else if (cmd == "batch_delete") {
            std::string path;
            std::string prefix;
            int count = 0;
            if (!(ss >> path >> prefix >> count)) {
                std::cout << "[ERR] batch_delete args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            vfs_batch_delete(path, prefix, count);
        } else if (cmd == "delete") {
            std::string path;
            if (!(ss >> path)) {
                std::cout << "[ERR] delete args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_delete(path.c_str()) != 0) {
                std::cout << "[ERR] delete failed" << std::endl;
            }
        } else if (cmd == "open") {
            std::string path;
            std::string mode_str;
            if (!(ss >> path >> mode_str)) {
                std::cout << "[ERR] open args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }

            int mode = 0;
            bool truncate = false;
            if (mode_str == "r") {
                mode = FREAD;
            } else if (mode_str == "w") {
                mode = FWRITE;
            } else if (mode_str == "a") {
                mode = FWRITE | FAPPEND;
            } else if (mode_str == "rw" || mode_str == "wr" || mode_str == "r+" || mode_str == "+r") {
                mode = FREAD | FWRITE;
            } else if (mode_str == "w+" || mode_str == "+w") {
                mode = FREAD | FWRITE;
                truncate = true;
            } else if (mode_str == "a+" || mode_str == "+a") {
                mode = FREAD | FWRITE | FAPPEND;
            }

            int fd = sys_open(path.c_str(), mode);
            if (fd >= 0) {
                if (mode_str == "w") {
                    truncate = true;
                }
                if (truncate) {
                    if (vfs_truncate_fd(fd) != 0) {
                        std::cout << "[ERR] truncate failed" << std::endl;
                    }
                }
                std::cout << "[OK] fd=" << fd << std::endl;
                vfs_sync_cache_lru();
            } else {
                std::cout << "[ERR] open failed" << std::endl;
            }
        } else if (cmd == "close") {
            int fd = -1;
            if (!(ss >> fd)) {
                std::cout << "[ERR] close args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_close(fd) != 0) {
                std::cout << "[ERR] close failed" << std::endl;
            } else {
                vfs_sync_cache_lru();
            }
        } else if (cmd == "write") {
            int fd = -1;
            if (!(ss >> fd)) {
                std::cout << "[ERR] write args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }

            std::string rest;
            std::getline(ss, rest);
            rest = vfs_trim(rest);

            if (rest.empty()) {
                std::cout << "[ERR] write args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }

            std::stringstream rs(rest);
            std::string flag;
            rs >> flag;

            if (flag == "-s") {
                int size = 0;
                rs >> size;
                vfs_cmd_write(fd, size);
            } else if (flag == "-t") {
                std::string text;
                std::getline(rs, text);
                text = vfs_trim(text);
                vfs_cmd_write_text(fd, text);
            } else {
                vfs_cmd_write_text(fd, rest);
            }
            vfs_sync_cache_lru();
        } else if (cmd == "read") {
            int fd = -1;
            int size = 0;
            if (!(ss >> fd >> size)) {
                std::cout << "[ERR] read args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            vfs_cmd_read(fd, size);
            vfs_sync_cache_lru();
        } else if (cmd == "seek") {
            int fd = -1;
            long long offset = 0;
            if (!(ss >> fd >> offset)) {
                std::cout << "[ERR] seek args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (vfs_seek(fd, offset) != 0) {
                std::cout << "[ERR] seek failed" << std::endl;
            }
        } else if (cmd == "cd") {
            std::string path;
            if (!(ss >> path)) {
                std::cout << "[ERR] cd args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_chdir(path.c_str()) != 0) {
                std::cout << "[ERR] cd failed" << std::endl;
            }
        } else if (cmd == "chdir") {
            std::string path;
            if (!(ss >> path)) {
                std::cout << "[ERR] chdir args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_chdir(path.c_str()) != 0) {
                std::cout << "[ERR] chdir failed" << std::endl;
            }
        } else if (cmd == "dir") {
            if (current_user_id < 0) {
                std::cout << "[ERR] not logged in" << std::endl;
            } else {
                sys_dir();
            }
        } else if (cmd == "dirinode") {
            if (current_user_id < 0) {
                std::cout << "[ERR] not logged in" << std::endl;
            } else {
                std::string path;
                if (ss >> path) {
                    sys_dir_inode(path.c_str());
                } else {
                    sys_dir_inode(nullptr);
                }
            }
        } else if (cmd == "pwd") {
            std::cout << cur_path << std::endl;
        } else if (cmd == "statfs") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            if (myfs_phys_debug_statfs() != MYFS_OK) {
                std::cout << "[ERR] statfs failed" << std::endl;
            }
            vfs_sync_super_info();
        } else if (cmd == "super") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            if (myfs_phys_debug_super() != MYFS_OK) {
                std::cout << "[ERR] super failed" << std::endl;
            }
        } else if (cmd == "freegroup") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            if (myfs_phys_debug_free_group() != MYFS_OK) {
                std::cout << "[ERR] freegroup failed" << std::endl;
            }
        } else if (cmd == "sync") {
            /*
             * 强制刷盘：把所有 dirty 缓存块写回磁盘。
             *
             * 在 crash 之前调用 sync 可以保证文件数据持久化，
             * 不会因缓存未刷盘而丢失。
             */
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            if (myfs_phys_sync() == MYFS_OK) {
                std::cout << "[OK] cache flushed to disk" << std::endl;
            } else {
                std::cout << "[ERR] sync failed" << std::endl;
            }
        } else if (cmd == "cache") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            if (myfs_phys_debug_cache() != MYFS_OK) {
                std::cout << "[ERR] cache failed" << std::endl;
            }
            vfs_sync_cache_lru();
        } else if (cmd == "blockmap") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            uint32_t start = 0;
            uint32_t cnt = 0;
            if (!(ss >> start >> cnt)) {
                std::cout << "[ERR] blockmap args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            vfs_sync_used_blocks(start, cnt);
            myfs_phys_debug_blockmap_range(start, cnt);
        } else if (cmd == "fsck") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            myfs_phys_fsck_result_t result;
            memset(&result, 0, sizeof(result));
            int ret = myfs_phys_fsck(1, &result);
            if (ret != MYFS_OK) {
                std::cout << "[ERR] fsck failed: " << myfs_strerror(ret) << std::endl;
            } else {
                std::cout << "[OK] fsck summary:"
                          << " errors=" << result.errors_found
                          << " leaked_blocks=" << result.leaked_blocks
                          << " used_free_conflicts=" << result.used_free_conflicts
                          << " free_chain_err=" << result.free_chain_errors
                          << " free_count_err=" << result.free_block_count_errors
                          << " inode_state_err=" << result.inode_state_errors
                          << std::endl;
            }
        } else if (cmd == "recover") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            /*
             * 恢复流程：
             * 1. 先执行 journal 恢复——如果 journal 区有 committed 事务，
             *    重放到目标块
             * 2. 再执行 block 恢复——扫描 inode 收集已用块，遍历空闲链，
             *    回收泄漏块
             */
            int jret = myfs_journal_recover();
            if (jret != MYFS_OK) {
                std::cout << "[WARN] journal recover returned " << myfs_strerror(jret) << std::endl;
            } else {
                std::cout << "[OK] journal recovery done" << std::endl;
            }
            /*
             * 关键：journal recover 可能已向磁盘写入了新的超级块数据。
             * 必须重新从磁盘加载超级块，否则内存中的 free_blocks_count
             * 等字段仍然是旧值，导致后续 block 恢复和 fsck 看到过期数据。
             */
            myfs_super_load();
            if (myfs_phys_debug_recover_blocks() != MYFS_OK) {
                std::cout << "[ERR] block recover failed" << std::endl;
            }
        } else if (cmd == "bmap") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            unsigned int inode_id = 0;
            unsigned int logical_block = 0;
            if (!(ss >> inode_id >> logical_block)) {
                std::cout << "[ERR] bmap args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (myfs_phys_debug_bmap(inode_id, logical_block) != MYFS_OK) {
                std::cout << "[ERR] bmap failed" << std::endl;
            }
        } else if (cmd == "inode") {
            if (ensure_mounted() != 0) { vfs_print_prompt(); continue; }
            unsigned int inode_id = 0;
            if (!(ss >> inode_id)) {
                std::cout << "[ERR] inode args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (myfs_phys_debug_inode(inode_id) != MYFS_OK) {
                std::cout << "[ERR] inode failed" << std::endl;
            }
        } else if (cmd == "cat") {
            std::string path;
            ss >> path;
            vfs_cmd_cat(path);
        } else if (cmd == "rm") {
            std::string path;
            if (!(ss >> path)) {
                std::cout << "[ERR] rm args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_delete(path.c_str()) != 0) {
                std::cout << "[ERR] rm failed" << std::endl;
            }
        } else if (cmd == "link") {
            std::string oldpath, newpath;
            if (!(ss >> oldpath >> newpath)) {
                std::cout << "[ERR] link args missing. Usage: link <oldpath> <newpath>" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_link(oldpath.c_str(), newpath.c_str()) != 0) {
                std::cout << "[ERR] link failed" << std::endl;
            }
        } else if (cmd == "chmod") {
            std::string path;
            std::string mode_str;
            if (!(ss >> path >> mode_str)) {
                std::cout << "[ERR] chmod args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            unsigned short mode = static_cast<unsigned short>(std::stoi(mode_str, nullptr, 8));
            if (sys_chmod(path.c_str(), mode) != 0) {
                std::cout << "[ERR] chmod failed" << std::endl;
            } else {
                std::cout << "[OK] chmod success" << std::endl;
            }
        } else if (cmd == "chown") {
            std::string path;
            unsigned int uid, gid;
            if (!(ss >> path >> uid >> gid)) {
                std::cout << "[ERR] chown args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            if (sys_chown(path.c_str(), uid, gid) != 0) {
                std::cout << "[ERR] chown failed" << std::endl;
            } else {
                std::cout << "[OK] chown success" << std::endl;
            }
        } else if (cmd == "useradd") {
            std::string username, password;
            unsigned short uid, gid;
            if (!(ss >> username >> password >> uid >> gid)) {
                std::cout << "[ERR] useradd args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            sys_useradd(username.c_str(), password.c_str(), uid, gid);
        } else if (cmd == "userdel") {
            std::string username;
            if (!(ss >> username)) {
                std::cout << "[ERR] userdel args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            sys_userdel(username.c_str());
        } else if (cmd == "whoami") {
            if (current_user_id < 0) {
                std::cout << "[ERR] not logged in" << std::endl;
            } else {
                for (int i = 0; i < pwd_count; i++) {
                    if (pwd_table[i].p_uid == static_cast<unsigned short>(current_user_id)) {
                        std::cout << pwd_name[i] << " (uid=" << pwd_table[i].p_uid << " gid=" << pwd_table[i].p_gid << ")" << std::endl;
                        break;
                    }
                }
            }
        } else if (cmd == "su") {
            std::string user, pass;
            if (!(ss >> user >> pass)) {
                std::cout << "[ERR] su args missing" << std::endl;
                vfs_print_prompt();
                continue;
            }
            sys_logout();
            if (sys_login(user.c_str(), pass.c_str()) != 0) {
                std::cout << "[ERR] su failed" << std::endl;
            }
        } else if (cmd == "exit") {
            if (current_user_id >= 0) {
                sys_logout();
            }
            if (myfs_phys_is_mounted() == 1) {
                vfs_users_save();
                myfs_phys_umount();
            }
            break;
        } else {
            std::cout << "[ERR] unknown command [" << cmd << "] length=" << cmd.length() << " chars:";
            for (char c : cmd) {
                std::cout << " " << (int)c;
            }
            std::cout << std::endl;
        }

        vfs_print_prompt();
    }

    return 0;
}
