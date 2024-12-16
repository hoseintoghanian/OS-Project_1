#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>  
#include <stdarg.h>  
#include <sys/syscall.h>
#include <ctype.h>  // برای استفاده از isdigit
#include <limits.h> // برای PATH_MAX

#include "user.h" // فرض بر این است که این هدر فایل شامل توابع find_user_id و add_user_to_database است

#define MAX_ITEM_LENGTH 256
#define MAX_ORDER_ITEMS 100
#define MAX_PRODUCTS_PER_STORE 100

// تعریف ساختار محصول
typedef struct{
    char name[256];
    double price;
    double score;
    int entity;
    char file_path[PATH_MAX]; // مسیر فایل محصول
} Product;

// ساختار حافظه مشترک برای هر فروشگاه
typedef struct {
    Product products[MAX_PRODUCTS_PER_STORE];
    int count;
    pthread_mutex_t mutex;
} SharedStore;

// ساختار سفارش
typedef struct {
    char item_name[MAX_ITEM_LENGTH];
    int quantity;
} Order;

// ساختار اطلاعات فایل
typedef struct{
    char *folder_path;
    char *file_name;
    int storeID;
} FileInfo;

// ساختار آرگومان‌های ترد Orders
typedef struct {
    SharedStore *store1;
    SharedStore *store2;
    SharedStore *store3;
} OrdersThreadArgs;

// ساختار داده‌های مشترک برای انتقال بین تردها
typedef struct {
    int best_store_id;
    double total_price;
    int threshold;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int data_ready; // پرچم برای نشان دادن آماده بودن داده‌ها

    // افزودن موارد جدید
    pthread_cond_t scores_cond;
    int scores_update_needed;
    int purchase_finalized;
} SharedData;

// متغیرهای جهانی
SharedStore *sharedStore1;
SharedStore *sharedStore2;
SharedStore *sharedStore3;

Order orderlist[MAX_ORDER_ITEMS];
int order_count = 0;

// متغیر جهانی برای میوتکس فایل
pthread_mutex_t file_mutex;

// متغیر جهانی برای داده‌های مشترک بین تردها
SharedData sharedData;

// تابع برای دریافت Thread ID
pid_t get_tid() {
    return syscall(SYS_gettid);
}



// تابع لاگ‌نویسی
void logMessage(char *logFileName, char *message, ...) {
    // قفل کردن میوتکس قبل از نوشتن
    pthread_mutex_lock(&file_mutex);

    FILE *logFile = fopen(logFileName, "a");  // باز کردن یا ایجاد فایل لاگ
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
    // آزاد کردن میوتکس بعد از نوشتن
    pthread_mutex_unlock(&file_mutex);
}

// تابع پردازش فایل برای هر فروشگاه
void* processStoreFile(void* arg){
    FileInfo *fileInfo = (FileInfo *)arg;

    // تعیین نام فایل لاگ مخصوص به Store
    char logFileName[PATH_MAX];
    snprintf(logFileName, sizeof(logFileName), "%s/User1_Order0.log", fileInfo->folder_path);

    // لاگ‌گذاری توسط ترد
    logMessage(logFileName, "TID %ld child of PID %d reading file %s\n", (long)get_tid(), getpid(), fileInfo->file_name);

    // ساختن مسیر کامل فایل
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/%s", fileInfo->folder_path, fileInfo->file_name);

    // باز کردن فایل محصول
    FILE *productFile = fopen(filePath, "r");
    if (productFile == NULL) {
        perror("Unable to open product file");
        free(fileInfo->folder_path);
        free(fileInfo->file_name);
        free(fileInfo);
        pthread_exit(NULL);
    }

    // خواندن نام محصول
    char line[512];
    char productName[MAX_ITEM_LENGTH] = "";

    while (fgets(line, sizeof(line), productFile)) {
        if (strncmp(line, "Name:", 5) == 0) {
            sscanf(line, "Name: %[^\n]", productName);
            break;
        }
    }
    fclose(productFile);

    remove_spaces_and_newline(productName);

    // بررسی اینکه محصول در سفارش‌ها وجود دارد یا نه
    for (int i = 0; i < order_count; i++) {
        if (strcmp(productName, orderlist[i].item_name) == 0) {
            // اگر محصول در سفارش‌ها بود، اطلاعات کامل محصول را بخوانید
            FILE *file = fopen(filePath, "r");
            if (file == NULL) {
                perror("Unable to open file");
                pthread_exit(NULL);
            }

            Product product;
            memset(&product, 0, sizeof(Product));

            while (fgets(line, sizeof(line), file)) {
                if (strncmp(line, "Name:", 5) == 0) {
                    sscanf(line, "Name: %[^\n]", product.name);
                } else if (strncmp(line, "Price:", 6) == 0) {
                    sscanf(line, "Price: %lf", &product.price);
                } else if (strncmp(line, "Score:", 6) == 0) {
                    sscanf(line, "Score: %lf", &product.score);
                } else if (strncmp(line, "Entity:", 7) == 0) {
                    sscanf(line, "Entity: %d", &product.entity);
                }
            }
            fclose(file);

            logMessage(logFileName, "TID %ld child of PID %d found %s product\n", (long)get_tid(), getpid(), product.name);

            remove_spaces_and_newline(product.name);

            // تعیین مسیر فایل محصول
            strcpy(product.file_path, filePath);

            // تعیین فروشگاه هدف
            SharedStore *targetStore = NULL;
            if (fileInfo->storeID == 1) {
                targetStore = sharedStore1;
            }
            else if (fileInfo->storeID == 2) {
                targetStore = sharedStore2;
            }
            else if (fileInfo->storeID == 3) {
                targetStore = sharedStore3;
            } else {
                // شناسه فروشگاه نامعتبر
                free(fileInfo->folder_path);
                free(fileInfo->file_name);
                free(fileInfo);
                pthread_exit(NULL);
            }

            // اضافه کردن محصول به حافظه مشترک مربوطه با استفاده از میوتکس
            if (targetStore != NULL) {
                pthread_mutex_lock(&targetStore->mutex);
                if (targetStore->count < MAX_PRODUCTS_PER_STORE) {
                    targetStore->products[targetStore->count++] = product;
                }
                pthread_mutex_unlock(&targetStore->mutex);
            }
        }
    }

    // آزادسازی حافظه
    free(fileInfo->folder_path);
    free(fileInfo->file_name);
    free(fileInfo);

    pthread_exit(NULL);
}

// ایجاد فایل لاگ
void createLogFile(char *path){
    FILE *logFile = fopen(path, "w");
    if (logFile == NULL)
    {
        perror("Unable to open log file");
        return;
    }
    fclose(logFile);
}

// تابع برای ساخت ترد برای هر فایل داخل زیرپوشه‌های کتگوری
void processSubFolderFiles(char *path, int storeID){
    DIR *dir = opendir(path);
    if (dir == NULL)
    {
        perror("Unable to open directory");
        return;
    }

    struct dirent *entry;
    pthread_t threads[100];
    int threadCount = 0;

    // ایجاد مسیر فایل لاگ مخصوص به Store
    char logFileName[PATH_MAX];
    snprintf(logFileName, sizeof(logFileName), "%s/User1_Order0.log", path);
    createLogFile(logFileName);

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".txt") != NULL) {
            FileInfo *fileInfo = (FileInfo *)malloc(sizeof(FileInfo));
            fileInfo->folder_path = strdup(path);  // کپی ایمن مسیر
            fileInfo->file_name = strdup(entry->d_name); // کپی ایمن نام فایل
            fileInfo->storeID = storeID;
            if (pthread_create(&threads[threadCount], NULL, processStoreFile, ((void *)fileInfo)) != 0) {
                perror("Failed to create thread");
                free(fileInfo->folder_path);
                free(fileInfo->file_name);
                free(fileInfo);
            }
            threadCount++;
        }
    }

    for (int i = 0; i < threadCount; i++) {
        pthread_join(threads[i], NULL);
    }

    closedir(dir);
}

// توابعی که در فرآیندهای Store اجرا می‌شوند
void storeProcess(char *path, int storeID) {
    printf("PID %d create child for %s PID: %d\n", getppid(), path, getpid());

    // آرایه کتگوری‌ها
    const char *categories[8] = {"Digital", "Home", "Apparel", "Food", "Market", "Toys", "Beauty", "Sports"};
    pid_t cat_pids[8];

    for (int i = 0; i < 8; i++) {
        cat_pids[i] = fork();
        if (cat_pids[i] < 0) {
            perror("fork cat failed");
            exit(1);
        }
        else if (cat_pids[i] == 0) {
            printf("PID %d create child for %s PID:%d\n", getppid(), categories[i], getpid());
            char categoryPath[PATH_MAX];
            snprintf(categoryPath, sizeof(categoryPath), "%s/%s", path, categories[i]);
            processSubFolderFiles(categoryPath, storeID);
            exit(0);
        }
    }

    // انتظار برای اتمام فرایندهای کتگوری
    for (int i = 0; i < 8; i++) {
        waitpid(cat_pids[i], NULL, 0);
    }

    // در اینجا دیگر حافظه مشترک و مقادیر آن پر شده‌اند
    // حافظه مشترک در فرآیند پدر مدیریت می‌شود
}

// تابع Orders برای انتخاب بهترین فروشگاه
void* ordersThreadFunction(void* arg) {
    OrdersThreadArgs *args = (OrdersThreadArgs *)arg;
    SharedStore *store1 = args->store1;
    SharedStore *store2 = args->store2;
    SharedStore *store3 = args->store3;

    printf("PID %d create thread for Orders TID: %ld\n", getpid() ,(long)get_tid());

    double sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;

    // محاسبه مجموع ضرب امتیاز در قیمت برای فروشگاه اول
    pthread_mutex_lock(&store1->mutex);
    for (int i = 0; i < store1->count; i++) {
        sum1 += store1->products[i].score * store1->products[i].price;
    }
    pthread_mutex_unlock(&store1->mutex);

    // محاسبه مجموع ضرب امتیاز در قیمت برای فروشگاه دوم
    pthread_mutex_lock(&store2->mutex);
    for (int i = 0; i < store2->count; i++) {
        sum2 += store2->products[i].score * store2->products[i].price;
    }
    pthread_mutex_unlock(&store2->mutex);

    // محاسبه مجموع ضرب امتیاز در قیمت برای فروشگاه سوم
    pthread_mutex_lock(&store3->mutex);
    for (int i = 0; i < store3->count; i++) {
        sum3 += store3->products[i].score * store3->products[i].price;
    }
    pthread_mutex_unlock(&store3->mutex);

    printf("Store1 sum(score * price): %.2f\n", sum1);
    printf("Store2 sum(score * price): %.2f\n", sum2);
    printf("Store3 sum(score * price): %.2f\n", sum3);

    // انتخاب بهترین فروشگاه
    double max_sum = sum1;
    int best_store = 1;
    if (sum2 > max_sum) {
        max_sum = sum2;
        best_store = 2;
    }
    if (sum3 > max_sum) {
        max_sum = sum3;
        best_store = 3;
    }

    printf("Best store is Store%d with sum(score * price): %.2f\n", best_store, max_sum);
    logMessage("best_store.log", "Best store is Store%d with sum(score * price): %.2f\n", best_store, max_sum);

    // ذخیره بهترین فروشگاه و قیمت کل در sharedData
    pthread_mutex_lock(&sharedData.mutex);
    sharedData.best_store_id = best_store;
    sharedData.total_price = max_sum;
    sharedData.data_ready = 1;
    pthread_cond_signal(&sharedData.cond); // سیگنال به ترد Final
    pthread_mutex_unlock(&sharedData.mutex);

    return NULL;
}

// تابع Scores
void* scoresThreadFunction(void* arg) {
    printf("PID %d create thread for Scores TID: %ld\n", getpid() ,(long)get_tid());

    // منتظر سیگنال شدن scores_update_needed
    pthread_mutex_lock(&sharedData.mutex);
    while (sharedData.scores_update_needed == 0) {
        pthread_cond_wait(&sharedData.scores_cond, &sharedData.mutex);
    }
    int purchase_finalized = sharedData.purchase_finalized;
    pthread_mutex_unlock(&sharedData.mutex);

    if (purchase_finalized) {
        // درخواست امتیاز از کاربر برای هر محصول
        for (int i = 0; i < order_count; i++) {
            char *item_name = orderlist[i].item_name;
            double new_score;
            printf("Enter score for product '%s': ", item_name);
            if (scanf("%lf", &new_score) != 1) {
                printf("Invalid input for score. Skipping.\n");
                // پاکسازی بافر ورودی
                int c;
                while ((c = getchar()) != '\n' && c != EOF);
                continue;
            }

            // پیدا کردن محصول در فروشگاه بهترین
            SharedStore *best_store;
            pthread_mutex_lock(&sharedData.mutex);
            int best_store_id = sharedData.best_store_id;
            pthread_mutex_unlock(&sharedData.mutex);

            if (best_store_id == 1) {
                best_store = sharedStore1;
            } else if (best_store_id == 2) {
                best_store = sharedStore2;
            } else if (best_store_id == 3) {
                best_store = sharedStore3;
            } else {
                printf("Invalid best_store_id: %d\n", best_store_id);
                continue;
            }

            pthread_mutex_lock(&best_store->mutex);
            int found = 0;
            char filePath[PATH_MAX];
            for (int j = 0; j < best_store->count; j++) {
                if (strcmp(best_store->products[j].name, item_name) == 0) {
                    // به‌روزرسانی امتیاز در حافظه مشترک
                    best_store->products[j].score = new_score;
                    strcpy(filePath, best_store->products[j].file_path);
                    found = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&best_store->mutex);

            if (found) {
                // به‌روزرسانی امتیاز در فایل
                FILE *prod_file = fopen(filePath, "r+");
                if (prod_file == NULL) {
                    perror("Unable to open product file for updating score");
                    continue;
                }

                // خواندن محتویات فایل
                char file_contents[4096];
                size_t read_size = fread(file_contents, 1, sizeof(file_contents) - 1, prod_file);
                if (read_size < 0) {
                    perror("Error reading product file");
                    fclose(prod_file);
                    continue;
                }
                file_contents[read_size] = '\0';

                // پیدا کردن خط "Score:"
                char *score_line = strstr(file_contents, "Score:");
                if (score_line != NULL) {
                    char *line_end = strchr(score_line, '\n');
                    if (line_end != NULL) {
                        size_t before_score = score_line - file_contents;
                        size_t line_length = line_end - score_line + 1;
                        char new_score_line[100];
                        snprintf(new_score_line, sizeof(new_score_line), "Score: %.2f\n", new_score);

                        char new_file_contents[4096];
                        snprintf(new_file_contents, sizeof(new_file_contents), "%.*s%s%s",
                                 (int)before_score,
                                 file_contents,
                                 new_score_line,
                                 line_end + 1);

                        // نوشتن محتویات جدید در فایل
                        rewind(prod_file);
                        if (fputs(new_file_contents, prod_file) == EOF) {
                            perror("Error writing updated score to product file");
                        }
                    } else {
                        printf("No newline after Score in file '%s'\n", filePath);
                    }
                } else {
                    printf("No 'Score:' line found in product file '%s'\n", filePath);
                }

                fclose(prod_file);
                printf("Updated score for product '%s' to %.2f\n", item_name, new_score);
                logMessage("scores.log", "Updated score for product '%s' to %.2f\n", item_name, new_score);
            } else {
                printf("Product '%s' not found in Store%d\n", item_name, best_store_id);
            }
        }
    } else {
        printf("Purchase was not finalized. No scores to update.\n");
    }

    return NULL;
}

// تابع Final
void* finalThreadFunction(void* arg) {
    printf("PID %d create thread for Final TID: %ld\n", getpid() ,(long)get_tid());

    // انتظار برای سیگنال شدن داده‌ها
    pthread_mutex_lock(&sharedData.mutex);
    while (sharedData.data_ready == 0) { // منتظر سیگنال شدن داده‌ها
        pthread_cond_wait(&sharedData.cond, &sharedData.mutex);
    }

    int best_store_id = sharedData.best_store_id;
    int threshold = sharedData.threshold;
    pthread_mutex_unlock(&sharedData.mutex);

    printf("Final Thread: Best Store ID: %d, Threshold: %d\n", best_store_id, threshold);

    // تعیین فروشگاه هدف
    SharedStore *best_store = NULL;
    if (best_store_id == 1) {
        best_store = sharedStore1;
    }
    else if (best_store_id == 2) {
        best_store = sharedStore2;
    }
    else if (best_store_id == 3) {
        best_store = sharedStore3;
    } else {
        printf("Invalid best_store_id: %d\n", best_store_id);
        return NULL;
    }

    double total_price = 0.0;

    // محاسبه قیمت کل لیست خرید از فروشگاه بهترین
    pthread_mutex_lock(&best_store->mutex);
    for (int i = 0; i < order_count; i++) {
        for (int j = 0; j < best_store->count; j++) {
            if (strcmp(orderlist[i].item_name, best_store->products[j].name) == 0) {
                total_price += best_store->products[j].price * orderlist[i].quantity;
                break;
            }
        }
    }
    pthread_mutex_unlock(&best_store->mutex);

    printf("Final Thread: Total Price of Best Purchase List: %.2f\n", total_price);
    logMessage("final.log", "Final Thread: Total Price of Best Purchase List: %.2f\n", total_price);

    // مقایسه با آستانه
    if (total_price <= threshold) {
        printf("Final Price %.2f is within the threshold %d. Finalizing purchase.\n", total_price, threshold);
        logMessage("final.log", "Final Price %.2f is within the threshold %d. Purchase finalized.\n", total_price, threshold);

        // تنظیم پرچم نهایی شدن خرید و سیگنال دادن به Scores
        pthread_mutex_lock(&sharedData.mutex);
        sharedData.purchase_finalized = 1;
        sharedData.scores_update_needed = 1;
        pthread_cond_signal(&sharedData.scores_cond);
        pthread_mutex_unlock(&sharedData.mutex);
    } else {
        printf("Final Price %.2f exceeds the threshold %d. Purchase not finalized.\n", total_price, threshold);
        logMessage("final.log", "Final Price %.2f exceeds the threshold %d. Purchase not finalized.\n", total_price, threshold);

        // تنظیم پرچم عدم نهایی شدن خرید و سیگنال دادن به Scores
        pthread_mutex_lock(&sharedData.mutex);
        sharedData.purchase_finalized = 0;
        sharedData.scores_update_needed = 1;
        pthread_cond_signal(&sharedData.scores_cond);
        pthread_mutex_unlock(&sharedData.mutex);
    }

    return NULL;
}

int main() {

    int price_threshold;
    char username[100];
    int user_id;

    // مقداردهی اولیه میوتکس فایل
    if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        printf("میوتکس نتوانست مقداردهی اولیه شود\n");
        exit(1);
    }

    // مقداردهی اولیه ساختار داده‌های مشترک
    sharedData.best_store_id = -1;
    sharedData.total_price = 0.0;
    sharedData.threshold = 0; // مقدار اولیه، بعداً تنظیم می‌شود
    sharedData.data_ready = 0;
    sharedData.scores_update_needed = 0;
    sharedData.purchase_finalized = 0;

    if (pthread_mutex_init(&sharedData.mutex, NULL) != 0) {
        perror("Mutex initialization failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&sharedData.cond, NULL) != 0) {
        perror("Condition variable initialization failed");
        exit(EXIT_FAILURE);
    }

    if (pthread_cond_init(&sharedData.scores_cond, NULL) != 0) {
        perror("Scores condition variable initialization failed");
        exit(EXIT_FAILURE);
    }

    // تنظیم ویژگی‌های میوتکس برای حافظه‌های مشترک
    pthread_mutexattr_t mutexAttr;
    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_SHARED);

    // ایجاد حافظه‌های مشترک در فرآیند پدر
    sharedStore1 = mmap(NULL, sizeof(SharedStore), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sharedStore1 == MAP_FAILED) {
        perror("mmap sharedStore1 failed");
        exit(EXIT_FAILURE);
    }
    sharedStore1->count = 0;
    if (pthread_mutex_init(&sharedStore1->mutex, &mutexAttr) != 0) {
        printf("Failed to initialize mutex for sharedStore1\n");
        exit(1);
    }

    sharedStore2 = mmap(NULL, sizeof(SharedStore), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sharedStore2 == MAP_FAILED) {
        perror("mmap sharedStore2 failed");
        exit(EXIT_FAILURE);
    }
    sharedStore2->count = 0;
    if (pthread_mutex_init(&sharedStore2->mutex, &mutexAttr) != 0) {
        printf("Failed to initialize mutex for sharedStore2\n");
        exit(1);
    }

    sharedStore3 = mmap(NULL, sizeof(SharedStore), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sharedStore3 == MAP_FAILED) {
        perror("mmap sharedStore3 failed");
        exit(EXIT_FAILURE);
    }
    sharedStore3->count = 0;
    if (pthread_mutex_init(&sharedStore3->mutex, &mutexAttr) != 0) {
        printf("Failed to initialize mutex for sharedStore3\n");
        exit(1);
    }

    // پاکسازی ویژگی‌های میوتکس
    pthread_mutexattr_destroy(&mutexAttr);

    printf("Enter the username: ");
    scanf("%s", username);

    user_id = find_user_id(username);

    if (user_id == -1) {
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
        // فرآیند فرزند

        printf("Enter orderlist (item name and quantity), type 'end' to stop:\n");
        while (1) {
            char item[MAX_ITEM_LENGTH];
            int quantity;
            printf("Item name and quantity: ");
            // استفاده از %[^\n] برای خواندن نام تا اولین عدد
            if (scanf(" %[^0-9]%d", item, &quantity) != 2) {
                // در صورت عدم موفقیت در خواندن، قطع حلقه
                break;
            }
            remove_spaces_and_newline(item);

            if (strcmp(item, "end") == 0) {
                break;
            }
            printf("Item: %s\n", item);
            printf("Quantity: %d\n", quantity);

            if (order_count < MAX_ORDER_ITEMS) {
                strcpy(orderlist[order_count].item_name, item);
                orderlist[order_count].quantity = quantity;
                order_count++;
            } else {
                printf("Maximum order items reached.\n");
                break;
            }
        }

        printf("Enter price threshold: ");
        char input[100];
        while (getchar() != '\n'); // پاکسازی بافر ورودی
        if (fgets(input, sizeof(input), stdin)) {
            if (input[0] == '\n') {
                price_threshold = 10000;
                printf("No input provided. Using default value: %d\n", price_threshold);
            } else {
                if (sscanf(input, "%d", &price_threshold) == 1) {
                    printf("You entered: %d\n", price_threshold);
                } else {
                    printf("Invalid input. Using default value: %d\n", 10000);
                    price_threshold = 10000;
                }
            }
        } else {
            price_threshold = 10000;
            printf("No input provided. Using default value: %d\n", price_threshold);
        }

        // تنظیم آستانه در sharedData
        pthread_mutex_lock(&sharedData.mutex);
        sharedData.threshold = price_threshold;
        pthread_mutex_unlock(&sharedData.mutex);

        printf("%s created with PID: %d\n", username, getpid());

        // ایجاد فرآیندهای فروشگاه‌ها
        pid_t pid1, pid2, pid3;

        // ایجاد فرآیند اول (Store1)
        pid1 = fork();
        if (pid1 < 0) {
            perror("Fork failed for process 1");
            exit(1);
        }

        if (pid1 == 0) {
            char store1Path[] = "/home/ali/Desktop/Dataset/Store1";
            storeProcess(store1Path, 1);
            exit(0);
        }
        waitpid(pid1, NULL, 0);

        // ایجاد فرآیند دوم (Store2)
        pid2 = fork();
        if (pid2 < 0) {
            perror("Fork failed for process 2");
            exit(1);
        }

        if (pid2 == 0) {
            char store2Path[] = "/home/ali/Desktop/Dataset/Store2";
            storeProcess(store2Path, 2);
            exit(0);
        }
        waitpid(pid2, NULL, 0);

        // ایجاد فرآیند سوم (Store3)
        pid3 = fork();
        if (pid3 < 0) {
            perror("Fork failed for process 3");
            exit(1);
        }

        if (pid3 == 0) {
            char store3Path[] = "/home/ali/Desktop/Dataset/Store3";
            storeProcess(store3Path, 3);
            exit(0);
        }
        waitpid(pid3, NULL, 0);

        // اکنون داده‌ها در حافظه‌های مشترک قرار دارند، می‌توان تردها را ایجاد کرد

        // ایجاد ساختار آرگومان‌ها برای ترد Orders
        OrdersThreadArgs args;
        args.store1 = sharedStore1;
        args.store2 = sharedStore2;
        args.store3 = sharedStore3;

        // ایجاد ترد Orders
        pthread_t OrdersThread;
        if (pthread_create(&OrdersThread, NULL, ordersThreadFunction, &args) != 0) {
            perror("pthread_create failed for OrdersThread");
            exit(1);
        }

        // ایجاد ترد Scores
        pthread_t Scoresthread;
        if (pthread_create(&Scoresthread, NULL, scoresThreadFunction, NULL) != 0) {
            perror("pthread_create failed for Scoresthread");
            exit(1);
        }

        // ایجاد ترد Final
        pthread_t Finalthread;
        if (pthread_create(&Finalthread, NULL, finalThreadFunction, NULL) != 0) {
            perror("pthread_create failed for Finalthread");
            exit(1);
        }

        // انتظار برای اتمام تمامی تردها
        pthread_join(OrdersThread, NULL);
        pthread_join(Scoresthread, NULL);
        pthread_join(Finalthread, NULL);

        // پاکسازی منابع: نابود کردن میوتکس‌ها و آزادسازی حافظه مشترک
        pthread_mutex_destroy(&sharedStore1->mutex);
        munmap(sharedStore1, sizeof(SharedStore));
        pthread_mutex_destroy(&sharedStore2->mutex);
        munmap(sharedStore2, sizeof(SharedStore));
        pthread_mutex_destroy(&sharedStore3->mutex);
        munmap(sharedStore3, sizeof(SharedStore));

        // نابود کردن میوتکس فایل
        pthread_mutex_destroy(&file_mutex);

        // نابود کردن sharedData
        pthread_mutex_destroy(&sharedData.mutex);
        pthread_cond_destroy(&sharedData.cond);
        pthread_cond_destroy(&sharedData.scores_cond);

    } else {
        // فرآیند پدر
        wait(NULL);
    }

    return 0;
}
