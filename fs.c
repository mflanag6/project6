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
int nblocks, ninodes, ninodeblocks;
int mounted = 0;
int blocksize = 4096;

int blockToInode(int blocknum, int inodenum)
{
	return ((blocknum -1) * INODES_PER_BLOCK) + inodenum + 1;
}

int fs_format()
{
	//if(fs_mount())	//do not run on already mounted disk
	if (mounted)	
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
	union fs_block block, idblock;
	//struct fs_inode inode;

	disk_read(0,block.data);

	printf("superblock:\n");
	printf("    %d blocks\n",block.super.nblocks);
	printf("    %d inode blocks\n",block.super.ninodeblocks);
	printf("    %d inodes\n",block.super.ninodes);

	int n = 0, nblocks = block.super.nblocks;
	//int ninodes = block.super.ninodes;
	int ninodeblocks = block.super.ninodeblocks;

	//printf("num blocks is %d\n", nblocks);
	
	//maybe also read the indirect block to get the data
		//that it points to
	int i, j, k;
	for (n = 1; n <= ninodeblocks; n++)	//start at 1, 0 is done above
	{
		disk_read(n, block.data); 
		//printf("block: %d\n", n);

		for (i = 0; i < INODES_PER_BLOCK; i++) 
		{
			//skip if inode is empty or invalid
			//if (block.inode[i].size == 0) continue;
			if(block.inode[i].isvalid == 0) continue;

			printf("inode %d:\n", blockToInode(n, i));
			printf("    size: %d bytes\n", block.inode[i].size);
			printf("    direct blocks: ");

			for (j = 0; j < POINTERS_PER_INODE; j++) 
			{
				if (block.inode[i].direct[j] != 0)
					printf("%d ", block.inode[i].direct[j]);
			}
			printf("\n");
			if (block.inode[i].indirect != 0)
			{
				printf("    indirect block: %d\n", block.inode[i].indirect);
				printf("    indirect data blocks: ");
				disk_read(block.inode[i].indirect, idblock.data);
				for(k=0; k < POINTERS_PER_BLOCK; k++)
				{
					if(idblock.pointers[k] > 0 && idblock.pointers[k] < nblocks)
					{
						printf("%d ", idblock.pointers[k]);
					}
				}
				printf("\n");
			}
				
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
	ninodeblocks = block.super.ninodeblocks;
	nblocks = block.super.nblocks;
	ninodes = block.super.ninodes;
	if (temp != NULL)
	{
		fbb = temp;
		fbb[0] = 1; //superblock is in use
		for(n = 1; n <= ninodeblocks; n++)
			fbb[n] = 1; 	//inode blocks are in use
		for(n = n; n < block.super.nblocks; n++)
			fbb[n] = 0;		//mark data blocks as unused to start 
	}
	else return 0;	//something failed 
	
	int i, j, k;
	for (i = 1; i <= ninodeblocks; i++)
	{
		disk_read(i, block.data);
		for (j = 0; j < INODES_PER_BLOCK; j++)
		{		
			if (block.inode[j].isvalid == 0) continue;
			if (block.inode[j].indirect != 0)
				fbb[block.inode[j].indirect] = 1;

			for (k = 0; k < POINTERS_PER_INODE; k++)
			{
				if(block.inode[j].direct[k] != 0)
					fbb[block.inode[j].direct[k]] = 1;
			}
		
		}
	}

	//this will hopefully set all indirect pointers to 0
	for(i=ninodeblocks+1; i<nblocks; i++)
	{
		disk_read(i, block.data);
		for(j=0; j<POINTERS_PER_BLOCK; j++)
		{
			block.pointers[j] = 0;
		}
			
	}
	
	mounted = 1;

	printf("free block bitmap is as follows:\n");
	for(i=0; i <nblocks; i++)
		printf("%d ", fbb[i]);

	printf("\n");
	return 1;
}

int fs_create()
{
	if(!mounted)
	{
		printf("Error: disk not mounted.  Run mount first\n");
		return 0;
	}

	union fs_block block;
	int blocknum = 1; //, inode = 0;
	disk_read(0, block.data);
	printf("\n\n");
	printf("Disk has been read.\n");
	int inodeblocks = block.super.ninodeblocks + 1;
	printf("Number of inodeblocks is %d.\n", inodeblocks);
	while (blocknum != inodeblocks){
		printf("Number of blocknum %d.\n", blocknum);
		disk_read(blocknum, block.data);
		int i = 0;
		for (i = 0; i < INODES_PER_BLOCK; i++){
			printf("We are on inode %d \n", i);
			if (!block.inode[i].isvalid){
				printf("Number of blocknum again is %d.\n", blocknum);
				int inumber = ((blocknum -1) * INODES_PER_BLOCK) + i + 1;
				block.inode[i].size = 0;
				memset(block.inode[i].direct, 0, sizeof block.inode[i].direct);
				block.inode[i].isvalid = 1;
				block.inode[i].indirect = 0;
				printf("Inode %d is not valid and thus free.\n", i);
				printf("inumber is %d.\n", inumber);
				disk_write(blocknum, block.data);
				return inumber;
			}
		}
		blocknum++;
	}

	return 0;
}

int fs_delete( int inumber )
{
	if(!mounted)
	{
		printf("Error: disk not mounted.  Run mount first\n");
		return 0;
	}

	union fs_block block, indirectblock;
	//disk_read(0, block.data);

	//add 1 b/c 0 is superblock, inodes start at 1
	int blocknum = inumber/INODES_PER_BLOCK + 1;
	int inode = inumber % INODES_PER_BLOCK;

	disk_read(blocknum, block.data);
	int i;

	//do not run if inode is invalid
	if(block.inode[inode].isvalid == 0)
	{
		printf("Error: inode is invalid\n");
		return 0;
	}

	for(i=0; i<POINTERS_PER_INODE; i++)
	{
		fbb[block.inode[inode].direct[i]] = 0;
		//mark this block as free
	}

	//if this inode used indirect block, we need to clear it
	if(block.inode[inode].indirect != 0)
	{
		disk_read(block.inode[inode].indirect, indirectblock.data);
		for(i=0; i < POINTERS_PER_BLOCK; i++)
		{
			printf("i is this %d\n",i);
			printf("the indirect pointer is this %d\n", indirectblock.pointers[i]);
			if(indirectblock.pointers[i] > 0+ninodeblocks && indirectblock.pointers[i] < nblocks)
			{
				//only clear it if it was a valid pointer
				//don't worry about garbage values
				fbb[indirectblock.pointers[i]] = 0;
			}
			printf("the indirect pointer is this %d\n", indirectblock.pointers[i]);
		}
		disk_write(block.inode[inode].indirect, indirectblock.data);
	}

	//all the blocks are freed, mark this inode as invalid
	printf("marking number %d as invalid\n", inode);
	block.inode[inode].isvalid = 0;
	disk_write(blocknum, block.data);


	/*
	disk_read(block.inode[inode].indirect, indirectblock.data);
	fbb[block.inode[inode].indirect] = 0;
	//free indirect block itself
	
	//mark all the blocks it pointed to as free
	for(i = 0; i < POINTERS_PER_BLOCK; i++)
	{
		fbb[indirectblock.pointers[i]] = 0;
	}
	*/

	return 1;
}

int fs_getsize( int inumber )
{
	if(!mounted)
	{
		printf("Error: disk not mounted.  Run mount first\n");
		return -1;
	}

	union fs_block block;
	//struct fs_inode inode;

	disk_read(0,block.data);

	int blocknum = inumber/INODES_PER_BLOCK + 1;
	int inode = inumber % INODES_PER_BLOCK;// % inumber;
	
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
	//printf("attempting read\n");
	if(!mounted)
	{
		printf("Error: disk not mounted.  Run mount first\n");
		return 0;
	}

	union fs_block inodeblock, datablock, indirectblock;
	int iblock = inumber / INODES_PER_BLOCK + 1;
	int inode = inumber % INODES_PER_BLOCK - 1;
	//printf("reading from block %d, array inode %d\n", iblock, inode);
	//printf("corresponds to inode number %d\n", blockToInode(iblock, inode));
	int i, blockremaining = blocksize;
	disk_read(iblock, inodeblock.data);
	//char *garbage;
	int bytesread = 0;

	//printf("size: %d\n", inodeblock.inode[inode].size);
	//printf("direct: %d\n", inodeblock.inode[inode].direct[0]);

	//initialize an array that could contain every block we need
	int blockArray[POINTERS_PER_INODE + POINTERS_PER_BLOCK] = {0};
	
	//maybe we can take all the possible blocks
	//and put them into an array to save some code reuse
	for(i=0; i < POINTERS_PER_INODE; i++)
	{
		blockArray[i] = inodeblock.inode[inode].direct[i];
		printf("added %d to the array\n", blockArray[i]);
		//load direct blocks into the array
	}

	disk_read(inodeblock.inode[inode].indirect, indirectblock.data);
	for(i=POINTERS_PER_INODE; i < POINTERS_PER_BLOCK; i++)
	{
		blockArray[i] = indirectblock.pointers[i-POINTERS_PER_INODE];
	}
	
	//check through array of block pointers
	for(i=0; i < POINTERS_PER_INODE + POINTERS_PER_BLOCK; i++)
	{
		//printf("entered for loop %d\n", blockArray[i]);
		if(blockArray[i] <= 0) continue;
		if(blockArray[i] >=nblocks) continue;
		if(i >= nblocks) return bytesread;

		disk_read(blockArray[i], datablock.data);
		if(offset >= blocksize) 
		{
			offset -= blocksize;			
			continue;
		}
		else if(offset > 0) 
		{
			blockremaining = blocksize - offset;
			memcpy(data+offset, datablock.data, blockremaining);
			offset -= offset;
		}		

		blockremaining = blocksize;
		if(offset <= 0)
		{	//we're now past the offset
			if(length == 0)
			{
				//printf("length is 0 returning\n");
				return bytesread;
			}

			if(length > blockremaining)
			{
				//printf("length > remaining\n");
				memcpy(data + bytesread, datablock.data + bytesread, blockremaining);
				length -= blockremaining;
				bytesread += blockremaining;
			}
			else if(length < blockremaining)
			{
				//this is the last part to copy
				//printf("entering final copy\n");
				memcpy(data + bytesread, datablock.data + bytesread, length);
				bytesread += length;
				return bytesread;
			}
			blockremaining = blocksize;
		}
	
	}

	//now do indirect
	/*disk_read(inodeblock.inode[inode].indirect, indirectblock.data);
	for(i=0; i < POINTERS_PER_BLOCK; i++)
	{
		disk_read(indirectblock.pointers[i], datablock.data);
		
	}*/

	//printf("returning 0\n");
	return 0;
}

int getFreeBlock() {
	int i;
	for(i=1; i < nblocks; i++)
	{
		if(fbb[i] == 0)
			return i;
	}
	return -1;
	//no free blocks
}

int blockToWrite(int *offset, struct fs_inode inode)
{
	int counter;
	while(*offset > blocksize)
	{
		counter++;
		*offset -= blocksize;
	}

	if(counter < POINTERS_PER_INODE)
	{
		if(inode.direct[counter] != 0)
			return inode.direct[counter];
		else
			return getFreeBlock();
	}
	else if(counter >= POINTERS_PER_INODE)
	{
		union fs_block block;
		disk_read(inode.indirect, block.data);
		if(block.pointers[counter-POINTERS_PER_INODE] != 0)
			return block.pointers[counter-POINTERS_PER_INODE];
		else
			return getFreeBlock();
	}
	return -1;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
	printf("Attempting write\n");
	printf("%s", data);
	if(!mounted)
	{
		printf("Error: disk not mounted.  Run mount first\n");
		return 0;
	}

	union fs_block inodeblock, datablock, idblock;
	int iblock = inumber / INODES_PER_BLOCK + 1;
	int inode = inumber % INODES_PER_BLOCK -1;
	int byteswritten = 0, blockremaining = 4096;
	
	disk_read(iblock, inodeblock.data);
	printf("read inode block data\n");



	/*blocknum = blockToWrite(&offset, inodeblock.inode[inode]);
	blockremaining = blocksize - offset;
	while(length > blocksize)
	{
		memcpy(
	}*/


	//direct first, maybe we adjust to do like read later
	int i, blocknum, id = -1;
	for(i=0; i < POINTERS_PER_INODE + POINTERS_PER_BLOCK; i++)
	{
		if(i < POINTERS_PER_INODE)
		{
			//direct blocks
			//if there's no block in place, we need to allocate one
			blocknum = inodeblock.inode[inode].direct[i];
			printf("found direct block %d in place\n", blocknum);
		}
		else
		{
			//indirect block
			if(inodeblock.inode[inode].indirect == 0)
			{
				id = getFreeBlock();
				inodeblock.inode[inode].indirect = id;
				printf("found indirect block to allocate %d\n", id);
			}
			disk_read(inodeblock.inode[inode].indirect, idblock.data);
			blocknum = idblock.pointers[i-POINTERS_PER_INODE];
		}
		if(blocknum == 0)
		{
			blocknum = getFreeBlock();
			printf("found direct block to allocate %d\n", blocknum);
			if(blocknum == -1)
			{
				printf("Error: No free blocks found\n");
				return 0;
			}
			if(i < POINTERS_PER_INODE)
				inodeblock.inode[inode].direct[i] = blocknum;
		}
		disk_read(blocknum, datablock.data);
		printf("read from block %d\n", blocknum);

		if(offset > blocksize)
		{
			printf("offset over a block\n");
			offset -= blocksize;
			continue;
		}
		else if(offset > 0)
		{
			printf("offset less than a block, copying\n");
			blockremaining = blocksize-offset;	
			memcpy(datablock.data + offset, data, blockremaining);
			offset -= offset;
			byteswritten += blockremaining;
		}

		printf("remaining offset is %d\n", offset);
		blockremaining = blocksize;
		if(offset <= 0)
		{
			if(length == 0)
			{
				printf("breaking\n");
				break;
			}

			if(length >= blockremaining)
			{
				memcpy(datablock.data + byteswritten, data, blockremaining);
				length -= blockremaining;
				byteswritten += blockremaining;
			}
			else if(length < blockremaining)
			{
				printf("performing last copy\n");
				//this is the last part to copy
				memcpy(datablock.data + byteswritten, data + byteswritten, length);
				//printf(">>>>>");
				//printf("%s", data);
				//printf("<<<<<<\n");
				byteswritten += length;
				inodeblock.inode[inode].size = byteswritten;
				disk_write(iblock, inodeblock.data);
				if (id != -1)
					disk_write(id, idblock.data);
				disk_write(blocknum, datablock.data);
				//printf("writing out to block %d\n", blocknum);
				return byteswritten;
			}
			blockremaining = blocksize;
		}
		disk_write(blocknum, datablock.data);
		//printf("writing out to block %d\n", blocknum);
	}

	inodeblock.inode[inode].size = byteswritten;
	disk_write(iblock, inodeblock.data);
	if(id != -1)
		disk_write(id, idblock.data);
	disk_write(blocknum, datablock.data);
	//printf("writing out to block %d\n", blocknum);
	return byteswritten;

	//return 0;
}
