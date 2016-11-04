#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "f16fs.h"
#include "block_store.h"

#define INODE_SIZE 64
#define INODE_COUNT 256 //this should be fine for formatting the first few blocks I think?
	 				  //64 byte inode, 512 byte block, so 8 inodes per block? 

#define INODE_BLOCK_COUNT 32
#define BLOCK_BYTE_COUNT 512
#define FS_NAME_MAX 64

bool write_inode(F16FS_t *, int, inode_t*); 

typedef struct F16FS {
	file_descriptor_t file_descriptor_table[256];
	block_store_t *bs;	
} F16FS_t;

//int is size 4 bytes i checked
//enum for file type is 4 bytes
typedef struct inode {
	char meta[36];
	int refCount;
	unsigned int file_size;
	file_t type;
	short directPtrs[6];
	short indirectOne; //index for inode
	short indirectTwo; //index for inode
} inode_t;		//tested this, comes out to 64 bytes


typedef struct direct_entr {
	char fname[64]; //sloppy
	int inode_index;
} directory_entry_t;
//limit to 7 directory entries per directory file data

void test_inode_size(){
	printf("%d", (int)sizeof(inode_t));
}	
//Mount assumes the file exists with data for a formatted block store
//for format, I dont think I will call mount, as the logic for mounting after formatting it would do a lot 
//of unnecessary logic I think

F16FS_t *fs_format(const char *path){
	//printf("\nsize of enum: %d" , (int)sizeof(inode_t));
	
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
	
	for ( i = 16; i < INODE_BLOCK_COUNT + 16; i++){ //start at 16 since we cant use the first 16(0-15) blocks 
		inode_t block_format[8]; //8 inode per block
		int j = 0;
		for (j = 0; j < 8; j++)
				block_format[j].refCount = -1;
		if( !block_store_request((block_store_t *const)bs, (const unsigned)i) ){
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
	
	//Make first inode the root directory 
	//first inode is in block 17, first inode in that block
	//The inode will point to an open datablock, maybe just 48
	//block will contain directory entries
	inode_t root;
	root.type = FS_DIRECTORY;
	root.file_size = 512; //only going to point to one block since it is directory
	root.directPtrs[0] = 48; //points to first block because why not
	root.refCount = 1;
	inode_t temp[8]; //will be temp storage for formatted block
	if(!block_store_read((block_store_t *const)bs, (const unsigned) 16, (void *const)temp))
		return NULL;
	temp[0] = root; //put inode 0 to be root directory inode 

	if(!block_store_write( (block_store_t *const)bs, (const unsigned) 16, (const void *const)temp))
		return NULL;			
	//inode written, now we have to format the block we pointed to in the inode to be array of directory entries

	directory_entry_t directory_data[7];
	
	for (i = 0; i < 7; i++){
		directory_data[i].inode_index = -1;
		directory_data[i].fname[0] = '\0';
	}
	void* temp_block = &directory_data;
	if (!block_store_write((block_store_t *const)bs, (const unsigned) 48, (const void *const)temp_block))
		return NULL;
	
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


int fs_create(F16FS_t *fs,  const char *path, file_t type){
	if (path == NULL || fs == NULL)
		return -1;	
	if( type != FS_DIRECTORY && type != FS_REGULAR )
		return -1; 
	
	int index = creation_traversal(fs, path);
	if (index == -1)
		return -1;	
	//here we SHOULD have the index of the parent inode, so we just need to make sure the file does not
	//exist already before we create it
	int i = 0;
	int path_len = 0;
	while (path[i] != (char)0)
		i++;	
	int j = 0;
	path_len = i;
	i = path_len - 1; //index of last char is length - 1
	char fname[FS_NAME_MAX];
	while (path[i] != '/'){
		j++;
		i--;
	}
	int fn_len = j;
	//now j is fname length
	
	for (j = 0, i = path_len - fn_len; j < fn_len; j++, i++){
		fname[j] = path[i];
	}
	fname[fn_len] = '\0';
	//Now we have the fname we want to create LOL
	
	//so, traverse path should have given parent dir, so we make sure that our directory does not have it
	//already
	inode_t node_t;
	inode_t *node = &node_t;
	get_inode(fs, index, node);
	
	if(node->type != FS_DIRECTORY)
		return -1;
	int block_ind = node->directPtrs[0];
	//int offset = index % 8;	
	directory_entry_t *entries;
	char tmp_block[512];
	block_store_read(fs->bs, block_ind, tmp_block);
	entries = (directory_entry_t*)tmp_block;
	int freeDir = -1;
	for ( i = 0; i < 7; i++ ){
		if(strcmp(entries[i].fname, fname) == 0)
			return -1;
		if( freeDir < 0 && entries[i].inode_index < 0) //free dir
			 freeDir = i;		
	}

	if (freeDir < 0) //no free spots
		return -1;

	//now let's create it 
	
	inode_t new_t, temp_t;
	new_t.refCount = -1;
	inode_t *new = &new_t;
	inode_t *temp = &temp_t;
	int newInodeIndex = -1;
	//find empty inode 
	for(i = 1; i < 256; i++){
		get_inode(fs, i, temp);	
		if (temp->refCount < 0){
			new = temp;
			new->refCount = 1;
			newInodeIndex = i;
			i = 256;
		}
	}
	//we have empty inode, so let's make it a file
	// need to init its meta data, if its a dir then need to give it a block of dir entries
	// need to set this new file inside old one, using freeDir will give index of direct entry of parent that
	// can be used for new inode
	

	if (new->refCount < 0)
		return -1;

	new->type = type;
	
	if (type == FS_REGULAR){
		new->file_size = 0;
		//can leave its block pointers alone
		//done?
		//call inode_write() which will place new inode into inode table
		write_inode(fs, newInodeIndex, new);
		//success, then we done
	} else { //has to be directory, checked that at beginning
		new->file_size = 512;
		//need free block
		int blockID = block_store_allocate(fs->bs);
		if (blockID < 1)
			return -1; //out of blocks 
		new->directPtrs[0] = blockID;
		write_inode(fs, newInodeIndex, new);
		
		directory_entry_t directory_data[7];
	
		for (i = 0; i < 7; i++){
			directory_data[i].inode_index = -1;
			directory_data[i].fname[0] = '\0';
		}
		block_store_write(fs->bs, blockID, &directory_data);
		//set directpointer to free block
		//write to inode table
		//success 
	}
	//before we finish, need to add the new file/directory into the parent directory entries
	//if we got here, we should have a spot for it
	strcpy(entries[freeDir].fname, fname);
	entries[freeDir].inode_index = newInodeIndex;

	block_store_write(fs->bs, block_ind, entries);
	return 0;
}

int fs_open(F16FS_t *fs, const char *path){
	if (fs == NULL || path == NULL)
		return -1;		

	//traverse_path(fs, path);
	return -1;
}

int fs_close(F16FS_t *fs, int fd){
	if (fd < 0 || fd > 255 || fs == NULL)
		return -1;
	return -1;
}


dyn_array_t *fs_get_dir(F16FS_t *fs, const char *path){
	if (fs == NULL || path == NULL)
		return NULL;
	return NULL;
}


//helper function, returns inode of last file in path
//-1 on error
//i copied this kind of from Will,
//Add a 3rd param, bool, if true, do one thing,
//if false, do another
//when we call a wrapper, the wrapper will call with a certain bool
//logic is same for both so it should be good

//if existingFile is true, we are calling this function with an existing file, so the last 
//name in the list will be needed. otherwise, we want to return the parent so we can create the
//new file inside


int traverse_path(F16FS_t *fs, const char *path, bool existingFile){
	if (fs == NULL || path == NULL || path[0] != '/')
		return -1;
	//if we get here, starts with root, so lets move forward with that assumption.

	//parse into ordered list, so we will push and pop into dyn_array
	
	//create temp pointer to adjust as we move through the path, so first get length
	
	int i = 1;
	int j = 0;
	dyn_array_t *ordered_list = dyn_array_create( 0, sizeof(char) * FS_NAME_MAX, NULL);
	if (ordered_list == NULL)
		printf("\nDyn array is NULL");
	
	while(path[i] != (char)0){
		char *temp = (char*)malloc(sizeof(char) * FS_NAME_MAX);
		j = 0;
		while (path[i] != '/' && path[i] != (char)0){
			temp[j] = path[i];
			i++;
			j++;
			if (j > FS_NAME_MAX - 1){
				free(temp);
				dyn_array_destroy(ordered_list);
				return -1;
			}
		}
		if ( path[i] != (char)0 ){
			i++;
			temp[j+1] = '\0';
			dyn_array_push_front(ordered_list, temp);	
			free(temp);
		} else if (existingFile){
			temp[j+1] = '\0';	
			dyn_array_push_front(ordered_list, temp);
			free(temp);

		} else {
			free(temp);
		}
		
	}
	if (i <= 1){
		dyn_array_destroy(ordered_list);	
		return -1;
	}
	int nodeIndex = 0;
	char *fname;	
	while (!dyn_array_empty(ordered_list)){	
		fname = (char*)malloc(64);
		dyn_array_extract_back(ordered_list, fname);
		//printf("\n%s\n", fname);
		//block = nodeIndex / 8;
		//nodeOffset = nodeIndex % 8;
		//node temp = get_inode()
		//use inode to get to block data
		//block data has dirEntries
		//search those entries for 
		inode_t *temp = calloc(1, sizeof(inode_t));
		get_inode(fs, nodeIndex, temp);
		//have node itself, if we are here we can check for directory or filename inside
		//should be directory right?
		
		//if the array is empty, we should be at a directory or a file
		//if fileExists, then we should be at a file, 
		//if file does not exist, then we should be at a directory 
		if( dyn_array_empty(ordered_list) ){
			if( existingFile && temp->type != FS_REGULAR ){
					free(temp);
					free(fname);
					dyn_array_destroy(ordered_list);			
					return -1; 
			} else if ( !existingFile && temp->type != FS_DIRECTORY ) {
					free(temp);
					free(fname);
					dyn_array_destroy(ordered_list);
					return -1;
			}
		}
		//should be directory, if not, error
		//THINK THROUGH
		//if we are creating, then the last element is left off the path, 
		//so we should always have directory
		//we may or may not be at last element, does it matter?
		//if we are at last one, should only matter if we are going to existing file
		//so if it is last element, and it is file, we still need to search the element
		//that is popped inside the current inode
		
		int blockId = temp->directPtrs[0];
		directory_entry_t *entries;
		char tmp_block[512];
		block_store_read(fs->bs, blockId, tmp_block);
		entries = (directory_entry_t*)tmp_block;
		nodeIndex = -1;
		for (i = 0; i < 7; i++){
			if ( entries[i].inode_index != -1){
				if( strcmp(fname, entries[i].fname) == 0) //found what we want
					nodeIndex = entries[i].inode_index;
			}
		}
		if (nodeIndex == -1){ //we never found the right directory
			free(temp);
			free(fname);
			dyn_array_destroy(ordered_list);
			return -1;
		} 
	free(temp);
	free(fname);
	}
	
	dyn_array_destroy(ordered_list);
	return nodeIndex;		
}

int creation_traversal(F16FS_t *fs, const char *path){
	if (fs == NULL || path == NULL)
		return -1;

	return traverse_path(fs, path, false);
}

bool get_inode(F16FS_t *fs, int index, inode_t *node){
		//need block index and inode offset within block
		int block = index / 8;
		//block index + 17 for taken blocks.
		//0 index + 17 = first block
		//1 index + 17 = second block, 18th block number
		int offset = index % 8;

		inode_t nodes[8];
		block_store_read(fs->bs, block + 16, nodes);
		memcpy( node, nodes+offset, sizeof(inode_t));
		return true;
}

bool write_inode(F16FS_t *fs, int index, inode_t *new_node){
	int block = index / 8 + 16;
	int offset = index % 8;

	inode_t nodes[8];
	block_store_read(fs->bs, block, nodes);
	memcpy( nodes+offset, new_node, sizeof(inode_t));
	//now our block has our new inode so we write it
	block_store_write(fs->bs, block, nodes);
	return true;
}
