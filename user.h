#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>  

#define MAX_FILE_NAME_LENGTH 512
#define MAX_ITEM_LENGTH 256
#define MAX_ORDER_ITEMS 100
#define MAX_PRODUCTS_PER_STORE 100
#define MAX_QUANTITY_LENGTH 10
#define MAX_INPUT_LENGTH 100


void remove_spaces_and_newline(char *str) {
    int i = 0, j = 0;

    // پیمایش رشته
    while (str[i] != '\0') {
        // اگر کاراکتر اسپیس یا \n نباشد، آن را در رشته جدید قرار می‌دهیم
        if (str[i] != ' ' && str[i] != '\n') {
            str[j++] = str[i];
        }
        i++;
    }

    // پایان دادن به رشته جدید
    str[j] = '\0';
}



// مسیر پایگاه داده
#define DATABASE_FILE "/home/ali/Desktop/database.txt"




// ساختار برای ذخیره یوزرنیم و یوزرآیدی
struct user {
    char username[100];
    int user_id;
};



// تابع برای پیدا کردن یوزر در دیتابیس
int find_user_id(const char *username) {
    FILE *file = fopen(DATABASE_FILE, "r");
    if (file == NULL) {
        perror("Unable to open database file");
        return -1;
    }

    struct user u;
    while (fscanf(file, "%s %d\n", u.username, &u.user_id) != EOF) {
        if (strcmp(u.username, username) == 0) {
            fclose(file);
            return u.user_id;
        }
    }

    fclose(file);
    return -1;
}



// تابع برای اضافه کردن یوزر جدید به دیتابیس
int add_user_to_database(const char *username) {
    FILE *file = fopen(DATABASE_FILE, "a");
    if (file == NULL) {
        perror("Unable to open database file for writing");
        return -1;
    }

    srand(time(NULL));
    int new_user_id = rand() % 1000 + 1;
    fprintf(file, "%s %d\n", username, new_user_id);
    fclose(file);
    return new_user_id;
}







