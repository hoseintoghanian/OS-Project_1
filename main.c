#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

void processSubFolder(const char *folderName){
    printf("Processing folder: %s by (PID: %d)\n", folderName, getpid());
}

void process_folder(const char *folder_name) {
    printf("Processing folder: %s by (PID: %d)\n", folder_name, getpid());
    const char *base_dir = folder_name;
    const char *subfolders[] = {"Apparel", "Beauty", "Digital", "Food", "Home", "Market", "Sports", "Toys"};
    pid_t pid2;
    for (int i = 0; i < 7; i++)
    {
        char folder_path[512];
        snprintf(folder_path, sizeof(folder_path), "%s/%s", base_dir, subfolders[i]);
        pid2 = fork();
        if (pid2 == -1)
        {
            perror("fork failed");
            exit(1);
        }
        if (pid2 == 0 )
        {
            printf("PID %d create child for %s PID:%d\n", getppid(),subfolders[i],getpid());
            processSubFolder(folder_path);
            exit(0);
        }
        if (pid2>0)
        {   

        }
    
    }
    for (int i = 0; i < 7; i++)
    {
        wait(NULL);
    }
    
    printf("Parent process (PID: %d) completed\n", getpid());    
} 




// ساختار دیتابیس (فایل ساده)
#define DATABASE_FILE "/home/ali/Desktop/database.txt"

// ساختار برای ذخیره یوزرنیم و یوزرآیدی
struct user {
    char username[100];
    int user_id;
};


struct {
    char name[100];
    int Count;
    float price;
} Product;

#define MAX_ORDER_ITEMS 100
#define MAX_ITEM_LENGTH 50

// ساختار برای ذخیره هر آیتم سفارش
typedef struct {
    char item_name[MAX_ITEM_LENGTH];
    int quantity;
} Order;



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
            return u.user_id;  // یوزر پیدا شد
        }
    }

    fclose(file);
    return -1;  // یوزر پیدا نشد
}

// تابع برای اضافه کردن یوزر جدید به دیتابیس
int add_user_to_database(const char *username) {
    FILE *file = fopen(DATABASE_FILE, "a");
    if (file == NULL) {
        perror("Unable to open database file for writing");
        return -1;
    }

    // پیدا کردن یوزرآیدی منحصر به فرد
    srand(time(NULL));
    int new_user_id = rand() % 1000 + 1;  // برای سادگی از عدد تصادفی بین 1 و 1000 استفاده می‌کنیم

    // ذخیره یوزرنیم و یوزرآیدی در فایل
    fprintf(file, "%s %d\n", username, new_user_id);
    fclose(file);
    return new_user_id;  // یوزرآیدی جدید
}



int main() {
    Order orderlist[MAX_ORDER_ITEMS];
    int price_threshold;
    int order_count = 0;
    char username[100];
    int user_id;

    // دریافت یوزرنیم از کاربر
    printf("Enter the username: ");
    scanf("%s", username);

    // بررسی اینکه آیا یوزرنیم در دیتابیس هست یا نه
    user_id = find_user_id(username);

    if (user_id == -1) {
        // اگر یوزر پیدا نشد، یک یوزر جدید ایجاد می‌کنیم
        printf("User not found. Creating a new user...\n");
        user_id = add_user_to_database(username);
        if (user_id == -1) {
            printf("Failed to create user\n");
            return 1;
        }
        printf("New user %s created with user ID: %d\n", username, user_id);
    } else {
        printf("User %s found with user ID: %d\n", username, user_id);
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        exit(1);
    } else if (pid == 0) {
        // پراسس فرزند
        printf("%s created with PID: %d\n", username,getpid());
        printf("The user ID for this process is: %d\n", user_id);
//////////////////////////////////////////////////////////////////////////
        // گرفتن آیتم‌های سفارش تا زمانی که کاربر 'end' وارد کند
        printf("Enter orderlist (item name and quantity), type 'end' to stop:\n");
        while (1) {
            char item[MAX_ITEM_LENGTH];
            int quantity;

            // گرفتن نام آیتم و مقدار آن
            printf("Item name: ");
            scanf("%s", item);

            if (strcmp(item, "end") == 0) {
                break;  // اتمام ورودی‌ها
            }

            printf("Quantity: ");
            scanf("%d", &quantity);

            // ذخیره اطلاعات آیتم سفارش
            strcpy(orderlist[order_count].item_name, item);
            orderlist[order_count].quantity = quantity;
            order_count++;

            if (order_count >= MAX_ORDER_ITEMS) {
                printf("Maximum order items reached.\n");
                break;
            }
        }

        // گرفتن قیمت آستانه
        printf("Enter price threshold: ");
        char input[100];
        while (getchar() != '\n');
        if (fgets(input, sizeof(input), stdin)) {
            // اگر ورودی خالی باشد (یعنی کاربر فقط Enter زده باشد)
            if (input[0] == '\n') {
                price_threshold = 10000;
                printf("No input provided. Using default value: %d\n", price_threshold);
            } else {
                // اگر ورودی داده شده باشد، تبدیل ورودی به عدد
                if (sscanf(input, "%d", &price_threshold) == 1) {
                    printf("You entered: %d\n", price_threshold);
                } else {
                    printf("Invalid input. Using default value: %d\n", 10000);
                    price_threshold = 10000;
                }
            }
        }
        const char *base_dir = "/home/ali/Desktop/Dataset";
        const char *subfolders[] = {"Store1", "Store2", "Store3"};
        pid_t pid1;
        for (int i = 0; i < 3; i++)
        {
            char folder_path[512];
            snprintf(folder_path, sizeof(folder_path), "%s/%s", base_dir, subfolders[i]);
            pid1 = fork();
            if (pid1 == -1)
            {
                perror("fork failed");
                exit(1);
            }
            if (pid1 == 0 )
            {
                printf("PID %d create child for %s PID:%d\n", getppid(),subfolders[i],getpid());
                process_folder(folder_path);
                exit(0);
            }
            if (pid1>0)
            {   

            }
        
        }
        for (int i = 0; i < 3; i++)
        {
            wait(NULL);
        }
        printf("Parent process (PID: %d) completed\n", getpid());

        

            
//////////////////////////////////////////////////////////////
        exit(0);
    } else {
        // پراسس والد
        wait(NULL);

        printf("Parent process (PID: %d) completedd\n", getpid());
    }

    return 0;
}

