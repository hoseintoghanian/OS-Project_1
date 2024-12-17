# Multi-Process and Multi-Threaded Shopping System

This program simulates a shopping system where users can register, create a shopping list, and select items from various stores. 
The system uses **processes** for store handling and **threads** for category and product management. It demonstrates key concepts 
in **process management**, **shared memory**, **mutex synchronization**, and **thread communication**.

## Features

- **User Registration**: Registers new users or retrieves existing users from the database.
- **Shopping List Creation**: Users input their desired items and quantities.
- **Store and Category Handling**: Each store and category is managed via processes and threads, ensuring efficient data handling.
- **Thread Communication**: Threads update product scores, handle purchases, and apply discounts.
- **Shared Memory**: Products from different stores are loaded into shared memory for thread operations.
- **Dynamic Logging**: Logs all activities (e.g., thread creation, file operations) to user-specific log files.

## Project Structure

- **Processes**: Used to manage stores (`Store1`, `Store2`, `Store3`) and their respective categories.
- **Threads**: Created for each product category to handle product information and updates.
- **Shared Memory**: Ensures data consistency across multiple threads for each store.
- **Synchronization**: Mutexes and condition variables for shared resource access and thread communication.

## Setup and Requirements

### Dependencies

The program relies on the following libraries:
- `pthread`: For thread creation and management.
- `sys/mman.h`: For shared memory.
- `semaphore.h`: For inter-process synchronization.
- `dirent.h`: For directory handling.
- `stdio.h`, `stdlib.h`, `string.h`: Standard C libraries.

Ensure these libraries are installed and available on your system. On a Linux system, you may need to install development tools:
```bash‍‍‍‍‍‍‍‍‍
sudo apt-get install build-essential
sudo apt-get install manpages-dev
```
‍‍‍‍‍‍‍
## Compilation

Compile the program using GCC with pthread support:
```bash
gcc -o shopping_system main.c -lpthread
```

## File Structure
Ensure the following directory structure is created:

```vbnet
/home/hosein/Desktop/Dataset/
├── Store1/
│   ├── Digital/
│   ├── Home/
│   ├── ...
│   └── Sports/
├── Store2/
│   └── (same structure as Store1)
└── Store3/
    └── (same structure as Store1)
```    
Each category folder contains product files (*.txt) with the following structure:

## make file

- Name: Product_Name

- Price: Product_Price

- Score: Product_Score

- Entity: Product_Quantity

## Execution
Run the compiled program:

```bash
./shopping_system
```
## Usage
1. `Enter Username`: The program checks if the user exists in the database. If not, it registers a new user.

2. `Create Shopping List`:Input item names and quantities. Type end to finish the list.

3. `Set Price Threshold`: Enter the maximum allowable price for the purchase.

4. `Processing`:
-   Processes and threads handle stores and categories to locate items.

-   The best store is selected based on the price-to-score ratio.
4. `Finalize Purchase`:
- If the total price is within the threshold, the purchase is completed.

- Users can update product scores post-purchase.

### Logs
Activity logs are generated in:

```bash

<Store_Path>/User1_Order0.log
```
#### Logs include:

- Thread creation and processing.
- Found products.
- Final purchase decisions.

## Key Functions and Flow
### Main Workflow

1. #### User Input:

- `load_user()`: Handles user registration and retrieval.
- `Orderlist`: Captures user orders.

2. #### Store Processes:

- `storeProcess()`: Forks processes for each store.
- `processSubFolderFiles()`: Threads process category files.

3. #### Order Processing:

- `ordersThreadFunction()`: Determines the best store based on product scores and prices.

4. #### Finalization:

- `finalThreadFunction()`: Calculates total price, applies discounts, and updates inventory.
5. #### Score Updates:

- `scoresThreadFunction()`: Prompts users to update product scores and modifies product files.


### Thread Safety and Communication

- Mutex Locks: Protect shared resources (SharedStore).

- Condition Variables: Synchronize threads (e.g., waiting for purchase finalization).


## Example Interaction
```yaml

Enter the username: JohnDoe
New user JohnDoe created with user ID: 123

Enter orderlist (item name and quantity), type 'end' to stop:
Item name and quantity: Laptop 1
Item name and quantity: Mouse 2
Item name and quantity: end

Enter price threshold: 5000

Processing Store1...
Processing Store2...
Processing Store3...

Best store is Store2 with sum(score * price): 4890.00
Final Price 4890.00 is within the threshold 5000. Purchase is finalized.

Enter score for product 'Laptop':
9.5
Enter score for product 'Mouse':
8.0
```

### Logs:

```csharp
Copy code
TID 12345 child of PID 67890 found Laptop product
TID 12346 child of PID 67890 found Mouse product
Final Price 4890.00 is within the threshold 5000. Purchase is finalized.
```