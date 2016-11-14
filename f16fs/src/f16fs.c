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
	
	//now we have a block store created at file, so, we must format the first 32 blocks to be inode
	for ( i = 16; i < INODE_BLOCK_COUNT + 16; i++){ //start at 16 since we cant use the first 16(0-15) blocks 
		inode_t block_format[8]; //8 inode per block
		int j = 0;
		
		for (j = 0; j < 8; j++){
				block_format[j].refCount = -1;
				int k = 0;
				for ( k = 0; k < 6; k++)
					block_format[j].directPtrs[k] = -1;
				block_format[j].indirectOne = -1;
				block_format[j].indirectTwo = -1;
		}
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
	if (!block_store_request(bs, 48) )
		return NULL;
	if (!block_store_write((block_store_t *const)bs, (const unsigned) 48, (const void *const)temp_block))
		return NULL;
	
	//if we made it here, we have formatted the block store, so all that is left is to create the FS object, fill it, then return it
	
	F16FS_t *fs = (F16FS_t*)malloc(sizeof(F16FS_t) );

	if (fs == NULL)
		return NULL;

	fs->bs = bs;	
	for (i = 0; i < 256; i++)
		fs->file_descriptor_table[i].inode_index = -1;
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
	for (i = 0; i < 256; i++){
		fs->file_descriptor_table[i].inode_index = -1;
	}
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

	int index = existing_traversal(fs, path);
	
	if (index < 0)
		return -1;

	int open_fd_index = -1;
	int i;
	for (i = 0; i < 256; i++){
		if (fs->file_descriptor_table[i].inode_index < 0){
			open_fd_index = i;
			i = 256;
		}
			
	}
	if (open_fd_index < 0)
		return -1;
	size_t offset = 0;
	file_descriptor_t temp;
	temp.inode_index = index;
	temp.offset = offset;
	
	fs->file_descriptor_table[open_fd_index] = temp;	

	inode_t node;
	get_inode(fs, index, &node);
	node.refCount++;
	write_inode(fs, index, &node);

	return open_fd_index;
}

int fs_close(F16FS_t *fs, int fd){
	if (fd < 0 || fd > 255 || fs == NULL)
		return -1;
	
	if( fs->file_descriptor_table[fd].inode_index < 0)
		return -1;
	
	inode_t node;
	get_inode(fs, fs->file_descriptor_table[fd].inode_index, &node);
	node.refCount--;
	write_inode(fs, fs->file_descriptor_table[fd].inode_index, &node);
	
	fs->file_descriptor_table[fd].inode_index = -1;
	
	
	
	return 0;
}


dyn_array_t *fs_get_dir(F16FS_t *fs, const char *path){
	if (fs == NULL || path == NULL)
		return NULL;

	int index = existing_traversal_directory(fs, path);
	

	if (path[0] == '/' && path[1] == (char)0){
		index = 0;	
	}
		if (index < 0)
		return NULL;

	
	inode_t dir;
	get_inode(fs, index, &dir);
	dyn_array_t *directories = dyn_array_create( 0, sizeof(char) * FS_NAME_MAX, NULL);	
	int i;
	int blockId = dir.directPtrs[0];
	directory_entry_t *entries;
	char tmp_block[512];
	block_store_read(fs->bs, blockId, tmp_block);
	entries = (directory_entry_t*)tmp_block;
	char fname[64];
	for (i = 0; i < 7; i++){
		if ( entries[i].inode_index != -1){
			memcpy(fname, entries[i].fname, 64);
			dyn_array_push_front( directories, fname);
		}
	}
	return directories;

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


int traverse_path(F16FS_t *fs, const char *path, bool existingFile, bool getDir){
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
			temp[j] = '\0';
			dyn_array_push_front(ordered_list, temp);	
			free(temp);
		} else if (existingFile){
			temp[j] = '\0';	
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
		 	if ( !existingFile && temp->type != FS_DIRECTORY ) {
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
				if( strcmp(fname, entries[i].fname) == 0){ //found what we want
					nodeIndex = entries[i].inode_index;
				}
			}
		}
		if (nodeIndex == -1){ //we never found the right directory
			free(temp);
			free(fname);
			dyn_array_destroy(ordered_list);
			return -1;
		} 
		
	free(fname);
	free(temp);
	}

	inode_t testNode;

	dyn_array_destroy(ordered_list);
	get_inode(fs, nodeIndex, &testNode);
	if(existingFile && !getDir && testNode.type == FS_DIRECTORY)
		nodeIndex = -1;
	else if (existingFile && getDir && testNode.type == FS_REGULAR)
		nodeIndex = -1;
	return nodeIndex;		
}

int creation_traversal(F16FS_t *fs, const char *path){
	if (fs == NULL || path == NULL)
		return -1;

	return traverse_path(fs, path, false, false);
}

int existing_traversal(F16FS_t *fs, const char *path){
	if (fs == NULL || path == NULL)
		return -1;

	return traverse_path(fs, path, true, false);
}

int existing_traversal_directory(F16FS_t *fs, const char *path){
	if (fs == NULL || path == NULL)
		return -1;

	return traverse_path(fs, path, true, true);
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

off_t fs_seek(F16FS_t *fs, int fd, off_t offset, seek_t whence){
	if (fs == NULL || fd < 0 || fs->file_descriptor_table[fd].inode_index < 0)
		return -1;
	if ( whence != FS_SEEK_SET && whence != FS_SEEK_CUR && whence != FS_SEEK_END)
		return -1;

	int inode_ind = fs->file_descriptor_table[fd].inode_index;
	inode_t node;
	get_inode(fs, inode_ind, &node);
	int startFrom;

	if (whence == FS_SEEK_CUR)
		startFrom = fs->file_descriptor_table[fd].offset;
	else if (whence == FS_SEEK_END)
		startFrom = node.file_size;
	else 
		//handle seek_set
		startFrom = 0;

	if ( offset + startFrom > node.file_size ){
		fs->file_descriptor_table[fd].offset = node.file_size;
		return node.file_size;
	} //go to end of file if too big
	if ( offset + startFrom < 0){
		fs->file_descriptor_table[fd].offset = 0;
		return 0;
	}

	fs->file_descriptor_table[fd].offset = startFrom + offset;
	return fs->file_descriptor_table[fd].offset;
	
}

ssize_t fs_read(F16FS_t *fs, int fd, void *dst, size_t nbyte){
	if (fs == NULL || fd < 0 || dst == NULL || fs->file_descriptor_table[fd].inode_index < 0)
		return -1;
	if (nbyte == 0)
		return 0;

	char temp_block[512] ={0}; 	//this will be where we put memory to be read
								//first read in a free block, memcpy to dest	

	size_t currByte = 0; 		//this will allow us to track how man bytes we have read so far, 
	size_t currOffset = 0; 		//this will tell us where we are currently at within the file as we read
	int relativeBlock = 0; 		//this will tell us what block we are at, relative to the blocks within a file
	size_t bytesLeft = 0;
	bytesLeft = nbyte;


	currOffset = fs->file_descriptor_table[fd].offset;
	//now we need the inode for the file in the fd
	int inode_ind = fs->file_descriptor_table[fd].inode_index;
	int block_index = 0;
	//check if we start in middle of block
	int block_byte_offset = currOffset % 512; //any bytes over 512 means we are inside a block 
	relativeBlock = currOffset / 512;
	if (block_byte_offset > 0){
		//we are starting inside a block, so read it in to the dest

		block_index = get_actual_block_read(relativeBlock, inode_ind, fs);
		
		if (block_index < 0)
			return -1;
		
		//we can read
		block_store_read(fs->bs, block_index, temp_block);
		memcpy(dst, temp_block + block_byte_offset, 512 - block_byte_offset);
		currByte+=(512- block_byte_offset); 	
		bytesLeft-=(512-block_byte_offset);
		currOffset+=(512-block_byte_offset);
		relativeBlock++;
	}

	//once here, we should always be starting with full block.
	bool file_not_full = true;
	while (bytesLeft > 511 && file_not_full){
		block_index = get_actual_block_read(relativeBlock, inode_ind, fs);
		
		if (block_index < 0){
			fs->file_descriptor_table[fd].offset+=currByte;	
			return currByte;
		}
		block_store_read(fs->bs, block_index, temp_block);
		memcpy(dst + currByte, temp_block, 512);
	
		currByte+=512;
		bytesLeft-=512;
		currOffset+=512;
		relativeBlock++;

		if (relativeBlock > 65797)
			file_not_full = false;
	}

	if (bytesLeft > 0 && file_not_full){
		block_index = get_actual_block_read(relativeBlock, inode_ind, fs);
		
		if (block_index < 0){
			fs->file_descriptor_table[fd].offset+=currByte;
			return currByte;
		}

		block_store_read(fs->bs, block_index, temp_block);
		memcpy(dst + currByte, temp_block, bytesLeft);
	
		currByte += bytesLeft;
		currOffset+=bytesLeft;

	}
	fs->file_descriptor_table[fd].offset+=currByte;
	return currByte;
}

// 6 + 256 + 256*256 = 65,798 max block index is 65,797 then
ssize_t fs_write(F16FS_t *fs, int fd, const void *src, size_t nbyte){
	if (fs == NULL || fd < 0 || src == NULL || fs->file_descriptor_table[fd].inode_index < 0)
		return -1;
	if (nbyte == 0)
		return 0;

	char temp_block[512] ={0}; 	//this will be where we put memory to be written
								//first read in a free block, memcpy to it, then write that block back in

	size_t currByte = 0; 		//this will allow us to track how man bytes we have written so far, 
	size_t currOffset = 0; 		//this will tell us where we are currently at within the file as we write
	int relativeBlock = 0; 		//this will tell us what block we are at, relative to the blocks within a file
	size_t bytesLeft = 0;
	bytesLeft = nbyte;
	inode_t node;

	currOffset = fs->file_descriptor_table[fd].offset;
	//now we need the inode for the file in the fd
	int inode_ind = fs->file_descriptor_table[fd].inode_index;
	int block_index = 0;
	//check if we start in middle of block
	int block_byte_offset = currOffset % 512; //any bytes over 512 means we are inside a block 
	relativeBlock = currOffset / 512;
	if (block_byte_offset > 0){
		//we are starting inside a block, so read it in the we can write to it then write to block_store

		block_index = get_actual_block_write(relativeBlock, inode_ind, fs);
		//printf("\nBLOCK INDEX: %d", block_index);		
		if (block_index < 0)
			return -1;
		
		//we can read
		block_store_read(fs->bs, block_index, temp_block);
		memcpy(temp_block + block_byte_offset, src, 512 - block_byte_offset);
		block_store_write(fs->bs, block_index, temp_block); //should be ok
		currByte+=(512- block_byte_offset); 	
		bytesLeft-=(512-block_byte_offset);
		currOffset+=(512-block_byte_offset);
		relativeBlock++;
	}

	//once here, we should always be starting with full block.
	bool file_not_full = true;
	while (bytesLeft > 511 && file_not_full){
		block_index = get_actual_block_write(relativeBlock, inode_ind, fs);
		//printf("\nBLOCK INDEX: %d", block_index);
		if (block_index < 0){
			fs->file_descriptor_table[fd].offset+=currByte;	
			get_inode(fs, inode_ind, &node);
			node.file_size += currByte;
			write_inode(fs, inode_ind, &node);
			return currByte;
		}
		memcpy(temp_block, src + currByte, 512);
		block_store_write(fs->bs, block_index, temp_block);
		currByte+=512;
		bytesLeft-=512;
		currOffset+=512;
		relativeBlock++;

		if (relativeBlock > 65797)
			file_not_full = false;
	}

	if (bytesLeft > 0 && file_not_full){
		block_index = get_actual_block_write(relativeBlock, inode_ind, fs);
		//printf("\nBLOCK INDEX: %d", block_index);
		if (block_index < 0){
			fs->file_descriptor_table[fd].offset+=currByte;
			get_inode(fs, inode_ind, &node);
			node.file_size += currByte;
			write_inode(fs, inode_ind, &node);
			return currByte;
		}

		memcpy(temp_block, src + currByte, bytesLeft);
		block_store_write(fs->bs, block_index, temp_block);
		currByte += bytesLeft;
		currOffset+=bytesLeft;

	}
	fs->file_descriptor_table[fd].offset+=currByte;
	get_inode(fs, inode_ind, &node);
	node.file_size += currByte;
	write_inode(fs, inode_ind, &node);
	return currByte;
}

int fs_remove(F16FS_t *fs, const char *path){
	if (fs == NULL || path == NULL || path[0] != '/')
		return -1;
	
	//so, first, we need to see if our path is valid, so lets get the inode it leads to.
	int index = existing_traversal(fs, path);

	//if index < 0, means we could not reach the file, or it might be a directory so try that too
	if (index < 0){
		index = existing_traversal_directory(fs, path);
		if ( index < 0 )
			return -1;
	}
	//now, we should have the inode index. So, if its a directory and not empty, error.
	//after that, the handle is the same, free all stuff for that file
	//doesn't matter if directory or not if we handled the file creation and writes properly.
	//the issue is, how do we invalidate the file_descriptors?
	//loop through all of them, then remove all of the ones for the inode index of this file?
	
	inode_t node;
	get_inode(fs, index, &node);
	uint16_t temp[256] = {0};

	//can use get dir then check the dyn array size, test does it and it passes so yeah
	if (node.type == FS_DIRECTORY){
		dyn_array_t *results = fs_get_dir(fs, path);
		if ( dyn_array_size(results) != 0 ){
		//directory not empty, remove fails
		dyn_array_destroy(results);
		return -1;
		}
	dyn_array_destroy(results);
	}
	//directory is empty, so now, free the file. Just check what is there and release the taken blocks.
	
	int i; 
	for (i = 0; i < 6; i++){
		if (node.directPtrs[i] != -1){
			block_store_release(fs->bs, node.directPtrs[i]);
			node.directPtrs[i] = -1;
			//free the blocks, and set the pointers to null (we will clear this inode when we remove, so it is clean for other stuff
			//clean meaning same as when we formatted it in the original format
		}
	}
	if (node.indirectOne != -1){	//now we check if there are indirect pointers.
									//release all 256 if present, that block points to
									//then release pointer block
									//then set to -1

		
		//read in the pointer block
		block_store_read(fs->bs, node.indirectOne, temp);
		for (i = 0; i < 256; i++){
			if (temp[i] != 0){ //if points to block
				block_store_release(fs->bs, temp[i]);						
			}
		}
		//now we free the pointer block
		block_store_release(fs->bs, node.indirectOne);
		node.indirectOne = -1;	
	}	
	//freed direct, indirect one, now second indirect if exists.
	
	if (node.indirectTwo != -1){				//so, for every block that our 1st pointer block points to
											//do what we did for the first indirect
		uint16_t temp2[256] = {0};
		block_store_read(fs->bs, node.indirectTwo, temp);
		//now we have the block that points to blocks of pointers.
		int j;
		for( i = 0; i < 256; i++){
			if (temp[i] != 0){
				block_store_read(fs->bs, temp[i], temp2);
				//gotta loop thru it now
				for (j = 0; j < 256; j++){
					if (temp[j] != 0)
						block_store_release(fs->bs, temp[j]);
					
				}
				block_store_release(fs->bs, temp[i]); //release block of pointers

			}

		}
		node.indirectTwo = -1;
		
	}
	node.refCount = -1;
	for (i = 0; i < 256; i++){
		if (fs->file_descriptor_table[i].inode_index == index){
			fs->file_descriptor_table[i].inode_index = -1;
		}
	}
	write_inode(fs, index, &node);
	//now, we have to remove the file name and reference from the parent directory. 
	//gonna be kind of a hacky fix, but we have a creation path traversal, which gives parent inode,
	//so got to parent inode, find the matching file name for our file, then delete that reference.
	index = creation_traversal(fs, path);
	get_inode(fs, index, &node);
	
	i = 0;
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
	char tmp_block[512];
	block_store_read(fs->bs, node.directPtrs[0], tmp_block);
	directory_entry_t *entries = (directory_entry_t*)tmp_block;
	for (i = 0; i < 7; i++){
		if (strcmp(entries[i].fname, fname) == 0){
			memset(entries[i].fname, 0, 64);
			entries[i].inode_index = -1;
		}
	}
	block_store_write(fs->bs, node.directPtrs[0], tmp_block);
	return 0;
}

int get_actual_block_write(int relativeIndex, int inode_index, F16FS_t *fs){
	return get_actual_block_index(relativeIndex, inode_index, fs, false);
}

int get_actual_block_read(int relativeIndex, int inode_index, F16FS_t *fs){
	return get_actual_block_index(relativeIndex, inode_index, fs, true);
}


//takes in relativeIndex for file block (0-5 for direct, 6-261 for 1stDirect, 262-65,797`
int get_actual_block_index(int relativeIndex, int inode_index, F16FS_t *fs, bool isRead){
	if (relativeIndex < 0 || relativeIndex > 65797)
		return -1;

	int block_ind;
	inode_t node;
	get_inode(fs, inode_index, &node);
	if (relativeIndex < 6){
		block_ind = node.directPtrs[relativeIndex];
		if (block_ind == -1){ 	//this means no block allocated to this block pointer
			if(isRead){ //if we are reading, but the pointer points no where, nothing to read
				return -1;
			}
			block_ind = block_store_allocate(fs->bs);
			if (block_ind <= 0) //if allocate failed
				return -1;
			node.directPtrs[relativeIndex] = block_ind;
			write_inode(fs, inode_index, &node); //now node is updated with new pointer	
		}
		return block_ind;	
	}	else if (relativeIndex < 262){ //first Indirect
		block_ind = node.indirectOne;

		if (block_ind == -1){ 	//this means no indirect block for the indirect block pointer, 
								//so we must get free block, make it pointers, set it the indirect
								//then get another free block, point the first indirect block pointer in the new block
								//to that allocated block, then return that index
			if( isRead )
				return -1;
			
			block_ind = block_store_allocate(fs->bs);
			if (block_ind <= 0)
				return -1;
			node.indirectOne = block_ind;
			write_inode(fs, inode_index, &node);
			int NewBlockInd = block_store_allocate(fs->bs);
			if (NewBlockInd <= 0)
				return -1;
			uint16_t block[256] = {0}; //block of pointers fam
		
			block[relativeIndex - 6] = NewBlockInd;
			//This allows for gaps in the block of block pointers, in theory, this should be okay, because when we later ask for 
			//things that do not have spots, we will know. Since this function is relative to actual tranlsation, we do not want
			//to make any assumptions about the block pointers, but we also dont want to start sequentially, because the would defeat
			//the translations purpose
			//if we find some errors due to this behavior, that will suck

			block_store_write(fs->bs, block_ind, block);
			//now we have the indirect block pointing to a block of pointers, so we use the free block to be given back
			//as the block index to be used for a write, it is allocated, but we don't need to do anything other than keep track of it
			//which we did when we put it into the indirectBlock pointer
			//return the new pointer?
			return NewBlockInd;
		} else { 	//if we get here, then the indirectOne has a block set up already, so find the relative block,
					//if it exists, great, return its real index
					//if it is 0, meaning it doesn't exist, get a free block, point to it, then return it
			uint16_t block[256] = {0};
			block_store_read(fs->bs, block_ind, block);

			if (block[relativeIndex - 6] == 0){
				
				if (isRead)
					return -1;

				int NewBlockInd = block_store_allocate(fs->bs);
				if (NewBlockInd <= 0)
					return -1;
				block[relativeIndex - 6] = NewBlockInd;
				block_store_write(fs->bs, block_ind, block);
				return NewBlockInd;
			} else {	//if here, then block should be allocated for use, so just return its index
				return block[relativeIndex - 6];
			}
		}
	
	} else { 	//can do else, we know it is not beyond scope, and it isnt in the other 2, so has to be second indirect
		block_ind = node.indirectTwo;
		
		if (block_ind < 0) { 	//this means, we have no second indirect block, so gotta get that
								//Then we gotta allocate a block of pointers point to
								//then allocate another block of pointers to  point to
								//finally, point the relative spot in the previous block to a free block
								//then return that free block
		
			if (isRead)
				return -1;
				
			block_ind = block_store_allocate(fs->bs);
			if (block_ind <= 0)
				return -1;
			node.indirectTwo = block_ind;
			write_inode(fs, inode_index, &node);
			uint16_t block[256] = {0};
	
			//now need a block of pointers to point to
			int NewPointerBlock = block_store_allocate(fs->bs);
			if (NewPointerBlock <= 0)
				return -1;
			//so we have a relative block num. 
			//First take off 262
			//Now, divide by 256 to get the block inside we need
			//then the remainder can be used to get the index within that block
			relativeIndex = relativeIndex - 262;
			
			//Level one block = block pointing to blocks of pointers
			//level two block = pointer to by level one, points to real block
			int levelOneBlockIndex = relativeIndex / 256; //0 means first block of pointers
			int levelTwoBlockIndex = relativeIndex % 256; //0 means first block pointed to by the second level block

			//we know neither exist because we just made it
			block[levelOneBlockIndex] = NewPointerBlock;
			block_store_write(fs->bs, block_ind, block);
			//now we point to block, which points to another block
			//that other block will be pointers too
			block[levelOneBlockIndex] = 0; //now all zeros
			
			
			int NewBlockForStorage = block_store_allocate(fs->bs);
			if (NewBlockForStorage <= 0)
				return -1;
				
			block[levelTwoBlockIndex] = NewBlockForStorage; 
			block_store_write(fs->bs, NewPointerBlock, block);
			return NewBlockForStorage;	
		} else {		//indrect points to block, so now we need to see if we can get the block we need....

			relativeIndex = relativeIndex - 262;

			int levelOneBlockIndex = relativeIndex / 256;

			uint16_t temp[256] = {0};

			block_store_read(fs->bs, block_ind, temp); //we gotta check this block

			if( temp[levelOneBlockIndex] == 0 ){ //we have a block, points to nothing, so two allocs for pointer block and actual block
				
				if (isRead)
					return -1;

				int newPointerBlock = block_store_allocate(fs->bs);
				if (newPointerBlock <= 0)
					return -1;

				temp[levelOneBlockIndex] = newPointerBlock;
				block_store_write(fs->bs, block_ind, temp);

				int levelTwoBlockIndex = relativeIndex % 256;

				int newBlockForStorage = block_store_allocate(fs->bs);

				if (newBlockForStorage <= 0)
					return -1;

				int i;
				for (i = 0; i < 255; i++)
					temp[i] = 0;

				temp[levelTwoBlockIndex] = newBlockForStorage;
				block_store_write(fs->bs, newPointerBlock, temp);
				return newBlockForStorage;
			} else {	//We have a block, points to a block of pointers, see if the block of pointers has the block we want
				uint16_t pointers[256] = {0};

				block_store_read(fs->bs, temp[levelOneBlockIndex], pointers);

				int levelTwoIndex = relativeIndex % 256;

				//either we have a block, or not, we do, return its index, we dont allocate for it
				if ( pointers[levelTwoIndex] == 0){
					
					if (isRead)
						return -1;

					int newBlock = block_store_allocate(fs->bs);
					if (newBlock <= 0)
						return -1;

					pointers[levelTwoIndex] = newBlock;
					block_store_write(fs->bs, temp[levelOneBlockIndex], pointers);
					return newBlock;
				} else {
					int index = pointers[levelTwoIndex];
					return index;
				}

			}
		}
	}
	return -1;		
}

int fs_move(F16FS_t *fs, const char *src, const char *dst){	
		if ( fs == NULL || src == NULL || dst == NULL ){
			return -1;
		}	
		char root[2] = {'/', '\0'};
		if (strcmp(root, src) == 0)
			return -1;

		int srcNode = creation_traversal(fs, src);
		int dstNode = creation_traversal(fs, dst);
		
		if (srcNode < 0 || dstNode < 0)
			return -1;
		
		int i = 0;
		int path_len = 0;
		while (src[i] != (char)0)
			i++;	
		int j = 0;
		path_len = i;
		i = path_len - 1; //index of last char is length - 1
		char oldName[FS_NAME_MAX];
		while (src[i] != '/'){
			j++;
			i--;
		}
		int fn_len = j;
		//now j is fname length

		for (j = 0, i = path_len - fn_len; j < fn_len; j++, i++){
			oldName[j] = src[i];
		}
		oldName[fn_len] = '\0';	
		
		i = 0;
		path_len = 0;
		while (dst[i] != (char)0)
			i++;	
		j = 0;
		path_len = i;
		i = path_len - 1; //index of last char is length - 1
		char newName[FS_NAME_MAX];
		while (dst[i] != '/'){
			j++;
			i--;
		}
		fn_len = j;
		//now j is fname length
	
		for (j = 0, i = path_len - fn_len; j < fn_len; j++, i++){
			newName[j] = dst[i];
		}
		newName[fn_len] = '\0';
		char dstPathDirectory[64];
		for (j = 0; j < path_len - fn_len; j++)
				dstPathDirectory[j] = dst[j];
		
		dstPathDirectory[path_len - fn_len] = (char)0;
		
		if ( srcNode == dstNode){ 	//so first check if its just a rename, if so
									//just rename and return
			char temp[512];
			inode_t node;
			get_inode(fs, dstNode, &node);
			block_store_read(fs->bs, node.directPtrs[0], temp);
			directory_entry_t *entries = (directory_entry_t*)temp;
			
			

			//find this fname in parent node, change it to new fname
			for (i = 0; i < 7; i++){
				if ( strcmp(entries[i].fname, oldName) == 0 ){
					memcpy(entries[i].fname, newName, 64);
					block_store_write(fs->bs, node.directPtrs[0], temp);
					return 0;
				}
			}
		
		}
		//check if parentDir is full

		dyn_array_t *result = fs_get_dir(fs, dstPathDirectory);
		if (dyn_array_size(result) >= 7){
			dyn_array_destroy(result);
			return -1; //full directory and not a rename
		}
		dyn_array_destroy(result);
		//first, lets get the inode index for the file.
		int fileIndex = existing_traversal(fs, src);
		
		if (fileIndex < 0){ //if it failed, try to get it as a directory	
			fileIndex = existing_traversal_directory(fs, src);
			if (fileIndex < 0)
				return -1; //still fails then error
		}	
		if (fileIndex == dstNode) //we have to node, if it matches its parent directory then error
			return -1;
				
		if (fileIndex < 0){ //could be directory
				fileIndex = existing_traversal_directory(fs, src);
				if (fileIndex < 0)
					return -1;
		}
		//check if new file already exists at the destination
		int newPrnt = creation_traversal(fs, dst);
		inode_t node;
		get_inode(fs, newPrnt, &node);

		char test[512];
		block_store_read(fs->bs, node.directPtrs[0], test);
		directory_entry_t *entries = (directory_entry_t*)test;
		
		for (i = 0; i < 7; i++){
			if (strcmp(entries[i].fname, newName) == 0)
				return -1;
		}
		//so we have the file inode, it does not matter if a file or a directory, we just need
		//to move it the new place and remove its ref from the old
		
		int oldParent = creation_traversal(fs, src);
		get_inode(fs, oldParent, &node);

		char tmp[512];

		//have to find it in old to remove it
		block_store_read(fs->bs, node.directPtrs[0], tmp);
		entries = (directory_entry_t*)tmp;
		
		for (i = 0; i < 7; i++){
			if (strcmp(entries[i].fname, oldName) == 0){
				entries[i].inode_index = -1;
				memset(entries[i].fname, 0, 64);
			}
		}
		block_store_write(fs->bs, node.directPtrs[0], tmp);
		//old entry does not have it anymore, so put it in new one
		
		int newParent = creation_traversal(fs, dst);
		get_inode(fs, newParent, &node);

		block_store_read(fs->bs, node.directPtrs[0], tmp);

		entries = (directory_entry_t*)tmp;
		
		for (i = 0; i < 7; i++){
			if (entries[i].inode_index < 0){ //free spot for it
				entries[i].inode_index = fileIndex;
				memcpy(entries[i].fname, newName, 64);
				block_store_write(fs->bs, node.directPtrs[0], tmp);
				return 0;
			}
		}
		return 0;
}
