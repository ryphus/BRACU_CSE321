// validator_public.c — minimal MiniVSFS checks
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u

// ---- CRC32 ----
static uint32_t T[256];
static void crc32_init(void){
  for (uint32_t i=0;i<256;i++){ uint32_t c=i; for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1); T[i]=c; }
}
static uint32_t crc32(const void* buf, size_t n){
  const uint8_t* p = (const uint8_t*)buf; uint32_t c=0xFFFFFFFFu;
  for(size_t i=0;i<n;i++) c = T[(c^p[i])&0xFF] ^ (c>>8);
  return c ^ 0xFFFFFFFFu;
}

// ---- On-disk structs ----
#pragma pack(push,1)
typedef struct {
  uint32_t magic, version, block_size;
  uint64_t total_blocks, inode_count;
  uint64_t ibm_start, ibm_blocks;
  uint64_t dbm_start, dbm_blocks;
  uint64_t itbl_start, itbl_blocks;
  uint64_t data_start, data_blocks;
  uint64_t root_inode, mtime_epoch;
  uint32_t flags, checksum;
} superblock_t;

typedef struct {
  uint16_t mode, links; uint32_t uid, gid;
  uint64_t size_bytes, atime, mtime, ctime;
  uint32_t direct[12]; uint32_t indirect1;
  uint32_t r0, r1, proj_id, uid16_gid16;
  uint64_t xattr_ptr, inode_crc;
} inode_t;

typedef struct {
  uint32_t ino; uint8_t type; char name[58]; uint8_t checksum;
} dirent64_t;
#pragma pack(pop)

static void die(const char* msg){ fprintf(stderr,"[FAIL] %s\n", msg); }
static void ok(const char* msg){  fprintf(stdout,"[ OK ] %s\n", msg); }

static uint8_t dirent_checksum(const dirent64_t* de){
  const uint8_t* p=(const uint8_t*)de; uint8_t x=0; for(int i=0;i<63;i++) x^=p[i]; return x;
}

int main(int argc, char** argv){
  if(argc!=2){ fprintf(stderr,"Usage: %s out.img\n", argv[0]); return 2; }
  FILE* f=fopen(argv[1],"rb"); if(!f) die("open image");

  crc32_init();

  // read superblock
  uint8_t sbraw[BS]; if(fread(sbraw,1,BS,f)!=BS) die("read superblock");
  superblock_t sb; memcpy(&sb, sbraw, sizeof sb);

  // basic fields
  if(sb.magic != 0x4D565346u) die("bad magic");
  if(sb.version != 1) die("bad version");
  if(sb.block_size != BS) die("bad block_size");
  ok("superblock header fields");

  // checksum verify
  uint32_t saved = sb.checksum;
  ((superblock_t*)sbraw)->checksum = 0;
  uint32_t got = crc32(sbraw, BS-4); // checksum field last
  if(saved != got) die("superblock checksum mismatch");
  ok("superblock checksum");

  // region sanity
  uint64_t tb = sb.total_blocks;
  if(!(sb.ibm_start==1)) die("ibm start");
  if(sb.itbl_start >= tb || sb.data_start >= tb) die("region bounds");
  if(sb.ibm_start + sb.ibm_blocks > sb.dbm_start) die("region overlap 1");
  if(sb.dbm_start + sb.dbm_blocks > sb.itbl_start) die("region overlap 2");
  if(sb.itbl_start + sb.itbl_blocks > sb.data_start) die("region overlap 3");
  if(sb.data_start + sb.data_blocks > tb) die("data region overflow");
  ok("region layout");

  // read inode #1 (root)
  if(fseek(f, (__off_t)(sb.itbl_start*BS + (ROOT_INO-1)*INODE_SIZE), SEEK_SET)!=0) die("seek itbl");
  inode_t root; if(fread(&root,1,sizeof root,f)!=sizeof root) die("read root inode");

  // inode CRC
  uint8_t tmp[INODE_SIZE]; memcpy(tmp,&root,INODE_SIZE); memset(&tmp[120],0,8);
  uint32_t icrc = crc32(tmp,120);
  if(((uint32_t)root.inode_crc) != icrc) die("root inode crc");
  ok("root inode crc");

  // root must be a dir, links>=2, at least one data block
  if((root.mode & 0040000)==0) die("root not directory");
  if(root.links < 2) die("root.links < 2");
  if(root.direct[0]==0) die("root has no data block");
  ok("root inode basic fields");

  // read root dir block
  uint32_t rblk = root.direct[0];
  if(rblk < sb.data_start || rblk >= sb.total_blocks) die("root block out of range");
  if(fseek(f, (__off_t)rblk*BS, SEEK_SET)!=0) die("seek root block");
  uint32_t entries = root.size_bytes / sizeof(dirent64_t);
  dirent64_t de[entries]; if(fread(de,1,sizeof de,f)!=sizeof de) die("read dir entries");

  // check "." entry
  if(de[0].ino != ROOT_INO || de[0].type != 2 || strcmp(de[0].name,".")!=0) die("bad '.'");
  if(de[0].checksum != dirent_checksum(&de[0])) die("bad '.' checksum");

  // check ".." entry
  if(de[1].ino != ROOT_INO || de[1].type != 2 || strcmp(de[1].name,"..")!=0) die("bad '..'");
  if(de[1].checksum != dirent_checksum(&de[1])) die("bad '..' checksum");
  ok("root directory has '.' and '..'");
  
  // check other entries
  for(int i = 2; i < entries; i++) {
    printf("[INFO] entry found. name: %s\n", de[i].name);
  }

  // spot-check bitmaps reflect allocations:
  // - inode bitmap bit 0 (inode #1) should be set
  if(fseek(f, (__off_t)sb.ibm_start*BS, SEEK_SET)!=0) die("seek ibm");
  uint8_t ib[1]; if(fread(ib,1,1,f)!=1) die("read ibm byte");
  if( (ib[0] & 0x01) == 0 ) die("inode #1 bit not set");
  ok("inode bitmap marks inode #1");

  // - data bitmap bit for root_data_rel should be set
  uint64_t root_rel = (uint64_t)rblk - sb.data_start;
  if(fseek(f, (__off_t)sb.dbm_start*BS + (root_rel>>3), SEEK_SET)!=0) die("seek dbm");
  uint8_t db; if(fread(&db,1,1,f)!=1) die("read dbm byte");
  if( (db & (1u << (root_rel & 7))) == 0 ) die("root data block not marked allocated");
  ok("data bitmap marks root data block");

  puts("[PASS] Basic MiniVSFS checks OK.");

  // read second inode

  return 0;
}
