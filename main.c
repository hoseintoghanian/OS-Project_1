#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

// ساختار برای ذخیره اطلاعات محصول
typedef struct {
    char name[100];
    int quantity;
    float price;
} Product;

// ساختار برای ذخیره اطلاعات خرید
typedef struct {
    char store_name[100];
    Product *products;
    int num_products;
    float budget;
} ShoppingList;

// ساختار برای ذخیره وضعیت فروشگاه و خرید
typedef struct {
    ShoppingList *shopping_list;
    int store_id;
} StoreInfo;

// توابعی برای پردازش‌ها
void *evaluate_cart(void *arg) {
    StoreInfo *store_info = (StoreInfo *)arg;
    ShoppingList *shopping_list = store_info->shopping_list;

    printf("Evaluating shopping list for store %d: %s\n", store_info->store_id, shopping_list->store_name);
    // محاسبه ارزش سبد خرید (برای سادگی اینجا فقط مجموع قیمت‌ها محاسبه می‌شود)
    float total = 0;
    for (int i = 0; i < shopping_list->num_products; i++) {
        total += shopping_list->products[i].price * shopping_list->products[i].quantity;
    }
    printf("Total value of shopping cart for store %d: %.2f\n", store_info->store_id, total);

    // اگر سقف خرید تعریف شده باشد، بررسی کنیم که آیا سبد خرید از سقف عبور کرده است
    if (shopping_list->budget > 0 && total > shopping_list->budget) {
        printf("Warning: Cart exceeds the budget of %.2f!\n", shopping_list->budget);
    }

    pthread_exit(NULL);
}

void *update_ratings(void *arg) {
    StoreInfo *store_info = (StoreInfo *)arg;
    ShoppingList *shopping_list = store_info->shopping_list;

    printf("Updating ratings for store %d: %s\n", store_info->store_id, shopping_list->store_name);
    // به عنوان مثال فرض می‌کنیم امتیازدهی به محصولات انجام می‌شود
    for (int i = 0; i < shopping_list->num_products; i++) {
        printf("Updated rating for product %s\n", shopping_list->products[i].name);
    }
    pthread_exit(NULL);
}

void *update_product_status(void *arg) {
    StoreInfo *store_info = (StoreInfo *)arg;
    ShoppingList *shopping_list = store_info->shopping_list;

    printf("Updating product status for store %d: %s\n", store_info->store_id, shopping_list->store_name);
    // به عنوان مثال فرض می‌کنیم که وضعیت محصولات آپدیت می‌شود
    for (int i = 0; i < shopping_list->num_products; i++) {
        printf("Updated status for product %s\n", shopping_list->products[i].name);
    }
    pthread_exit(NULL);
}

// تابع برای دریافت اطلاعات خرید از کاربر
void get_shopping_list(ShoppingList *shopping_list) {
    printf("Enter store name: ");
    scanf("%s", shopping_list->store_name);

    printf("Enter number of products: ");
    scanf("%d", &shopping_list->num_products);

    shopping_list->products = (Product *)malloc(shopping_list->num_products * sizeof(Product));

    for (int i = 0; i < shopping_list->num_products; i++) {
        printf("Enter name of product %d: ", i + 1);
        scanf("%s", shopping_list->products[i].name);

        printf("Enter quantity for %s: ", shopping_list->products[i].name);
        scanf("%d", &shopping_list->products[i].quantity);

        printf("Enter price for %s: ", shopping_list->products[i].name);
        scanf("%f", &shopping_list->products[i].price);
    }

    printf("Enter budget (0 for no budget): ");
    scanf("%f", &shopping_list->budget);
    printf("\n\n");
}

int main() {
    ShoppingList shopping_list;

    // دریافت اطلاعات خرید از کاربر
    get_shopping_list(&shopping_list);

    // ایجاد پروسه فرزند برای هر فروشگاه
    pid_t pid = fork();
    if (pid == -1) {
        perror("Fork failed");
        return 1;
    } else if (pid == 0) {
        // این بخش در پروسه فرزند اجرا می‌شود

        // ایجاد تردها برای فروشگاه
        pthread_t eval_thread, rating_thread, status_thread;
        StoreInfo store_info = { &shopping_list, 1 };  // Store ID = 1

        pthread_create(&eval_thread, NULL, evaluate_cart, (void *)&store_info);
        pthread_create(&rating_thread, NULL, update_ratings, (void *)&store_info);
        pthread_create(&status_thread, NULL, update_product_status, (void *)&store_info);

        // منتظر ماندن برای پایان یافتن تردها
        pthread_join(eval_thread, NULL);
        pthread_join(rating_thread, NULL);
        pthread_join(status_thread, NULL);

        // آزاد کردن حافظه
        free(shopping_list.products);

        printf("Store process %d finished.\n", getpid());
    } else {
        // این بخش در پروسه والد اجرا می‌شود
        wait(NULL);  // منتظر می‌ماند تا پروسه فرزند تمام شود
        printf("Parent process %d finished.\n", getpid());
    }

    return 0;
}
