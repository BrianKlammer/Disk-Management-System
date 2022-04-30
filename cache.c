#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
	if(cache_size != 0 || num_entries < 2 || num_entries > 4096)
  	{
    	return -1;
  	}
  	cache_size = num_entries;
  	cache = calloc(cache_size, sizeof(cache_entry_t));
	if(cache == NULL)
	{
		return -1;
	}
  	return 1;
}

int cache_destroy(void) {
	if(cache_size == 0)
  	{
    	return -1;
  	}
  	free(cache);
  	cache = NULL;
  	cache_size = 0;
  	return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
	if(cache_size == 0 || buf == NULL || cache->valid != true || disk_num < 0 || disk_num >= JBOD_NUM_DISKS || block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK)
	{
		return -1;
	}
	num_queries++;
	for(int counter = 0; counter < cache_size; counter++)
	{
		if((cache + counter)->valid == true && (cache + counter)->disk_num == disk_num && (cache + counter)->block_num == block_num)
		{
			memcpy(buf, (cache + counter)->block, JBOD_BLOCK_SIZE);
			num_hits++;
			(cache + counter)->access_time = clock;
			clock++;
			return 1;
		}
	}
	return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
	for(int counter = 0; counter < cache_size; counter++)
	{
		if((cache + counter)->valid == true && (cache + counter)->disk_num == disk_num && (cache + counter)->block_num == block_num)
		{
			memcpy((cache + counter)->block, buf, JBOD_BLOCK_SIZE);
			(cache + counter)->access_time = clock;
			clock++;
			return;
		}
	}
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
	if(cache_size == 0 || buf == NULL || disk_num < 0 || disk_num >= JBOD_NUM_DISKS || block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK)
	{
		return -1;
	}
	if((cache + cache_size - 1)->valid == false) //cache is not full
	{
		for(int counter = 0; counter < cache_size; counter++)
		{
			if((cache + counter)->valid == true && (cache + counter)->disk_num == disk_num && (cache + counter)->block_num == block_num)
			{
				return -1; //block already exists in cache
			}
			if((cache + counter)->valid == false)
			{
				(cache + counter)->valid = true;
				(cache + counter)->disk_num = disk_num;
				(cache + counter)->block_num = block_num;
				memcpy((cache + counter)->block, buf, JBOD_BLOCK_SIZE);
				(cache + counter)->access_time = clock;
				clock++;
				return 1;
			}
		}
	}
	else //cache is full
	{
		int LRU_index = 0; //index of least recently used cache element
		int LRU_time = clock;
		for(int counter = 0; counter < cache_size; counter++)
		{
			if((cache + counter)->valid == true && (cache + counter)->disk_num == disk_num && (cache + counter)->block_num == block_num)
			{
				return -1; //block already exists in cache
			}
			if((cache + counter)->access_time < LRU_time)
			{
				LRU_index = counter;
				LRU_time = (cache + counter)->access_time;
			}
		}
		(cache + LRU_index)->disk_num = disk_num;
		(cache + LRU_index)->block_num = block_num;
		memcpy((cache + LRU_index)->block, buf, JBOD_BLOCK_SIZE);
		(cache + LRU_index)->access_time = clock;
		clock++;
		return 1;
	}
	return -1; //insert fails
}

bool cache_enabled(void) {
	if(cache_size == 0)
	{
		return false;
	}
  	return true;
}

void cache_print_hit_rate(void) {
	fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
