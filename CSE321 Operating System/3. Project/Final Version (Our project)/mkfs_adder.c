#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <inttypes.h>


#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12
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
    uint32_t direct[12]; //direct blocks
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
    
    // THIS FIELD SHOULD STAY AT THE END
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


// Function to read superblock from image
// fp: pointer to input_fp(output_fp)
int read_superblock(FILE *fp, superblock_t *sb) {
    //SEEK_SET: read from beginning of sb
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return -1; //go back to main when nothing to read (file empty)
    }
    
    
    //fread(sb, sizeof(superblock_t), 1, fp):
        //sb: pointer to address of sb struct where the read data will be stored
        //sizeof(superblock_t): size of each elem to read (size of one sb struct = 116 bytes). reading 116 bytes
        //1: number of superblocks. we have 1 sb
        //fp: file pointer to read from (input_fp)
    size_t num_of_superblock_read = fread(sb, sizeof(superblock_t), 1, fp);
    //on success fread() returns the third parameter
    //on success fread() returns 1 (it has read 1 superblock), 
    //else it enters loop and go back to main()
    if (num_of_superblock_read != 1) {
        return -1;
    }

    
    //Verify magic number to verify img file sb
    if (sb->magic != 0x4D565346) {
        printf("Error in matching sb.magic\n");
        return -1;
    }
    
    return 0;
}

//reading a specific inode from img file
int read_inode(FILE *fp, superblock_t *sb, uint64_t inode_no, inode_t *root_inode) {
    if (inode_no < 1 || inode_no > sb->inode_count) {
        return -1;
    }
    
    // Calculate inode position (inodes are 1-indexed)
    uint64_t inode_table_start_address = sb->inode_table_start * BS;
    uint64_t inode_address = inode_table_start_address + (inode_no - 1) * INODE_SIZE;
    
    if (fseek(fp, inode_address, SEEK_SET) != 0) {
        return -1;
    }
    
    //fread(root_inode, INODE_SIZE, 1, fp):
        //root_inode: pointer to address of root_inode where the read data will be stored
        //INODE_SIZE: size of each elem to read (size of 1 INODE). reading 128 bytes
        //1: number of INODE. 
        //fp: file pointer to read from (input_fp)
    if (fread(root_inode, INODE_SIZE, 1, fp) != 1) {
        return -1;
    }
    
    return 0;
}

//writing a specific inode to img file
int write_inode(FILE *fp, superblock_t *sb, int free_inode_no, inode_t *new_inode) {
    if (free_inode_no < 1 || free_inode_no > sb->inode_count) {
        return -1;
    }
    
    //Calculating inode position (inodes are 1-indexed)
    uint64_t inode_table_start_address = sb->inode_table_start * BS;
    uint64_t inode_address = inode_table_start_address + (free_inode_no - 1) * INODE_SIZE;
    
    //SEEK_SET: move cursor to beginning. offset=inode_address
    if (fseek(fp, inode_address, SEEK_SET) != 0) {
        return -1;
    }
    
    //fwrite(new_inode, INODE_SIZE, 1, fp) - 
       //new_inode: the element we want to write in fp
       //INODE_SIZE: writes INODE_SIZE (in bytes) at once
       //1: no. of elements to write (1 inode for this)
       //fp: tells fwrite where to write (input_fp for this)    
    if (fwrite(new_inode, INODE_SIZE, 1, fp) != 1) {
        return -1;
    }
    
    return 0;
}

// Function to read bitmap
int read_bitmap(FILE *fp, uint64_t bitmap_start, uint8_t *bitmap) {
    //SEEK_SET offset=bitmap_start * BS (moving cursor to start of inode/data bmap) 
    if (fseek(fp, bitmap_start * BS, SEEK_SET) != 0) {
        return -1;
    }
    

    //fread(bitmap, BS, 1, fp) - 
       //bitmap: memory allocated(1 BS) in this location. all data read will be stored in this array
       //BS: reads 1 BS (in bytes) at once
       //1: no. of elements to read (1 block for this)
       //fp: tells fread where to read from (input_fp for this)    
    size_t num_of_bitmap_read = fread(bitmap, BS, 1, fp);
    //on success fread() returns the third parameter
    //on success fread() returns 1 (it has read 1 superblock), 
    //else it enters loop and go back to main()
    if (num_of_bitmap_read != 1) {
        return -1;
    }
    
    return 0;
}

// Function to write bitmap
int write_bitmap(FILE *fp, uint64_t bitmap_start, uint8_t *bitmap) {
    //SEEK_SET: move cursor to beginning of file. offset = bitmap_start * BS (beginning of ibmap)
    if (fseek(fp, bitmap_start * BS, SEEK_SET) != 0) {
        return -1;
    }
    
    //fwrite(bitmap, BS, 1, fp) - 
       //bitmap: the element we want to write
       //BS: writes BS (in bytes) at once
       //1: no. of elements to write (1 block for this)
       //fp: tells fwrite where to write (input_fp for this)    
    size_t num_of_bitmap_write = fwrite(bitmap, BS, 1, fp);
    //on success fwrite() returns the third parameter
    //on success fwrite() returns 1 (it has written 1 ibmap/dbmap), 
    //else it enters loop and go back to main()
    if (num_of_bitmap_write != 1) {
        return -1;
    }
    
    return 0;
}


//Function to find first free bit in bitmap
int find_free_bit(uint8_t *bitmap) { //bitmap has one BS mem allocation
    for (int i = 0; i < BS; i++) {
        if (bitmap[i] != 1) { //bitmap[i]!=1 means that indexed inode/datablock is free
            //printf("Free inode/datablock is found: %d no inode/datablock", i+1); //i+1 (bc 1 indexed)
            return i;
        }
    }
    return -1; // No free bits found
}


// Function to add a directory entry
// add_directory_entry(input_fp, &sb, &root_inode, free_inode + 1, 1, file)
//5th parameter= directory_entry.type = 1 (as its a file)

int add_directory_entry(FILE *fp, superblock_t *sb, inode_t *root_dir_inode, int new_inode_no, uint8_t type,  char *name) {
    // If we get here, we need to allocate a new data block for the directory
    // Read data bitmap
    uint8_t *data_bitmap = malloc(BS);
    if (data_bitmap == NULL) {
        return -1;
    }
    
    if (read_bitmap(fp, sb->data_bitmap_start, data_bitmap) != 0) {
        free(data_bitmap);
        return -1;
    }
    
    // Find free data block
    int free_data_block = find_free_bit(data_bitmap);
    if (free_data_block == -1) {
        free(data_bitmap);
        printf("Error: No free data blocks available\n");
        return -1;
    }
    
    // Allocate the data block
    //set_bitmap_bit(data_bitmap, free_data_block);
    data_bitmap[free_data_block] = 1;
    if (write_bitmap(fp, sb->data_bitmap_start, data_bitmap) != 0) {
        free(data_bitmap);
        return -1;
    }
    
    free(data_bitmap); 
    
    // Find a free direct pointer in the ROOT directory inode
    // TO point to the datablock having the file.txt directory entry
    int free_direct = -1;
    for (int i = 0; i < DIRECT_MAX; i++) {
        if (root_dir_inode->direct[i] == 0) {
            free_direct = i;
            break;
        }
    }
    
    if (free_direct == -1) {
        printf("Error: Directory has no free direct pointers\n");
        return -1;
    }
    
    // Setting the direct pointer to the new data block
    // free_data_block : holds the file.txt directory entry
    root_dir_inode->direct[free_direct] = free_data_block;
    
    // Initialize the new data block with zeros
    uint8_t *empty_block = malloc(BS);
    if (empty_block == NULL) {
        return -1;
    }
    memset(empty_block, 0, sb->block_size);
    
    // block_address where the empty block will be placed
    uint64_t block_address = sb->data_region_start * BS + free_data_block * BS;
    
    if (fseek(fp, block_address, SEEK_SET) != 0) {
        free(empty_block);
        return -1;
    }
    //Writing empty data block to fp
    if (fwrite(empty_block, BS, 1, fp) != 1) {
        free(empty_block);
        return -1;
    }
    
    free(empty_block);
    
    // Now add the directory entry to the new empty block
    dirent64_t new_entry;
    memset(&new_entry, 0, sizeof(new_entry));
    new_entry.inode_no = new_inode_no;
    new_entry.type = type;
    strncpy(new_entry.name, name, sizeof(new_entry.name));
    dirent_checksum_finalize(&new_entry);
    
    if (fseek(fp, block_address, SEEK_SET) != 0) {
        return -1;
    }
    
    //writing the new entry in new datablock
    if (fwrite(&new_entry, sizeof(new_entry), 1, fp) != 1) {
        return -1;
    }
    
    // Update directory inode size and modification time
    root_dir_inode->size_bytes += sizeof(dirent64_t);
    root_dir_inode->mtime = time(NULL);
    root_dir_inode->atime = time(NULL);
    
    return 0;
}

int main(int argc, char *argv[]) {
    crc32_init();
    
    char *input = NULL;
    char *output = NULL;
    char *file = NULL;
    
    input = argv[2];
    output = argv[4];
    file = argv[6];

    if (*input == NULL || *output == NULL || *file == NULL) {
        printf("Usage: %s --input <file> --output <file> --file <file>\n", argv[0]);
        exit(1);
    }
    // Opening to get the size of the file that we want to add into the filesystem
    //rb: "read binary"
    // r: Open file for reading
    // b: Open in binary mode
    //fopen returns a pointer to the FILE obj(file_fp)
    FILE *file_fp = fopen(file, "rb"); 
    if (file_fp == NULL) {
        printf("Error opening file we want to add\n");
        exit(1);
    }
    
    // Get file size
    //SEEK_END moves cursor to end of file to get the total file size
    if (fseek(file_fp, 0, SEEK_END) != 0) {
        printf("Error in seeking: moving the cursor to the end of the file we want to add\n");
        fclose(file_fp);
        exit(1);
    }

    //as cursor is at the end of the file now,
    //the current file position (ftell) equals the file size in bytes.
    //ftell() returns "long" type
    long file_size = ftell(file_fp);
    if (file_size == -1) {
        printf("Error getting file size\n");
        fclose(file_fp);
        exit(1);
    }
    
    //SEEK_SET moves the cursor back to the start of the file 
    //to read the contents from the beginning
    if (fseek(file_fp, 0, SEEK_SET) != 0) {
        printf("Error in seeking: moving the cursor to the start of the file we want to add\n");
        fclose(file_fp);
        exit(1);
    }
    
    // Opening input image
    //r+: read and write on EXISTING file
    //b: in binary 
    FILE *input_fp = fopen(input, "r+b");
    if (input_fp == NULL) {
        printf("Error in opening: read and write on existing input .img file in binary mode\n");
        fclose(file_fp);
        exit(1);
    }
    
    // Copying input image to output
    //w+: read and write after CREATING file
    //b: in binary
    FILE *output_fp = fopen(output, "w+b");
    if (output_fp == NULL) {
        printf("Error in opening: read and write on creating new .img file in binary mode\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    //Copying the entire input .img file to output .img file
    //in builder, pwrite: write at a given offset (as we are creating .img file), writes specific elements at specific locations
    //in addder, fwrite: sequential writing (as we are copying data), writes where the fp currently
    uint8_t buffer[BS]; //an array of one block size. each idx is 1 byte
    size_t bytes_read; //fread returns size_t type
    
    //fread(buffer, 1, bytes_read, input_fp):
        //buffer: pointer to mem where the read bytes will be stored
        //1: size of each element to read (in bytes). reading 1 byte at a time
        //BS: number of total bytes to read
        //input_fp: file pointer to read from

    //fread(buffer, 1, BS, input_fp): reading 1 block, storing it to buffer
    //fwrite(buffer, 1, bytes_read, output_fp): writing that one block(buffer) into new .img file 
    //loop stops when no more blocks remain- fread return 0
    while ((bytes_read = fread(buffer, 1, BS, input_fp)) > 0) {
        if (fwrite(buffer, 1, bytes_read, output_fp) != bytes_read) {
            printf("Error in writing to output .img file\n");
            fclose(file_fp);
            fclose(input_fp);
            fclose(output_fp);
            exit(1);
        }
    }
    
    // Now we'll work with the output image
    fclose(input_fp);
    input_fp = output_fp;/// ekhanee eshe copoied 
    
    
    //Reading and verifying superblock
    superblock_t sb;
    if (read_superblock(input_fp, &sb) != 0) {
        printf("Error reading superblock\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }

    //===EXISTING FILE CHECKER ===========================================================
//bla bla

//c c 


    // Reading root inode (inode #1)
    inode_t checking_root_inode;
    if (read_inode(output_fp, &sb, ROOT_INO, &checking_root_inode) != 0) {
        printf("Error: could not read root inode\n");
        fclose(output_fp);
        return 1;
    }

    // buffer to hold one block
    // each index 1 byte
    // total 4096 bytes of raw binary data
    uint8_t block_buf[BS];

    // Check each direct block of root directory
    for (int i = 0; i < DIRECT_MAX; i++) {
        if (checking_root_inode.direct[i] == 0) continue; // empty slot

        // Seek to that block and read
        // checking the occupied blocks
        fseek(output_fp, (sb.data_region_start + checking_root_inode.direct[i]) * BS, SEEK_SET);

        //block_buf: storing the data thats being read
        //       1 : reading 1 byte at a time
        //      BS : no. of elements being read = 4096 
        //output_fp: reading from this img file
        fread(block_buf, 1, BS, output_fp);

        // reading root directory entries
        // casting raw data(4096 bytes of raw binary data) 
        // as an array of dirent64_t structures (directory entries)
        // where each index is an object of direct64_t struct
        dirent64_t *entries = (dirent64_t *)block_buf;
        int entries_per_block = BS / sizeof(dirent64_t);

        for (int j = 0; j < entries_per_block; j++) {
            if (entries[j].inode_no == 0) continue; // unused entry
            
            // checking the used entries
            //if given file name already exists in the inputted img file system
            if (strcmp(entries[j].name, file) == 0) {
                printf("Error: '%s' already exists in filesystem\n", file);
                fclose(output_fp);
                exit(1); // ends the code here
            }
        }
    }
    //=====================================================================================
    
    //Reading inode bitmap
    uint8_t *inode_bitmap = malloc(1*BS);
    if (inode_bitmap == NULL) {
        printf("Error in allocating memory for inode bitmap\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    if (read_bitmap(input_fp, sb.inode_bitmap_start, inode_bitmap) != 0) {
        printf("Error reading inode bitmap\n");
        free(inode_bitmap); //bc malloc was used
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    //Finding free inode from inode bitmap 
    int free_inode = find_free_bit(inode_bitmap);
    if (free_inode == -1) {
        printf("Error: No free inodes available\n");
        free(inode_bitmap);
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    inode_bitmap[free_inode] = 1; //allocate it in malloc (not in img file)
    
    
    //writing the update inode bmap into the .img file
    if (write_bitmap(input_fp, sb.inode_bitmap_start, inode_bitmap) != 0) {
        printf("Error in writing inode bitmap in img file\n");
        free(inode_bitmap); //from malloc
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    free(inode_bitmap); //freeing inode bmap from mem allocation as it's work is done in .img file
    
    //Reading data bitmap
    uint8_t *data_bitmap = malloc(1*BS);
    if (data_bitmap == NULL) {
        printf("Error in allocating one block for data bitmap in memory");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    if (read_bitmap(input_fp, sb.data_bitmap_start, data_bitmap) != 0) {
        printf("Error in reading data bitmap from img file\n");
        free(data_bitmap);
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    } 


    //calculating how many blocks the file needs

    //ceiling. e.g. if blocks needed=1.2, I would still need 2 blocks to store the file
    int blocks_needed = (file_size + BS - 1) / BS;  
    if (blocks_needed > DIRECT_MAX) {
        printf("Error: File is too large to be accommodated with %d direct blocks\n", DIRECT_MAX);
        free(data_bitmap);
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    // Allocating data blocks
    uint32_t free_data_blocks_list[DIRECT_MAX] = {0};
    
    for (int i = 0; i < blocks_needed; i++) {
        int free_data_block = find_free_bit(data_bitmap);
        if (free_data_block == -1) {
            printf("Error: No free data blocks available\n");
            free(data_bitmap);
            fclose(file_fp);
            fclose(input_fp);
            exit(1);
        }
        
        //updating data bmap to allocate it for the file's data blocks
        data_bitmap[free_data_block] = 1; //allocate it in malloc (not in img file)
    
        free_data_blocks_list[i] = free_data_block;
    }
    
    //Writing updated data bitmap in img file
    if (write_bitmap(input_fp, sb.data_bitmap_start, data_bitmap) != 0) {
        printf("Error writing data bitmap\n");
        free(data_bitmap);
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    free(data_bitmap);
    
    //Create the new inode
    inode_t new_inode;
    memset(&new_inode, 0, sizeof(new_inode));
    new_inode.mode = 0x8000; //Regular file
    new_inode.links = 1;     //Only one link (from root directory) bc file placed in root dir
    new_inode.size_bytes = file_size; //the size of the file (in bytes) that this inode is pointing to
    new_inode.atime = time(NULL);
    new_inode.mtime = time(NULL);
    new_inode.ctime = time(NULL);
    new_inode.proj_id = 8; //group ID
    
    //Setting direct pointers to the free data blocks (where the file is to be placed later)
    for (int i = 0; i < blocks_needed; i++) {
        new_inode.direct[i] = free_data_blocks_list[i]; 
    }
    
    inode_crc_finalize(&new_inode);
    
    //Writing new inode
    //free_inode coming from find_free_bit(), we used earlier
    //free_inode no. = free_inode+1 (bc 1-indexing)
    if (write_inode(input_fp, &sb, free_inode + 1, &new_inode) != 0) {
        printf("Error in writing new inode to img file\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    //Writing file data to data blocks ======================================================================================
    for (int i = 0; i < blocks_needed; i++) {
        //starting address of data region = sb.data_region_start * BS
        //offset of free data block = free_data_blocks_list[i] * BS
        uint64_t block_address = sb.data_region_start * BS + free_data_blocks_list[i] * BS;
        
        if (fseek(input_fp, block_address, SEEK_SET) != 0) {
            printf("Error in seeking to data block\n");
            fclose(file_fp);
            fclose(input_fp);
            exit(1);
        }
        
        // how many bytes to read from file.txt while writing each data block
        // if file size exceeded, then we'll face error while trying to read the file.txt
        size_t bytes_to_read;
        if (i == blocks_needed - 1) {
            // This is the last block, retriving the remaining bytes to read from file.txt
            bytes_to_read = file_size % BS;

            if (bytes_to_read == 0) {
                // File size is exactly divisible by block size
                bytes_to_read = BS;
            }
        } 
        else {
            // Not the last block, always read full block
            bytes_to_read = BS;
        }

        //file_data
        uint8_t *file_data = malloc(bytes_to_read);
        if (file_data == NULL) {
            printf("Error in allocating memory for file (file to add) data\n");
            fclose(file_fp);
            fclose(input_fp);
            exit(1);
        }
        


        if (fread(file_data, 1, bytes_to_read, file_fp) != bytes_to_read) {
            printf("Error in reading file data\n");
            free(file_data);
            fclose(file_fp);
            fclose(input_fp);
            exit(1);
        }
        
        //writing the data block (the bytes we just read above) in img file
        if (fwrite(file_data, 1, bytes_to_read, input_fp) != bytes_to_read) {
            printf("Error in writing file data\n");
            free(file_data);
            fclose(file_fp);
            fclose(input_fp);
            exit(1);
        }
        
        free(file_data);
    } //=============================================================================================================
    
    // Read root inode
    inode_t root_inode;
    if (read_inode(input_fp, &sb, ROOT_INO, &root_inode) != 0) {
        printf("Error in reading root inode\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    // Adding directory entry for the new file
    if (add_directory_entry(input_fp, &sb, &root_inode, free_inode + 1, 1, file) != 0) {
        printf("Error in adding directory entry of the new file to add\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    //As new file adds a link to the root directory
    //Updating root inode link count 
    root_inode.links++;
    
    // Write updated (increament of root_inode.links) root inode
    inode_crc_finalize(&root_inode);
    if (write_inode(input_fp, &sb, ROOT_INO, &root_inode) != 0) {
        printf("Error in writing root inode in the img file\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    // Updating superblock modification time
    sb.mtime_epoch = time(NULL);
    superblock_crc_finalize(&sb);
    
    // Writing updated superblock
    if (fseek(input_fp, 0, SEEK_SET) != 0) {
        printf("Error in seeking to superblock to update the modification time\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    if (fwrite(&sb, BS, 1, input_fp) != 1) {
        printf("Error in writing the update of modification time in superblock\n");
        fclose(file_fp);
        fclose(input_fp);
        exit(1);
    }
    
    // Cleaning up by closing the opened files
    fclose(file_fp);
    fclose(input_fp);
    
    printf("File '%s' added successfully to inode %d\n", file, free_inode + 1);
    
    return 0;
}


