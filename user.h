#ifndef USER_H
#define USER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>  
#include <ctype.h>  
#include <limits.h>
#include <sys/mman.h>
#include <dirent.h>
#include <stdarg.h>  
#include <semaphore.h>
#include <pthread.h>
#include <gtk/gtk.h> 

#define DATABASE_FILE "/home/ali/Desktop/database.txt"
#define MAX_ITEM_LENGTH 256
#define MAX_ORDER_ITEMS 100
#define MAX_PRODUCTS_PER_STORE 100

pthread_mutex_t file_mutex;



struct user {
    char username[100];
    int user_id;
    int store_purchases[3]; 
};


void remove_spaces_and_newline(char *str) {
    int i = 0, j = 0;

    while (str[i] != '\0') {
        if (str[i] != ' ' && str[i] != '\n') {
            str[j++] = str[i];
        }
        i++;
    }

    str[j] = '\0';
}


struct user find_user(const char *username) {
    FILE *file = fopen(DATABASE_FILE, "r");
    struct user found_user;
    found_user.user_id = -1; 

    if (file == NULL) {
        perror("Unable to open database file");
        return found_user;
    }

    while (fscanf(file, "%s %d %d %d %d\n", 
                  found_user.username, 
                  &found_user.user_id, 
                  &found_user.store_purchases[0],
                  &found_user.store_purchases[1],
                  &found_user.store_purchases[2]) != EOF) {
        if (strcmp(found_user.username, username) == 0) {
            fclose(file);
            return found_user;
        }
    }

    fclose(file);
    found_user.user_id = -1;
    return found_user; 
}


int add_user_to_database(const char *username) {
    FILE *file = fopen(DATABASE_FILE, "a");
    if (file == NULL) {
        perror("Unable to open database file for writing");
        return -1;
    }

    srand(time(NULL));
    int new_user_id = rand() % 1000 + 1;

    fprintf(file, "%s %d %d %d %d\n", username, new_user_id, 0, 0, 0);
    fclose(file);
    return new_user_id;
}


int update_user_purchase(const char *username, int store_id) {
    FILE *file = fopen(DATABASE_FILE, "r+");
    if (file == NULL) {
        perror("Unable to open database file for updating");
        return -1;
    }

    struct user current_user;
    long pos;
    while ((pos = ftell(file)) != -1 &&
           fscanf(file, "%s %d %d %d %d\n", 
                  current_user.username, 
                  &current_user.user_id, 
                  &current_user.store_purchases[0],
                  &current_user.store_purchases[1],
                  &current_user.store_purchases[2]) != EOF) {
        if (strcmp(current_user.username, username) == 0) {
            if (store_id >=1 && store_id <=3) {
                current_user.store_purchases[store_id -1] = 1;
            } else {
                fclose(file);
                return -1; 
            }

            fseek(file, pos, SEEK_SET);
            fprintf(file, "%s %d %d %d %d\n", 
                    current_user.username, 
                    current_user.user_id, 
                    current_user.store_purchases[0],
                    current_user.store_purchases[1],
                    current_user.store_purchases[2]);
            fclose(file);
            return 0; 
        }
    }

    fclose(file);
    return -1; 
}


pid_t get_tid() {
    return syscall(SYS_gettid);
}


void int_to_str(int num, char *str, int base) {
    sprintf(str, "%d", num);
}


void logMessage(char *logFileName, char *message, ...) {
    pthread_mutex_lock(&file_mutex);

    FILE *logFile = fopen(logFileName, "a");  
    if (logFile == NULL) {
        perror("Unable to open log file");
        pthread_mutex_unlock(&file_mutex);
        pthread_exit(NULL);
    }

    va_list args;
    va_start(args, message);
    vfprintf(logFile, message, args);
    va_end(args);

    fclose(logFile);
    
    pthread_mutex_unlock(&file_mutex);
}


void createLogFile(char *path){
    FILE *logFile = fopen(path, "w");
    if (logFile == NULL)
    {
        perror("Unable to open log file");
        return;
    }
    fclose(logFile);
}


#endif 
