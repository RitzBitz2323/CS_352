/*
    Implementation of API. 
    Treat the comments inside a function as friendly hints; but you are free to implement in your own way as long as it meets the functionality expectation. 
*/

#include "def.h"

pthread_mutex_t mutex_for_fs_stat;

//init file system
int RSFS_init(){

    //init data blocks
    for(int i=0; i<NUM_DBLOCKS; i++){
      void *block = malloc(BLOCK_SIZE); //a data block is allocated from memory
      if(block==NULL){
        printf("[sys_init] fails to init data_blocks\n");
        return -1;
      }
      data_blocks[i] = block;  
    } 

    //init data and inode bitmap 
    for(int i=0; i<NUM_DBLOCKS; i++) data_bitmap[i]=0;
    pthread_mutex_init(&data_bitmap_mutex,NULL);
    for(int i=0; i<NUM_INODES; i++) inode_bitmap[i]=0;
    pthread_mutex_init(&inode_bitmap_mutex,NULL);    

    //init inodes
    for(int i=0; i<NUM_INODES; i++){
        inodes[i].length=0;
        for(int j=0; j<NUM_POINTER; j++) 
            inodes[i].block[j]=-1; //assign to -1 if not used
        
    }
    pthread_mutex_init(&inodes_mutex,NULL); 

    //init open file table
    for(int i=0; i<NUM_OPEN_FILE; i++){
        struct open_file_entry entry=open_file_table[i];
        entry.used=0; //each entry is not used initially
        pthread_mutex_init(&entry.entry_mutex,NULL);
        entry.position=0;
        entry.access_flag=-1;
    }
    pthread_mutex_init(&open_file_table_mutex,NULL); 

    //init root directory
    root_dir.head = root_dir.tail = NULL;

    //init mutex_for_fs_stat
    pthread_mutex_init(&mutex_for_fs_stat,NULL);

    //return 0 means success
    return 0;
}


//create file with the provided file name
//if file does not exist, create the file and return 0;
//if file_name already exists, return -1; 
//otherwise, return -2.
int RSFS_create(char *file_name){

    //search root_dir for dir_entry matching provided file_name
    struct dir_entry *dir_entry = search_dir(file_name);

    if(dir_entry){//already exists
        printf("[create] file (%s) already exists.\n", file_name);
        return -1;
    }else{

        if(DEBUG) printf("[create] file (%s) does not exist.\n", file_name);

        //construct and insert a new dir_entry with given file_name
        dir_entry = insert_dir(file_name);
        if(DEBUG) printf("[create] insert a dir_entry with file_name:%s.\n", dir_entry->name);
        
        //access inode-bitmap to get a free inode 
        int inode_number = allocate_inode();
        if(inode_number<0){
            printf("[create] fail to allocate an inode.\n");
            return -2;
        } 
        if(DEBUG) printf("[create] allocate inode with number:%d.\n", inode_number);

        //save inode-number to dir-entry
        dir_entry->inode_number = inode_number;
        
        return 0;
    }
}



//open a file with RSFS_RDONLY or RSFS_RDWR flags
//return the file descriptor (i.e., the index of the open file entry in the open file table) if succeed, or -1 in case of error
int RSFS_open(char *file_name, int access_flag){
    int fd = -1;
    //sanity test: access_flag should be either RSFS_RDONLY or RSFS_RDWR
    if(!(access_flag == RSFS_RDONLY || access_flag == RSFS_RDWR)){
        return -1;
    }
    //find dir_entry matching file_name
    struct dir_entry *dir_entry = search_dir(file_name);
    if(dir_entry){

        //find the corresponding inode 
        int inodeNumberForDirEntry = dir_entry->inode_number;
        struct inode *inode = &inodes[inodeNumberForDirEntry];
        //int inodeForDirEntry = inode_bitmap[inodeNumberForDirEntry];
        //find an unused open-file-entry in open-file-table and use it
        for(int i = 0; i < NUM_OPEN_FILE; i++){
            struct open_file_entry *entry = &open_file_table[i];
             if(entry->used==0){
                fd = i;
                //mark entry used
                entry->used = 1;

                //entry set up
                entry->access_flag = access_flag;
                entry->dir_entry = dir_entry; 
            
                break;
             }
        }
    }
    else{
        return fd; 
    }
    return fd;
    
    //return the file descriptor (i.e., the index of the open file entry in the open file table)
    
}




//read from file: read up to size bytes from the current position of the file of descriptor fd to buf;
//read will not go beyond the end of the file; 
//return the number of bytes actually read if succeed, or -1 in case of error.
int RSFS_read(int fd, void *buf, int size){

    //sanity test of fd and size
    if (fd < 0 || fd >= NUM_OPEN_FILE || size <= 0) {
        return -1;
    }

    //get open file entry of fd
    struct open_file_entry *entry = &open_file_table[fd];

    //lock open file entry
    pthread_mutex_lock(&(entry->entry_mutex));

    //get dir entry
    struct dir_entry *dir_entry = entry->dir_entry;

    //get inode
    struct inode *inode = &inodes[dir_entry->inode_number];

    //copy data from the data block(s) to buf and update current position
    int bytes_read = 0;
    int remainingBytes = size;
    int currentPos = entry->position;

    while (remainingBytes > 0 && currentPos < inode->length) {
        int blockIndex = currentPos / BLOCK_SIZE;
        int offsetInBlock = currentPos % BLOCK_SIZE;
        int blockNum = inode->block[blockIndex];

        int bytes_to_read_in_block = BLOCK_SIZE - offsetInBlock;
        int bytes_to_read = remainingBytes < bytes_to_read_in_block ? remainingBytes : bytes_to_read_in_block;

        memcpy(buf + bytes_read, data_blocks[blockNum] + offsetInBlock, bytes_to_read);

        bytes_read += bytes_to_read;
        remainingBytes -= bytes_to_read;
        currentPos += bytes_to_read;
    }

    //update current position
    entry->position = currentPos;

    //unlock open file entry
    pthread_mutex_unlock(&(entry->entry_mutex));

    //return the number of bytes read
    return bytes_read;

}

//write file: write size bytes from buf to the file with fd
//return the number of bytes that have been written if succeed; return -1 in case of error
//write file: write size bytes from buf to the file with fd
//return the number of bytes that have been written if succeed; return -1 in case of error
int RSFS_write(int fd, void *buf, int size){

    //sanity test of fd and size
    if (fd < 0 || fd >= NUM_OPEN_FILE || size <= 0) {
        return -1;
    }

    //get open file entry
    struct open_file_entry *entry = &open_file_table[fd];

    //lock open file entry
    pthread_mutex_lock(&(entry->entry_mutex));

    //get dir entry
    struct dir_entry *dir_entry = entry->dir_entry;

    //get inode
    struct inode *inode = &inodes[dir_entry->inode_number];

    //copy data from buf to the data block(s)
    //update current position;
    //new data blocks may be allocated for the file if needed
    int bytesWritten = 0;
    int remainingBytes = size;
    int currentPos = entry->position;

    while (remainingBytes > 0) {
        int blockIndex = currentPos / BLOCK_SIZE;
        int offsetInBlock = currentPos % BLOCK_SIZE;

        if (blockIndex >= NUM_POINTER) {
            //If cannot allocate more blocks break
            break;
        }

        if (inode->block[blockIndex] == -1) {
            // allocate new block
            int newBlockNum = allocate_data_block();
            if (newBlockNum < 0) { // no free block available
                
                break;
            }
            inode->block[blockIndex] = newBlockNum;
        }

        int blockNum = inode->block[blockIndex];

        int blocksForBytesToWrite = BLOCK_SIZE - offsetInBlock;
        int bytesToWrite = remainingBytes < blocksForBytesToWrite ? remainingBytes : blocksForBytesToWrite;

        memcpy(data_blocks[blockNum] + offsetInBlock, buf + bytesWritten, bytesToWrite);

        bytesWritten += bytesToWrite;
        remainingBytes -= bytesToWrite;
        currentPos += bytesToWrite;
    }

    //update current position and file length
    entry->position = currentPos; 
    if (currentPos > inode->length) {
        inode->length = currentPos;
    }

    //unlock open file entry
    pthread_mutex_unlock(&(entry->entry_mutex));

    //return number of bytes actually written
    return bytesWritten;

}

//update current position: return the current position; if the position is not updated, return the original position
//if whence == RSFS_SEEK_SET, change the position to offset
//if whence == RSFS_SEEK_CUR, change the position to position+offset
//if whence == RSFS_SEEK_END, change hte position to END-OF-FILE-Position + offset
//position cannot be out of the file's range; otherwise, it is not updated
int RSFS_fseek(int fd, int offset, int whence){

    //sanity test of fd and whence    
    if (fd < 0 || fd >= NUM_OPEN_FILE) {
        return -1;
    }
    if (whence != RSFS_SEEK_SET && whence != RSFS_SEEK_CUR && whence != RSFS_SEEK_END) {
        return -1;
    }
    else
    {
        // get open file entry of fd
        struct open_file_entry *entry = &open_file_table[fd];

        // lock the entry
        pthread_mutex_lock(&(entry->entry_mutex));

        // get the current position
        int position = entry->position;

        // get the dir entry
        struct dir_entry *dir_entry = entry->dir_entry;

        // get the inode
        int inode = &dir_entry->inode_number;
        // change the position
        if (position < 0 || position > dir_entry->inode_number)
        {
            // if it less thatn pos 0 or greater than inode dont update
        }
        else
        {
            if (whence == RSFS_SEEK_SET)
            {
                position = offset;
            }
            if (whence == RSFS_SEEK_CUR)
            {
                position = position + offset;
            }
            if (whence == RSFS_SEEK_END)
            {
                position = dir_entry->inode_number - position + offset;
            }
        }

        // unlock the entry
        pthread_mutex_unlock(&(entry->entry_mutex));

        // return the current position
        return position;
    }
}


//close file: return 0 if succeed, or -1 if fd is invalid
int RSFS_close(int fd){

    //sanity test of fd   
    if ((fd != -1))
    {
        // free open file entry with fd
        free_open_file_entry(fd);
        return 0;
    }
    
    
    return -1;
}


//delete file with provided file name: return 0 if succeed, or -1 in case of error
int RSFS_delete(char *file_name){

    //find the dir_entry; if find, continue, otherwise, return -1. 
    struct dir_entry *dir_entry = search_dir(file_name);
    if(dir_entry){
    //find the inode
    int inodeNumberForDirEntry = dir_entry->inode_number;
    struct inode *inodeForEntry = &inodes[inodeNumberForDirEntry];
    //find the data blocks, free them in data-bitmap
    for (int i = 0; i < data_bitmap[NUM_DBLOCKS]; i++)
    {
        free_data_block(i);
    }
    

    //free the inode in inode-bitmap
    for (int i = 0; i < inode_bitmap[NUM_INODES]; i++)
    {
        free_inode(i);
    }
    
    //free the dir_entry
    for (int i = 0; i < NUM_OPEN_FILE; i++)
    {
        delete_dir(file_name);
    }

    }
    else{
        return -1;
    }
    return 0;
}


//print status of the file system
void RSFS_stat(){

    pthread_mutex_lock(&mutex_for_fs_stat);

    printf("\nCurrent status of the file system:\n\n %16s%10s%10s\n", "File Name", "Length", "iNode #");

    //list files
    struct dir_entry *dir_entry = root_dir.head;
    while(dir_entry!=NULL){

        int inode_number = dir_entry->inode_number;
        struct inode *inode = &inodes[inode_number];
        
        printf("%16s%10d%10d\n", dir_entry->name, inode->length, inode_number);
        dir_entry = dir_entry->next;
    }
    
    //data blocks
    int db_used=0;
    for(int i=0; i<NUM_DBLOCKS; i++) db_used+=data_bitmap[i];
    printf("\nTotal Data Blocks: %4d,  Used: %d,  Unused: %d\n", NUM_DBLOCKS, db_used, NUM_DBLOCKS-db_used);

    //inodes
    int inodes_used=0;
    for(int i=0; i<NUM_INODES; i++) inodes_used+=inode_bitmap[i];
    printf("Total iNode Blocks: %3d,  Used: %d,  Unused: %d\n", NUM_INODES, inodes_used, NUM_INODES-inodes_used);

    //open files
    int of_num=0;
    for(int i=0; i<NUM_OPEN_FILE; i++) of_num+=open_file_table[i].used;
    printf("Total Opened Files: %3d\n\n", of_num);

    pthread_mutex_unlock(&mutex_for_fs_stat);
}
