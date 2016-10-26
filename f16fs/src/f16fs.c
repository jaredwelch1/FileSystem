#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "f16fs.h"
#include "block_store.h"
#define INODE_SIZE 64
#define INODE_COUNT 256 //this should be fine for formatting the first few blocks I think?
	 				  //64 byte inode, 512 byte block, so 8 inodes per block? 
#define INODE_BLOCK_COUNT 32
typedef struct F16FS {
	file_descriptor_t file_descriptor_table[256];
	block_store_t *bs;	
} F16FS_t;

typedef struct inode {
	char meta[48];
	short directPtrs[6];
	short indirectOne; //index for inode
	short indirectTwo; //index for inode
} inode_t;		//tested this, comes out to 64 bytes


void test_inode_size(){
	printf("%d", (int)sizeof(inode_t));
}	
//Mount assumes the file exists with data for a formatted block store
//for format, I dont think I will call mount, as the logic for mounting after formatting it would do a lot 
//of unnecessary logic I think

F16FS_t *fs_format(const char *path){
	if (path == NULL)
			return NULL;
	size_t i = 0;
	while(path[i] != '\0'){
		if(path[i] == '\n')
			return NULL;
			i++;
		
	}
	
	block_store_t *bs = block_store_create(path);
	
	if (bs == NULL)
		return NULL;
	
	//now we have a block store created at file, so, we must format the first 32 blocks to be inodes
	
	for ( i = 17; i < INODE_BLOCK_COUNT + 16; i++){ //start at 17 since we cant use the first couple blocks 
		inode_t block_format[8]; //8 inode per block
		if( !block_store_request((block_store_t *const)bs, (const unsigned)i) ){
			printf("\nThe for loop is at %d", (int)i);
			return NULL;		
		} 

		//so, if we get here, we have allocated a certain block, so if we write to it, we can format it to an inode?
		//will write an array of 8 inodes to each block, then when referring to specific inodes, will use that index
		//to determine which block and which inode within that block

		//first create a buffer, will free it before we finish of course, just going to alloc before the loop
		//since block store uses mem copy it creates it own copy of it so we dont need to alloc every time, just once in the right
		//format then copy that format to each block
		if ( ! block_store_write( (block_store_t *const)bs, (const unsigned) i, (const void *const)block_format) )
			return NULL;
	}		
	//if we made it here, we have formatted the block store, so all that is left is to create the FS object, fill it, then return it
	F16FS_t *fs = (F16FS_t*)malloc(sizeof(F16FS_t) );

	if (fs == NULL)
		return NULL;

	fs->bs = bs;

	return fs;
}

F16FS_t *fs_mount(const char *path){
	if (path == NULL)
			return NULL;
	
	uint32_t i = 0;
	
	while(path[i] != '\0'){
	if(path[i] == '\n')
		return NULL;
		i++;
	}
	int fileRef = open(path, O_RDONLY);
	//couldnt think of a better way to check if file existed or not
	if (fileRef == -1){	
		return NULL;
	} else {
		close(fileRef);
	}

	block_store_t *bs = block_store_open(path);
	
	if (bs == NULL)
		return NULL;

	F16FS_t *fs = (F16FS_t*)malloc(sizeof(F16FS_t) );
	
	if (fs == NULL)
		return NULL;

	fs->bs = bs;

	//since the file itself should have been a block store that is formatted correctly, I think we are done? 
	
	return fs;
}

int fs_unmount(F16FS_t *fs){
	if (fs == NULL)
		return -1;
	block_store_close(fs->bs);		
	free(fs);

	return 0;
}
