// main.c
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
#include <ctype.h>  // For isdigit
#include <limits.h> // For PATH_MAX
#include <time.h>   // For time functions

#include "user.h" // Includes find_user and add_user_to_database functions

#define MAX_ITEM_LENGTH 256
#define MAX_ORDER_ITEMS 100
#define MAX_PRODUCTS_PER_STORE 100

// Define Product structure
typedef struct{
    char name[256];
    double price;
    double score;
    int entity;
    char file_path[PATH_MAX]; // Product file path
} Product;

// Shared memory structure for each store
typedef struct {
    Product products[MAX_PRODUCTS_PER_STORE];
    int count;
    pthread_mutex_t mutex;
} SharedStore;

// Order structure
typedef struct {
    char item_name[MAX_ITEM_LENGTH];
    int quantity;
} Order;

// File information structure
typedef struct{
    char *folder_path;
    char *file_name;
    int storeID;
} FileInfo;

// Orders Thread arguments structure
typedef struct {
    SharedStore *store1;
    SharedStore *store2;
    SharedStore *store3;
} OrdersThreadArgs;

// Shared data structure for inter-thread communication
typedef struct {
    int best_store_id;
    double total_price;
    int threshold;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int data_ready; // Flag indicating data readiness

    // Additional fields
    pthread_cond_t scores_cond;
    int scores_update_needed;
    int purchase_finalized;
} SharedData;





// Global variables
SharedStore *sharedStore1;
SharedStore *sharedStore2;
SharedStore *sharedStore3;

Order orderlist[MAX_ORDER_ITEMS];
int order_count = 0;

// Global file mutex
pthread_mutex_t file_mutex;

// Shared data between threads
SharedData sharedData;

// Structure to hold user information
struct user_info {
    struct user user;
    int discount_applied; // Flag to indicate if discount has been applied
};

// Global user information
struct user_info current_user;

// Function to get Thread ID
pid_t get_tid() {
    return syscall(SYS_gettid);
}

// Utility function to convert integer to string (since C doesn't have itoa)
void int_to_str(int num, char *str, int base) {
    sprintf(str, "%d", num);
}

// Logging function
void logMessage(char *logFileName, char *message, ...) {
    // Lock the mutex before writing
    pthread_mutex_lock(&file_mutex);

    FILE *logFile = fopen(logFileName, "a");  // Open or create log file
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
    // Unlock the mutex after writing
    pthread_mutex_unlock(&file_mutex);
}

// Function to update a specific field in a product file
// If the field exists, it updates it; otherwise, it appends the field at the end
int update_product_field(const char *file_path, const char *field, const char *new_value) {
    FILE *prod_file = fopen(file_path, "r+");
    if (prod_file == NULL) {
        perror("Unable to open product file for updating");
        return -1;
    }

    // Read the entire file into memory
    char file_contents[8192];
    size_t read_size = fread(file_contents, 1, sizeof(file_contents) - 1, prod_file);
    if (read_size < 0) {
        perror("Error reading product file");
        fclose(prod_file);
        return -1;
    }
    file_contents[read_size] = '\0';

    // Find the line starting with the specified field
    char search_pattern[50];
    snprintf(search_pattern, sizeof(search_pattern), "%s:", field);
    char *field_line = strstr(file_contents, search_pattern);

    char new_field_line[256];
    snprintf(new_field_line, sizeof(new_field_line), "%s: %s\n", field, new_value);

    if (field_line != NULL) {
        // Field exists, replace it
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

            // Write the updated contents back to the file
            rewind(prod_file);
            if (fputs(new_file_contents, prod_file) == EOF) {
                perror("Error writing updated field to product file");
                fclose(prod_file);
                return -1;
            }
        } else {
            // No newline after the field, append the new field
            fseek(prod_file, 0, SEEK_END);
            if (fputs(new_field_line, prod_file) == EOF) {
                perror("Error appending new field to product file");
                fclose(prod_file);
                return -1;
            }
        }
    } else {
        // Field does not exist, append it at the end
        fseek(prod_file, 0, SEEK_END);
        if (fputs(new_field_line, prod_file) == EOF) {
            perror("Error appending new field to product file");
            fclose(prod_file);
            return -1;
        }
    }

    fflush(prod_file); // Ensure data is written to disk
    fclose(prod_file);
    return 0;
}

// Function to process each store file
void* processStoreFile(void* arg){
    FileInfo *fileInfo = (FileInfo *)arg;

    // Determine the log file name specific to the store
    char logFileName[PATH_MAX];
    snprintf(logFileName, sizeof(logFileName), "%s/User1_Order0.log", fileInfo->folder_path);

    // Logging by the thread
    logMessage(logFileName, "TID %ld child of PID %d reading file %s\n", (long)get_tid(), getpid(), fileInfo->file_name);

    // Construct the full file path
    char filePath[PATH_MAX];
    snprintf(filePath, sizeof(filePath), "%s/%s", fileInfo->folder_path, fileInfo->file_name);

    // Open the product file
    FILE *productFile = fopen(filePath, "r");
    if (productFile == NULL) {
        perror("Unable to open product file");
        free(fileInfo->folder_path);
        free(fileInfo->file_name);
        free(fileInfo);
        pthread_exit(NULL);
    }

    // Read the product name
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

    // Check if the product exists in the order list
    for (int i = 0; i < order_count; i++) {
        if (strcmp(productName, orderlist[i].item_name) == 0) {
            // If the product is in the orders, read complete product information
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

            // Set the product file path
            strcpy(product.file_path, filePath);

            // Determine the target store
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
                // Invalid store ID
                free(fileInfo->folder_path);
                free(fileInfo->file_name);
                free(fileInfo);
                pthread_exit(NULL);
            }

            // Add the product to the corresponding shared memory with mutex protection
            if (targetStore != NULL) {
                pthread_mutex_lock(&targetStore->mutex);
                if (targetStore->count < MAX_PRODUCTS_PER_STORE) {
                    targetStore->products[targetStore->count++] = product;
                }
                pthread_mutex_unlock(&targetStore->mutex);
            }
        }
    }

    // Free allocated memory
    free(fileInfo->folder_path);
    free(fileInfo->file_name);
    free(fileInfo);

    pthread_exit(NULL);
}

// Function to create a log file (initialize it)
void createLogFile(char *path){
    FILE *logFile = fopen(path, "w");
    if (logFile == NULL)
    {
        perror("Unable to open log file");
        return;
    }
    fclose(logFile);
}

// Function to process subfolder files by creating threads for each product file
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

    // Create the log file specific to the store
    char logFileName[PATH_MAX];
    snprintf(logFileName, sizeof(logFileName), "%s/User1_Order0.log", path);
    createLogFile(logFileName);

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG && strstr(entry->d_name, ".txt") != NULL) {
            FileInfo *fileInfo = (FileInfo *)malloc(sizeof(FileInfo));
            fileInfo->folder_path = strdup(path);  // Safe copy of path
            fileInfo->file_name = strdup(entry->d_name); // Safe copy of filename
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

// Function executed by each store process
void storeProcess(char *path, int storeID) {
    printf("PID %d create child for %s PID: %d\n", getppid(), path, getpid());

    // Array of categories
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

    // Wait for category processes to finish
    for (int i = 0; i < 8; i++) {
        waitpid(cat_pids[i], NULL, 0);
    }

    // At this point, shared memory stores are populated
    // Shared memory is managed in the parent process
}



// Orders Thread function to select the best store
void* ordersThreadFunction(void* arg) {
    OrdersThreadArgs *args = (OrdersThreadArgs *)arg;
    SharedStore *store1 = args->store1;
    SharedStore *store2 = args->store2;
    SharedStore *store3 = args->store3;

    printf("PID %d create thread for Orders TID: %ld\n", getpid() ,(long)get_tid());

    double sum1 = 0.0, sum2 = 0.0, sum3 = 0.0;

    // Calculate sum(score * price) for Store1
    pthread_mutex_lock(&store1->mutex);
    for (int i = 0; i < store1->count; i++) {
        sum1 += store1->products[i].score * store1->products[i].price;
    }
    pthread_mutex_unlock(&store1->mutex);

    // Calculate sum(score * price) for Store2
    pthread_mutex_lock(&store2->mutex);
    for (int i = 0; i < store2->count; i++) {
        sum2 += store2->products[i].score * store2->products[i].price;
    }
    pthread_mutex_unlock(&store2->mutex);

    // Calculate sum(score * price) for Store3
    pthread_mutex_lock(&store3->mutex);
    for (int i = 0; i < store3->count; i++) {
        sum3 += store3->products[i].score * store3->products[i].price;
    }
    pthread_mutex_unlock(&store3->mutex);

    // Select the best store
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

    // Store the best store and total price in sharedData
    pthread_mutex_lock(&sharedData.mutex);
    sharedData.best_store_id = best_store;
    sharedData.total_price = max_sum;
    sharedData.data_ready = 1;
    pthread_cond_signal(&sharedData.cond); // Signal to Final thread
    pthread_mutex_unlock(&sharedData.mutex);

    return NULL;
}

// Scores Thread function
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
        // Request scores from the user for each product
        for (int i = 0; i < order_count; i++) {
            char *item_name = orderlist[i].item_name;
            double new_score;
            printf("Enter score for product '%s': \n", item_name);
            if (scanf("%lf", &new_score) != 1) {
                printf("Invalid input for score. Skipping.\n");
                // Clear the input buffer
                int c;
                while ((c = getchar()) != '\n' && c != EOF);
                continue;
            }

            // Find the product in the best store
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
                    // Update the score in shared memory
                    best_store->products[j].score = new_score;
                    strcpy(filePath, best_store->products[j].file_path);
                    found = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&best_store->mutex);

            if (found) {
                // Update the score in the product file
                char new_score_str[50];
                snprintf(new_score_str, sizeof(new_score_str), "%.2f", new_score);
                if (update_product_field(filePath, "Score", new_score_str) == 0) {
                    // Score updated successfully
                } else {
                    printf("Failed to update score for product '%s'\n", item_name);
                }

                // Retrieve current system time
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                char time_str[100];
                strftime(time_str, sizeof(time_str)-1, "%Y-%m-%d %H:%M:%S", t);

                // Update the Last Modified field in the product file
                if (update_product_field(filePath, "Last Modified", time_str) == 0) {
                    // Last Modified date updated successfully
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



// Function to apply discount if applicable
double apply_discount(double total_price, int store_id) {
    // Check if the user has previously purchased from this store
    if (current_user.user.store_purchases[store_id - 1] == 1) {
        // Apply 10% discount
        double discounted_price = total_price * 0.9;
        printf("10%% discount applied! Original Price: %.2f, Discounted Price: %.2f\n", total_price, discounted_price);
        return discounted_price;
    } else {
        // No discount
        return total_price;
    }
}

// Function to load user information
int load_user(const char *username) {
    current_user.user = find_user(username);
    if (current_user.user.user_id == -1) {
        // User not found, create a new user
        current_user.user.user_id = add_user_to_database(username);
        if (current_user.user.user_id == -1) {
            printf("Failed to create user\n");
            return -1;
        }
        strcpy(current_user.user.username, username);
        // Initialize store_purchases to 0
        current_user.user.store_purchases[0] = 0;
        current_user.user.store_purchases[1] = 0;
        current_user.user.store_purchases[2] = 0;
        current_user.discount_applied = 0;
        printf("New user %s created with user ID: %d\n", username, current_user.user.user_id);
    } else {
        printf("User %s found with user ID: %d\n", username, current_user.user.user_id);
        // Reset discount_applied flag for the session
        current_user.discount_applied = 0;
    }
    return 0;
}

// Final Thread function
void* finalThreadFunction(void* arg) {
    printf("PID %d create thread for Final TID: %ld\n", getpid() ,(long)get_tid());

    // Wait for the data_ready signal
    pthread_mutex_lock(&sharedData.mutex);
    while (sharedData.data_ready == 0) { // Wait for data readiness
        pthread_cond_wait(&sharedData.cond, &sharedData.mutex);
    }

    int best_store_id = sharedData.best_store_id;
    int threshold = sharedData.threshold;
    pthread_mutex_unlock(&sharedData.mutex);

    // Determine the target store
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

    // Calculate the total price of the purchase list from the best store
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

    // Apply discount if applicable
    double final_price = apply_discount(total_price, best_store_id);

    // Compare with the threshold
    if (final_price <= threshold) {
        printf("Final Price %.2f is within the threshold %d. The purchase is finalized.\n", final_price, threshold);

        // Set the purchase_finalized flag and signal the Scores thread
        pthread_mutex_lock(&sharedData.mutex);
        sharedData.purchase_finalized = 1;
        sharedData.scores_update_needed = 1;
        pthread_cond_signal(&sharedData.scores_cond);
        pthread_mutex_unlock(&sharedData.mutex);

        // Update the inventory of products in the best store
        pthread_mutex_lock(&best_store->mutex);
        for (int i = 0; i < order_count; i++) {
            for (int j = 0; j < best_store->count; j++) {
                if (strcmp(orderlist[i].item_name, best_store->products[j].name) == 0) {
                    // Check for sufficient inventory
                    if (best_store->products[j].entity >= orderlist[i].quantity) {
                        best_store->products[j].entity -= orderlist[i].quantity;
                        // Update the entity in the product file
                        char new_entity_str[50];
                        int_to_str(best_store->products[j].entity, new_entity_str, 10);
                        if (update_product_field(best_store->products[j].file_path, "Entity", new_entity_str) == 0) {
                            // Successfully updated
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

        // Update user's purchase history
        if (update_user_purchase(current_user.user.username, best_store_id) == 0) {
            current_user.user.store_purchases[best_store_id - 1] = 1;
            // printf("User %s's purchase history updated for Store%d.\n", current_user.user.username, best_store_id);
        } else {
            printf("Failed to update user's purchase history.\n");
        }

    } else {
        printf("Final Price %.2f exceeds the threshold %d. Purchase not finalized.\n", final_price, threshold);

        // Set the purchase_finalized flag to false and signal the Scores thread
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

    // Initialize the file mutex
    if (pthread_mutex_init(&file_mutex, NULL) != 0) {
        printf("Mutex initialization failed\n");
        exit(1);
    }

    // Initialize shared data structure
    sharedData.best_store_id = -1;
    sharedData.total_price = 0.0;
    sharedData.threshold = 0; // Initial value, will be set later
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

    // Set mutex attributes for shared memory
    pthread_mutexattr_t mutexAttr;
    pthread_mutexattr_init(&mutexAttr);
    pthread_mutexattr_setpshared(&mutexAttr, PTHREAD_PROCESS_SHARED);

    // Create shared memory for store1
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

    // Create shared memory for store2
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

    // Create shared memory for store3
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

    // Clean up mutex attributes
    pthread_mutexattr_destroy(&mutexAttr);

    // Prompt user for username
    printf("Enter the username: ");
    scanf("%s", username);

    // Load user information
    if (load_user(username) != 0) {
        printf("Error loading user information.\n");
        return 1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("Fork failed");
        exit(1);
    } else if (pid == 0) {
        // Child process

        printf("Enter orderlist (item name and quantity), type 'end' to stop:\n");
        while (1) {
            char item[MAX_ITEM_LENGTH];
            int quantity;
            printf("Item name and quantity: ");
            // Use %[^\n] to read the name until the first digit
            if (scanf(" %[^0-9]%d", item, &quantity) != 2) {
                // If reading fails, break the loop
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
        while (getchar() != '\n'); // Clear the input buffer
        if (fgets(input, sizeof(input), stdin)) {
            if (input[0] == '\n') {
                price_threshold = INT_MAX;
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

        // Set the threshold in sharedData
        pthread_mutex_lock(&sharedData.mutex);
        sharedData.threshold = price_threshold;
        pthread_mutex_unlock(&sharedData.mutex);

        printf("%s created with PID: %d\n", username, getpid());

        // Create store processes
        pid_t pid1, pid2, pid3;

        // Create first store process (Store1)
        pid1 = fork();
        if (pid1 < 0) {
            perror("Fork failed for process 1");
            exit(1);
        }

        if (pid1 == 0) {
            char store1Path[] = "/home/hosein/Desktop/Dataset/Store1";
            storeProcess(store1Path, 1);
            exit(0);
        }
        waitpid(pid1, NULL, 0);

        // Create second store process (Store2)
        pid2 = fork();
        if (pid2 < 0) {
            perror("Fork failed for process 2");
            exit(1);
        }

        if (pid2 == 0) {
            char store2Path[] = "/home/hosein/Desktop/Dataset/Store1";
            storeProcess(store2Path, 2);
            exit(0);
        }
        waitpid(pid2, NULL, 0);

        // Create third store process (Store3)
        pid3 = fork();
        if (pid3 < 0) {
            perror("Fork failed for process 3");
            exit(1);
        }

        if (pid3 == 0) {
            char store3Path[] = "/home/hosein/Desktop/Dataset/Store1";
            storeProcess(store3Path, 3);
            exit(0);
        }
        waitpid(pid3, NULL, 0);

        // Now data is in shared memory stores, create threads

        // Create OrdersThread arguments
        OrdersThreadArgs args;
        args.store1 = sharedStore1;
        args.store2 = sharedStore2;
        args.store3 = sharedStore3;

        // Create Orders thread
        pthread_t OrdersThread;
        if (pthread_create(&OrdersThread, NULL, ordersThreadFunction, &args) != 0) {
            perror("pthread_create failed for OrdersThread");
            exit(1);
        }

        // Create Scores thread
        pthread_t Scoresthread;
        if (pthread_create(&Scoresthread, NULL, scoresThreadFunction, NULL) != 0) {
            perror("pthread_create failed for Scoresthread");
            exit(1);
        }

        // Create Final thread
        pthread_t Finalthread;
        if (pthread_create(&Finalthread, NULL, finalThreadFunction, NULL) != 0) {
            perror("pthread_create failed for Finalthread");
            exit(1);
        }

        // Wait for all threads to finish
        pthread_join(OrdersThread, NULL);
        pthread_join(Scoresthread, NULL);
        pthread_join(Finalthread, NULL);

        // Clean up resources: destroy mutexes and unmap shared memory
        pthread_mutex_destroy(&sharedStore1->mutex);
        munmap(sharedStore1, sizeof(SharedStore));
        pthread_mutex_destroy(&sharedStore2->mutex);
        munmap(sharedStore2, sizeof(SharedStore));
        pthread_mutex_destroy(&sharedStore3->mutex);
        munmap(sharedStore3, sizeof(SharedStore));

        // Destroy file mutex
        pthread_mutex_destroy(&file_mutex);

        // Destroy sharedData mutex and condition variables
        pthread_mutex_destroy(&sharedData.mutex);
        pthread_cond_destroy(&sharedData.cond);
        pthread_cond_destroy(&sharedData.scores_cond);

    } else {
        // Parent process waits for child
        wait(NULL);
    }

    return 0;
}