
////////////////////////////////////////////////////////////////////////////////
//
//  File           : fs3_cache.c
//  Description    : This is the implementation of the cache for the 
//                   FS3 filesystem interface.
//
//  Author         : Patrick McDaniel
//  Last Modified  : Sun 17 Oct 2021 09:36:52 AM EDT
//

// Includes
#include <cmpsc311_log.h>

// Project Includes
#include <fs3_cache.h>
#include <fs3_controller.h>
#include <fs3_common.h>


//
// Support Macros/Data

//
// Implementation

typedef struct{
    struct Node *previous, *next;
    int sector;
    int track;
    char data[FS3_SECTOR_SIZE];
}Node;

typedef struct{
	int length;
    int currentCapacity;
    Node *first, *last;
}Cache;

typedef struct{
    Node *allNodes;
} AllNodes;

Cache cache;
int inserts, getss, hits, misses;
//AllNodes nodes;

int removeLRU(){
    if(cache.last == NULL){ //Cache is empty
        logMessage(LOG_WARNING_LEVEL, "Cache was empty");
        return -1;
    }

    logMessage(LOG_INFO_LEVEL, "Ejecting cache item %d.%d (trk.sct), length 1024", cache.last->track, cache.last->sector);

    if(cache.last == cache.first){
        cache.first = NULL;
    }

    Node *temp = cache.last;
    cache.last = cache.last->previous;
    if(cache.last != NULL) cache.last->next = NULL;

    free(temp);

    cache.currentCapacity--;

    return 0;
}

Node* newNode(FS3TrackIndex trk, FS3SectorIndex sct, void *buf){
    Node *temp = (Node *)malloc(sizeof(Node));
    temp->track = trk;
    temp->sector = sct;
    temp->next = NULL;
    temp->previous = NULL;
    memcpy(temp->data, buf, FS3_SECTOR_SIZE);
    return temp;
}

//returns NULL if not found, returns node ... complexity: O(n) [hashmap would be constant time]
Node* lookupNode(FS3TrackIndex trk, FS3SectorIndex sct){
    if(cache.first == NULL) return NULL;

    Node *temp = cache.first;
    int count = 0;
    while(!(temp->track == trk && temp->sector == sct) && temp->next != NULL){
        temp = ((Node *) temp)->next;
        count++;
    }

    if(temp->track != trk || temp->sector != sct){
        return(NULL);
    }
    else{
        return (Node *)temp;
    }

    return NULL;
}


////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_init_cache
// Description  : Initialize the cache with a fixed number of cache lines
//
// Inputs       : cachelines - the number of cache lines to include in cache
// Outputs      : 0 if successful, -1 if failure

int fs3_init_cache(uint16_t cachelines) {

    //cache = (Cache *)malloc(sizeof(Cache)+sizeof(Node)*2+100000);
    //nodes = (Node *)malloc(sizeof(Node)*cachelines);
    
    cache.length = cachelines;
    cache.currentCapacity = 0;
    cache.first = NULL;
    cache.last = NULL;
    logMessage(LOG_INFO_LEVEL, "Size of cache: %d", sizeof(cache));
    logMessage(LOG_INFO_LEVEL, "Is it null: %d", cache.first == NULL);

    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_close_cache
// Description  : Close the cache, freeing any buffers held in it
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int fs3_close_cache(void)  {
    
    if(cache.last == NULL){ //Cache is empty
        logMessage(LOG_WARNING_LEVEL, "Cache was empty");
        return 0;
    }

    if(cache.last == cache.first){
        cache.first = NULL;
    }

    Node *cur = cache.last;
    while(cur->previous != NULL){
        Node *temp = cur->previous;
        free(cur);
        cur = temp;
    }
    free(cur);
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_put_cache
// Description  : Put an element in the cache
//
// Inputs       : trk - the track number of the sector to put in cache
//                sct - the sector number of the sector to put in cache
// Outputs      : 0 if inserted, -1 if not inserted

int fs3_put_cache(FS3TrackIndex trk, FS3SectorIndex sct, void *buf) {

    Node *temp = lookupNode(trk, sct);
    int nodeCreated = 0;

    if(temp != NULL){
        memcpy(temp->data, buf, FS3_SECTOR_SIZE);
        if(cache.first == temp) return 0;
        if(temp == cache.last) cache.last = (Node *)(temp->previous);
        else {((Node *)(temp->next))->previous = temp->previous;}
        ((Node *)(temp->previous))->next = temp->next;
        temp->previous=NULL;
    } else{
        if(cache.currentCapacity == cache.length){
            int l = removeLRU();
            if (l == -1) return -1;
        }
        temp = newNode(trk, sct, buf);
        nodeCreated = 1;
    }
    temp->next = cache.first;

    if(cache.currentCapacity == 0){
        cache.last = temp;
        cache.first = temp;
    } else{
        cache.first->previous = temp;
        cache.first = temp;
    }

    if(nodeCreated == 1) cache.currentCapacity++, inserts++, logMessage(LOG_INFO_LEVEL, "Added cache item %d.%d (trk.sct), length 1024", trk, sct);

    logMessage(LOG_INFO_LEVEL, "Cache state [%d items, %d bytes used, %d bytes remaining]", cache.currentCapacity, cache.currentCapacity*1024, cache.length*1024 - cache.currentCapacity*1024);

    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_get_cache
// Description  : Get an element from the cache (
//
// Inputs       : trk - the track number of the sector to find
//                sct - the sector number of the sector to find
// Outputs      : returns NULL if not found or failed, pointer to buffer if found

void * fs3_get_cache(FS3TrackIndex trk, FS3SectorIndex sct)  {

    logMessage(LOG_INFO_LEVEL, "Cache state [%d items, %d bytes used, %d bytes remaining]", cache.currentCapacity, cache.currentCapacity*1024, cache.length*1024 - cache.currentCapacity*1024);
    getss++;
    Node *node = lookupNode(trk, sct);
    if (node == NULL){
        logMessage(LOG_INFO_LEVEL, "Getting cache item %d.%d (trk.sct)... not found!", trk, sct);
        misses++;
        return NULL;
    } else{
        hits++;
        if (cache.first != node){ 
            if(node == cache.last) cache.last = (Node *)(node->previous);
            else {((Node *)(node->next))->previous = node->previous;}
            ((Node *)(node->previous))->next = node->next;
            node->next = cache.first;
            cache.first->previous = node;
            cache.first = node;
        }
        return (void *) node->data;
    }
    // IF returns null, then add to cache in driver code
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : fs3_log_cache_metrics
// Description  : Log the metrics for the cache 
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int fs3_log_cache_metrics(void) {
    logMessage(LOG_OUTPUT_LEVEL, "Cache inserts    [     %d]", inserts);
    logMessage(LOG_OUTPUT_LEVEL, "Cache gets       [     %d]", getss);
    logMessage(LOG_OUTPUT_LEVEL, "Cache hits       [     %d]", hits);
    logMessage(LOG_OUTPUT_LEVEL, "Cache misses     [     %d]", misses);
    logMessage(LOG_OUTPUT_LEVEL, "Hit ratio: %%%f", ((hits/(float)getss)*100));
    return(0);
}