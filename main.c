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
#include <fcntl.h>  // برای استفاده از O_CREAT

#define MAX_FILE_NAME_LENGTH 512
#define MAX_ORDER_ITEMS 100
#define MAX_ITEM_LENGTH 50

// ساختار برای ذخیره اطلاعات سفارش
typedef struct {
    char item_name[MAX_ITEM_LENGTH];
    int quantity;
} Order;

// ساختار برای ذخیره اطلاعات فایل و مسیر آن
typedef struct {
    char *folder_path;  
    char *file_name;
    Order *orderlist;   
    int order_count;
} FileInfo;

// ساختار داده مشترک در حافظه مشترک
struct Shared {
    float p;
    float s;
};

// مسیر پایگاه داده
#define DATABASE_FILE "/home/ali/Desktop/database.txt"

// ساختار برای ذخیره یوزرنیم و یوزرآیدی
struct user {
    char username[100];
    int user_id;
};

// متغیرهای سراسری برای والد
static key_t key;
static int shmid;
static struct Shared *shared_memory;
static sem_t *sem;

int contains_order_item(const char *line, const char *item_name) {
    char line_copy[512];
    strcpy(line_copy, line);

    char *token = strtok(line_copy, " \t\n");
    while (token != NULL) {
        if (strcmp(token, item_name) == 0) {
            return 1;
        }
        token = strtok(NULL, " \t\n");
    }
    return 0;
}

// تابع برای استخراج قیمت و امتیاز از فایل
int extract_price_and_score(FILE *input_file, float *price, float *score) {
    char line[512];
    *price = 0.0;
    *score = 0.0;
    fseek(input_file, 0, SEEK_SET);

    while (fgets(line, sizeof(line), input_file)) {
        if (strstr(line, "Price:") != NULL) {
            sscanf(line, "Price: %f", price);
        }
        if (strstr(line, "Score:") != NULL) {
            sscanf(line, "Score: %f", score);
        }
    }

    return (*price != 0.0 && *score != 0.0) ? 1 : 0;
}

// این تابع در تردهای فرزند برای پردازش فایل اجرا می‌شود
void *process_file(void *arg) {
    FileInfo *file_info = (FileInfo *)arg;
    char input_file_path[MAX_FILE_NAME_LENGTH];
    char output_file_path[MAX_FILE_NAME_LENGTH];

    snprintf(input_file_path, sizeof(input_file_path), "%s/%s", file_info->folder_path, file_info->file_name);
    snprintf(output_file_path, sizeof(output_file_path), "%s %s", file_info->file_name, "output.txt");

    FILE *input_file = fopen(input_file_path, "r");
    if (input_file == NULL) {
        perror("Unable to open input file");
        pthread_exit(NULL);
    }

    FILE *output_file = fopen(output_file_path, "a");
    if (output_file == NULL) {
        perror("Unable to open output file");
        fclose(input_file);
        pthread_exit(NULL);
    }

    // در اینجا فرض بر این است که این کد در تردهای فرزند اجرا می‌شود
    // بنابراین سمافور و حافظه مشترک قبلاً توسط والد ایجاد شده‌اند
    // ما فقط با sem_open بدون O_CREAT سمافور را باز می‌کنیم
    sem_t *local_sem = sem_open("semaphore1", 0);
    if (local_sem == SEM_FAILED) {
        perror("sem_open failed in thread (child)");
        fclose(input_file);
        fclose(output_file);
        pthread_exit(NULL);
    }

    int local_shmid = shmget(key, sizeof(struct Shared), 0666);
    if (local_shmid == -1) {
        perror("shmget failed in thread");
        fclose(input_file);
        fclose(output_file);
        pthread_exit(NULL);
    }

    struct Shared *local_shared = (struct Shared *)shmat(local_shmid, NULL, 0);
    if (local_shared == (struct Shared *)-1) {
        perror("shmat failed in thread");
        fclose(input_file);
        fclose(output_file);
        pthread_exit(NULL);
    }

    char line[512];
    fseek(input_file, 0, SEEK_SET);

    while (fgets(line, sizeof(line), input_file)) {
        for (int i = 0; i < file_info->order_count; i++) {
            if (contains_order_item(line, file_info->orderlist[i].item_name)) {
                float price = 0.0, score = 0.0;
                fseek(input_file, 0, SEEK_SET);
                if (extract_price_and_score(input_file, &price, &score)) {
                    float total_price = price * score * file_info->orderlist[i].quantity;
                    fprintf(output_file, "%d Item: %s, Quantity: %d, Price: %.2f, Score: %.2f, Total: %.2f found in file %s, processed by thread TID: %lu\n",
                            i, file_info->orderlist[i].item_name, file_info->orderlist[i].quantity,
                            price, score, total_price, file_info->file_name, (unsigned long)pthread_self());

                    // نوشتن در حافظه مشترک با استفاده از سمافور
                    sem_wait(local_sem);
                    local_shared->p = price;
                    local_shared->s = score;
                    sem_post(local_sem);

                } else {
                    fprintf(output_file, "Item: %s, Quantity: %d found in file %s, processed by thread TID: %lu, but price and score could not be extracted.\n",
                            file_info->orderlist[i].item_name, file_info->orderlist[i].quantity,
                            file_info->file_name, (unsigned long)pthread_self());
                }
            }
        }
    }

    shmdt(local_shared);
    fclose(input_file);
    fclose(output_file);
    free(file_info);
    pthread_exit(NULL);
}

// تابع برای پردازش فایل‌های یک زیرپوشه و ایجاد تردها
void processSubFolderFiles(const char *folder_path, Order *orderlist, int order_count) {
    DIR *dir = opendir(folder_path);
    if (dir == NULL) {
        perror("Unable to open directory");
        return;
    }

    struct dirent *entry;
    pthread_t threads[100]; 
    int thread_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            FileInfo *file_info = (FileInfo *)malloc(sizeof(FileInfo));
            file_info->folder_path = (char *)folder_path;
            file_info->file_name = entry->d_name;
            file_info->orderlist = orderlist;
            file_info->order_count = order_count;

            if (pthread_create(&threads[thread_count], NULL, process_file, (void *)file_info) != 0) {
                perror("Failed to create thread");
            }
            thread_count++;
        }
    }

    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    closedir(dir);
}

// تابع پردازش هر پوشه (استورها) که شامل زیرپوشه‌ها است
void process_folder(const char *folder_name, Order *orderlist, int order_count) {
    printf("Processing folder: %s by PID: %d\n", folder_name, getpid());

    const char *subfolders[] = {"Apparel", "Beauty", "Digital", "Food", "Home", "Market", "Sports", "Toys"};
    for (int i = 0; i < 7; i++) {
        char folder_path[512];
        snprintf(folder_path, sizeof(folder_path), "%s/%s", folder_name, subfolders[i]);

        pid_t pid2 = fork();
        if (pid2 == -1) {
            perror("Fork failed");
            exit(1);
        }

        if (pid2 == 0) {
            printf("Child process for folder: %s, PID: %d\n", subfolders[i], getpid());
            processSubFolderFiles(folder_path, orderlist, order_count);
            exit(0);
        }
    }

    for (int i = 0; i < 7; i++) {
        wait(NULL);
    }

    printf("Parent process (PID: %d) completed subfolders\n", getpid());
}

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

int main() {
    Order orderlist[MAX_ORDER_ITEMS];
    int price_threshold;
    int order_count = 0;
    char username[100];
    int user_id;

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

    // ابتدا والد حافظه مشترک و سمافور را ایجاد می‌کند
    key = ftok("/tmp", 'A');
    if (key == -1) {
        perror("ftok failed");
        exit(1);
    }

    shmid = shmget(key, sizeof(struct Shared), 0666 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }

    shared_memory = (struct Shared *)shmat(shmid, NULL, 0);
    if (shared_memory == (struct Shared *)-1) {
        perror("shmat failed");
        exit(1);
    }

    // مقدار اولیه حافظه مشترک
    shared_memory->p = 0.0;
    shared_memory->s = 0.0;

    // ایجاد سمافور توسط والد با O_CREAT
    sem = sem_open("semaphore1", O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open failed in parent");
        exit(1);
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        exit(1);
    } else if (pid == 0) {
        // فرایند فرزند
        printf("%s created with PID: %d\n", username, getpid());

        printf("Enter orderlist (item name and quantity), type 'end' to stop:\n");
        while (1) {
            char item[MAX_ITEM_LENGTH];
            int quantity;
            printf("Item name: ");
            scanf("%s", item);

            if (strcmp(item, "end") == 0) {
                break;
            }

            printf("Quantity: ");
            scanf("%d", &quantity);

            strcpy(orderlist[order_count].item_name, item);
            orderlist[order_count].quantity = quantity;
            order_count++;

            if (order_count >= MAX_ORDER_ITEMS) {
                printf("Maximum order items reached.\n");
                break;
            }
        }

        printf("Enter price threshold: ");
        char input[100];
        while (getchar() != '\n');
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
        }

        const char *base_dir = "/home/ali/Desktop/Dataset";
        const char *subfolders[] = {"Store1", "Store2", "Store3"};

        // ایجاد فرایندهایی برای استورها
        for (int i = 0; i < 3; i++) {
            char folder_path[512];
            snprintf(folder_path, sizeof(folder_path), "%s/%s", base_dir, subfolders[i]);

            pid_t pid1 = fork();
            if (pid1 == -1) {
                perror("Fork failed");
                exit(1);
            }

            if (pid1 == 0) {
                process_folder(folder_path, orderlist, order_count);
                exit(0);
            }
        }

        for (int i = 0; i < 3; i++) {
            wait(NULL);
        }

        // اکنون داده‌ها در حافظه مشترک ممکن است توسط تردها به‌روز شده باشند
        sem_t *child_sem = sem_open("semaphore1", 0);
        if (child_sem == SEM_FAILED) {
            perror("sem_open in child failed");
            exit(1);
        }

        int child_shmid = shmget(key, sizeof(struct Shared), 0666);
        if (child_shmid == -1) {
            perror("shmget in child failed");
            exit(1);
        }

        struct Shared *child_shared = (struct Shared *)shmat(child_shmid, NULL, 0);
        if (child_shared == (struct Shared *)-1) {
            perror("shmat in child failed");
            exit(1);
        }

        sem_wait(child_sem);
        printf("Child process read shared memory final: p = %f, s = %f\n", child_shared->p, child_shared->s);
        sem_post(child_sem);

        shmdt(child_shared);
        printf("Child process (PID: %d) completed\n", getpid());
        exit(0);
    } else {
        // فرایند والد منتظر اتمام فرزند
        wait(NULL);

        // پس از اتمام همه چیز، منابع را آزاد می‌کند
        if (shmdt(shared_memory) == -1) {
            perror("shmdt failed");
        }

        if (shmctl(shmid, IPC_RMID, NULL) == -1) {
            perror("shmctl failed");
        }

        sem_close(sem);
        sem_unlink("semaphore1");

        printf("Parent process (PID: %d) completed\n", getpid());
    }

    return 0;
}
