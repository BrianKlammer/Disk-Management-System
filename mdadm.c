#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "net.h"

uint32_t format_operation(jbod_cmd_t cmd, int diskID, int blockID) { //formats a 32-bit integer to represent a JBOD operation in accordance with the JBOD operation format
     uint32_t operation = cmd;
     operation <<= 26; //bits 26-31 represent the desired JBOD command
     diskID <<= 22; //bits 22-25 represent the desired DiskID
     operation |= diskID;
     operation |= blockID; //bits 0-7 represent the desired blockID
     return operation;
}

int mdadm_mount(void) { //mounts the linear device
     uint32_t operation = format_operation(JBOD_MOUNT, 0, 0);
     if(jbod_client_operation(operation, NULL) == 0)
     {
          return 1;
     }
     return -1;
}

int mdadm_unmount(void) { //unmounts the linear device
     uint32_t operation = format_operation(JBOD_UNMOUNT, 0, 0);
     if(jbod_client_operation(operation, NULL) == 0)
     {
	     return 1;
     }
     return -1;
}

void translate_address(uint32_t linear_addr, int *disk_num, int *block_num, int *offset) { //calculates the specific DiskID, BlockID, and Offset of a given linear address
     *disk_num = linear_addr / JBOD_DISK_SIZE;
     *block_num = (linear_addr % JBOD_DISK_SIZE) / JBOD_NUM_BLOCKS_PER_DISK;
     *offset = (linear_addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE;
}

int seek(int disk_num, int block_num) { //seeks to a specific DiskID and BlockID
     assert(disk_num >= 0 && disk_num < JBOD_NUM_DISKS);
     assert(block_num >= 0 && block_num < JBOD_NUM_BLOCKS_PER_DISK);
     uint32_t operation = format_operation(JBOD_SEEK_TO_DISK, disk_num, 0);
     if(jbod_client_operation(operation, NULL) != 0) //seeks to DiskID
     {
          return -1;
     }
     operation = format_operation(JBOD_SEEK_TO_BLOCK, 0, block_num);
     if(jbod_client_operation(operation, NULL) != 0) //seeks to BlockID
     {
          return -1;
     }
     return 1;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) { //reads len bytes into the given buffer starting from the provided address
     
     if((addr + len) > 1048576 || addr < 0 || len < 0 || len > 1024 || (buf == NULL && len != 0)) //checks for invalid parameters
     {
	     return -1;
     }
     int diskID;
     int blockID;
     int offset;
     translate_address(addr, &diskID, &blockID, &offset);
     uint8_t buf1[JBOD_BLOCK_SIZE]; //temporary buffer
     uint32_t bytes_read = 0; //number of bytes already read
     uint32_t bytes_to_read = len; //number of bytes still left to read
     uint32_t operation = format_operation(JBOD_READ_BLOCK, 0, 0);
     int originalDiskID = diskID;
     bool new_seek = false;
     if(seek(diskID, blockID) != 1) //seeks to the desired address
     {
          return -1;
     }
     while(bytes_read != len)
     {
          if(cache_enabled()) //when cache is enabled
          {
               if(cache_lookup(diskID, blockID, buf1) != 1) //block not in cache
               {
                    if(new_seek == true || (originalDiskID != diskID))
                    {
                         if(seek(diskID, blockID) != 1) //seeks to the desired address
                         {
                              return -1;
                         }
                         originalDiskID = diskID;
                         new_seek = false;

                    }
                    if(jbod_client_operation(operation, buf1) != 0) //reads the desired block and stores its contents in buf1
                    {
                         return -1;
                    }
                    if(cache_insert(diskID, blockID, buf1) != 1) //inserts block into cache
                    {
                         return -1;
                    }
               }
               else
               {
                    new_seek = true;
               }
          }
          else //when cache is not enabled
          {
               if(originalDiskID != diskID)
               {
                    if(seek(diskID, blockID) != 1) //seeks to the desired address
                    {
                         return -1;
                    }
                    originalDiskID = diskID;
               }
               if(jbod_client_operation(operation, buf1) != 0) //reads the desired block and stores its contents in buf1
               {
                    return -1;
               }
          }
          if(offset + bytes_to_read > JBOD_BLOCK_SIZE) //when a read crosses blocks
          {
               memcpy(buf + bytes_read, buf1 + offset, JBOD_BLOCK_SIZE - offset); //copies or appends the contents of buf1 to buf
               bytes_read += (JBOD_BLOCK_SIZE - offset);
               bytes_to_read -= (JBOD_BLOCK_SIZE - offset);
               translate_address(addr + bytes_read, &diskID, &blockID, &offset); //to obtain a new diskID if the read ever crosses disks
          }
          else
          {
               memcpy(buf + bytes_read, buf1 + offset, bytes_to_read); //copies or appends the contents of buf1 to buf
               bytes_read += bytes_to_read;
               bytes_to_read -= bytes_to_read;
          }
     }
     return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
     if((addr + len) > 1048576 || addr < 0 || len < 0 || len > 1024 || (buf == NULL && len != 0)) //checks for invalid parameters
     {
          return -1;
     }
     int diskID;
     int blockID;
     int offset;
     translate_address(addr, &diskID, &blockID, &offset);
     uint8_t buf1[JBOD_BLOCK_SIZE]; //temporary buffer
     uint32_t bytes_written = 0; //number of bytes already written
     uint32_t bytes_to_write = len; //number of bytes still left to write
     uint32_t operation = format_operation(JBOD_WRITE_BLOCK, 0, 0);
     while(bytes_written != len)
     {
          mdadm_read((addr + bytes_written) - offset, JBOD_BLOCK_SIZE, buf1); //copies the current state of the desired block to the temp buffer
          if(seek(diskID, blockID) != 1) //seeks back to the desired address after the read from the previous line
          {
               return -1;
          }
          if(offset + bytes_to_write > JBOD_BLOCK_SIZE) //when a write crosses blocks
          {
               memcpy(buf1 + offset, buf + bytes_written, JBOD_BLOCK_SIZE - offset); //copies the desired write contents to the temp buffer
               if(jbod_client_operation(operation, buf1) != 0) //writes to the desired address
               {
                    return -1;
               }
               if(cache_enabled()) //when cache is enabled
               {
                    cache_update(diskID, blockID, buf1);
               }
               bytes_written += (JBOD_BLOCK_SIZE - offset);
               bytes_to_write -= (JBOD_BLOCK_SIZE - offset);
               translate_address(addr + bytes_written, &diskID, &blockID, &offset); //to obtain a new diskID if the write ever crosses disks
          }
          else
          {
               memcpy(buf1 + offset, buf + bytes_written, bytes_to_write); //copies the desired write contents to the temp buffer
               if(jbod_client_operation(operation, buf1) != 0) //writes to the desired address
               {
                    return -1;
               }
               if(cache_enabled()) //when cache is enabled
               {
                    cache_update(diskID, blockID, buf1);
               }
               bytes_written += bytes_to_write;
               bytes_to_write -= bytes_to_write;
          }
     }
     return len;
     }
