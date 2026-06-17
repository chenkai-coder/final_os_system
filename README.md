# os_system
A Visual Terminal Operating System

🔗 GitHub: [https://github.com/chenkai-coder/final_os_system](https://github.com/chenkai-coder/final_os_system)

# 操作系统课程设计 - 模拟 UNIX 多级目录文件系统 (VFS)

## 📖 项目简介

本项目为操作系统课程设计的高标准实现方案。系统采用 **“C++ 内核引擎 + Python 可视化外壳”** 的前后端分离架构，在内存与本地二进制文件（虚拟磁盘）中模拟了一个完整的、类 UNIX 的多用户、多级目录文件系统。

项目不仅实现了基础的文件创建、读写与目录切换功能，还在底层严格复现了真实的操作系统资源管理算法，并通过跨进程管道通信实现了极具极客感的可视化交互。

## ✨ 核心特性

### 🧱 硬核底层架构 (C++ Core)
* **纯粹的字节映射**：完全脱离高级容器，基于严格对齐的 `struct` (`#pragma pack(1)`) 将内存数据精准映射至本地 `.img` 虚拟盘文件，支持系统退出保存与重启恢复。
* **成组链接法 (Group Linked List)**：严格按照教科书实现了 100 块为一组的空闲盘块管理栈，保证在海量文件操作下依然维持极高的分配效率。
* **大文件混合索引 (Mixed Indexing)**：基于 `i_addr[10]` 实现了直接索引与一/二次间接索引，突破了单块文件大小的物理限制。
* **Hash 链表加速**：在内存 i 节点管理中引入 Hash 链表，加速高频次的文件查找与缓存命中。

### 🖥️ 现代化交互与展现 (Python UI)
* **非阻塞可视化**：抛弃了容易卡死的单进程 GUI 方案，使用 Python `subprocess` 在后台挂载 C++ 核心进程。
* **磁盘空间热力图**：根据终端截获的 `[SYNC]` 协议，实时将底层的块分配/回收动作转化为 64x64 磁盘网格的颜色变化，物理空间状态一目了然。
* **动态文件树**：左侧面板实时同步当前多级目录的层级结构。

---

## 🛠️ 环境依赖与快速启动

### 环境要求
- **编译器**：支持 C++17（MSVC 2019+ / MinGW-w64 / GCC 9+）
- **构建工具**：CMake 3.10+
- **Python**：Python 3.8+，需安装 PyQt5
- **操作系统**：Windows（主要开发环境）或 Linux

### 1. 编译核心系统（C++ 后端）
```bash
# 进入 C++ 项目目录
cd os_system

# === Windows (MSVC) ===
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug

# === Windows (MinGW-w64) ===
cmake -B cmake-build-debug-mingw-msys2 -G "MinGW Makefiles"
cmake --build cmake-build-debug-mingw-msys2

# === Linux ===
cmake -B build -G "Unix Makefiles"
cmake --build build

# 编译产物：
#   build/Debug/vfs_upper.exe          — VFS 主程序
#   build/Debug/*_test1.exe            — 各模块单元测试
#   build/Debug/myfs_core.lib          — 核心静态库
#   cmake-build-debug-mingw-msys2/mkfs_myfs.exe  — 格式化工具
```

### 2. 运行命令行版本（终端模式）
```bash
# 直接运行 VFS 主程序，进入交互式终端
./build/Debug/vfs_upper.exe

# 首次使用需格式化磁盘（MinGW 构建中包含 mkfs_myfs.exe）
./cmake-build-debug-mingw-msys2/mkfs_myfs.exe
```

### 3. 启动可视化界面（Python GUI）
```bash
cd fs_ui
python main.py
```
GUI 启动后自动在后台挂载 C++ 核心进程，通过 stdin/stdout 管道通信，支持磁盘热力图、文件树、终端面板。

---

## 系统概述

MYFS 是一个教学用虚拟文件系统，采用 **C++ 内核引擎 + Python 可视化外壳** 的前后端分离架构，包含：
- **Python GUI 界面** (`fs_ui/`)：可视化操作界面，支持工具栏按钮点击
- **C++ 核心引擎** (`os_system/`)：文件系统底层实现，通过 stdin/stdout 与 GUI 通信

### 核心特性

| 特性 | 说明 |
|------|------|
| 物理层 | 基于 disk.img 的块设备模拟，块大小 4KB |
| 日志系统 | Write-Ahead Logging (WAL) 日志，保护元数据一致性 |
| 块缓存 | LRU 缓存，容量 128 块，write-back 回写策略 |
| 空闲块管理 | 成组链接法 (Grouped Free Block Chain) |
| 间接寻址 | 支持 12 个直接块 + 一级间接块 + 二级间接块 |
| 硬链接 | 支持 link 创建硬链接，link_count 追踪 |
| 权限模型 | Unix 风格 rwx 权限位，owner/group/other |
| 用户系统 | 多用户支持，持久化到 /.users 文件 |

### 文件系统规格

| 参数 | 值 |
|------|-----|
| 总块数 | 32768 (每块 4KB，总计 128MB) |
| 总 Inode 数 | 4096 |
| 数据区起始块 | 1282 |
| 数据区块数 | 31486（32768 − 1282 开销区） |
| 缓存容量 | 128 块 (LRU 回写策略) |
| 成组链接组大小 | 100 |
| 系统打开文件表 | 40 个槽位 |
| 每用户最多打开文件 | 20 个 |
| 目录项名称长度 | 14 字符 |

### Inode 时间戳说明

每个文件/目录的 inode 包含四个时间戳，执行 `inode <编号>` 可查看：

| 时间字段 | 全称 | 含义 | 何时更新 |
|----------|------|------|----------|
| **atime** | Access Time | 最近访问时间 | `read`、`cat` 等读取操作时更新 |
| **mtime** | Modification Time | 最近内容修改时间 | `write` 修改文件数据时更新 |
| **ctime** | Change Time | inode 元数据修改时间 | `chmod`、`chown`、`link`、`write`（size 变化）时更新 |
| **crtime** | Creation Time | 创建时间 | `create` 创建文件时设置，之后不再改变 |

**举例区分 atime / mtime / ctime**：

```
# 创建文件 — crtime 设定，atime/mtime/ctime 也初始化为当前时间
create /test.txt

# 写入内容 — mtime 和 ctime 更新（内容变了，size 也变了）
open /test.txt rw
write 0 hello
close 0

# 读取文件 — atime 更新
cat /test.txt

# 修改权限 — 只有 ctime 更新（元数据变了，内容没变）
chmod /test.txt 0600

# 修改所有者 — 只有 ctime 更新
chown /test.txt 1000 1000
```

---

## 物理层 API 参考

底层各模块核心函数及其职责、参数、返回值与调用关系如下。

| 函数名 | 所在文件 | 功能 | 主要参数 | 返回值/结果 | 调用关系 |
|--------|----------|------|----------|-------------|----------|
| `myfs_disk_read_block()` / `myfs_disk_write_block()` | disk.cpp | 按物理块号读取或写入磁盘镜像中的 4KB 块 | 物理块号、输入或输出缓冲区 | 成功返回 MYFS_OK，失败返回错误码 | 所有底层读写的基础接口 |
| `myfs_layout_build()` | layout.cpp | 根据磁盘总块数和 inode 数量计算文件系统布局 | 总块数、inode 数量、布局输出结构 | 返回布局计算结果 | 格式化和超级块初始化依赖该接口 |
| `myfs_super_init()` | superblock.cpp | 初始化超级块结构 | 超级块指针、总块数、inode 数 | 成功后生成内存超级块 | 格式化阶段调用 |
| `myfs_super_load()` | superblock.cpp | 从磁盘第 0 块加载超级块 | 无 | 成功后内存中保存超级块状态 | 挂载阶段调用 |
| `myfs_super_sync()` | superblock.cpp | 将内存超级块写回磁盘 | 无 | 成功后 block 0 更新 | 同步、卸载、资源计数变化时调用 |
| `myfs_mkfs()` | mount.cpp | 格式化文件系统 | 磁盘路径、总块数、inode 数 | 成功后创建可挂载的磁盘镜像 | 负责串联布局、超级块、inode、日志和空闲块初始化 |
| `myfs_mount()` / `myfs_umount()` | mount.cpp | 挂载和卸载文件系统 | 磁盘路径或无参数 | 成功后改变文件系统挂载状态 | 负责加载超级块和维护 clean/dirty 状态 |
| `myfs_inode_alloc()` / `myfs_inode_free()` | inode.cpp | 分配或释放 inode | inode 类型、权限、用户、组、inode 编号等 | 返回分配结果或释放状态 | 与 inode 位图、inode 表、超级块和块释放逻辑相关 |
| `myfs_inode_table_read()` / `myfs_inode_table_write()` | inode.cpp | 读取或写回指定 inode 表项 | inode 编号、inode 结构指针 | 成功后完成 inode 表访问 | inode 管理和块映射模块依赖该接口 |
| `myfs_block_group_init()` | block_alloc.cpp | 初始化数据区成组链接法空闲块链 | 数据区起始块、数据块数量 | 成功后建立空闲块结构 | 格式化阶段调用 |
| `myfs_block_alloc()` / `myfs_block_free()` | block_alloc.cpp | 分配或回收数据块 | 输出块号或待释放块号 | 返回分配或释放结果 | 被文件块映射、截断和 inode 释放过程调用 |
| `myfs_inode_get_data_block()` | block_map.cpp | 将文件逻辑块号映射为物理块号，必要时分配新块 | inode 编号、逻辑块号、是否创建、输出物理块 | 返回映射结果和空洞状态 | 文件读写模块依赖该接口 |
| `myfs_inode_release_data_block()` | block_map.cpp | 释放指定逻辑块对应的数据块 | inode 编号、逻辑块号 | 成功后释放对应块并更新索引 | 文件截断和删除相关流程调用 |
| `myfs_inode_read_data()` / `myfs_inode_write_data()` | raw_file.cpp | 基于 inode 执行文件数据读取和写入 | inode 编号、偏移量、缓冲区、长度 | 返回实际读写字节数 | 调用块映射和缓存模块 |
| `myfs_inode_truncate_data()` | raw_file.cpp | 调整文件大小并释放多余数据块 | inode 编号、新大小 | 成功后更新文件大小和块占用 | 文件删除、截断和覆盖写相关流程调用 |
| `myfs_cache_read_block()` / `myfs_cache_write_block()` | cache.cpp | 通过缓存读写物理块 | 物理块号、缓冲区、元数据标志 | 成功后完成缓存访问 | 文件读写和部分元数据操作依赖该接口 |
| `myfs_cache_flush_all()` | cache.cpp | 刷新所有脏缓存块 | 无 | 成功后脏数据写回磁盘 | 同步和卸载流程调用 |
| `myfs_journal_write_metadata_block()` | journal.cpp | 以日志方式写入元数据块 | 目标块号、块内容 | 成功后完成日志保护写入 | inode 位图、inode 表和空闲组管理块写入使用 |
| `myfs_journal_recover()` | journal.cpp | 根据 committed 日志重放元数据块 | 无 | 成功后恢复日志保护的元数据 | 异常后恢复或调试恢复时使用 |
| `myfs_fsck_run()` | fsck.cpp | 执行物理一致性检查 | 检查选项、结果结构 | 返回检查过程状态，错误详情写入结果结构 | 用于检测超级块、inode、数据块和空闲链一致性 |
| `myfs_phys_sync()` | physical_api.cpp | 对外封装文件系统同步操作 | 无 | 成功后缓存和超级块写回 | 供 VFS 层或测试入口调用 |

## 命令分类

### 一、系统管理命令

#### 1. `format` — 格式化磁盘

**功能**：创建全新的 MYFS 文件系统，擦除所有数据。

**GUI 操作**：工具栏点击 **Format** 按钮

**终端输入**：
```
format
```

**说明**：
- 如果文件系统已挂载，会先自动卸载
- 初始化超级块、Inode 位图、成组链接空闲块链
- 创建 root inode (根目录 `/`)
- 自动挂载并初始化默认用户 (root/root, user/user)
- 格式化后需要重新 `login` 才能进行文件操作

---

#### 2. `mount` — 挂载文件系统

**功能**：打开 disk.img 并挂载文件系统。

**GUI 操作**：程序启动时自动执行，无需手动挂载

**终端输入**：
```
mount
```

**说明**：
- 从 disk.img 读取超级块
- 初始化 LRU 缓存
- 重置所有 inode 的 open_count 为 0（因为进程已重启，旧 fd 已无效）
- 检测文件系统状态：如果上次未正常卸载，提示 `DIRTY` 警告
- 从 `/.users` 文件加载用户信息
- 挂载后自动进入根目录 `/`

**输出示例（干净卸载）**：
```
[OK] mounted successfully
```

**输出示例（上次异常退出）**：
```
[WARN] filesystem is DIRTY — previous unmount was not clean
[WARN] run 'fsck' to check consistency, then 'recover' to repair
```

---

#### 3. `exit` — 退出程序

**功能**：保存用户信息、卸载文件系统并退出。

**GUI 操作**：关闭窗口会自动执行

**终端输入**：
```
exit
```

**说明**：
- 如果已登录，先自动 `logout`
- 调用 `vfs_users_save()` 保存用户列表到磁盘
- 调用 `myfs_phys_umount()` 刷新脏缓存、标记 clean、关闭磁盘
- **重要：务必使用 exit 退出，不要直接关闭窗口或 kill 进程，否则文件系统标记为 DIRTY 且缓存数据丢失**

---

### 二、用户管理命令

#### 4. `login` — 用户登录

**功能**：使用用户名和密码登录系统。

**GUI 操作**：
1. 点击工具栏 **Login** 按钮
2. 输入用户名，点击 OK
3. 输入密码，点击 OK

**终端输入**：
```
login <用户名> <密码>
```

**示例**：
```
login root root
login user user
```

**说明**：
- 登录成功后才能进行文件/目录操作
- 同一时间只能一个用户登录
- 默认用户：`root/root` (uid=0), `user/user` (uid=1000)

---

#### 5. `logout` — 用户登出

**功能**：当前用户登出。

**GUI 操作**：点击工具栏 **Logout** 按钮

**终端输入**：
```
logout
```

**说明**：登出时会自动关闭该用户打开的所有文件描述符。

---

#### 6. `whoami` — 查看当前用户

**功能**：显示当前登录用户的用户名、UID、GID。

**GUI 操作**：点击工具栏 **Whoami** 按钮

**终端输入**：
```
whoami
```

**示例输出**：
```
root (uid=0 gid=0)
```

---

#### 7. `su` — 切换用户

**功能**：先登出当前用户，再切换到另一个用户。

**GUI 操作**：点击工具栏 **Su** 按钮 → 输入用户名 → 输入密码

**终端输入**：
```
su <用户名> <密码>
```

**示例**：
```
su user user
su alice 123456
```

**说明**：如果当前已登录，会先自动 logout。切换后当前目录回到 `/`。

---

#### 8. `useradd` — 添加用户

**功能**：创建新用户（仅 root 可操作）。

**GUI 操作**：点击工具栏 **UserAdd** 按钮，按提示输入：
1. 用户名
2. 密码
3. UID（数字）
4. GID（数字）

**终端输入**：
```
useradd <用户名> <密码> <UID> <GID>
```

**示例**：
```
useradd alice 123456 1001 1001
useradd bob pass123 1002 1002
```

**说明**：
- 只有 root (uid=0) 可以添加用户
- UID 不能重复，用户名也不能重复
- 不能添加名为 root 的用户

**错误信息**：
```
[ERR] only root can add users       — 非 root 用户执行
[ERR] user table full               — 用户表已满（最多 32 个）
[ERR] user already exists           — 用户名已存在
[ERR] uid already exists            — UID 已被占用
[ERR] cannot add root               — 不允许添加名为 root 的用户
```

---

#### 9. `userdel` — 删除用户

**功能**：删除指定用户（仅 root 可操作）。

**GUI 操作**：点击工具栏 **UserDel** 按钮 → 输入用户名

**终端输入**：
```
userdel <用户名>
```

**示例**：
```
userdel alice
```

**说明**：不能删除 root 用户。

**错误信息**：
```
[ERR] only root can delete users    — 非 root 用户执行
[ERR] cannot delete root            — 不能删除 root
[ERR] user not found                — 用户不存在
```

---

#### 10. `chmod` — 修改权限

**功能**：修改文件或目录的访问权限。

**GUI 操作**：点击工具栏 **Chmod** 按钮，输入：
```
<路径> <八进制权限>
```

**终端输入**：
```
chmod <路径> <八进制权限>
```

**示例**：
```
chmod /myfile 0755
chmod /secret 0600
chmod /script 0744
```

**权限位说明**：

| 权限值 | 含义 |
|--------|------|
| 0700 | 所有者：读+写+执行 |
| 0070 | 同组用户：读+写+执行 |
| 0007 | 其他用户：读+写+执行 |
| 0755 | 所有者全权限，其他人读+执行 |
| 0644 | 所有者读写，其他人只读 |
| 0600 | 仅所有者可读写 |
| 0777 | 所有人全部权限 |

**说明**：只有 root 或文件所有者可以修改权限。

---

#### 11. `chown` — 修改所有者

**功能**：修改文件或目录的所有者 (uid) 和所属组 (gid)（仅 root 可操作）。

**GUI 操作**：点击工具栏 **Chown** 按钮，输入：
```
<路径> <UID> <GID>
```

**终端输入**：
```
chown <路径> <UID> <GID>
```

**示例**：
```
chown /myfile 1001 1001
chown /docs 1000 1000
```

**说明**：
- **只有 root (uid=0) 可以修改所有者**
- UID 和 GID 必须是数字，不支持用户名（如 `root`、`user`）
- 修改成功后，ctime 会更新

**错误信息**：
```
[ERR] chown: not mounted or not logged in
[ERR] chown: path not found
[ERR] chown: only root can change ownership (current uid=4)
[ERR] chown args missing
```

---

### 三、目录操作命令

#### 12. `mkdir` — 创建目录

**功能**：在当前路径下创建新目录。

**GUI 操作**：点击工具栏 **Mkdir** 按钮 → 输入路径

**终端输入**：
```
mkdir <路径>
```

**示例**：
```
mkdir /docs
mkdir /docs/project
mkdir newfolder
```

**说明**：
- 路径可以是绝对路径或相对路径（相对于当前目录）
- 新建目录会自动包含 `.` 和 `..` 目录项
- 需要父目录有写权限

---

#### 13. `batch_mkdir` — 批量创建目录

**功能**：在指定目录下批量创建多个目录。

**终端输入**：
```
batch_mkdir <父目录路径> <名称前缀> <数量>
```

**示例**：
```
batch_mkdir / test_dir_ 10
```
会创建 `/test_dir_0` 到 `/test_dir_9` 共 10 个目录。

```
batch_mkdir /docs chapter_ 5
```
会创建 `/docs/chapter_0` 到 `/docs/chapter_4`。

---

#### 14. `cd` / `chdir` — 切换目录

**功能**：切换到指定目录。

**GUI 操作**：点击工具栏 **Cd** 按钮 → 输入路径

**终端输入**：
```
cd <路径>
```
或
```
chdir <路径>
```

**示例**：
```
cd /docs
cd ..
cd /
cd project
cd ../other
```

**说明**：支持 `..`（上级目录）和 `.`（当前目录）。`cd` 和 `chdir` 完全等价。

---

#### 15. `pwd` — 显示当前路径

**功能**：显示当前工作目录的绝对路径。

**GUI 操作**：点击工具栏 **Pwd** 按钮

**终端输入**：
```
pwd
```

**示例输出**：
```
/docs/project
```

---

#### 16. `dir` — 列出目录内容

**功能**：列出当前目录下的所有文件和子目录。

**GUI 操作**：点击工具栏 **Dir** 按钮

**终端输入**：
```
dir
```

**输出格式**：
```
<DIR> 0 .
<DIR> 0 ..
<DIR> 4096 docs
<FILE> 0 readme.txt
<FILE> 8192 data.bin
```

每行格式：`<类型> <文件大小(字节)> <名称>`

---

#### 17. `rmdir` — 删除空目录

**功能**：删除一个空的目录。

**GUI 操作**：点击工具栏 **Rmdir** 按钮 → 输入路径

**终端输入**：
```
rmdir <路径>
```

**示例**：
```
rmdir /old_dir
rmdir /docs/empty_folder
```

**说明**：目录必须为空（只有 `.` 和 `..`）才能删除。要删除非空目录，需先删除其中的所有内容。

---

### 四、文件操作命令

#### 18. `create` — 创建文件

**功能**：创建一个空文件，如果文件已存在则清空内容。

**GUI 操作**：点击工具栏 **Create** 按钮 → 输入路径

**终端输入**：
```
create <路径>
```

**示例**：
```
create /myfile.txt
create /docs/readme.md
create newnote
```

**说明**：创建成功后会自动打开文件用于写入，然后关闭。因此创建后 inode 的 crtime、mtime、ctime 均已设置。

---

#### 19. `batch_create` — 批量创建文件

**功能**：批量创建多个文件，并可选择是否写入数据。

**终端输入**：
```
batch_create <父目录路径> <名称前缀> <数量> <每个文件写入的字节数>
```

**示例**：
```
batch_create / demo_ 10 4096
```
创建 `/demo_0` 到 `/demo_9` 共 10 个文件，每个文件写入 4096 字节（A-Z 循环）。

```
batch_create / test_ 5 0
```
创建 5 个空文件（不写入数据）。

```
batch_create /docs file_ 3 100
```
创建 `/docs/file_0`、`/docs/file_1`、`/docs/file_2`，各写入 100 字节。

---

#### 20. `batch_delete` — 批量删除文件

**功能**：批量删除多个文件。

**终端输入**：
```
batch_delete <父目录路径> <名称前缀> <数量>
```

**示例**：
```
batch_delete / demo_ 10
```
删除 `/demo_0` 到 `/demo_9`。

---

#### 21. `delete` / `rm` — 删除文件

**功能**：删除指定文件（不能删除目录）。

**GUI 操作**：点击工具栏 **Delete** 按钮 → 输入路径

**终端输入**：
```
delete <路径>
```
或
```
rm <路径>
```

**示例**：
```
delete /old_file.txt
rm /tmp.txt
```

**说明**：
- 只能删除文件，不能删除目录（删除目录请使用 `rmdir`）
- 删除时会将 link_count 减 1
- 如果 link_count 和 open_count 都为 0，则释放数据块
- 如果文件正被打开，数据会保留到 close
- `delete` 和 `rm` 完全等价

**输出示例**：
```
[OK] file deleted (link_count=0, open_count=0)          — 文件完全删除
[OK] link removed (link_count=1)                        — 还有硬链接存在
[OK] link removed, file data kept (open_count=1)        — 文件正被打开
```

---

#### 22. `open` — 打开文件

**功能**：打开文件并获取文件描述符 (fd)，以便后续读写。

**GUI 操作**：点击工具栏 **Open** 按钮 → 输入 `<路径> <模式>`

**终端输入**：
```
open <路径> <模式>
```

**模式说明**：

| 模式 | 说明 | 文件不存在 | 文件已存在 |
|------|------|------------|------------|
| `r` | 只读 | 失败 | 偏移=0 |
| `w` | 只写 | 失败 | 清空后偏移=0 |
| `a` | 追加写入 | 失败 | 偏移=文件末尾 |
| `rw` / `r+` | 读写 | 失败 | 偏移=0 |
| `w+` | 读写 | 失败 | 清空后偏移=0 |
| `a+` | 读写 | 失败 | 偏移=文件末尾 |

**示例**：
```
open /myfile.txt r
open /myfile.txt w
open /logfile.txt a
open /data.bin rw
```

**成功输出**：
```
[OK] fd=0
```

**说明**：
- 返回的 fd 是当前用户级别的文件描述符（0~19）
- open_count 会自动加 1
- 如果以 `w` 模式打开，文件内容会被清空

---

#### 23. `close` — 关闭文件

**功能**：关闭一个已打开的文件描述符。

**GUI 操作**：点击工具栏 **Close** 按钮 → 输入 fd 号

**终端输入**：
```
close <fd>
```

**示例**：
```
close 0
close 1
```

**说明**：
- open_count 会自动减 1
- 如果 close 后 link_count=0 且 open_count=0，文件会被自动删除
- 关闭后 fd 被释放，可以重新用于新的 open

---

#### 24. `write` — 写入文件

**功能**：向已打开的文件描述符写入数据。

**GUI 操作**：点击工具栏 **Write** 按钮 → 输入 `<fd> <数据>`

**终端输入**（三种方式）：

**1. 写入文本**（直接输入文字）：
```
write <fd> <文本内容>
```

**2. 写入指定大小的生成数据**（用 `-s` 标志）：
```
write <fd> -s <字节数>
```
会生成指定字节数的字母序列数据 (A-Z 循环：ABCDEFGHIJKLMNOPQRSTUVWXYZABCD...)。

**3. 写入指定文本**（用 `-t` 标志）：
```
write <fd> -t <文本内容>
```

**示例**：
```
write 0 hello world
write 0 -s 4096
write 1 -t "some important data"
write 0 这是一段中文文本
```

**成功输出**：
```
[OK] wrote 11 bytes
```

**说明**：
- 写入后文件偏移量会自动前进
- 写入可能触发块分配（文件增长时）
- 如果 fd 以追加模式打开，写入从文件末尾开始

---

#### 25. `read` — 读取文件

**功能**：从已打开的文件描述符读取指定大小的数据。

**GUI 操作**：点击工具栏 **Read** 按钮 → 输入 `<fd> <大小>`

**终端输入**：
```
read <fd> <大小>
```

**示例**：
```
read 0 64
read 1 4096
```

**说明**：
- 读取后文件偏移量会自动前进
- 到达文件末尾时输出 `[INFO] EOF`
- 需要 fd 有读权限（以 r/rw/r+/a+ 模式打开）

---

#### 26. `seek` — 移动文件偏移量

**功能**：移动已打开文件的读写偏移量到指定位置。

**终端输入**：
```
seek <fd> <偏移量>
```

**示例**：
```
seek 0 0          # 回到文件开头
seek 0 100        # 跳到第 100 字节处
seek 1 4096       # 跳到第 4096 字节处
```

**说明**：
- 偏移量必须 >= 0，不支持负偏移（没有 SEEK_END/SEEK_CUR）
- seek 本身不检查偏移量是否超过文件大小
- 常用于需要从文件特定位置读取或写入的场景

**使用场景**：
```
open /data.bin rw
[OK] fd=0
write 0 -s 100
[OK] wrote 100 bytes
seek 0 50                     # 回到中间位置
read 0 20                     # 从第 50 字节开始读
```

---

#### 27. `cat` — 查看文件内容

**功能**：直接读取并打印整个文件内容。

**终端输入**：
```
cat <路径>
```

**示例**：
```
cat /readme.txt
cat /docs/note.md
```

**说明**：内部实现是自动打开文件（只读模式）、循环读取直到 EOF、然后关闭。适合快速查看小文件内容。

---

#### 28. `link` — 创建硬链接

**功能**：为一个已有文件创建一个新的硬链接（两个路径指向同一个 inode）。

**终端输入**：
```
link <源文件路径> <新链接路径>
```

**示例**：
```
link /docs/readme.txt /backup/readme_link.txt
link /data/file1 /data/file1_alias
```

**说明**：
- 不能对目录创建硬链接
- 创建链接后，源文件和链接文件的 link_count 都会增加
- 删除其中一个路径不会影响另一个（link_count 减 1，数据块仍然保留）
- 只有当 link_count 和 open_count 都为 0 时，数据才会被真正释放

**示例流程**：
```
create /original.txt
link /original.txt /link.txt
dir                              → 显示两个文件
delete /original.txt             → link_count 从 2 变为 1，数据保留
cat /link.txt                    → 仍可正常读取
```

**错误信息**：
```
[ERR] cannot link to directory   — 不能对目录创建硬链接
```

---

### 五、文件系统状态查询命令

#### 29. `statfs` — 文件系统统计

**功能**：显示文件系统的整体统计信息。

**GUI 操作**：点击工具栏 **Statfs** 按钮

**终端输入**：
```
statfs
```

**输出内容**：
- 总块数 / 已用块数 / 空闲块数
- 数据区起始块号和数据区块数
- 空闲组大小、块大小
- Inode 总数 / 已用数 / 空闲数
- 缓存大小和脏块数

**额外行为**：
- 同时输出 `[SYNC] SUPER` 同步信息（更新 GUI 热图布局）
- 输出 `[SYNC] GROUP_ITEMS` 成组链接当前组信息
- 输出 `[SYNC] CACHE_LRU` 缓存 LRU 列表

---

#### 30. `super` — 查看超级块

**功能**：显示超级块的详细信息。

**GUI 操作**：点击工具栏 **Super** 按钮

**终端输入**：
```
super
```

**输出内容**：
- 魔数 (magic)、版本号
- 总块数、Inode 总数
- 数据区起始块号
- 空闲块计数、空闲 Inode 计数
- 文件系统状态 (clean/dirty)
- 根目录 Inode 号

---

#### 31. `freegroup` — 查看空闲块组

**功能**：显示成组链接法当前的空闲块组信息。

**GUI 操作**：点击工具栏 **Free Group** 按钮

**终端输入**：
```
freegroup
```

**说明**：成组链接法将空闲块分组管理，每组最多 100 个块。当前活动组的空闲块会被列出。

---

#### 32. `cache` — 查看缓存状态

**功能**：显示 LRU 块缓存的当前状态。

**GUI 操作**：点击工具栏 **Cache** 按钮

**终端输入**：
```
cache
```

**说明**：
- 缓存是 write-back（回写）策略，启动后为空
- 只有被访问过的块才会进入缓存
- 缓存最大容量 128 个块
- LRU 链表示最近使用顺序

---

#### 33. `blockmap` — 查看块使用状态

**功能**：查询指定范围内的数据块是已分配还是空闲。

**GUI 操作**：热图面板翻页时自动触发

**终端输入**：
```
blockmap <起始块号> <块数量>
```

**示例**：
```
blockmap 1282 512
```
查询从数据区起始块开始的 512 个块的使用情况。

**输出格式**：
```
[SYNC] USED_BLOCKS 1282 512 5 1285 1290 1293 1300 1310
```
表示范围内有 5 个已用块，后面跟着具体块号。

---

#### 34. `inode` — 查看 Inode 信息

**功能**：显示指定 Inode 的详细信息。

**GUI 操作**：点击工具栏 **Inode** 按钮 → 输入 Inode 号

**终端输入**：
```
inode <Inode号>
```

**示例**：
```
inode 0
inode 5
```

**输出内容**：
```
--- inode 0 ---
type                = 2 (目录)
mode                = 0755
uid                 = 0
gid                 = 0
size                = 4096
block_count         = 1
link_count          = 2
open_count          = 0
atime               = 2026-06-03 10:30:15
mtime               = 2026-06-03 10:30:15
ctime               = 2026-06-03 10:30:15
crtime              = 2026-06-03 10:30:15
direct[0]           = 1285
direct[1]           = 0
...
indirect1           = 0
indirect2           = 0
checksum            = 0x1234ABCD
```

**说明**：
- Inode 0 通常是根目录
- 直接块最多 12 个 (direct[0] ~ direct[11])
- 一级间接块 (indirect1) 包含 1024 个块指针
- 二级间接块 (indirect2) 包含 1024 个一级间接块指针
- 四个时间戳格式为 `YYYY-MM-DD HH:MM:SS`

---

#### 35. `bmap` — 逻辑块到物理块映射

**功能**：查看文件的某个逻辑块号映射到哪个物理块。

**GUI 操作**：点击工具栏 **Bmap** 按钮 → 输入 Inode 号和逻辑块号

**终端输入**：
```
bmap <Inode号> <逻辑块号>
```

**示例**：
```
bmap 5 0        # 第 0 个逻辑块 → 物理块号
bmap 5 10       # 第 10 个逻辑块 → 物理块号
bmap 5 12       # 进入一级间接块范围
```

**说明**：
- 逻辑块 0~11 由直接块指针映射
- 逻辑块 12~1035 由一级间接块映射
- 逻辑块 ≥1036 由二级间接块映射

---

### 六、维护与恢复命令

#### 36. `fsck` — 文件系统检查

**功能**：对文件系统进行全面的一致性检查，检测各种类型的损坏。

**GUI 操作**：点击工具栏 **FSCK** 按钮

**终端输入**：
```
fsck
```

**检查内容（共 9 类）**：

| 检查项 | 说明 |
|--------|------|
| `inode_state_errors` | Inode 位图与 Inode 表状态不一致（位图标记已用但表为空，或反之） |
| `free_inode_count_errors` | 超级块中空闲 Inode 计数与实际遍历结果不符 |
| `invalid_block_refs` | 文件指针（直接/间接）指向了无效的数据块范围 |
| `duplicated_used_blocks` | 同一个数据块被多个 inode 引用（重复分配） |
| `duplicated_free_blocks` | 成组链接空闲链中出现重复块号 |
| `used_free_conflicts` | 一个块同时出现在已用集合和空闲集合中 |
| `leaked_blocks` | 数据块既不属于任何 inode，也不在空闲链中（泄露块） |
| `free_chain_errors` | 成组链接链结构损坏（循环引用、越界等） |
| `free_block_count_errors` | 遍历得到的空闲块数与超级块记录不符 |

**输出示例（干净文件系统）**：
```
[OK] fsck summary: errors=0 leaked_blocks=0 used_free_conflicts=0 free_chain_err=0 free_count_err=0 inode_state_err=0
```

**输出示例（有问题）**：
```
[OK] fsck summary: errors=20 leaked_blocks=20 used_free_conflicts=0 free_chain_err=0 free_count_err=0 inode_state_err=0
```

**说明**：
- fsck 是**只读检查**，不执行修复
- 检查过程不会修改磁盘数据
- 检查范围：超级块合法性 → Inode 一致性 → 直接/间接指针扫描 → 空闲链遍历 → 冲突检测 → 泄漏检测

---

#### 37. `recover` — 文件系统恢复

**功能**：执行文件系统恢复，修复 fsck 检测到的问题。

**GUI 操作**：点击工具栏 **Recover** 按钮

**终端输入**：
```
recover
```

**恢复流程（两步）**：

**第一步 — Journal 恢复**：
将 journal 区域中已 commit 但未完全写入磁盘的元数据事务重放到目标块。
```
[OK] journal recovery done
```

**第二步 — Block 恢复**：
- 遍历所有 inode，收集被引用的数据块（直接块 + 间接块）
- 遍历成组链接空闲链，收集空闲块
- 找出既不在已用集合也不在空闲集合的**泄露块**
- 找出同时出现在已用集合和空闲集合的**冲突块**
- 将泄露块回收到空闲链
- 修正超级块中的 free_blocks_count

**标准演示流程（crash → fsck → recover → fsck）**：

```
# 1. 制造泄露块
login root root
leak 20
crash

# 2. 重启后检查（看到 20 个错误）
mount
fsck
→ errors=20 leaked_blocks=20

# 3. 恢复
recover
→ [OK] journal recovery done
→ [OK] block recovery done: X leaked blocks recycled, ...
→ [OK] free_blocks_count corrected: 15432 → 15452

# 4. 再次检查（0 个错误）
fsck
→ errors=0 leaked_blocks=0
```

---

#### 38. `sync` — 手动刷新缓存

**功能**：强制将所有 dirty 缓存块写回磁盘。

**终端输入**：
```
sync
```

**成功输出**：
```
[OK] cache flushed to disk
```

**说明**：
- 缓存采用 write-back 策略，数据可能滞留在缓存中
- `sync` 命令强制将所有脏块写入磁盘
- 在 crash 之前调用 sync 可以保证文件数据持久化
- 正常情况下，exit 退出时会自动 sync

**使用场景**：
```
write 0 -s 4096          # 数据进入缓存，可能未落盘
sync                      # 强制落盘，确保数据安全
```

---

#### 39. `crash` — 模拟系统崩溃

**功能**：模拟真实的系统崩溃（不刷缓存、不标记 clean、直接退出）。

**终端输入**：
```
crash
```

**说明**：
- 不刷新写回缓存 — 脏数据块不会写入磁盘
- 不标记文件系统为 clean — 超级块保持 DIRTY
- 直接 exit(1) — 模拟断电/内核 panic
- 崩溃后重新 mount 会看到 DIRTY 警告

**崩溃后恢复**：参考 `recover` 命令中的标准演示流程。

---

#### 40. `leak` — 制造泄露块（调试用）

**功能**：分配 N 个数据块但不关联到任何 inode，用于演示 fsck/recover 流程。

**终端输入**：
```
leak <数量>
```

**示例**：
```
leak 20     # 制造 20 个泄露块
leak 50     # 制造 50 个泄露块
```

**说明**：
- 默认创建 20 个，最多 500 个
- 分配的块从空闲链移除但不引用，fsck 会检测为 leaked_blocks
- 配合 `crash` → `fsck` → `recover` → `fsck` 流程演示恢复功能

---

## 命令速查表

| 类别 | 命令 | 功能 |
|------|------|------|
| **系统** | `format` | 格式化磁盘 |
| | `mount` | 挂载文件系统 |
| | `exit` | 安全退出（刷缓存、标记clean） |
| **用户** | `login <用户名> <密码>` | 登录 |
| | `logout` | 登出 |
| | `whoami` | 查看当前用户 |
| | `su <用户名> <密码>` | 切换用户 |
| | `useradd <用户名> <密码> <UID> <GID>` | 添加用户（仅root） |
| | `userdel <用户名>` | 删除用户（仅root） |
| | `chmod <路径> <八进制权限>` | 修改权限 |
| | `chown <路径> <UID> <GID>` | 修改所有者（仅root，UID/GID为数字） |
| **目录** | `mkdir <路径>` | 创建目录 |
| | `rmdir <路径>` | 删除空目录 |
| | `cd <路径>` | 切换目录 |
| | `chdir <路径>` | 切换目录（cd 的别名） |
| | `pwd` | 显示当前路径 |
| | `dir` | 列出目录内容 |
| | `batch_mkdir <父目录> <前缀> <数量>` | 批量创建目录 |
| **文件** | `create <路径>` | 创建文件（已存在则清空） |
| | `delete <路径>` | 删除文件 |
| | `rm <路径>` | 删除文件（delete 的别名） |
| | `open <路径> <模式>` | 打开文件 (r/w/a/rw/w+/a+) |
| | `close <fd>` | 关闭文件 |
| | `write <fd> <数据>` | 写入文件 (支持 -s 生成数据, -t 指定文本) |
| | `read <fd> <大小>` | 读取文件 |
| | `seek <fd> <偏移量>` | 移动文件读写偏移量 |
| | `cat <路径>` | 查看文件全部内容 |
| | `link <源路径> <新路径>` | 创建硬链接 |
| | `batch_create <父目录> <前缀> <数量> <大小>` | 批量创建文件 |
| | `batch_delete <父目录> <前缀> <数量>` | 批量删除文件 |
| **查询** | `statfs` | 文件系统统计 |
| | `super` | 查看超级块详情 |
| | `freegroup` | 查看空闲块组 |
| | `cache` | 查看缓存状态 |
| | `blockmap <起始块> <数量>` | 查看块使用情况 |
| | `inode <Inode号>` | 查看 Inode 详情（含时间戳） |
| | `bmap <Inode号> <逻辑块>` | 逻辑块→物理块映射 |
| **维护** | `fsck` | 文件系统一致性检查（只读） |
| | `recover` | 文件系统恢复（journal恢复+block恢复） |
| | `sync` | 手动刷新缓存到磁盘 |
| | `crash` | 模拟崩溃（不刷缓存直接退出） |
| | `leak <数量>` | 制造泄露块（调试用） |

---

## 典型使用场景

### 场景一：基本文件操作

```
login root root
mkdir /docs
cd /docs
create readme.txt
open /docs/readme.txt rw
[OK] fd=0
write 0 Hello MYFS!
[OK] wrote 11 bytes
seek 0 0
read 0 11
Hello MYFS!
close 0
dir
<DIR> 0 .
<DIR> 0 ..
<FILE> 11 readme.txt
cat /docs/readme.txt
Hello MYFS!
```

### 场景二：用户管理

```
login root root
useradd alice 123456 1001 1001
[OK] user added
useradd bob pass99 1002 1002
[OK] user added
logout
login alice 123456
whoami
alice (uid=1001 gid=1001)
su bob pass99
whoami
bob (uid=1002 gid=1002)
```

### 场景三：权限管理

```
login root root
create /public.txt
create /secret.txt
chmod /public.txt 0644
chmod /secret.txt 0600
chown /secret.txt 1001 1001

logout
login alice 123456
cat /public.txt          → 成功（其他用户可读）
cat /secret.txt          → 失败（只有所有者可读写）
```

### 场景四：硬链接

```
login root root
create /original.txt
open /original.txt rw
write 0 this is important data
close 0
link /original.txt /backup.txt
dir
<FILE> XX original.txt
<FILE> XX backup.txt
delete /original.txt     → link_count 从 2 变为 1
cat /backup.txt          → 数据仍然存在
```

### 场景五：crash 恢复演示

```
login root root
batch_create / demo_ 10 4096
leak 20
crash
(程序退出)

--- 重新启动程序 ---
mount
(显示 DIRTY 警告)

fsck
→ errors=20 leaked_blocks=20

recover
→ 回收 20 个泄露块

fsck
→ errors=0 (修复完成)

login root root
dir
→ 文件都在（目录项已通过 journal 保护）
```

### 场景六：seek 随机读写

```
login root root
create /data.bin
open /data.bin rw
[OK] fd=0
write 0 -s 100             # 写入 100 字节
[OK] wrote 100 bytes
seek 0 50                  # 跳到中间
write 0 OVERWRITE           # 从中间覆盖写入
seek 0 0
read 0 100                 # 读取查看效果
close 0
```
