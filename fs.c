#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct fs_superblock {
	int magic;
	int nblocks;
	int ninodeblocks;
	int ninodes;
};

struct fs_inode {
	int isvalid;
	int size;
	int direct[POINTERS_PER_INODE];
	int indirect;
};

union fs_block {
	struct fs_superblock super;
	struct fs_inode inode[INODES_PER_BLOCK];
	int pointers[POINTERS_PER_BLOCK];
	char data[DISK_BLOCK_SIZE];
};

int *fbb = NULL;
//free block bitmap, 
//fbb[block] = 1  => block is in use
//fbb[block] = 0  => block is free

int fs_format()
{
	//if(fs_mount())	//do not run on already mounted disk
	if (fbb != NULL)	
		return 0;

	int nblocks = disk_size();
	printf("nblocks is %d\n", nblocks);
	int ninodeblocks = (nblocks + 9)/10;
	//this unusual division is to ensure rounding up
	//for some bizarre reason, the ceil function was acting up
	printf("ninodeblocks is %d\n", ninodeblocks);
	
	union fs_block block;
	//set superblock data
	block.super.magic = FS_MAGIC;
	block.super.nblocks = nblocks;
	block.super.ninodeblocks = ninodeblocks;
	block.super.ninodes = ninodeblocks * INODES_PER_BLOCK;
	disk_write(0, block.data);  //write superblock to disk

	//clear out inodes, write to disk
	int i, j, k;
	//start at 1, 0 is superblock above
	for (i = 1; i <= ninodeblocks; i++)
	{
		for (k = 0; k < INODES_PER_BLOCK; k++)
		{
			block.inode[k].isvalid = 0;
			block.inode[k].size = 0;
			block.inode[k].indirect = 0;
			for(j = 0; j < POINTERS_PER_INODE; j++)
				block.inode[k].direct[j] = 0;

		}
		disk_write(i, block.data);
	}

	return 1;
}

void fs_debug()
{
	union fs_block block;
	//struct fs_inode inode;

	disk_read(0,block.data);

	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	int nblocks = block.super.nblocks, n = 0;
	int ninodes = block.super.ninodes;
	int ninodeblocks = block.super.ninodeblocks;

	//printf("num blocks is %d\n", nblocks);
	
	//maybe also read the indirect block to get the data
		//that it points to
	int i, j;
	for (n = 1; n <= ninodeblocks; n++)	//start at 1, 0 is done above
	{
		disk_read(n, block.data); 
		printf("block: %d\n", n);

		for (i = 0; i < INODES_PER_BLOCK; i++) 
		{
			//skip if inode is empty or invalid
			if (block.inode[i].size == 0) continue;
			if (block.inode[i].isvalid == 0) continue;

			printf("inode %d\n", i);
			printf("    size: %d bytes\n", block.inode[i].size);
			printf("    direct blocks: ");

			for (j = 0; j < POINTERS_PER_INODE; j++) 
			{
				if (block.inode[i].direct[j] != 0)
					printf("%d ", block.inode[i].direct[j]);
			}
			printf("\n");
			if (block.inode[i].indirect != 0)
				printf("    indirect block: %d\n", block.inode[i].indirect);
				
		}
	}
}

int fs_mount()
{
	union fs_block block;
	disk_read(0, block.data);
	//filesystem is not present
	if (block.super.magic != FS_MAGIC)
		return 0;

	int n;
	int *temp = (int*) realloc (fbb, block.super.nblocks);
	int ninodeblocks = block.super.ninodeblocks;
	if (temp != NULL)
	{
		fbb = temp;
		fbb[0] = 1; //superblock is in use
		for(n = 1; n < ninodeblocks; n++)
			fbb[n] = 1; 	//inode blocks are in use
		for(n = n; n < block.super.nblocks; n++)
			fbb[n] = 0;		//mark data blocks as unused to start 
	}
	else return 0;	//something failed probabaly
	
	int i, j, k;
	for (i = 1; i < ninodeblocks; i++)
	{
		disk_read(i, block.data);
		for (j = 0; j < INODES_PER_BLOCK; j++)
		{		
			if (block.inode[j].isvalid == 0) continue;
			if (block.inode[j].indirect != 0)
				fbb[block.inode[j].indirect] = 1;
			//do we need to set all the blocks indirect
										//points to as used?

			for (k = 0; k < POINTERS_PER_INODE; k++)
			{
				if(block.inode[j].direct[k] != 0)
					fbb[block.inode[j].direct[k]] = 1;
			}
		
		}
	}
	

	return 1;
}

int fs_create()
{
	union fs_block block;
	int blocknum = 1, inode = 0;
	disk_read(0, block.data);
	inodeblocks = block.super.ninodeblocks + 1;
	while (blocknum != inodeblocks){
		disk_read(blocknum, block.data);
		int i = 0;
		for (i = 0; i < INODES_PER_BLOCK; i++){
			if (!block.inode[i].isvalid){
				inumber = (blocknum -1 * INODES_PER_BLOCK) + i;
				return inumber;
			}
		}
	}

	return 0;
}

int fs_delete( int inumber )
{
	return 0;
}

int fs_getsize( int inumber )
{
	union fs_block block;
	//struct fs_inode inode;

	disk_read(0,block.data);

	int blocknum = inumber/INODES_PER_BLOCK + 1;
	int inode = INODES_PER_BLOCK % inumber;
	
	disk_read(blocknum, block.data);
	printf("Size: \n");
	printf("    %d bytes\n", block.inode[inode].size);
	return block.inode[inode].size;

	//maybe also read the indirect block to get the data
		//that it points to

	
	return -1;
}

int fs_read( int inumber, char *data, int length, int offset )
{
	return 0;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	return 0;
}
