////////////////////////////////////////////////////////////////////////////////
//
//  File           : fs3_driver.c
//  Description    : This is the implementation of the standardized IO functions
//                   for used to access the FS3 storage system.
//
//   Author        : *** INSERT YOUR NAME ***
//   Last Modified : *** DATE ***
//

// Includes
#include <string.h>
#include <stdlib.h>
#include <cmpsc311_log.h>
#include <fs3_controller.h>
#include <string.h>
#include <fs3_cache.h>

// Project Includes
#include <fs3_driver.h>
#include <fs3_common.h>

//
// Defines
#define SECTOR_INDEX_NUMBER(x) ((int)(x/FS3_SECTOR_SIZE))
#define FILE_TRACK_CAP 64 //Files to keep track of

//
// Static Global Variables

//struct for keeping file state aka its file flags
typedef struct{
	int isOpen;
	int index;
	int track[50];
	int sector;
	int position;
	int length;
	int fileHandle;
	char fileName[32];
}flags;

// deconstructedCmdBlock struct
typedef struct{
	uint8_t opcode;
	uint16_t sectorNumber;
	uint32_t trackNumber;
	uint8_t returnVal;
} deconstVals;

flags files[FILE_TRACK_CAP] = {{0}}; //arbitrary number of files to keep track of
int tracksOccupied[FS3_MAX_TRACKS] = {0}; //iterate over tracks occupied until 1 is available

uint64_t cmdblock;
int isMounted;
int byteCount;
flags currentFile; //only really used to create data structure for file to keep track of its state

//
// Implementation

int findEmptyTrack(){
	int x = 0;
	while(tracksOccupied[x] != 0){
		x++;
		if(x == FS3_MAX_TRACKS) return -1;
	}
	tracksOccupied[x] = 1;
	return x;
}

void printCmdBlock(FS3CmdBlk cmdblk, int lvl){
	char charCMDBLK[64] = {[0 ... 63] = '0'};
	int x = 0;
	while(cmdblk){
		if (cmdblk & 1){
			charCMDBLK[63-x] = '1';
		} else{
			charCMDBLK[63-x] = '0';
		}
		cmdblk >>= 1;
		x++;
	}
	charCMDBLK[64] = '\0';
	if (lvl == 0) logMessage(LOG_INFO_LEVEL, "[CMDBLK] %s", charCMDBLK);
	else logMessage(LOG_OUTPUT_LEVEL, "[CMDBLK] %s", charCMDBLK);
}

FS3CmdBlk makeCmdBlock(uint8_t opcode, uint16_t sectorNumber, uint32_t trackNumber, uint8_t returnValue){
	uint64_t block = 0;
	block = block | ((uint64_t)opcode << 60) | ((uint64_t)sectorNumber << 44) | ((uint64_t)trackNumber << 12) | ((uint64_t)returnValue << 11);

	return block;
}

/*
HOW TO DECONST:
define op, sec, track, ret
deconstruct command block from sys call output and validate the return value
*/
uint8_t deconstCmdBlock(FS3CmdBlk blk, deconstVals *vals){
	uint8_t topcode;
	uint16_t tsectorNumber;
	uint32_t ttrackNumber;
	uint8_t treturnValue;

	printCmdBlock(blk, 0);

	treturnValue = ((uint64_t)blk >> 11) & ((uint64_t)1);
	ttrackNumber = ((uint64_t)blk >> 12) & ((uint64_t)4294967295);
	tsectorNumber = ((uint64_t)blk >> 44);// & ((uint64_t)1 << 16);
	topcode =  (uint8_t)((uint64_t)blk >> 60);// & ((uint64_t)1 << 4);
	logMessage(LOG_INFO_LEVEL, "[CMDBLK] opcode: %d ", topcode);
	logMessage(LOG_INFO_LEVEL, "[CMDBLK] sector: %d ", tsectorNumber);
	logMessage(LOG_INFO_LEVEL, "[CMDBLK] track: %d ", ttrackNumber);
	logMessage(LOG_INFO_LEVEL, "[CMDBLK] return code: %d ", treturnValue);

	//whole block source of large amount of memory leaks... previously an issue
	(vals->opcode) = topcode;
	//logMessage(LOG_INFO_LEVEL, "op: okay ");
	(vals->sectorNumber) = tsectorNumber;
	//logMessage(LOG_INFO_LEVEL, "sect: okay ");
	(vals->trackNumber) = ttrackNumber;
	//logMessage(LOG_INFO_LEVEL, "track: okay ");
	(vals->returnVal) = treturnValue;
	//logMessage(LOG_INFO_LEVEL, "ret: okay ");
	return treturnValue;
}

//for the time being i'm assuming there will be no collisions (will update in the future)
int hash(char *input){
	int hash = 1000;
	int c;
	while (c = *input++){
		hash = ((hash << 5) + hash) + c;
	}
	int fh = abs(hash % FILE_TRACK_CAP);
	while (files[fh].isOpen != NULL){
		fh = abs(fh + 1 % FILE_TRACK_CAP);
	}
	return fh;
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_mount_disk
// Description  : FS3 interface, mount/initialize filesystem
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t fs3_mount_disk(void) {
	isMounted = 0;

	if (isMounted == 0){
		network_fs3_syscall(makeCmdBlock(FS3_OP_MOUNT, 0, 0, 0), 0);
		isMounted = 1;
		logMessage(FS3DriverLLevel, "FS3 DRVR: mounted.\n");
		return(0);
	}
	logMessage(LOG_ERROR_LEVEL, "FS3 DRVR: failed mounting.\n");
	return(-1);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_unmount_disk
// Description  : FS3 interface, unmount the disk, close all files
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t fs3_unmount_disk(void) {
	if(isMounted == 1){
		isMounted = 0;
		//Need to close out all files first ... for file in files, check if isOpened. If yes close(fd)
		cmdblock = makeCmdBlock(FS3_OP_UMOUNT, 0, 0, 0);
		network_fs3_syscall(cmdblock, 0);
		return 0;
	}
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_open
// Description  : This function opens the file and returns a file handle
//
// Inputs       : path - filename of the file to open
// Outputs      : file handle if successful, -1 if failure

int16_t fs3_open(char *path) {
	char filename[strlen(path)];
	strcpy(filename, path);
	int fileHandle = hash(&filename); //generates filehandle based on filename
	if ((files[fileHandle].isOpen == NULL)){ //create file
		currentFile.isOpen = 1;
		currentFile.track[currentFile.index] = 0; //zero for now, we can allocate track later
		currentFile.sector = 0;
		currentFile.position = 0;
		currentFile.length = 0;
		currentFile.fileName[strlen(filename)];
		strcpy(currentFile.fileName, filename);
		currentFile.fileHandle = fileHandle; // file handle can be constant for now since dealing with only one file
		files[fileHandle] = currentFile; //start tracking this file in system
		logMessage(FS3DriverLLevel, "Driver creating new file [%s]\n", currentFile.fileName);
		logMessage(FS3DriverLLevel, "File [%s] opened in driver, fh, %d.\n", currentFile.fileName, currentFile.fileHandle);

		return (currentFile.fileHandle); 
	} else if(files[fileHandle].isOpen != 1){ //if the file is not open, but is created it opens it
		currentFile = files[fileHandle];
		currentFile.isOpen = 1;
		logMessage(FS3DriverLLevel, "File [%s] opened in driver, fh, %d.\n", currentFile.fileName, currentFile.fileHandle);
		return (currentFile.fileHandle); 
	}

	return(-1);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_close
// Description  : This function closes the file
//
// Inputs       : fd - the file descriptor
// Outputs      : 0 if successful, -1 if failure

int16_t fs3_close(int16_t fd) {
	if(files[fd].isOpen == 1){
		files[fd].isOpen = 0; //pretty basic, just checks if open. if it is, then it sets its state to closed
		return 0;
	}
	
	return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_read
// Description  : Reads "count" bytes from the file handle "fh" into the 
//                buffer "buf"
//
// Inputs       : fd - filename of the file to read from
//                buf - pointer to buffer to read into
//                count - number of bytes to read
// Outputs      : bytes read if successful, -1 if failure

int32_t fs3_read(int16_t fd, void *buf, int32_t count) {

	if (files[fd].isOpen != 1){
		return -1; //aborts since file not open
	}
	void *resizedBuff = (void *)malloc(FS3_SECTOR_SIZE + 1);
	byteCount = 0;
	int offset = 0;

	deconstVals vals;
	
	logMessage(LOG_INFO_LEVEL, "POSITION: %d LENGTH: %d", files[fd].position, files[fd].length);

	//if the amount to be read is less than or equal to the file length, than it proceeds with reading 'count' bytes
	if (files[fd].position + count <= files[fd].length){
		
		if ((files[fd].position % FS3_SECTOR_SIZE) + count > FS3_SECTOR_SIZE){ //if read amount exceeds sector length

			int span = (int)((files[fd].position + count) / FS3_SECTOR_SIZE) - (int)(files[fd].position / FS3_SECTOR_SIZE); //find number of sectors read call spans

			logMessage(LOG_INFO_LEVEL, "READ Spanning %d sectors ", span);

			for(int i = 0; i < span; i++){
				cmdblock = makeCmdBlock(FS3_OP_TSEEK, 0, files[fd].track[files[fd].index], 0); //seeks to the correct track
				FS3CmdBlk ret = network_fs3_syscall(cmdblock, NULL);
				int validity = deconstCmdBlock(ret, &vals);
				if(validity != 0) return -1;

				void *tempc = fs3_get_cache(files[fd].track[files[fd].index], files[fd].sector);
				if (tempc == NULL){
					cmdblock = makeCmdBlock(FS3_OP_RDSECT, files[fd].sector, 0, 0); //reads the sector
					ret = network_fs3_syscall(cmdblock, resizedBuff);
					int validity = deconstCmdBlock(ret, &vals);
					if(validity != 0) return -1;
					fs3_put_cache(files[fd].track[files[fd].index], files[fd].sector, resizedBuff);
				} else{
					resizedBuff = (void *)tempc;
				}

				int adjSectorRead = FS3_SECTOR_SIZE - files[fd].position % 1024;
				logMessage(LOG_INFO_LEVEL, "%s", (char *)resizedBuff);
				memcpy(buf + offset, resizedBuff + files[fd].position % FS3_SECTOR_SIZE, adjSectorRead); //copies the read bytes into the buffer
				logMessage(LOG_INFO_LEVEL, "FS3 DRVR: read on fh %d (%d bytes)\n", fd, count);

				offset += adjSectorRead;
				count -= adjSectorRead;
				byteCount += adjSectorRead;
				files[fd].position += adjSectorRead; //updates file pointer

				if(tempc == NULL) free(resizedBuff);

				resizedBuff  = (void *)malloc(FS3_SECTOR_SIZE + 1);

				if(files[fd].sector + 1 == FS3_TRACK_SIZE){
					files[fd].index += 1;
					files[fd].track[files[fd].index] = findEmptyTrack();
				} else{
					files[fd].sector += 1;
				}
				logMessage(LOG_INFO_LEVEL, "BYTECOUNT: %d (position: %d)\n", byteCount, files[fd].position);
			}
		} 

		cmdblock = makeCmdBlock(FS3_OP_TSEEK, 0, files[fd].track[files[fd].index], 0); //seeks to the correct track
		FS3CmdBlk ret = network_fs3_syscall(cmdblock, NULL);
		int validity = deconstCmdBlock(ret, &vals);
		if(validity != 0) return -1;

		void *tempc = fs3_get_cache(files[fd].track[files[fd].index], files[fd].sector);
		if (tempc == NULL){
			cmdblock = makeCmdBlock(FS3_OP_RDSECT, files[fd].sector, 0, 0); //reads the sector
			ret = network_fs3_syscall(cmdblock, resizedBuff);
			int validity = deconstCmdBlock(ret, &vals);
			if(validity != 0) return -1;
			fs3_put_cache(files[fd].track[files[fd].index], files[fd].sector, resizedBuff);
		} else{
			resizedBuff = (void *)tempc;
		}

		memcpy(buf + offset, resizedBuff + files[fd].position % FS3_SECTOR_SIZE, count); //copies the read bytes into the buffer
		logMessage(LOG_INFO_LEVEL, "FS3 DRVR: read on fh %d (%d bytes)\n", fd, count);
		
		byteCount += count; //updating bytecount and file pointer
		files[fd].position += count; //updates file pointer

		if(tempc == NULL) free(resizedBuff);
		logMessage(LOG_INFO_LEVEL, "BYTECOUNT: %d (position: %d)\n", byteCount, files[fd].position);

		return byteCount;

	} 
	// if the amount to be read is greater than the file length, then it just reads to the end of the file
	else if (files[fd].position + count > files[fd].length && files[fd].position != files[fd].length){ //i dont think this ever gets invoked, test later
		byteCount = files[fd].length - files[fd].position;

		cmdblock = makeCmdBlock(FS3_OP_TSEEK, 0, files[fd].track[files[fd].index], 0);
		FS3CmdBlk ret = network_fs3_syscall(cmdblock, NULL);
		int validity = deconstCmdBlock(ret, &vals);
		if(validity != 0) return -1;

		cmdblock = makeCmdBlock(FS3_OP_RDSECT, files[fd].sector, 0, 0);
		ret = network_fs3_syscall(cmdblock, resizedBuff);
		validity = deconstCmdBlock(ret, &vals);
		if(validity != 0) return -1;

		memcpy(buf, resizedBuff + files[fd].position, byteCount); //copies the read bytes into the buffer

		files[fd].position += files[fd].length; //sets file pointer to eof

		free(resizedBuff);

		return byteCount; 
	}
	else if (files[fd].position == files[fd].length){
		logMessage(LOG_INFO_LEVEL, "File length eqauls its file position!");
		return 0;
	}

	return -1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_write
// Description  : Writes "count" bytes to the file handle "fh" from the 
//                buffer  "buf"
//
// Inputs       : fd - filename of the file to write to
//                buf - pointer to buffer to write from
//                count - number of bytes to write
// Outputs      : bytes written if successful, -1 if failure

int32_t fs3_write(int16_t fd, void *buf, int32_t count) {

	void *resizedBuff  = (void *)malloc(FS3_SECTOR_SIZE + 1); //FS3_SECTOR_SIZE
	byteCount = 0;
	int offset = 0;

	deconstVals vals;

	logMessage(LOG_INFO_LEVEL, "LENGTH: %d || COUNT: %d", files[fd].length, count);
	//null value for isOpen indicates the file handle is bad
	if (files[fd].isOpen != NULL && files[fd].isOpen == 1){
		if(files[fd].length == 0){
			files[fd].track[files[fd].index] = findEmptyTrack();
			logMessage(FS3DriverLLevel, "FS3 driver: allocated fs3 track %d, sector 0 for fh/index %d/%d", files[fd].track[files[fd].index], files[fd].fileHandle, files[fd].index);
		}
		if(files[fd].length >= files[fd].position + count && !((files[fd].position % FS3_SECTOR_SIZE) + count > FS3_SECTOR_SIZE)){ //if there is enough space in file for bytes to be written, than don't change its length
			files[fd].length += 0;
		} else if((files[fd].position % FS3_SECTOR_SIZE) + count > FS3_SECTOR_SIZE){
			//NTD: update similar to how read function works with write span calculations and such (side note: sector utilization is poor, update track and sector allocation process)
			//[1] seek to current track
			logMessage(LOG_INFO_LEVEL, "WRITE Spanning 2 Tracks... LENGTH: %d || POSITION: %d", files[fd].length, files[fd].position);
			cmdblock = makeCmdBlock(FS3_OP_TSEEK, 0, files[fd].track[files[fd].index], 0); //seeks to appropriate track
			FS3CmdBlk ret = network_fs3_syscall(cmdblock, NULL);
			//validate = malloc(sizeof(deconstVals)); //big source of memory leaks
			int validity = deconstCmdBlock(ret, &vals);
			if(validity != 0) return -1;

			//[2] read in file to resized buff
			void *tempc = fs3_get_cache(files[fd].track[files[fd].index], files[fd].sector);
			if (tempc == NULL){
				cmdblock = makeCmdBlock(FS3_OP_RDSECT, files[fd].sector, files[fd].track[files[fd].index], 0); //reads the sector into the resized buffer || ? need 'files[fd].track[files[fd].index]'
				ret = network_fs3_syscall(cmdblock, resizedBuff);
				int validity = deconstCmdBlock(ret, &vals);
				if(validity != 0) return -1;
			} else{
					resizedBuff = (void *)tempc;
			}
			//[3] write only up to sector length
			int adjustedCount = FS3_SECTOR_SIZE - files[fd].position % FS3_SECTOR_SIZE;
			offset = adjustedCount;
			//logMessage(LOG_INFO_LEVEL, "[track %d sector %d] Before: %s", files[fd].track[files[fd].index], files[fd].sector, (void*)resizedBuff); //prints out buffers, debugging purposes
			memcpy(resizedBuff + files[fd].position % FS3_SECTOR_SIZE, buf, adjustedCount);
			if (tempc == NULL) fs3_put_cache(files[fd].track[files[fd].index], files[fd].sector, resizedBuff);
			
			cmdblock = makeCmdBlock(FS3_OP_WRSECT, files[fd].sector, files[fd].track[files[fd].index], 0); //writes buffer with new data into the correct sector
			ret = network_fs3_syscall(cmdblock, resizedBuff);
			validity = deconstCmdBlock(ret, &vals);
			if(validity != 0) return -1;
			
			//[4] update position
			count -= adjustedCount;
			byteCount += adjustedCount;
			files[fd].position += adjustedCount;

			//[5] free and reinit resized buf
			if(tempc == NULL) free(resizedBuff);
			resizedBuff  = (void *)malloc(FS3_SECTOR_SIZE + 1); //FS3_SECTOR_SIZE


			if(files[fd].sector + 1 == FS3_TRACK_SIZE){
				files[fd].index += 1;
				files[fd].track[files[fd].index] = findEmptyTrack();
			} else{
				files[fd].sector += 1;
			}
			if(files[fd].length >= files[fd].position + count){
				files[fd].length += 0;
			} else{
				files[fd].length += (files[fd].position - files[fd].length + count);
			}		
			logMessage(FS3DriverLLevel, "FS3 driver: allocated fs3 track %d, sector 0 for fh/index %d/%d", files[fd].track[files[fd].index], files[fd].fileHandle, files[fd].index);
		}
		else {
			files[fd].length += (files[fd].position - files[fd].length + count); //updates length to provide enough room for bytes to be written
		}

		logMessage(LOG_INFO_LEVEL, "LENGTH: %d || POSITION: %d", files[fd].length, files[fd].position);
		cmdblock = makeCmdBlock(FS3_OP_TSEEK, 0, files[fd].track[files[fd].index], 0); //seeks to appropriate track
		FS3CmdBlk ret = network_fs3_syscall(cmdblock, NULL);
		int validity = deconstCmdBlock(ret, &vals);
		if(validity != 0) return -1;

		void *tempc = fs3_get_cache(files[fd].track[files[fd].index], files[fd].sector);
		if (tempc == NULL){
			cmdblock = makeCmdBlock(FS3_OP_RDSECT, files[fd].sector, files[fd].track[files[fd].index], 0); //reads the sector into the resized buffer || ? need 'files[fd].track[files[fd].index]'
			ret = network_fs3_syscall(cmdblock, resizedBuff);
			int validity = deconstCmdBlock(ret, &vals);
			if(validity != 0) return -1;
		} else{
			resizedBuff = (void *)tempc;
		}
		//logMessage(LOG_INFO_LEVEL, "Before: %s", (void*)resizedBuff);
		memcpy(resizedBuff + files[fd].position % FS3_SECTOR_SIZE, buf + offset, count); 
		if (tempc == NULL) fs3_put_cache(files[fd].track[files[fd].index], files[fd].sector, resizedBuff);
		//copies the bytes to be written over into the resized buffer 
		//logMessage(LOG_INFO_LEVEL, "Dest: %s || Src: %s", (void*)resizedBuff, (void*)buf); //debug purposes
		
		cmdblock = makeCmdBlock(FS3_OP_WRSECT, files[fd].sector, files[fd].track[files[fd].index], 0); //writes buffer with new data into the correct sector
		ret = network_fs3_syscall(cmdblock, resizedBuff);
		validity = deconstCmdBlock(ret, &vals);
		if(validity != 0) return -1;

		byteCount += count; //updating bytecount and file pointer
		files[fd].position += count;

		if(tempc == NULL) free(resizedBuff);

		return byteCount;
	}
	return (-1);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_seek
// Description  : Seek to specific point in the file
//
// Inputs       : fd - filename of the file to write to
//                loc - offfset of file in relation to beginning of file
// Outputs      : 0 if successful, -1 if failure

int32_t fs3_seek(int16_t fd, uint32_t loc) {
	if(files[fd].isOpen == 1){
		if (loc > files[fd].length){
			files[fd].length = loc; //if the location is outside of the files current length, update its size appropriatley
		}
		files[fd].position = loc;
		files[fd].sector = (int)(files[fd].position / FS3_SECTOR_SIZE) % 1024;
		files[fd].index = (int)(files[fd].sector/ FS3_TRACK_SIZE);
		logMessage(LOG_INFO_LEVEL, "Updated position: %d (length: %d).. sect: %d", files[fd].position, files[fd].length, files[fd].sector); //update the file pointer to the location specified	
		return 0;
	}
	return -1;
}
