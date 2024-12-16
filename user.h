// user.h
#ifndef USER_H
#define USER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DATABASE_FILE "/home/ali/Desktop/database.txt"

// Structure to store user information, including purchase history
struct user {
    char username[100];
    int user_id;
    int store_purchases[3]; // Index 0: Store1, 1: Store2, 2: Store3
};

// Function to remove spaces and newlines from a string
void remove_spaces_and_newline(char *str) {
    int i = 0, j = 0;

    // Traverse the string
    while (str[i] != '\0') {
        // If the character is not a space or \n, add it to the new string
        if (str[i] != ' ' && str[i] != '\n') {
            str[j++] = str[i];
        }
        i++;
    }

    // Terminate the new string
    str[j] = '\0';
}

// Function to find a user by username
struct user find_user(const char *username) {
    FILE *file = fopen(DATABASE_FILE, "r");
    struct user found_user;
    found_user.user_id = -1; // Initialize as not found

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
    return found_user; // user_id will be -1 if not found
}

// Function to add a new user to the database
int add_user_to_database(const char *username) {
    FILE *file = fopen(DATABASE_FILE, "a");
    if (file == NULL) {
        perror("Unable to open database file for writing");
        return -1;
    }

    srand(time(NULL));
    int new_user_id = rand() % 1000 + 1;

    // Initialize store purchases to 0 (no purchases)
    fprintf(file, "%s %d %d %d %d\n", username, new_user_id, 0, 0, 0);
    fclose(file);
    return new_user_id;
}

// Function to update a user's purchase history for a specific store
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
            // Update the specific store purchase flag
            if (store_id >=1 && store_id <=3) {
                current_user.store_purchases[store_id -1] = 1;
            } else {
                fclose(file);
                return -1; // Invalid store_id
            }

            // Move the file pointer back to the start of this line
            fseek(file, pos, SEEK_SET);
            fprintf(file, "%s %d %d %d %d\n", 
                    current_user.username, 
                    current_user.user_id, 
                    current_user.store_purchases[0],
                    current_user.store_purchases[1],
                    current_user.store_purchases[2]);
            fclose(file);
            return 0; // Success
        }
    }

    fclose(file);
    return -1; // User not found
}

#endif // USER_H
