#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <fcntl.h>   
#include "threadpool.h"

#define MAX_FILES 100
#define MAX_SIZE 1024 * 1024 * 1024
#define CHUNK_SIZE 1024 * 4

int getopt(int rgc, char *const argv[], const char *optstring); 
threadpool_t pool;    
int threads_num = 0;

typedef struct chunk_s {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char *str;    // str need encode
    int size;   
    char *encoded;  // result after encode
    bool done;   // whether it is done
}chunk_t; 

void rle_encode(void *arg) {
    int enc_len = 0;
    chunk_t *chunk = (chunk_t *) arg;  // arg is one chunk
    if (chunk == NULL) {
        return ;
    } else if (chunk->encoded == NULL || chunk->str == NULL)
    {
        return ;
    }
    unsigned char count = 1;
    if(threads_num!=0){
        if(pthread_mutex_lock(&chunk->mutex)!=0){
            perror("chunk_mutex_lock");
        }
    }
    for (int i = 0; i < chunk->size; i++) {
        chunk->encoded[enc_len++] = chunk->str[i];
        count = 1;
        while (chunk->str[i] == chunk->str[i + 1]) {
            if (i + 1 >= chunk->size) {
                break;
            }
            count++;
            i++;
        }
        chunk->encoded[enc_len++] = count;
    }
    chunk->encoded[enc_len] = '\0';
    
    chunk->done = true;
    if(threads_num!=0){
        if(pthread_cond_signal(&chunk->cond)!=0){
        perror("chunk_cond_signal");
        }
        if(pthread_mutex_unlock(&chunk->mutex)!=0){
            perror("chunk_mutex_unlock");
        }
    }
    return ;
}

int main (int argc, char *argv[]) 
{
    if (argc <= 1) {
        fprintf(stderr, "Usage: %s filename\n", argv[0]);
        exit(1);
    }
    int ch, fd; 

    struct stat file_info;       // Get File Info
    ssize_t file_size = 0;       // For judge whether it will 

    if ((ch = getopt(argc, argv, "j:")) != -1) {  // before read files, create pool based on optagr
        if (ch == 'j') {
            threads_num = atoi(argv[2]);
            threadpool_init(&pool, threads_num);
        }
    }

    // whether the num of files exceed 100
    int file_num = argc - optind;
    if(file_num > MAX_FILES){
        fprintf(stderr,"More than %d files error\n", MAX_FILES);
        exit(-1);
    }

    int i = 1;
    if(optarg){    // -j x
        optind++;
    }

    chunk_t *chunks; // new chunks
    int max_chunks = 0; 
    int m = 0;

    // calculate the max_chunks first
    for (char **p = argv; *p; ++argv, ++p, m++) {
        if((i == 1) | (i < optind)){   // skip the args before files: without -j/ with -j 
            i++;
            continue;
        }else{
            if ((fd = open(*argv, O_RDWR)) < 0){    
                fprintf(stderr,"Open file error\n");
                exit(-1);
            }
            if(fstat(fd, &file_info) < 0){
                fprintf(stderr,"Get file stat error\n");
                exit(-1);
            }
            file_size += file_info.st_size;     
            if (file_size >= MAX_SIZE) {
                fprintf(stderr, "Size more than 1GB error\n");
                exit(-1);
            }
        }
        if(file_info.st_size > CHUNK_SIZE){       // 4kB
            if(file_info.st_size%CHUNK_SIZE!= 0){
                max_chunks += (file_info.st_size/CHUNK_SIZE)+1;
            }else{
                max_chunks += (file_info.st_size/CHUNK_SIZE);
            }
        }else{
            max_chunks += 1;
        }
        close(fd);
    }

    if (file_size == 0) {    
        fprintf(stderr, "empty file\n");
        exit(1);
    }

    chunks = calloc(max_chunks, sizeof(struct chunk_s));  
    int chunk_num = 0;   
    
    for(int j = 0; j < m; j++){
        argv--;
    }
    
    i = 1;
    for (char **p = argv; *p; ++argv, ++p) {
        if ((i == 1) | (i < optind)) { // skip the args before files: no -j/ with -j
            i++;
            continue;
        } else {
            if ((fd = open(*p, O_RDWR)) < 0) {           
                exit(-1);
            }
            if (fstat(fd, &file_info) < 0) {             
                exit(-1);
            }
            char *content = mmap(NULL, file_info.st_size, 
                                 PROT_READ,
                                 MAP_SHARED,
                                 fd, 0);
            int start = 0;     // 开始
            while ((long long) start < file_info.st_size) {      // for everytime after 4kb
                int chunk_size = file_info.st_size - start;   
                if (chunk_size > CHUNK_SIZE) {                
                    chunk_size = CHUNK_SIZE;               // if chunk_size > 4kb, extract 4kb
                }
                (chunks+chunk_num)->str = content + start;      
                (chunks+chunk_num)->size = chunk_size;        
                (chunks+chunk_num)->encoded = calloc(chunk_size * 2, sizeof(char)); // worst result 8kb
                (chunks+chunk_num)->done = false;               // init done false
                if(pthread_mutex_init(&(chunks+chunk_num)->mutex, NULL)!=0){
                    perror("chunk_mutex_init");
                }
                if(pthread_cond_init(&(chunks+chunk_num)->cond, NULL)!=0){
                    perror("chunk_cond_init");
                }
                if(threads_num == 0){
                    rle_encode(chunks + chunk_num);
                }else{
                    threadpool_add_task(&pool, rle_encode, chunks + chunk_num);   //chunks[chunk_num]
                }
                start += chunk_size;     // next 4kb content
                chunk_num++;             // next chunk
                // have n chunks, every chunk.str is a (<=)4kb part of file content
            }
            close(fd);
        }
    }

    // already got chunk_num
    unsigned char last[4] = {0}; 
    char *encoded = NULL; 
    int enc_len = 0; 
    for (int i = 0; i < chunk_num; i++) {  
        if(threads_num!=0){
            if(pthread_mutex_lock(&(chunks+i)->mutex)!=0){
                perror("chunk_mutex_lock");
            }
            while(1){
                if(!(chunks+i)->done){     // wait until chunks[i] is done
                    if(pthread_cond_wait(&(chunks+i)->cond ,&(chunks+i)->mutex)!=0){
                        perror("chunk_cond_wait");
                    }
                }else{
                    break;
                }
            }
            if(pthread_mutex_unlock(&(chunks+i)->mutex)!=0){
                perror("chunk_mutex_unlock");
            }
        }
        encoded = (chunks+i)->encoded;  
        enc_len = strlen(encoded);    
        if (chunk_num > 1) {
            if(i == 0){
                last[0] = encoded[enc_len - 2];  
                last[1] = encoded[enc_len - 1];
                encoded[enc_len - 2] ='\0';
                printf("%s",encoded);
            }else{
                if(last[0] == encoded[0]){
                    encoded[1] += last[1]; 
                }else{
                    printf("%s", last); 
                }
                last[0] = encoded[enc_len - 2];  
                last[1] = encoded[enc_len - 1];
                encoded[enc_len - 2] ='\0';
                printf("%s",encoded);
            }
        } else {
            printf("%s",encoded);
        }
        free((chunks+i)->encoded);
    }
    
    if(chunk_num > 1) {     // if chunk num > 1, print the last two chars
        printf("%s", last);
    }
    return 0;
}
