#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>



// struct {
//     char name[100];
//     int Count;
//     float price;
// } Product;

// #define MAX_ORDER_ITEMS 100
// #define MAX_ITEM_LENGTH 50

// // ساختار برای ذخیره هر آیتم سفارش
// typedef struct {
//     char item_name[MAX_ITEM_LENGTH];
//     int quantity;
// } Order;





// int main() {
//     Order orderlist[MAX_ORDER_ITEMS];
//     int price_threshold;
//     int order_count = 0;



//     // گرفتن آیتم‌های سفارش تا زمانی که کاربر 'end' وارد کند
//     printf("Enter orderlist (item name and quantity), type 'end' to stop:\n");
//     while (1) {
//         char item[MAX_ITEM_LENGTH];
//         int quantity;

//         // گرفتن نام آیتم و مقدار آن
//         printf("Item name: ");
//         scanf("%s", item);

//         if (strcmp(item, "end") == 0) {
//             break;  // اتمام ورودی‌ها
//         }

//         printf("Quantity: ");
//         scanf("%d", &quantity);

//         // ذخیره اطلاعات آیتم سفارش
//         strcpy(orderlist[order_count].item_name, item);
//         orderlist[order_count].quantity = quantity;
//         order_count++;

//         if (order_count >= MAX_ORDER_ITEMS) {
//             printf("Maximum order items reached.\n");
//             break;
//         }
//     }

    
    


//     // گرفتن قیمت آستانه
//     printf("Enter price threshold: ");
//     scanf("%d", &price_threshold);

//     // نمایش ورودی‌ها
//     printf("\nUsername: %s\n", username);
//     printf("Order list:\n");
//     for (int i = 0; i < order_count; i++) {
//         printf("%s %d\n", orderlist[i].item_name, orderlist[i].quantity);
//     }
//     printf("Price threshold: %d\n", price_threshold);

//     return 0;
// }