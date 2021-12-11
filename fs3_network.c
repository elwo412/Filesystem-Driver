////////////////////////////////////////////////////////////////////////////////
//
//  File           : fs3_netowork.c
//  Description    : This is the network implementation for the FS3 system.

//
//  Author         : Patrick McDaniel
//  Last Modified  : Thu 16 Sep 2021 03:04:04 PM EDT
//

// Includes
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cmpsc311_log.h>

// Project Includes
#include <fs3_network.h>
#include <fs3_driver.h>
#include <cmpsc311_util.h>

//
//  Global data
unsigned char     *fs3_network_address = NULL; // Address of FS3 server
unsigned short     fs3_network_port = 0;       // Port of FS3 serve
int sock;
typedef struct{
	uint8_t opcode;
	uint16_t sectorNumber;
	uint32_t trackNumber;
	uint8_t returnVal;
} deconstVals;

//
// Network functions


int mountoperations(FS3CmdBlk *cmdBlk, FS3CmdBlk *ret){
    fs3_network_address = (char *)FS3_DEFAULT_IP;
    fs3_network_port = FS3_DEFAULT_PORT;
    struct sockaddr_in v4;

    v4.sin_family = AF_INET;
    v4.sin_port = htons(fs3_network_port);

    int returnvaleualsd = inet_aton(fs3_network_address, &(v4.sin_addr));
    if (returnvaleualsd == 0){
        logMessage(LOG_ERROR_LEVEL, "Invalid address specified");
        kill(getpid(), SIGUSR1);
        //return -1;
    } 
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1){
        logMessage(LOG_ERROR_LEVEL, "Could not create socket");
        kill(getpid(), SIGUSR1);
        //return -1;
    } 

    if (connect(sock, (const struct sockaddr *)&v4, sizeof(v4)) == -1){
        logMessage(LOG_ERROR_LEVEL, "Could not connect to server... might not be running");
        kill(getpid(), SIGUSR1);
        //return -1;
    } 

    printCmdBlock(*cmdBlk, 1);

    write(sock, cmdBlk, sizeof(cmdBlk));
    read(sock, ret, sizeof(FS3CmdBlk));

    *ret = ntohll64(*ret);

    return 0;
}

int seekoperations(FS3CmdBlk *cmdBlk, FS3CmdBlk *ret){

    //printCmdBlock(*cmdBlk, 1);
    *cmdBlk = htonll64(*cmdBlk);
    write(sock, cmdBlk, sizeof(FS3CmdBlk));
    //write(sock, cmdBlk, sizeof(FS3CmdBlk));
    read(sock, ret, sizeof(FS3CmdBlk));

    *ret = ntohll64(*ret);

    return 0;
}

int readoperations(FS3CmdBlk *cmdBlk, FS3CmdBlk *ret, char *readbuffer){
    
    printCmdBlock(*cmdBlk, 1);

    *cmdBlk = htonll64(*cmdBlk);
    write(sock, cmdBlk, sizeof(FS3CmdBlk));
    read(sock, ret, sizeof(FS3CmdBlk));
    *ret = ntohll64(*ret); 
    read(sock, readbuffer, sizeof(char)*1024); //NOTE: maybe try padding
    //*readbuffer = ntohll64(*readbuffer); //MAYBE: not needed
    
    return 0;
}

int writeoperations(FS3CmdBlk *cmdBlk, FS3CmdBlk *ret, char *writebuffer){
    
    printCmdBlock(*cmdBlk, 1);
    *cmdBlk = htonll64(*cmdBlk);
    write(sock, cmdBlk, sizeof(FS3CmdBlk));
    //*writebuffer = htonll64(*writebuffer); //MAYBE: not needed
    write(sock, writebuffer, sizeof(char)*1024);  //NOTE: maybe try padding
    read(sock, ret, sizeof(FS3CmdBlk));
    *ret = ntohll64(*ret);

    return 0;
}

int unmountoperations(FS3CmdBlk *cmdBlk){
    
    write(sock, htons(cmdBlk), sizeof(cmdBlk));
    close(sock);
    sock = -1;

    return 0;
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : network_fs3_syscall
// Description  : Perform a system call over the network
//
// Inputs       : cmd - the command block to send
//                ret - the returned command block
//                buf - the buffer to place received data in
// Outputs      : 0 if successful, -1 if failure

int network_fs3_syscall(FS3CmdBlk cmd, FS3CmdBlk *ret, void *buf)
{
    int opret;
    FS3CmdBlk blk = cmd;
    FS3CmdBlk returned;
    deconstVals vals;
    if (deconstCmdBlock(cmd, &vals) != 0) return -1;
    logMessage(LOG_INFO_LEVEL, "OPCODE RECIEVED: %d", vals.opcode);
    switch(vals.opcode){
        case 0:
            //mounting op
            opret = mountoperations(&blk, &ret);
            logMessage(LOG_INFO_LEVEL, "Mounted Disk, Returned: %d ", opret);
            break;
        case 1:
            //seeking op
            opret = seekoperations(&blk, &ret); //NEED TO IMPLEMENT
            logMessage(LOG_INFO_LEVEL, "Seeking to track: %d ", vals.trackNumber);
            break;
        case 2:
            //reading op
            opret = readoperations(&blk, &ret, buf); //NEED TO IMPLEMENT
            logMessage(LOG_INFO_LEVEL, "Reading from {sector: %d, track: %d}", vals.sectorNumber, vals.trackNumber);
            break;
        case 3:
            //writing op
            opret = writeoperations(&blk, &ret, buf); //NEED TO IMPLEMENT
            logMessage(LOG_INFO_LEVEL, "Writing from {sector: %d, track: %d}", vals.sectorNumber, vals.trackNumber);
            break;
        case 4:
            //unmounting op
            opret = unmountoperations(&blk);
            logMessage(LOG_INFO_LEVEL, "Unmounted Disk, Returned: %d ", opret);
            break;
        default:
            //invalid code
            return -1;
    }
    //memcpy(ret, &returned, sizeof(FS3CmdBlk));
    return opret;
}



