#ifndef FS_H
#define FS_H

#pragma pack(1) // 消除结构体字节对齐，确保写入二进制虚拟盘时的空间精确性 [cite: 868]

// ============================================================================
// 一、 核心常量定义 (严格匹配课设规范)
// ============================================================================
#define BLOCKSIZ      512   // 每块物理盘块的大小 (字节) [cite: 795]
#define SYSOPENFILE   40    // 系统打开文件表最大项数 [cite: 796]
#define DIRNUM        128   // 每个目录项数组包含的最大目录项数 [cite: 797]
#define DIRSIZ        14    // 每个目录项文件名部分所占字节数 [cite: 798]
#define PWDSIZ        12    // 用户登录口令字最大长度 [cite: 799]
#define PWDNUM        32    // 系统最多可注册的用户/口令数量 [cite: 800]
#define NOFILE        20    // 每个用户进程最多可同时打开的文件项数 [cite: 801]
#define NADDR         10    // 每个i节点拥有的物理块指针数 (addr[0]~addr[9]用于混合索引) [cite: 802]
#define NHINO         128   // 内存i节点管理所用的Hash链表数 (必须为2的幂) [cite: 803]
#define USERNUM       10    // 允许同时登录的最大用户数 [cite: 804]
#define DINODESIZ     32    // 每个磁盘i节点所占的严格字节数 
#define DINODEBLK     32    // 磁盘索引节点区占用的总物理块数 [cite: 806]
#define FILEBLK       512   // 数据区（目录及用户文件）拥有的总物理块数 [cite: 807]
#define NICFREE       50    // 超级块中成组链接法空闲块栈的最大容量 [cite: 808]
#define NICINOD       50    // 超级块中空闲i节点栈的最大容量 [cite: 809]

// 物理卷绝对地址边界定义
#define DINODESTART   (2 * BLOCKSIZ)                // i节点区起始物理字节地址 (0#引导块, 1#超级块) [cite: 122, 123, 810]
#define DATASTART     ((2 + DINODEBLK) * BLOCKSIZ)  // 目录与数据文件区起始物理字节地址 [cite: 811]

// ============================================================================
// 二、 核心数据结构定义
// ============================================================================

// 1. 磁盘 i 节点结构 (严格占据 32 字节)
struct dinode {
    unsigned short di_mode;         // 文件类型及存取权限 (rwx格式位) [cite: 786]
    char           di_nlink;        // 关联文件数 (文件的硬链接数) [cite: 786]
    char           di_uid;          // 所有者用户ID [cite: 786]
    int            di_size;         // 文件当前大小 (字节数) [cite: 786]
    unsigned short di_addr[NADDR];  // 物理块号数组，0-7直接索引，8一次间址，9二次间址 [cite: 615, 802]
};

// 2. 目录项结构 (严格占据 16 字节)
struct direct {
    unsigned short d_ino;           // 关联的磁盘 i 节点号 (2字节) [cite: 798]
    char           d_name[DIRSIZ];  // 文件或目录的名称 (14字节) [cite: 798]
};

// 3. 超级块结构 (Filsys - 负责组织磁盘管理信息)
struct filsys {
    unsigned short s_isize;                 // 磁盘i节点区占用的总块数 [cite: 784]
    unsigned short s_fsize;                 // 数据物理块总数 [cite: 784]
    
    unsigned short s_nfree;                 // 内存空闲块栈顶指针/当前组可分配空闲块数 (count) [cite: 337, 784]
    unsigned short s_free[NICFREE];         // 空闲块号栈数组 (成组链接法核心结构) [cite: 784, 808]
    
    unsigned short s_ninode;                // 当前栈内空闲i节点数 [cite: 784]
    unsigned short s_inode[NICINOD];        // 空闲i节点号栈数组 [cite: 784, 809]
    
    char           s_flock;                 // 封锁空闲块控制标志 [cite: 784]
    char           s_ilock;                 // 封锁空闲i节点控制标志 [cite: 784]
    char           s_fmod;                  // 超级块修改标志 (用于同步回写虚拟盘) [cite: 784]
};

// 4. 内存 i 节点结构 (Active Inode Cache)
struct inode {
    unsigned short i_mode;                  // 复制自磁盘i节点：存取权限与类型 [cite: 787]
    char           i_nlink;                 // 硬链接数
    char           i_uid;                   // 用户ID
    int            i_size;                  // 文件大小
    unsigned short i_addr[NADDR];           // 块物理指针映射
    
    struct inode  *i_forw;                  // Hash链表前向指针 (加速查找) [cite: 210, 787]
    struct inode  *i_back;                  // Hash链表后向指针 [cite: 787]
    unsigned short i_count;                 // 内存引用计数 (被多少个进程打开) [cite: 787]
    unsigned short i_num;                   // 对应的磁盘 i 节点号 [cite: 787]
    char           i_flag;                  // 内存状态标志 (如 IUPD 修改未写回) [cite: 787]
};

// 5. 系统打开文件表结构
struct file {
    char          f_flag;                   // 文件操作读写标志 (FREAD/FWRITE) [cite: 790]
    unsigned short f_count;                  // 引用计数 [cite: 790]
    struct inode *f_inode;                  // 指向内存 i 节点表的指针 [cite: 790]
    int           f_offset;                 // 当前文件读写指针的字节偏移量 [cite: 790]
};

// 6. 用户结构 (控制打开表和身份权限)
struct user {
    char           u_uid;                   // 用户独有的识别 ID [cite: 791]
    char           u_gid;                   // 用户所属组 ID [cite: 791]
    unsigned short u_ofile[NOFILE];         // 用户打开文件表 (索引指向系统打开文件表) [cite: 791, 801]
};

#pragma pack()

// ============================================================================
// 三、 函数接口声明 (明确划分团队开发边界)
// ============================================================================

// ----------------------------------------------------------------------------
// 【成员1 负责实现】底层物理与物理空间管理层 (Block/Inode Driver)
// ----------------------------------------------------------------------------
int           format_disk();                // 初始化并创建虚拟磁盘卷，划分超级块与数据区 [cite: 822]
int           balloc();                     // 申请空闲数据盘块 (严格成组链接法弹出) [cite: 819]
void          bfree(int block_no);          // 释放指定物理块号 (严格成组链接法压栈) [cite: 819]
struct inode* ialloc();                     // 从索引区申请一个空闲磁盘i节点，并载入内存 [cite: 813]
void          ifree(unsigned short ino);    // 回收指定的磁盘i节点号 [cite: 813]
int           read_block(int block_no, char *buf);  // 读取绝对物理块数据到内存缓冲区 [cite: 756]
int           write_block(int block_no, char *buf); // 将内存缓冲区数据写入绝对物理块 [cite: 756]

// ----------------------------------------------------------------------------
// 【成员2 负责实现】上层逻辑与系统调用接口 (VFS Logic & Command Engine)
// ----------------------------------------------------------------------------
struct inode* namei(const char *path);      // 路径寻址引擎：将 "/a/b" 路径转化为内存 inode 指针 [cite: 817]
struct inode* iget(unsigned short ino);     // 根据i节点号获取/维护内存 inode (管理Hash链表) [cite: 814]
void          iput(struct inode *p);        // 减少内存引用计数，若为0则触发写回并释放 [cite: 814]
int           access(struct inode *ip, unsigned short mode); // 校验用户对特定节点是否有读写权限 [cite: 815]

// 用户系统调用命令接口 [cite: 821]
int           sys_login(const char *username, const char *password); // 用户登录验证 [cite: 823]
int           sys_logout();                                          // 用户注销回写 [cite: 823]
int           sys_mkdir(const char *path);                          // 创建新多级子目录 [cite: 827]
int           sys_chdir(const char *path);                          // 改变当前工作工作目录 (CWD) [cite: 827]
void          sys_dir();                                             // 显示当前工作目录下的项 (同ls) [cite: 827]
int           sys_create(const char *path, unsigned short mode);     // 创建新文件并分配节点 [cite: 825]
int           sys_open(const char *path, int mode);                  // 打开文件并注册到系统/用户打开表 [cite: 824]
int           sys_close(int fd);                                     // 关闭文件，释放打开表项 [cite: 824]
int           sys_read(int fd, char *buf, int size);                 // 读文件：根据偏移量及混合索引读块 [cite: 826]
int           sys_write(int fd, const char *buf, int size);          // 写文件：动态分配块并维护混合索引 [cite: 826]
int           sys_delete(const char *path);                          // 删除文件，释放其节点及所有物理盘块 [cite: 825]

#endif // FS_H