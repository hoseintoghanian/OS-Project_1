#include <gtk/gtk.h>
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
#include <ctype.h>  
#include <limits.h> 
#include <time.h>   


#include "user.h" 


/* To Start Project correctly copy this command and paste it in opened external terminal in this folder:
  gcc -g projectFinal.c $(pkg-config --cflags --libs gtk+-3.0 harfbuzz) -o projectFinal
    ./projectFinal
*/

typedef struct{
    char name[256];
    double price;
    double score;
    int entity;
    char file_path[PATH_MAX]; 
} Product;

typedef struct {
    Product products[MAX_PRODUCTS_PER_STORE];
    int count;
    pthread_mutex_t mutex;
} SharedStore;

typedef struct {
    char item_name[MAX_ITEM_LENGTH];
    int quantity;
} Order;

typedef struct{
    char *folder_path;
    char *file_name;
    int storeID;
} FileInfo;

typedef struct {
    SharedStore *store1;
    SharedStore *store2;
    SharedStore *store3;
} OrdersThreadArgs;

typedef struct {
    int best_store_id;
    double total_price;
    int threshold;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int data_ready; 
    pthread_cond_t scores_cond;
    int scores_update_needed;
    int purchase_finalized;
} SharedData;

struct user_info {
    struct user user;
    int discount_applied;
};

struct user_info current_user;

SharedStore *sharedStore1;
SharedStore *sharedStore2;
SharedStore *sharedStore3;

SharedData sharedData;

Order orderlist[MAX_ORDER_ITEMS];
int order_count = 0;

GtkWidget *output_label;
GtkWidget *window;



// Function to update a specific field in a product file
// If the field exists, it updates it; otherwise, it appends the field at the end
int update_product_field(const char *file_path, const char *field, const char *new_value) {
    FILE *prod_file = fopen(file_path, "r+");
    if (prod_file == NULL) {
        perror("Unable to open product file for updating");
        return -1;
    }

    char file_contents[8192];
    size_t read_size = fread(file_contents, 1, sizeof(file_contents) - 1, prod_file);
    if (read_size < 0) {
        perror("Error reading product file");
        fclose(prod_file);
        return -1;
    }
    file_contents[read_size] = '\0';

    char search_pattern[50];
    snprintf(search_pattern, sizeof(search_pattern), "%s:", field);
    char *field_line = strstr(file_contents, search_pattern);

    char new_field_line[256];
    snprintf(new_field_line, sizeof(new_field_line), "%s: %s\n", field, new_value);

    if (field_line != NULL) {
        char *line_end = strchr(field_line, '\n');
        if (line_end != NULL) {
            size_t before_field = field_line - file_contents;
            size_t line_length = line_end - field_line + 1;

            char new_file_contents[8192];
            snprintf(new_file_contents, sizeof(new_file_contents), "%.*s%s%s",
                     (int)before_field,
                     file_contents,
                     new_field_line,
                     line_end + 1);

            rewind(prod_file);
            if (fputs(new_file_contents, prod_file) == EOF) {
                perror("Error writing updated field to product file");
                fclose(prod_file);
                return -1;
            }
        } else {
            fseek(prod_file, 0, SEEK_END);
            if (fputs(new_field_line, prod_file) == EOF) {
                perror("Error appending new field to product file");
                fclose(prod_file);
                return -1;
            }
        }
    } else {
        fseek(prod_file, 0, SEEK_END);
        if (fputs(new_field_line, prod_file) == EOF) {
            perror("Error appending new field to product file");
            fclose(prod_file);
            return -1;
        }
    }

    fflush(prod_file); 
    fclose(prod_file);
    return 0;
}


double apply_discount(double total_price, int store_id) {
    if (current_user.user.store_purchases[store_id - 1] == 1) {
        double discounted_price = total_price * 0.9;
        printf("10%% discount applied! Original Price: %.2f, Discounted Price: %.2f\n", total_price, discounted_price);
        return discounted_price;
    } else {
        return total_price;
    }
}


int load_user(const char *username) {
    current_user.user = find_user(username);
    if (current_user.user.user_id == -1) {
        current_user.user.user_id = add_user_to_database(username);
        if (current_user.user.user_id == -1) {
            printf("Failed to create user\n");
            return -1;
        }
        strcpy(current_user.user.username, username);
        current_user.user.store_purchases[0] = 0;
        current_user.user.store_purchases[1] = 0;
        current_user.user.store_purchases[2] = 0;
        current_user.discount_applied = 0;
        printf("New user %s created with user ID: %d\n", username, current_user.user.user_id);
    } else {
        printf("User %s found with user ID: %d\n", username, current_user.user.user_id);
        current_user.discount_applied = 0;
    }
    return 0;
}


void* processStoreFile(void* arg){
    FileInfo *fileInfo = (FileInfo *)arg;

    char logFileName[PATH_MAX];
    snprintf(logFileName, sizeof(logFileName), "%s/User1_Order0.log", fileInfo->folder_path);

    logMessage(logFileName, "TID %ld child of PID %d reading file %s\n", (long)get_tid(), getpid(), fileInfo->file_name);

    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/%s", fileInfo->folder_path, fileInfo->file_name);

    FILE *productFile = fopen(filePath, "r");
    if (productFile == NULL) {
        perror("Unable to open product file");
        free(fileInfo->folder_path);
        free(fileInfo->file_name);
        free(fileInfo);
        pthread_exit(NULL);
    }

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

    for (int i = 0; i < order_count; i++) {
        if (strcmp(productName, orderlist[i].item_name) == 0) {
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

            strcpy(product.file_path, filePath);

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
                free(fileInfo->folder_path);
                free(fileInfo->file_name);
                free(fileInfo);
                pthread_exit(NULL);
            }

            if (targetStore != NULL) {
                pthread_mutex_lock(&targetStore->mutex);
                if (targetStore->count < MAX_PRODUCTS_PER_STORE) {
                    targetStore->products[targetStore->count++] = product;
                }
                pthread_mutex_unlock(&targetStore->mutex);
            }
        }
    }

    free(fileInfo->folder_path);
    free(fileInfo->file_name);
    free(fileInfo);

    pthread_exit(NULL);
}


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

    char logFileName[PATH_MAX];
    snprintf(logFileName, sizeof(logFileName), "%s/User1_Order0.log", path);
    createLogFile(logFileName);

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".txt") != NULL) {
            FileInfo *fileInfo = (FileInfo *)malloc(sizeof(FileInfo));
            fileInfo->folder_path = strdup(path);  
            fileInfo->file_name = strdup(entry->d_name); 
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


void storeProcess(char *path, int storeID) {
    printf("PID %d create child for %s PID: %d\n", getppid(), path, getpid());

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

    for (int i = 0; i < 8; i++) {
        waitpid(cat_pids[i], NULL, 0);
    }

}


void* ordersThreadFunction(void* arg) {
    OrdersThreadArgs *args = (OrdersThreadArgs *)arg;
    SharedStore *store1 = args->store1;
    SharedStore *store2 = args->store2;
    SharedStore *store3 = args->store3;

    printf("PID %d create thread for Orders TID: %ld\n", getpid() ,(long)get_tid());

    double sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;

    pthread_mutex_lock(&store1->mutex);
    for (int i = 0; i < store1->count; i++) {
        sum1 += store1->products[i].score * store1->products[i].price;
    }
    pthread_mutex_unlock(&store1->mutex);

    pthread_mutex_lock(&store2->mutex);
    for (int i = 0; i < store2->count; i++) {
        sum2 += store2->products[i].score * store2->products[i].price;
    }
    pthread_mutex_unlock(&store2->mutex);

    pthread_mutex_lock(&store3->mutex);
    for (int i = 0; i < store3->count; i++) {
        sum3 += store3->products[i].score * store3->products[i].price;
    }
    pthread_mutex_unlock(&store3->mutex);

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

    printf("Best store is Store %d with sum(score * price): %.2f\n", best_store, max_sum);

    pthread_mutex_lock(&sharedData.mutex);
    sharedData.best_store_id = best_store;
    sharedData.total_price = max_sum;
    sharedData.data_ready = 1;
    pthread_cond_signal(&sharedData.cond); // Signal to Final thread
    pthread_mutex_unlock(&sharedData.mutex);

    return NULL;
}


void* scoresThreadFunction(void* arg) {
    printf("PID %d create thread for Scores TID: %ld\n", getpid() ,(long)get_tid());

    // Wait for the scores_update_needed signal
    pthread_mutex_lock(&sharedData.mutex);
    while (sharedData.scores_update_needed == 0) {
        pthread_cond_wait(&sharedData.scores_cond, &sharedData.mutex);
    }
    int purchase_finalized = sharedData.purchase_finalized;
    pthread_mutex_unlock(&sharedData.mutex);

    if (purchase_finalized) {
        for (int i = 0; i < order_count; i++) {
            char *item_name = orderlist[i].item_name;
            double new_score;
            printf("Enter score for product '%s': \n", item_name);
            if (scanf("%lf", &new_score) != 1) {
                printf("Invalid input for score. Skipping.\n");
                int c;
                while ((c = getchar()) != '\n' && c != EOF);
                continue;
            }

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
                    best_store->products[j].score = new_score;
                    strcpy(filePath, best_store->products[j].file_path);
                    found = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&best_store->mutex);

            if (found) {
                char new_score_str[50];
                snprintf(new_score_str, sizeof(new_score_str), "%.2f", new_score);
                if (update_product_field(filePath, "Score", new_score_str) == 0) {

                } else {
                    printf("Failed to update score for product '%s'\n", item_name);
                }

                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                char time_str[100];
                strftime(time_str, sizeof(time_str)-1, "%Y-%m-%d %H:%M:%S", t);

                if (update_product_field(filePath, "Last Modified", time_str) == 0) {

                } else {
                    printf("Failed to update Last Modified for product '%s'\n", item_name);
                }
            } else {
                printf("Product '%s' not found in Store%d\n", item_name, best_store_id);
            }
        }
    } else {
        printf("Purchase was not finalized. No scores to update.\n");
    }

    return NULL;
}


void* finalThreadFunction(void* arg) {
    printf("PID %d create thread for Final TID: %ld\n", getpid() ,(long)get_tid());

    // Wait for the data_ready signal
    pthread_mutex_lock(&sharedData.mutex);
    while (sharedData.data_ready == 0) { 
        pthread_cond_wait(&sharedData.cond, &sharedData.mutex);
    }

    int best_store_id = sharedData.best_store_id;
    int threshold = sharedData.threshold;
    pthread_mutex_unlock(&sharedData.mutex);

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

    double final_price = apply_discount(total_price, best_store_id);

    if (final_price <= threshold) {
        printf("Final Price %.2f is within the threshold. The purchase is finalized.\n", final_price);

        // Set the purchase_finalized flag and signal the Scores thread
        pthread_mutex_lock(&sharedData.mutex);
        sharedData.purchase_finalized = 1;
        sharedData.scores_update_needed = 1;
        pthread_cond_signal(&sharedData.scores_cond);
        pthread_mutex_unlock(&sharedData.mutex);

        pthread_mutex_lock(&best_store->mutex);
        for (int i = 0; i < order_count; i++) {
            for (int j = 0; j < best_store->count; j++) {
                if (strcmp(orderlist[i].item_name, best_store->products[j].name) == 0) {
                    if (best_store->products[j].entity >= orderlist[i].quantity) {
                        best_store->products[j].entity -= orderlist[i].quantity;
                        char new_entity_str[50];
                        int_to_str(best_store->products[j].entity, new_entity_str, 10);
                        if (update_product_field(best_store->products[j].file_path, "Entity", new_entity_str) == 0) {

                        } else {
                            printf("Failed to update Entity for product '%s'\n", best_store->products[j].name);
                        }
                    } else {
                        printf("Insufficient inventory for product '%s'. Available: %d, Required: %d\n",
                               best_store->products[j].name,
                               best_store->products[j].entity,
                               orderlist[i].quantity);
                    }
                    break;
                }
            }
        }
        pthread_mutex_unlock(&best_store->mutex);

        if (update_user_purchase(current_user.user.username, best_store_id) == 0) {
            current_user.user.store_purchases[best_store_id - 1] = 1;
        } else {
            printf("Failed to update user's purchase history.\n");
        }

    } else {
        printf("Final Price %.2f exceeds the threshold. Purchase not finalized.\n", final_price);

        // Set the purchase_finalized flag to false and signal the Scores thread
        pthread_mutex_lock(&sharedData.mutex);
        sharedData.purchase_finalized = 0;
        sharedData.scores_update_needed = 1;
        pthread_cond_signal(&sharedData.scores_cond);
        pthread_mutex_unlock(&sharedData.mutex);
    }

    // نمایش نتایج در رابط گرافیکی
    char result[512];
    if (final_price <= threshold) {
        snprintf(result, sizeof(result), "Final Price %.2f is within the threshold.\nPurchase Finalized.\nBest Store: %d", final_price, best_store_id);
    } else {
        snprintf(result, sizeof(result), "Final Price %.2f exceeds the threshold.\nPurchase Not Finalized.\nBest Store: %d", final_price, best_store_id);
    }

    gtk_label_set_text(GTK_LABEL(output_label), result);

    return NULL;
}



typedef struct {
    GtkWidget *username_entry;
    GtkWidget *orders_entry;
    GtkWidget *threshold_entry;
} AppWidgets;


static void on_submit_clicked(GtkButton *button, gpointer user_data) {
    AppWidgets *widgets = (AppWidgets *)user_data;

    const char *username = gtk_entry_get_text(GTK_ENTRY(widgets->username_entry));
    const char *orders_input = gtk_entry_get_text(GTK_ENTRY(widgets->orders_entry));
    const char *threshold_text = gtk_entry_get_text(GTK_ENTRY(widgets->threshold_entry));
    int threshold = atoi(threshold_text);

    if (strlen(username) == 0) {
        gtk_label_set_text(GTK_LABEL(output_label), "Please enter a username.");
        return;
    }
    if (threshold == 0)
    {
        threshold = INT_MAX;
    }
    

    if (load_user(username) != 0) {
        gtk_label_set_text(GTK_LABEL(output_label), "Error loading user information.");
        return;
    }

    order_count = 0;
    char *orders_copy = strdup(orders_input);
    char *token = strtok(orders_copy, ",");
    while (token != NULL && order_count < MAX_ORDER_ITEMS) {
        char *colon = strchr(token, ':');
        if (colon != NULL) {
            *colon = '\0';
            char *item_name = token;
            int quantity = atoi(colon + 1);
            if (quantity > 0) {
                strcpy(orderlist[order_count].item_name, item_name);
                orderlist[order_count].quantity = quantity;
                order_count++;
            }
        }
        token = strtok(NULL, ",");
    }
    free(orders_copy);

    pthread_mutex_lock(&sharedData.mutex);
    sharedData.threshold = threshold;
    pthread_mutex_unlock(&sharedData.mutex);

    pid_t pid1, pid2, pid3;

    pid1 = fork();
    if (pid1 < 0) {
        perror("Fork failed for process 1");
        gtk_label_set_text(GTK_LABEL(output_label), "Error creating process for Store1.");
        return;
    }

    if (pid1 == 0) {
        char store1Path[] = "/home/ali/Desktop/Dataset/Store1";
        storeProcess(store1Path, 1);
        exit(0);
    }
    waitpid(pid1, NULL, 0);

    pid2 = fork();
    if (pid2 < 0) {
        perror("Fork failed for process 2");
        gtk_label_set_text(GTK_LABEL(output_label), "Error creating process for Store2.");
        return;
    }

    if (pid2 == 0) {
        char store2Path[] = "/home/ali/Desktop/Dataset/Store2";
        storeProcess(store2Path, 2);
        exit(0);
    }
    waitpid(pid2, NULL, 0);

    pid3 = fork();
    if (pid3 < 0) {
        perror("Fork failed for process 3");
        gtk_label_set_text(GTK_LABEL(output_label), "Error creating process for Store3.");
        return;
    }

    if (pid3 == 0) {
        char store3Path[] = "/home/ali/Desktop/Dataset/Store3";
        storeProcess(store3Path, 3);
        exit(0);
    }
    waitpid(pid3, NULL, 0);

    OrdersThreadArgs args;
    args.store1 = sharedStore1;
    args.store2 = sharedStore2;
    args.store3 = sharedStore3;

    pthread_t OrdersThread;
    if (pthread_create(&OrdersThread, NULL, ordersThreadFunction, &args) != 0) {
        perror("pthread_create failed for OrdersThread");
        gtk_label_set_text(GTK_LABEL(output_label), "Error creating OrdersThread.");
        return;
    }

    pthread_t Scoresthread;
    if (pthread_create(&Scoresthread, NULL, scoresThreadFunction, NULL) != 0) {
        perror("pthread_create failed for Scoresthread");
        gtk_label_set_text(GTK_LABEL(output_label), "Error creating Scoresthread.");
        return;
    }

    pthread_t Finalthread;
    if (pthread_create(&Finalthread, NULL, finalThreadFunction, NULL) != 0) {
        perror("pthread_create failed for Finalthread");
        gtk_label_set_text(GTK_LABEL(output_label), "Error creating Finalthread.");
        return;
    }

    pthread_join(OrdersThread, NULL);
    pthread_join(Scoresthread, NULL);
    pthread_join(Finalthread, NULL);

    pthread_mutex_destroy(&sharedStore1->mutex);
    munmap(sharedStore1, sizeof(SharedStore));
    pthread_mutex_destroy(&sharedStore2->mutex);
    munmap(sharedStore2, sizeof(SharedStore));
    pthread_mutex_destroy(&sharedStore3->mutex);
    munmap(sharedStore3, sizeof(SharedStore));

    pthread_mutex_destroy(&file_mutex);

    pthread_mutex_destroy(&sharedData.mutex);
    pthread_cond_destroy(&sharedData.cond);
    pthread_cond_destroy(&sharedData.scores_cond);
}



int main(int argc, char *argv[]) {

    gtk_init(&argc, &argv);

    if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        printf("Mutex initialization failed\n");
        exit(1);
    }

    sharedData.best_store_id = -1;
    sharedData.total_price = 0.0;
    sharedData.threshold = 0;
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

    pthread_mutexattr_t mutexAttr;
    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_SHARED);

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

    pthread_mutexattr_destroy(&mutexAttr);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Order Processing System");
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_add(GTK_CONTAINER(window), grid);

    GtkWidget *username_label = gtk_label_new("Username:");
    gtk_grid_attach(GTK_GRID(grid), username_label, 0, 0, 1, 1);
    GtkWidget *username_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), username_entry, 1, 0, 2, 1);

    GtkWidget *orders_label = gtk_label_new("Orders (e.g., item1:2,item2:3):");
    gtk_grid_attach(GTK_GRID(grid), orders_label, 0, 1, 1, 1);
    GtkWidget *orders_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), orders_entry, 1, 1, 2, 1);

    GtkWidget *threshold_label = gtk_label_new("Price Threshold:");
    gtk_grid_attach(GTK_GRID(grid), threshold_label, 0, 2, 1, 1);
    GtkWidget *threshold_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(grid), threshold_entry, 1, 2, 2, 1);

    GtkWidget *submit_button = gtk_button_new_with_label("Submit");
    gtk_grid_attach(GTK_GRID(grid), submit_button, 1, 3, 1, 1);

    output_label = gtk_label_new("");
    gtk_grid_attach(GTK_GRID(grid), output_label, 0, 4, 3, 1);

    AppWidgets *widgets = g_slice_new(AppWidgets);
    widgets->username_entry = username_entry;
    widgets->orders_entry = orders_entry;
    widgets->threshold_entry = threshold_entry;

    g_signal_connect(submit_button, "clicked", G_CALLBACK(on_submit_clicked), widgets);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}


