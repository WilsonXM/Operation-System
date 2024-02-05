#include "fs.h"
#include "buf.h"
#include "defs.h"
#include "slub.h"
#include "task_manager.h"
#include "virtio.h"
#include "vm.h"
#include "mm.h"

// --------------------------------------------------
// ----------- read and write interface -------------

void disk_op(int blockno, uint8_t *data, bool write) {
    struct buf b;
    b.disk = 0;
    b.blockno = blockno;
    b.data = (uint8_t *)PHYSICAL_ADDR(data);
    virtio_disk_rw((struct buf *)(PHYSICAL_ADDR(&b)), write);
}

#define disk_read(blockno, data) disk_op((blockno), (data), 0)
#define disk_write(blockno, data) disk_op((blockno), (data), 1)

// -------------------------------------------------
// ------------------ your code --------------------

// global variables
struct sfs_fs SFS;
bool isInit = 0;
uint16_t Hash(uint32_t blocknum) {
    return blocknum & 0x3FF;
}

#define min(X, Y)  ((X) < (Y) ? (X) : (Y))
#define max(X, Y)  ((X) < (Y) ? (Y) : (X))

int sfs_init() {
    disk_read(0, &SFS.super);
    SFS.freemap = (uint8_t *)kmalloc(SFS.super.blocks / 8 + 1);
    if(SFS.freemap == NULL) return -1;
    disk_read(2, SFS.freemap);
    SFS.super_dirty = 0;
    memset(SFS.buffer, 0, 1024);
    isInit = 1;
    return 0;
}

// buffer
// 获取一个指定block编号的block，同时记录block是否被指向，返回指向struct sfs_memory_block的指针
struct INODE *findBlock(uint16_t bufferid, uint32_t blockno) {
    struct INODE *node = SFS.buffer[bufferid];
    for( ; node != NULL; node = node->succ) 
        if(blockno == node->memblock->blockno) 
            break;
    return node;
}

// buffer operations
void* getBlock(uint32_t blockno, bool isclaim, bool isnode) {
    // 首先，在buffer中查询这个block
    uint16_t bufferid = Hash(blockno);
    struct INODE *node = findBlock(bufferid, blockno);
    if(node != NULL) {
        puts("found in buffer\n");
        node->memblock->reclaim_count += isclaim;
        return node->memblock->data;
    }
    puts("found out buffer\n");
    // 没有找到这个block，说明不在buffer中在磁盘上，则创建一个新的block接在buffer里
    struct INODE *newnode = kmalloc(sizeof(struct INODE));
    newnode->memblock = kmalloc(sizeof(struct sfs_memory_block));
    newnode->memblock->blockno = blockno;
    newnode->memblock->data = kmalloc(4096);
    newnode->memblock->dirty = 0;
    newnode->memblock->is_inode = isnode;
    newnode->memblock->reclaim_count = isclaim;
    disk_read(blockno, newnode->memblock->data);    // 这里说明新的block中data的内容是磁盘上的原始数据
    puts("found out buffer2\n");
    if(SFS.buffer[bufferid] == NULL) {
        puts("found not buffer\n");
        SFS.buffer[bufferid] = newnode;
        newnode->pref = NULL;
        newnode->succ = NULL;
    }
    else {
        puts("found hh buffer\n");
        struct INODE* Second = SFS.buffer[bufferid]->succ;
        SFS.buffer[bufferid]->succ = newnode;
        newnode->pref = SFS.buffer[bufferid]->succ;
        newnode->succ = Second;
        if(Second != NULL)
            Second->pref = newnode;
    }
    return newnode->memblock->data;
}

// 一个inode解除对一个block的链接
void releaseBlock(uint32_t blockno) {
    uint16_t bufferid = Hash(blockno);
    struct INODE *node = findBlock(bufferid, blockno);
    if(node != NULL) 
        node->memblock->reclaim_count--;
}

//  修改block时将其置为脏页
void setBlockdirty(uint32_t blockno) {
    uint16_t bufferid = Hash(blockno);
    struct INODE *node = findBlock(bufferid, blockno);
    if(node != NULL) 
        node->memblock->dirty = 1;
}

// 找到一个未被使用过的block，并返回其编号
uint32_t allocBlock() {
    int i = 0;
    for( ; i < SFS.super.blocks/8+1 && SFS.freemap[i] == 0xFF; i++) ; // 找到空闲block在位图的那个uint8_t中
    int j = 0;
    for( ; (SFS.freemap[i]>>j)&0x1 && j < 8; j++) ; // 找到这个元素中第几位是第一个0
    if(j == 8) return 0;    // 说明找不到空闲的block，返回0，0是superblock的编号;
    else return (8 * i) + j;
}

/**
 * 功能: 打开一个文件, 读权限下如果找不到文件，则返回一个小于 0 的值，表示出错，写权限如果没有找到文件，则创建该文件（包括缺失路径）
 * @path : 文件路径 (绝对路径)
 * @flags: 读写权限 (read, write, read | write)
 * @ret  : file descriptor (fd), 每个进程根据 fd 来唯一的定位到其一个打开的文件
 *         正常返回一个大于 0 的 fd 值, 其他情况表示出错
 */
void strcpy(char *a, char *b) {
  while (*b) {
    *a++ = *b++;
  }
  *a = '\0';
}

bool strcompare(char *a, char *b) {
    int index = 0;
    while(a[index] != '\0' && b[index] != '\0') {
        if(a[index] != b[index]) return 0;
        ++index;
    }
    return 1;
}
int sfs_open(const char *path, uint32_t flags) {
    puts("sfs_open begin\n");
    if(!isInit) sfs_init();
    if(path[0] != '/')
        return -1;//路径有问题
    struct sfs_inode* rootNode = (struct sfs_inode*)getBlock(1, 1, 1);   // 加载文件系统根目录
    struct sfs_inode* currinode = rootNode, *prev = NULL, *next = NULL;
    uint32_t nowino = 1, preino = 1, nextino = 1;
    int index = 1;
    // 下面开始逐层对path进行查询
    while(path[index] != 0) {
        int i = index, j = 0;
        if(currinode->type == SFS_FILE)
            return -1;
        else {
            int isFound = 0;
            // 获取路径上目录名/文件名
            char name[SFS_MAX_FILENAME_LEN + 1] = {0};
            int nentry = currinode->size / sizeof(struct sfs_entry);//entry的总数量
            while(path[i] != '/' && path[i] != '\0') 
                name[j++] = path[i++];
            // 在当前inode的子树上找该目录/文件
            // 直接索引
            for(int m = 0; m < currinode->blocks && m < SFS_NDIRECT; m++) {
                struct sfs_entry *dentry = (struct sfs_entry *)getBlock(currinode->direct[m], 1, 0);
                // 获得了direct block[m]中的所有entry
                for(int n = 0; n < SFS_NENTRY && m * SFS_NENTRY + n < nentry; n++) {
                    if(!strcompare(name, dentry[n].filename)) continue;
                    else {// 找到目录了
                        puts("find the file\n");
                        releaseBlock(currinode->direct[m]);
                        next = (struct sfs_inode*)getBlock(dentry[n].ino, 1, 0);
                        isFound = 1;
                        break;
                    }
                }
                if(isFound) break;
                releaseBlock(currinode->direct[m]);
            }
            // 间接索引
            if(!isFound && currinode->indirect) {
                uint32_t *idblock = (uint32_t *)getBlock(currinode->indirect, 1, 0);
                // 获得所有间接索引块中的直接索引块编号，下面就是直接索引的循环
                for(int k = 0; k < 4096/32; k++) {
                    struct sfs_inode *temp = (struct sfs_inode *)getBlock(idblock[k], 1, 0);
                    // 直接索引
                    for(int m = 0; m < SFS_NDIRECT; m++) {
                        struct sfs_entry *dentry = (struct sfs_entry *)getBlock(temp->direct[m], 1, 0);
                        // 获得了direct block[m]中的所有entry
                        for(int n = 0; n < 4096/(4+28); n++) {
                            if(!strcompare(name, dentry[n].filename)) continue;
                            else {// 找到目录了
                                puts("find the file\n");
                                releaseBlock(temp->direct[m]);
                                next = (struct sfs_inode*)getBlock(dentry[n].ino, 1, 0);
                                isFound = 1;
                                break;
                            }
                        }
                        if(isFound) break;
                        releaseBlock(temp->direct[m]);
                    }
                    releaseBlock(idblock[k]);
                }
                releaseBlock(currinode->indirect);
            }
            if(!isFound) {// 没有这个目录/文件，看权限
                if(flags == SFS_FLAG_READ) return -1;
                // 创建当前目录/文件
                uint32_t freshBlockno = allocBlock();
                struct sfs_inode *freshInode = (struct sfs_inode *)getBlock(freshBlockno, 1, 0);
                if(path[i] == '\0') {
                    freshInode->size = 0;
                    freshInode->type = SFS_FILE;
                }else {
                    freshInode->size = sizeof(struct sfs_entry) * 2;
                    freshInode->type = SFS_DIRECTORY;
                }
                freshInode->links = 1;
                freshInode->blocks = 1;
                freshInode->indirect = 0;
                freshInode->direct[0] = allocBlock();
                setBlockdirty(freshBlockno);
                if(freshInode->type == SFS_DIRECTORY) {
                    struct sfs_entry father[2];
                    father[0].ino = freshBlockno;
                    strcpy(father[0].filename, ".");
                    father[1].ino = nowino;
                    strcpy(father[1].filename, "..");
                    disk_write(freshInode->direct[0], &father);
                }
                struct sfs_entry newentry;
                strcpy(newentry.filename, name);
                newentry.ino = freshBlockno;
                if(currinode->size != currinode->blocks * sizeof(struct sfs_entry) * SFS_NENTRY){//即最后一块不是满的
                    struct sfs_entry* LastBlock = getBlock(currinode->direct[currinode->blocks - 1], 1, 0);//取出最后一块    
                    LastBlock[(currinode->size / sizeof(struct sfs_entry)) % 128] = newentry;
                    disk_write(currinode->direct[currinode->blocks], &newentry);
                    setBlockdirty(currinode->direct[currinode->blocks - 1]);
                    releaseBlock(currinode->direct[currinode->blocks - 1]);
                }
                else{//最后一块是满的
                    currinode->direct[currinode->blocks] = allocBlock();
                    disk_write(currinode->direct[currinode->blocks], &newentry);
                    currinode->blocks++;
                }
                currinode->size += sizeof(struct sfs_entry);//多了一个节点
                setBlockdirty(nowino);//这个块被我们修改了
                next = freshInode;
                nextino = freshBlockno;
            }
        }
        //用来记录当前目录的父目录，方便最后写入文件的path
        if(!(preino == nowino && preino == 1))
            releaseBlock(preino);
        prev = currinode;
        preino = nowino;
        currinode = next;
        nowino = nextino;
        index = i + 1;
    }
    //创建文件，找一个空的指针填入
    puts("end of open\n");
    for(int i = 0; i < 16; i++)
        if(current->fs.fds[i] == NULL){
            current->fs.fds[i] = kmalloc(sizeof(struct file));
            current->fs.fds[i]->flags = flags;
            current->fs.fds[i]->inode = currinode;
            current->fs.fds[i]->off = 0;
            current->fs.fds[i]->path = prev;
            current->fs.fds[i]->selfinode = nowino;
            current->fs.fds[i]->fatherinode = preino;
            return i;
        }
    return -1;
}

int sfs_get_files(const char* path, char* files[]) {
    if(!isInit) sfs_init();
}

// buffer operations
void writebackBlock(uint32_t blockno) {
    // 首先，在buffer中查询这个block
    uint16_t bufferid = Hash(blockno);
    struct INODE *node = findBlock(bufferid, blockno);
    if(node == NULL) 
        return;
    if(node->memblock->dirty) 
        disk_write(blockno, node->memblock->data);
    if(node->memblock->reclaim_count == 0) {
        kfree(node->memblock->data);
        kfree(node->memblock);
        if(node == SFS.buffer[bufferid]) {
            SFS.buffer[bufferid] = node->succ;
            if(node->succ)
                SFS.buffer[bufferid]->pref = NULL;
        }else {
            node->pref->succ = node->succ;
            if(node->succ)
                node->succ->pref = node->pref;
        }
        kfree(node);
    }
}

//将一个inode的所有数据块写回
void writebackInode(struct sfs_inode * nownode, bool Type){
    for(int i = 0; i < SFS_NDIRECT; i++){
        printf("%d ", nownode->direct[i]);
        writebackBlock(nownode->direct[i]);
    }
    puts("\n");
    if((nownode->size >> 12) > SFS_NDIRECT){//文件过大需要考虑间接索引是否存在
        uint32_t* indirect = getBlock(nownode->indirect, 1, 0);
        for(int i = 0; i < 4096/32; i++){
            if(indirect[i])
                writebackBlock(indirect[i - SFS_NDIRECT]);
        }
        releaseBlock(nownode->indirect);
        writebackBlock(nownode->indirect);
    }
}

/**
 * 功能: 关闭一个文件，并将其修改过的内容写回磁盘
 * @fd  : 该进程打开的文件的 file descriptor (fd)
 * @ret : 正确关闭返回 0, 其他情况表示出错
 */
int sfs_close(int fd) {
    struct file *f = current->fs.fds[fd];
    if(f == NULL)   //文件不存在
        return -1;
    //将文件写回
    puts("sfs_close: WriteBack files\n");
    struct sfs_inode * nownode = f->inode;
    writebackInode(nownode, SFS_FILE);
    puts("sfs_close: WriteBack fileinode\n");
    writebackBlock(f->selfinode);
    releaseBlock(f->selfinode);
    //将路径上所有访问过的点及其数据块写回，防止路径上的修改没有写回
    puts("sfs_close: WriteBack dirs\n");
    nownode = f->path;
    uint32_t recursive = f->fatherinode;
    while(recursive != 1){
        printf("sfs_close: nowino %d\n", recursive);
        struct sfs_entry* entrys = getBlock(nownode->direct[0], 1, 0);
        uint32_t nextino = entrys[1].ino;   // ..指向父目录
        releaseBlock(nownode->direct[0]);
        writebackInode(nownode, SFS_DIRECTORY);
        if(recursive == f->fatherinode)
            releaseBlock(recursive);
        writebackBlock(recursive);
        recursive = nextino;
        nownode = getBlock(recursive, 1, 0);
    }
    puts("sfs_close: WriteBack RootInode\n");
    printf("sfs_close: nowino %d\n", recursive);
    writebackInode(nownode, SFS_DIRECTORY);
    writebackBlock(recursive);
    if(SFS.super_dirty){
        disk_write(0, &SFS.super);
        disk_write(2, SFS.freemap);
        SFS.super_dirty = 0;
    }
    kfree(current->fs.fds[fd]);
    current->fs.fds[fd] = NULL;
    return 0;
}

/**
 * 功能  : 根据 fromwhere + off 偏移量来移动文件指针(可参考 C 语言的 fseek 函数功能)
 * @fd  : 该进程打开的文件的 file descriptor (fd)
 * @off : 偏移量
 * @fromwhere : SEEK_SET(文件头), SEEK_CUR(当前), SEEK_END(文件尾)
 * @ret : 表示错误码
 *        = 0 正确返回
 *        < 0 出错
 */
int sfs_seek(int fd, int32_t off, int fromwhere){
    struct file *f = current->fs.fds[fd];
    switch (fromwhere) {
        case SEEK_SET://文件头
            f->off = 0 + off;
            break;
        case SEEK_END://文件末尾
            f->off = f->inode->size + off;
            break;
        default:
            f->off = f->off + off;
            break;
    }
    if(f->off < 0 || f->off >= f->inode->size)
        return -1;
    else
        return 0;
}

/**
 * 功能  : 从文件的文件指针开始读取 len 个字节到 buf 数组中 (结合 sfs_seek 函数使用)，并移动对应的文件指针
 * @fd  : 该进程打开的文件的 file descriptor (fd)
 * @buf : 读取内容的缓存区
 * @len : 要读取的字节的数量
 * @ret : 返回实际读取的字节的个数
 *        < 0 表示出错
 *        = 0 表示已经到了文件末尾，没有能读取的了
 *        > 0 表示实际读取的字节的个数，比如 len = 8，但是文件只剩 5 个字节的情况，就是返回 5
 */

int sfs_read(int fd, char *buf, uint32_t len){
    struct file * f = current->fs.fds[fd];
    len = min(len, f->inode->size - f->off);
    int blockindex = f->off / 4096;
    int off = f->off % 4096;
    uint32_t res = 0;
    // 直接索引
    while(blockindex < SFS_NDIRECT && len){
        uint32_t blocklen = min(len, 4096 - off);
        char* block = (char *)getBlock(f->inode->direct[blockindex], 1, 0);
        memcpy(buf + res, block + off, blocklen);
        releaseBlock(f->inode->direct[blockindex]);
        res += blocklen;
        len -= blocklen;
        blockindex++;
        off = 0;
    }
    if(len > 0) {   // 检索间接索引
        blockindex -= SFS_NDIRECT;
        uint32_t* indirect = getBlock(f->inode->indirect, 1, 0);
        while(len){
            uint32_t blocklen = min(len, 4096 - off);
            char* block = getBlock(indirect[blockindex], 1, 0);
            memcpy(buf + res, block + off, blocklen);
            releaseBlock(indirect[blockindex]);
            res += blocklen;
            len -= blocklen;
            blockindex++;
            off = 0;
        }
        releaseBlock(f->inode->indirect);
    }
    f->off += res;
    return res;
}

/**
 * 功能  : 把 buf 数组的前 len 个字节写入到文件的文件指针位置(覆盖)(结合 sfs_seek 函数使用)，并移动对应的文件指针
 * @fd  : 该进程打开的文件的 file descriptor (fd)
 * @buf : 写入内容的缓存区
 * @len : 要写入的字节的数量
 * @ret : 返回实际的字节的个数
 *        < 0 表示出错
 *        >=0 表示实际写入的字节数量
 */
int sfs_write(int fd, char *buf, uint32_t len){
    puts("sfs write begin\n");
    struct file * f = current->fs.fds[fd];
    if(f->flags & SFS_FLAG_WRITE == 0) {
        puts("sfs write failing\n");
        return -1;
    }
    
    int blockindex = f->off / 4096;
    int off = f->off % 4096;
    uint32_t res = 0;
    if(len > f->inode->size - f->off){  // 超出了大小
        f->inode->size = f->off + len;
        setBlockdirty(f->selfinode);
    }
    struct sfs_inode* nownode = f->inode;

    // 直接索引
    while(blockindex < SFS_NDIRECT && len){
        puts("direct index begin\n");
        if(blockindex >= nownode->blocks){
            puts("direct index begin1\n");
            nownode->direct[blockindex] = allocBlock();
            nownode->blocks++;
        }
        uint32_t blocklen = min(len, 4096 - off);
        char* block = (char *)getBlock(nownode->direct[blockindex], 1, 1);
        memcpy(block + off, buf + res, blocklen);
        setBlockdirty(nownode->direct[blockindex]);
        releaseBlock(nownode->direct[blockindex]);
        res += blocklen;
        len -= blocklen;
        blockindex++;
        off = 0;
    }
    puts("direct index begin3\n");
    // 间接索引
    if(len){
        puts("indirect index begin\n");
        bool firstdirty = 0;
        blockindex -= SFS_NDIRECT;
        if(nownode->indirect == 0){
            nownode->indirect = allocBlock();
        }
        uint32_t* indirect = getBlock(nownode->indirect, 1, 1);
        while(len){
            uint32_t blocklen = min(len, 4096 - off);
            if(blockindex + SFS_NDIRECT >= nownode->blocks){
                indirect[blockindex] = allocBlock();
                if(!firstdirty){
                    firstdirty = 1;
                    setBlockdirty(nownode->indirect);
                }
                nownode->blocks++;
            }
            char* block = getBlock(indirect[blockindex], 1, 1);
            memcpy(block + off, buf + res, blocklen);
            setBlockdirty(indirect[blockindex]);
            releaseBlock(indirect[blockindex]);
            res += blocklen;
            len -= blocklen;
            blockindex++;
            off = 0;
        }
        releaseBlock(nownode->indirect);
    }
    f->off += res;
    return res;
}