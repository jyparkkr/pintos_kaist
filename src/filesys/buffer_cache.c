#include "filesys/buffer_cache.h"
#include "threads/palloc.h"
#include <string.h>
#include <debug.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/synch.h"
#include <stdio.h> 
#include <stdlib.h>

/*BUFFER_CACHE_ENTRY_NB : number of buffer cache entry which is 64 (32kb)*/
#define BUFFER_CACHE_ENTRY_NB 64
/* *p_buffer_cache : pointing buffer cache memory space*/
//static void* p_buffer_cache;
static char p_buffer_cache[BUFFER_CACHE_ENTRY_NB * BLOCK_SECTOR_SIZE];
/*buffer_head[]: buffer head array*/
static struct buffer_head buffer_head[BUFFER_CACHE_ENTRY_NB];
/*clock_hand : variable for clock algorithm when selecting victim entry*/
static struct buffer_head *clock_hand;


void bc_init(void) {
	//printf("111\n");
  	struct buffer_head *head;
  	int i=0;
  	//p_buffer_cache = malloc(BUFFER_CACHE_ENTRY_NB*BLOCK_SECTOR_SIZE*sizeof(char));
  	void* cache=p_buffer_cache;
  	for(i=0;i<BUFFER_CACHE_ENTRY_NB;i++){
		/* Allocation buffer cache in Memory */
		head = &buffer_head[i];
		memset(head,0, sizeof (struct buffer_head));
		/* 전역변수 buffer_head 자료구조 초기화 */
  		lock_init (&head->lock);
		head->clock = false;
		head->dirty = false;
		head->used = false;
		/* p_buffer_cache가 buffer cache 영역 포인팅 */
  		head->buffer = cache; 
  		cache += BLOCK_SECTOR_SIZE;
	}
  	clock_hand = buffer_head;
}

void bc_term(void)
{
	//printf("222\n");
	bc_flush_all_entries();
	//free(p_buffer_cache);

/* bc_flush_all_entries 함수를 호출하여 모든 buffer cache
entry를 디스크로 flush */
/* buffer cache 영역 할당 해제 */
} 

struct buffer_head* bc_select_victim (void) {
	//printf("333\n");
	struct buffer_head *check_clock;
	check_clock = clock_hand;
	
  	for (;clock_hand != buffer_head + BUFFER_CACHE_ENTRY_NB;clock_hand++)
    {
        lock_acquire (&clock_hand->lock);
        if (!clock_hand->used)
        {
            return clock_hand;
        }
        else{
        	if(!clock_hand->clock){
        		if(clock_hand->dirty){
		        	bc_flush_entry(clock_hand);
		        	clock_hand->clock = true;
		        	return clock_hand;
		        }
        	
	        	else if(!clock_hand->dirty){
		        	bc_flush_entry(clock_hand);
	        		clock_hand->clock = true;
	        		return clock_hand;
	        	}
	        }
	        else
	        	clock_hand->clock = false;
        }
        lock_release (&clock_hand->lock); 
    }
    clock_hand = buffer_head;
    for (; clock_hand != buffer_head + BUFFER_CACHE_ENTRY_NB;clock_hand++)
    {
        lock_acquire (&clock_hand->lock);
        if (!clock_hand->used)
        {
            return clock_hand;
        }
        else{ 
        	if(!clock_hand->clock){
	        	if(clock_hand->dirty){
		        	bc_flush_entry(clock_hand);
		        	clock_hand->clock = true; 
		        	return clock_hand;
	        	}
	        	
	        	else{
		        	bc_flush_entry(clock_hand);
	        		clock_hand->clock = true; 
	        		return clock_hand;
	        	}
        	}
        	else
        		clock_hand->clock = false;
        }
        lock_release (&clock_hand->lock); 
    }
    /*should not reach here*/
    clock_hand = buffer_head;
    return clock_hand;
}
/* clock 알고리즘을 사용하여 victim entry를 선택 */
/* buffer_head 전역변수를 순회하며 clock_bit 변수를 검사 */
/* 선택된 victim entry가 dirty일 경우, 디스크로 flush */
/* victim entry에 해당하는 buffer_head 값 update */
/* victim entry를 return */

struct buffer_head* bc_lookup (block_sector_t sector){
	//printf("444\n");
  	struct buffer_head *head;
  	int i=0;
  	for(i=0;i<BUFFER_CACHE_ENTRY_NB;i++){
  		head=&buffer_head[i];
      	if (head->used && head->disk_addr == sector)
        {
          	// 캐시 적중 상황입니다.
          	// 데이터에 접근하기 전에 더 구체적인 락을 획득하고,
          	lock_acquire (&head->lock);
          	return head;
        }
    }
  	return NULL;
/* buffe_head를 순회하며, 전달받은 sector 값과 동일한
sector 값을 갖는 buffer cache entry가 있는지 확인 */
/* 성공 : 찾은 buffer_head 반환, 실패 : NULL */
} 


void bc_flush_entry (struct buffer_head *p_flush_entry)
{
	////printf("555\n");
	//if (!p_flush_entry->used || !p_flush_entry->dirty)
    //	return;
  	p_flush_entry->dirty = false;
  	block_write (fs_device, p_flush_entry->disk_addr, p_flush_entry->buffer);
/* block_write을 호출하여, 인자로 전달받은 buffer cache entry
의 데이터를 디스크로 flush */
/* buffer_head의 dirty 값 update */
}

void bc_flush_all_entries (void){
	//printf("666\n");
	struct buffer_head *head;
  	int i=0;
  	for(i=0;i<BUFFER_CACHE_ENTRY_NB;i++){
  		head=&buffer_head[i];
    	if(head->dirty && head->used){
    		lock_acquire(&head->lock);
    		bc_flush_entry(head);
    		head->used = false;
    		head->clock = false;
    		lock_release(&head->lock);
    	}
    }
/* 전역변수 buffer_head를 순회하며, dirty인 entry는
block_write 함수를 호출하여 디스크로 flush */
/* 디스크로 flush한 후, buffer_head의 dirty 값 update */
}

bool bc_read (block_sector_t sector_idx, void *buffer,\
 off_t bytes_read, int chunk_size, int sector_ofs)
{
	//printf("777\n");
	//printf("%s\n",(char*)buffer);
	struct buffer_head *bf_head;
	/* sector_idx를 buffer_head에서 검색 (bc_lookup 함수 이용) */
	if (!(bf_head = bc_lookup (sector_idx)))
    {
    	/* 검색 결과가 없을 경우, 디스크 블록을 캐싱 할 buffer entry의
		buffer_head를 구함 (bc_select_victim 함수 이용) */
    	bf_head = bc_select_victim();
    	bf_head->dirty=false;
    	bf_head->disk_addr = sector_idx;
		/* block_read 함수를 이용해, 디스크 블록 데이터를 buffer cache
		로 read */
      	block_read (fs_device, sector_idx, bf_head->buffer);
    }
    /* buffer_head의 clock bit을 setting */
	bf_head->clock = true;
    bf_head->used=true;
	/* memcpy 함수를 통해, buffer에 디스크 블록 데이터를 복사 */
	memcpy (buffer + bytes_read, bf_head->buffer + sector_ofs, chunk_size);
	lock_release(&bf_head->lock);
	
	return true;
}

bool bc_write (block_sector_t sector_idx, void *buffer,\
 off_t bytes_written, int chunk_size, int sector_ofs)
{

	//printf("888\n");
	bool success = false;
	
	struct buffer_head *bf_head;
	/* sector_idx를 buffer_head에서 검색하여 buffer에 복사(구현)*/
	/* sector_idx를 buffer_head에서 검색 (bc_lookup 함수 이용) */
	if (!(bf_head = bc_lookup (sector_idx)))
    {
    	/* 검색 결과가 없을 경우, 디스크 블록을 캐싱 할 buffer entry의
		buffer_head를 구함 (bc_select_victim 함수 이용) */
    	bf_head = bc_select_victim();
    	clock_hand++;
    	if(clock_hand = buffer_head + BUFFER_CACHE_ENTRY_NB)
		{
			clock_hand = buffer_head;
		}	
    	//bc_flush_entry (bf_head);
    	bf_head->used=true;
    	bf_head->disk_addr = sector_idx;
		/* block_read 함수를 이용해, 디스크 블록 데이터를 buffer cache
		로 read */
		//printf("999\n");
      	block_read (fs_device, sector_idx, bf_head->buffer);
    }
	bf_head->clock = true;
	bf_head->dirty = true;
    bf_head->used=true;
	/* update buffer head (구현) */
	memcpy (bf_head->buffer + sector_ofs, buffer + bytes_written, chunk_size);
	//printf("999-1\n");
	/* buffer_head의 clock bit을 setting */
	lock_release (&bf_head->lock);
	success=true;
	return success;
}
