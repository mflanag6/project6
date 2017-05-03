#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

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

int fs_format()
{
	if(fs_mount())	//do not run on already mounted disk
		return 0;

	nblocks = disk_size();
	ninodeblocks = nblocks/10;  //maybe change this to round up later
	
	union fs_block block;
	//set superblock data
	block.super.magic = FS_MAGIC;
	block.super.nblocks = nblocks;
	block.super.ninodeblocks = ninodeblocks;
	block.super.ninodes = ninodeblocks * INODES_PER_BLOCK;
	disk_write(0, block);  //write superblock to disk

	//clear out inodes, write to disk
	int i, j;
	for (i = 0; i < ninodeblocks; i++)
	{
		block.inode[i].isvalid = 1;
		block.inode[i].size = 0;
		block.inode[i].indirect = 0;
		for(j = 0; j < POINTERS_PER_INODE; j++)
			block.inode[i].direct[j] = 0;

		disk_write(i, block);
	}
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

	//printf("num blocks is %d\n", nblocks);
	
	//maybe also read the indirect block to get the data
		//that it points to
	int i, j;
	for (n = 1; n < nblocks; n++)	//start at 1, 0 is done above
	{
		disk_read(n, block.data); 
		printf("block: %d\n", n);

		for (i = 0; i < ninodes; i++) 
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

	return 0;
}

int fs_create()
{
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

	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

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
