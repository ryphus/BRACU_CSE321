// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_minivsfs.c -o mkfs_builder
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#define BS 4096u               // block size
#define INODE_SIZE 128u
#define ROOT_INO 1u

uint64_t g_random_seed = 0; // This should be replaced by seed value from the CLI.

// below contains some basic structures you need for your project
// you are free to create more structures as you require

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t data_bitmap_start;
    uint64_t data_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;
    uint64_t mtime_epoch;
    uint32_t flags;
    
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint32_t checksum;            // crc32(superblock[0..4091])
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    uint16_t mode;
    uint16_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t size_bytes;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t direct_blocks[12];
    uint32_t reserved_0;
    uint32_t reserved_1;
    uint32_t reserved_2;
    uint32_t proj_id;
    uint32_t uid16_gid16;
    uint64_t xattr_ptr;
    
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc;   // low 4 bytes store crc32 of bytes [0..119]; high 4 bytes 0
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;
    uint8_t type;
    char name[58];
    
    uint8_t  checksum; // XOR of bytes 0..62
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");


// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *) sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; // low 4 bytes carry the crc
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];   // covers ino(4) + type(1) + name(58)
    de->checksum = x;
}
//USE OF CHECKSUM: 
//detect accidental errors in data during storage or transmission.
//initially checksum is computed, stores it, 
//when new data received if it doesnt match with previous checksum value, then there's error


void create_file_system(const char* image_name, uint64_t size_kib, uint64_t inodes);
void write_superblock(int fd, superblock_t* sb);
void write_bitmaps(int fd, superblock_t* sb);
void write_inode_table(int fd, superblock_t* sb);
void create_root_directory(int fd, superblock_t* sb);

int main(int argc, char *argv[]) {
    crc32_init();
    
    // Parse command line arguments
    char *image_name = NULL;
    uint64_t size_kib = 0;
    uint64_t inode_count = 0;
    

    // CLI parser 
    // ./mkfs_builder --image myfs.img --size-kib 180 --inodes 128
    image_name = argv[2];
    size_kib = strtoull(argv[4], NULL, 10);
    inode_count = strtoull(argv[6], NULL, 10);


    
    // Validating arguments
    if (!image_name || size_kib < 180 || size_kib > 4096 || inode_count < 128 || inode_count > 512) {
        printf("Invalid CLI arguments.\n");
        return 1;
    }
    
    // Check ing if size_kib is multiple of 4
    if (size_kib % 4 != 0) {
        printf("Invalid size-kib: must be a multiple of 4\n");
        return 1;
    }
    
    // Creating the file system
    create_file_system(image_name, size_kib, inode_count);
    
    return 0;
}


void create_file_system(const char* image_name, uint64_t size_kib, uint64_t inode_count) {
    
    // superblock initialization
    superblock_t sb;
    sb.magic = 0x4D565346; 
    sb.version = 1;
    sb.block_size = BS; //constant given in template
    sb.total_blocks = (size_kib * 1024) / BS;
    sb.inode_count = inode_count;  //from CLI

    sb.inode_bitmap_start = 1;  // block after superblock
    sb.inode_bitmap_blocks = 1;  // 1 inode bitmap block

    sb.data_bitmap_start = 2; //block after inode bmap
    sb.data_bitmap_blocks = 1;  //1 block for data bmap

    sb.inode_table_start = 3;
    sb.inode_table_blocks = ((inode_count * INODE_SIZE) + BS - 1) / BS ; //celling value needed
    
    // Calculating data region
    sb.data_region_start = 3 + sb.inode_table_blocks;
    sb.data_region_blocks = sb.total_blocks - sb.data_region_start;
    
    sb.root_inode = ROOT_INO; //root_inode index = ROOT_INO -1 (1 indexed)
    sb.mtime_epoch = time(NULL);
    sb.flags = 0;
    
    // Creating the image file
    //O_WRONLY : open for writing only.
    // O_CREAT : create the file if it doesn't exist
    // O_TRUNC : truncate the file (make it empty) if it already exists
    int fd = open(image_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        printf("Error in creating img file\n");
        exit(1);
    }
    
    // Compute superblock checksum (Write superblock)
    write_superblock(fd, &sb);
    
    // Write bitmaps
    write_bitmaps(fd, &sb);
    
    //INODE INITIALIZATION (Write inode table)
    write_inode_table(fd, &sb);
    
    // THEN CREATE YOUR FILE SYSTEM WITH A ROOT DIRECTORY
    // ROOT DIRECTORY INITIALIZATION in data block 0
    create_root_directory(fd, &sb);
    
    // Filling remaining space with zeros
    // If the file is smaller than file_size, it is extended (zero-filled)
    off_t file_size = sb.total_blocks * BS;
    if (ftruncate(fd, file_size) < 0) {
        printf("Error in setting file size\n");
        close(fd);
        exit(1);
    }
    
    close(fd);

    // unsigned integer's formate identifier: %" PRIu64
    printf("File system created successfully: %s\n", image_name);
    printf("Total blocks: %" PRIu64 "\n", sb.total_blocks);
    printf("Inodes: %" PRIu64 "\n", sb.inode_count);
    printf("Data region blocks: %" PRIu64 "\n", sb.data_region_blocks);
}

void write_superblock(int fd, superblock_t* sb) {
    // Calculating checksum
    superblock_crc_finalize(sb);
    
    // Writing superblock to block 0
    // fd: where to write
    // sb: points to the data we are going to write
    // sizeof(superblock_t): number of bytes to write
    // 0 : where in the file to start writing (0 = beginning of the file)

    // pwrite is lseek + write in one atomic call
    if (pwrite(fd, sb, sizeof(superblock_t), 0) != sizeof(superblock_t)) { //for raw byte access
        printf("Error writing superblock\n");
        exit(1);
    }
}

void write_bitmaps(int fd, superblock_t* sb) {
    // allocating 1 block for inode_bitmap
    //inode_bitmap[0].....inode_bitmap[4095]
    uint8_t *inode_bitmap = malloc(1*BS);
    if (!inode_bitmap) {
        printf("Error allocating memory for inode bitmap\n");
        exit(1);
    }
    
    // inode_bitmap[0] = 1 ; 1st inode  (Root inode) booked
    inode_bitmap[0] = 1; 
    
    // Write inode bitmap in .img file
    // off_t : (4th parameter of pwrite) Calculates the byte offset in the file where the inode bitmap block starts
    // sb->inode_bitmap_start * BS = getting the size (KB) from where writing the inode_bitmap will start 
    // ssize_t: holds the return value (a byte count) by the sysmtem call(pwrite())
    off_t inode_bitmap_offset = sb->inode_bitmap_start * BS;
    ssize_t num_of_bytes_written_ibmap = pwrite(fd, inode_bitmap, BS, inode_bitmap_offset);
    if ( num_of_bytes_written_ibmap != BS) {
        printf("Error writing inode bitmap\n");
        free(inode_bitmap);
        exit(1);
    }
    free(inode_bitmap);
    

    //allocating 1 block for data_bitmap
    //data_bitmap[0].....data_bitmap[4095]
    uint8_t *data_bitmap = malloc(1*BS);
    if (!data_bitmap) {
        printf("Error allocating memory for data bitmap\n");
        exit(1);
    }
    
    //data_bitmap[0] = 1 ; 1st data block  (Root directory data) booked
    data_bitmap[0] = 1; 
    
    // Write data bitmap in .img file
    // off_t : Calculates the byte offset in the file where the data bitmap block starts
    // sb->data_bitmap_start * BS = getting the size (KB) from where writing the data_bitmap will start
    // ssize_t: holds the return value (a byte count) by the sysmtem call(pwrite())
    off_t data_bitmap_offset = sb->data_bitmap_start * BS;
    ssize_t num_of_bytes_written_dbmap = pwrite(fd, data_bitmap, BS, data_bitmap_offset);
    if (num_of_bytes_written_dbmap != BS) {
        printf("Error writing data bitmap\n");
        free(data_bitmap);
        exit(1);
    }
    
    free(data_bitmap);
}

void write_inode_table(int fd, superblock_t* sb) {
    // Allocating for inode table
    uint8_t *inode_table = malloc(sb->inode_table_blocks*BS);
    if (!inode_table) {
        printf("Error allocating memory for inode table\n");
        free(inode_table)
        exit(1);
    }
    
    // ROOT INODE initialization
    // Root inode (#1)  = 1st inode = inode_table[0]
    // root inode index = root inode number = ROOT_INO - 1
    // offset =  inode number * size of inode 
    // Byte address or location of the inode number = start address of the inode table + offset
    // inode_table + (ROOT_INO - 1) * INODE_SIZE return uint8_t type
    //type casting it to inode_t structure
    inode_t *root_inode = (inode_t *)(inode_table + (ROOT_INO - 1) * INODE_SIZE);
    root_inode->mode = 0x4000; // Directory
    root_inode->links = 2;     // . and ..
    root_inode->uid = 0;
    root_inode->gid = 0;
    root_inode->size_bytes = 2 * sizeof(dirent64_t); // . and .. 2 diectory entries
    root_inode->atime = time(NULL);
    root_inode->mtime = time(NULL);
    root_inode->ctime = time(NULL);
    root_inode->direct_blocks[0] = 0; // (points to data block 0) First data block of root 
    root_inode->reserved_0 = 0;
    root_inode->reserved_1 = 0;
    root_inode->reserved_2 = 0;
    root_inode->proj_id = 8; // Your group ID
    root_inode->uid16_gid16 = 0;
    root_inode->xattr_ptr = 0;
    
    
    // Calculating root inode checksum
    inode_crc_finalize(root_inode);
    
    // Write inode table in .img file
    // sb->inode_table_start * BS  : retriving the size from where writing the inode table will start
    // sb->inode_table_blocks * BS : total size of inode table

    // size_t : represent the size of objects in bytes
    // it tells pwrite() how many bytes we want to write
    off_t inode_table_offset = sb->inode_table_start * BS;
    size_t inode_table_size = sb->inode_table_blocks * BS;
    ssize_t num_of_bytes_written_itable = pwrite(fd, inode_table, inode_table_size, inode_table_offset);
    if (num_of_bytes_written_itable != inode_table_size) {
        printf("Error writing inode table\n");
        free(inode_table);
        exit(1);
    }
    
    free(inode_table);
}

void create_root_directory(int fd, superblock_t* sb) {
    // ROOT directory initialization
    // Creating directory entries for root
    dirent64_t root_entries[2] = {0};
    
    // N.B : Strncpy is used instead of strcpy because 
    // name field has a fixed size (58 bytes), and      
    // this way we always fill it properly for the filesystem's on-disk format. 
    // Here for ".", 57 empty bytes will be set to null when strncpy is used

    // 1 data block storing 2 root directory entries

    // Entry for "."
    root_entries[0].inode_no = ROOT_INO; //pointing to root inode
    root_entries[0].type = 2; // Directory
    strncpy(root_entries[0].name, ".", sizeof(root_entries[0].name));
    dirent_checksum_finalize(&root_entries[0]);
    
    // Entry for ".."
    root_entries[1].inode_no = ROOT_INO;
    root_entries[1].type = 2; // Directory
    strncpy(root_entries[1].name, "..", sizeof(root_entries[1].name));
    dirent_checksum_finalize(&root_entries[1]);
    


    // Writing root directory entries to first data block in .img file
    // sb->data_region_start * BS : retriving the size (location or byte address) from where writing the data block will start
    off_t data_block_offset = sb->data_region_start * BS;
    ssize_t num_of_bytes_written_root_entries = pwrite(fd, root_entries, sizeof(root_entries), data_block_offset);
    if (num_of_bytes_written_root_entries != sizeof(root_entries)) {
        printf("Error writing root directory entries\n");
        exit(1);
    }
}


