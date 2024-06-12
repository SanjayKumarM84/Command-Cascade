#define _XOPEN_SOURCE 500 // Required for nftw
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <string.h>
#include <fcntl.h>
#include <ftw.h>
#include <time.h>
#include <linux/stat.h>
#include <sys/syscall.h>
#define _GNU_SOURCE

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

#ifndef AT_STATX_SYNC_AS_STAT
#define AT_STATX_SYNC_AS_STAT 0x1000
#endif

#define PORT_NUMBER 8002
#define NO_OF_CONNECTIONS 3
#define BUFFER_SIZE 256
#define MAX_FILENAME_LEN 512
#define ARGS_LENGTH 10
#define TAR_FILE "/w24project/temp.tar.gz"

char tarFile[100];

char **directories = NULL; // Declare directories as a pointer to an array of strings
int num_dirs = 0;
int capacity = 0;
int inputArgsLength = 0; // Initialize inputArgsLength
char *inputArgs[ARGS_LENGTH]; // To store each and every argument from the input data
char *concatenated_paths = NULL; // Initialize with an NULL
int dynamically_allocated = 0; // Flag to track if concatenated_paths was dynamically allocated

time_t target_time; // Global variable to hold the target time

// Structure to hold file information
struct FileInformation {
    char filename[MAX_FILENAME_LEN];
    off_t size;
    time_t created_time;
    mode_t permissions;
} file_info; // Define file_info object globally

// Structure to hold directory information
typedef struct {
    char *path;
    time_t ctime; // Creation time
} DirectoryInfo;

// Global variables
DirectoryInfo *directoriesObj = NULL;
size_t directoriesCount = 0;

// Function to clear the file_info object
void clear_file_info(struct FileInformation *file_info) {
    // Set each member to default values
    memset(file_info->filename, 0, sizeof(file_info->filename)); // Clear the filename
    file_info->size = 0; // Reset size to 0
    file_info->created_time = 0; // Reset created_time to 0
    file_info->permissions = 0; // Reset permissions to 0
}

// Function to clear the DirectoryInfo object
void clearDirectoryInfo(DirectoryInfo *directoriesObj) {
    // Free memory allocated for the path string
    if (directoriesObj->path != NULL) {
        free(directoriesObj->path);
        directoriesObj->path = NULL;
    }
    // Reset the creation time to 0
    directoriesObj->ctime = 0;
}

// Function to compare DirectoryInfo elements based on creation time
int compare_as_per_time(const void *a, const void *b) {
    const DirectoryInfo *dir1 = (const DirectoryInfo *)a;
    const DirectoryInfo *dir2 = (const DirectoryInfo *)b;
    return dir1->ctime - dir2->ctime;
}

// Define compare function for qsort
int compare(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

// Function to write content to a file
void write_to_file(const char *filename, const char *content) {
    // printf("Inside %s\n", filename);
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }
    fprintf(fp, "%s", content);
    fclose(fp);
}

// Function to send file content over socket
void send_file(int clientSocketDesc, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    // Get the file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    rewind(fp);

    // Allocate memory to hold file content
    char *file_content = (char *)malloc(file_size);
    if (file_content == NULL) {
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    // Read file content into memory
    size_t bytes_read = fread(file_content, 1, file_size, fp);
    if (bytes_read != file_size) {
        perror("Error reading file");
        exit(EXIT_FAILURE);
    }

    // Send file size to the client
    if (send(clientSocketDesc, &file_size, sizeof(long), 0) == -1) {
        perror("Error sending file size");
        exit(EXIT_FAILURE);
    }

    // printf("Inside send_file %s\n",file_content);
    // Send file content to the client
    if (send(clientSocketDesc, file_content, file_size, 0) == -1) {
        perror("Error sending file content");
        exit(EXIT_FAILURE);
    }

    fclose(fp);
    free(file_content);
}

// Function to parse input command into an array of string elements
void parseInputCommand(char *inputCommand, char *inputArgs[])
{
    // Initialize inputArgsLength to 0 before parsing
    inputArgsLength = 0;

    // Breaking down the entire string into each array element using strtok
    char *token = strtok(inputCommand, " ");
    int i = 0;
    while (token != NULL && i < ARGS_LENGTH - 1) { // Prevent overflow in inputArgs array
        inputArgs[i++] = token;
        token = strtok(NULL, " ");
        inputArgsLength++; // Incrementing args length
    }
    inputArgs[i] = NULL; // Mark the end of arguments
}

/*
Below are the list of functions which are used for traversal in nftw
*/
// Callback function to find file for nftw
int search_file_and_details(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    // Extracting filename from the path
    char *filename = strrchr(fpath, '/');
    if (filename == NULL)
        filename = (char *)fpath;
    else
        filename++;

    // Check if the current file matches the input filename received from the client
    if (strcmp(filename, inputArgs[1]) == 0) {
        // Copy file information to global structure object
        strncpy(file_info.filename, filename, MAX_FILENAME_LEN);
        file_info.size = sb->st_size;

        // // To find out created time (birth time) of the input file
        // Commenting this because it won't support in linux operating system
        // struct statx stx;
        // statx(AT_FDCWD, filename, AT_STATX_SYNC_AS_STAT, STATX_ALL, &stx);

        // file_info.created_time = stx.stx_btime.tv_sec;

        file_info.created_time = sb->st_ctime;
        file_info.permissions = sb->st_mode & (S_IRWXU | S_IRWXG | S_IRWXO);
        return 1; // Stop traversal
    }
    return 0; // Continue traversal
}

// Function used to list directories
int listDirectories(const char *name, const struct stat *status, int type, struct FTW *ftw) {
    if ((type == FTW_D) && (strstr(name, "/.") == NULL)) {

        // Check if the array needs to be extended
        if (num_dirs >= capacity) {
            // Calculate new capacity (double the current capacity)
            int new_capacity = (capacity == 0) ? 1 : capacity * 2;

            // Reallocate memory for directories array
            char **temp = realloc(directories, new_capacity * sizeof(char *));
            if (!temp) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }

            // Update directories and capacity
            directories = temp;
            capacity = new_capacity;
        }

        // Allocate memory for the directory path and copy the name
        directories[num_dirs] = strdup(name);
        if (!directories[num_dirs]) {
            perror("strdup");
            exit(EXIT_FAILURE);
        }

        // Increment the number of directories
        num_dirs++;
    }
    return 0;
}

// Function used to list directories with their creation time
int listDirectoriesTime(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if ((typeflag == FTW_D) && (strstr(fpath, "/.") == NULL)) {
        // Resize the directories array to accommodate the new directory
        DirectoryInfo *temp = realloc(directoriesObj, (directoriesCount + 1) * sizeof(DirectoryInfo));
        if (temp == NULL) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        directoriesObj = temp;

        // // To find out created time (birth time) of the input file
        // struct statx stx;
        // statx(AT_FDCWD, fpath, AT_STATX_SYNC_AS_STAT, STATX_ALL, &stx);

        // Store directory info
        directoriesObj[directoriesCount].path = strdup(fpath);
        // directoriesObj[directoriesCount].ctime = stx.stx_btime.tv_sec;
        directoriesObj[directoriesCount].ctime = sb->st_ctime;
        directoriesCount++;
    }
    return 0;
}

// Written by Bindu Lokesh
// Function to concatenate the paths of files created before the target time
int fileCreatedBeforeTargetTime(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {

    // Skip directories
    if ((typeflag == FTW_D) || (strstr(fpath, "/.") != NULL)){
        return 0;
    }

    // Exclude files or directories matching the specific path
    if (strncmp(fpath, "/home/sanjay/.", strlen("/home/sanjay/.")) == 0) {
        return 0; // Skip processing
    }

    // // Get file birth time
    // struct statx stx;
    // if (syscall(SYS_statx, AT_FDCWD, fpath, AT_STATX_SYNC_AS_STAT, STATX_ALL, &stx) == -1) {
    //     perror("statx");
    //     return -1; // Error handling
    // }

    if (sb->st_ctime < target_time) { // Check if the file's creation time is before the target time
        size_t path_length = strlen(fpath);
        // printf("%s\n", fpath);

        // Allocate memory for concatenated_paths if it's not dynamically allocated
        if (!dynamically_allocated) {
            concatenated_paths = malloc(1); // Start with an empty string
            if (!concatenated_paths) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            dynamically_allocated = 1;
        }

        // Resize the buffer to accommodate the new path
        char *temp = realloc(concatenated_paths, strlen(concatenated_paths) + path_length + 2); // 1 for '\n' and 1 for '\0'
        if (!temp) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        concatenated_paths = temp;

        // Concatenate the new path with a newline character
        strcat(concatenated_paths, fpath);
        strcat(concatenated_paths, "\n");
    }
    return 0; // Continue traversal
}

// Written by Bindu Lokesh
// Function to concatenate the paths of files created on or after the target time
int fileCreatedAfterTargetTime(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {

    // Skip directories
    if ((typeflag == FTW_D) || (strstr(fpath, "/.") != NULL)){
        return 0;
    }

    // // Get file birth time
    // struct statx stx;
    // if (syscall(SYS_statx, AT_FDCWD, fpath, AT_STATX_SYNC_AS_STAT, STATX_ALL, &stx) == -1) {
    //     perror("statx");
    //     return -1; // Error handling
    // }

    if (sb->st_ctime >= target_time) { // Check if the file's creation time is on or after the target time
        size_t path_length = strlen(fpath);
        // printf("%s\n", fpath);

        // Allocate memory for concatenated_paths if it's not dynamically allocated
        if (!dynamically_allocated) {
            concatenated_paths = malloc(1); // Start with an empty string
            if (!concatenated_paths) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            dynamically_allocated = 1;
        }

        // Resize the buffer to accommodate the new path
        char *temp = realloc(concatenated_paths, strlen(concatenated_paths) + path_length + 2); // 1 for '\n' and 1 for '\0'
        if (!temp) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        concatenated_paths = temp;

        // Concatenate the new path with a newline character
        strcat(concatenated_paths, fpath);
        strcat(concatenated_paths, "\n");
    }
    return 0; // Continue traversal
}

// Written by Bindu Lokesh
// Function to concatenate the paths of files whose size is greater than or equal to size one and less than or equal to size two
int find_files_based_on_size(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{

    // Skip directories
    if ((typeflag == FTW_D) || (strstr(fpath, "/.") != NULL)){
        return 0;
    }

    int size = (int)sb->st_size; // each file size
    if (size >= atoi(inputArgs[1]) && size <= atoi(inputArgs[2])) {
        // if the file size is greater than size1 and less than size2
        size_t path_length = strlen(fpath);

        // Allocate memory for concatenated_paths if it's not dynamically allocated
        if (!dynamically_allocated) {
            concatenated_paths = malloc(1); // Start with an empty string
            if (!concatenated_paths) {
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            dynamically_allocated = 1;
        }

        // Resize the buffer to accommodate the new path
        char *temp = realloc(concatenated_paths, strlen(concatenated_paths) + path_length + 2); // 1 for '\n' and 1 for '\0'
        if (!temp) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        concatenated_paths = temp;

        // Concatenate the new path with a newline character
        strcat(concatenated_paths, fpath);
        strcat(concatenated_paths, "\n");
    }
    return 0; // Continue traversal
}

// Written by Bindu Lokesh
// Function to concatenate the paths of files whose extension matches one of the input extensions
int find_files_based_on_extension(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {

    // Skip directories
    if ((typeflag == FTW_D) || (strstr(fpath, "/.") != NULL)){
        return 0;
    }

    const char *file_extension = strrchr(fpath, '.');
    if (file_extension != NULL) {
        // Move past the dot
        file_extension++;

        // Compare the file extension with each input extension
        for (int i = 1; i < inputArgsLength; i++) { // Start from 1 to skip "w24ft"
            // Compare file_extension with inputArgs[i]
            if (strcmp(file_extension, inputArgs[i]) == 0) {
                // Matching file found
                // printf("Matching file found: %s\n", fpath);

                // Allocate memory for concatenated_paths if it's not dynamically allocated
                if (!dynamically_allocated) {
                    concatenated_paths = malloc(1); // Start with an empty string
                    if (!concatenated_paths) {
                        perror("malloc");
                        exit(EXIT_FAILURE);
                    }
                    dynamically_allocated = 1;
                }

                // Resize the buffer to accommodate the new path
                size_t path_length = strlen(fpath) + 1; // Add 1 for the null terminator
                char *temp = realloc(concatenated_paths, strlen(concatenated_paths) + path_length + 1); // Add 1 for the newline character
                if (!temp) {
                    perror("realloc");
                    exit(EXIT_FAILURE);
                }
                concatenated_paths = temp;

                // Concatenate the new path with a newline character
                strcat(concatenated_paths, fpath);
                strcat(concatenated_paths, "\n");
                // break; // No need to continue checking inputArgs
            }
        }
    }
    return 0; // Continue traversal
}

// Function to handle client requests
void crequest(int clientSocketDesc)
{
    // Setting tar file path
    char *home = getenv("HOME");
    char *path = malloc(strlen(home) + strlen(TAR_FILE) + 1);

    strcpy(path, home);
    strcat(path, TAR_FILE);
    strcpy(tarFile, path);

    while (1)
    {
        // Global variables setting to its initial value
        num_dirs = 0; capacity = 0; dynamically_allocated = 0; directoriesCount = 0;
        concatenated_paths=NULL;

        // Creating an string (array of characters) to store input data from client
        char *inputDataFromClient = malloc(BUFFER_SIZE);
        if (inputDataFromClient == NULL){
            perror("Memory allocation failed");
        }

        // Receive data from the client
        int readInputBytes = recv(clientSocketDesc, inputDataFromClient, BUFFER_SIZE, 0);
        if (readInputBytes < 0){
            perror("Error in receiving data from client");
            free(inputDataFromClient);
            // break;
        }
        else if (readInputBytes == 0){
            // Client disconnected
            printf("Client disconnected\n");
            free(inputDataFromClient);
            break;
        }
        else{
            // Null-terminate the received data
            inputDataFromClient[readInputBytes] = '\0'; 

            // printf("Received message from client: %s\n", inputDataFromClient);

            // Parsing input command
            parseInputCommand(inputDataFromClient, inputArgs);

            // Below if condition is used for implementing 'quitc' command (to disconnect client)
            if (strcmp(inputArgs[0], "quitc") == 0) {
                // printf("Inside quitc\n");
                char *message = "Disconnecting";
                send(clientSocketDesc, message, strlen(message), 0);
                free(inputDataFromClient);
                break;
            }

            // Below if condition is used for implementing 'w24fn' command (to find a file details)
            else if (strcmp(inputArgs[0], "w24fn") == 0 && inputArgs[1] != NULL && strlen(inputArgs[1]) > 0) {

                // Traversing the directory tree rooted at ~ and call search_file_and_details function for each file
                if (nftw(getenv("HOME"), search_file_and_details, 20, FTW_PHYS) == -1) {
                    perror("nftw");
                    exit(EXIT_FAILURE);
                }

                // Sending file information to the client
                send(clientSocketDesc, &file_info, sizeof(struct FileInformation), 0);
                clear_file_info(&file_info);
            }

            // Below if condition is used for implementing 'w24ft' command (to find a file details based on extension)
            else if (strcmp(inputArgs[0], "w24ft") == 0 && inputArgs[1] != NULL) {
                const char *dir = getenv("HOME");

                // Traversing the directory tree rooted at ~ and call find_files_based_on_extension function for each file
                if (nftw(dir, find_files_based_on_extension, 20, FTW_PHYS) == -1) {
                    perror("nftw");
                    exit(EXIT_FAILURE);
                }

                if (concatenated_paths != NULL) {
                    // Writing content present in concatenated_paths to tar file
                    write_to_file(tarFile, concatenated_paths);

                    // Sending tar file to the client
                    send_file(clientSocketDesc, tarFile);

                    free(concatenated_paths);
                }
                else{
                    long isEmpty=0;
                    send(clientSocketDesc, &isEmpty, sizeof(long), 0);
                }

            }

            // Below if condition is used for implementing 'w24fz' command (to find files whose size is greater than size 1 and less than size 2)
            else if (strcmp(inputArgs[0], "w24fz") == 0 && inputArgs[1] != NULL && inputArgs[2] != NULL) {
                const char *dir = getenv("HOME");
                
                // Traversing the directory tree rooted at ~ and call find_files_based_on_size function for each file
                if (nftw(dir, find_files_based_on_size, 20, FTW_PHYS) == -1) {
                    perror("nftw");
                    exit(EXIT_FAILURE);
                }

                if (concatenated_paths != NULL) {
                    // printf("Inside %s\n",concatenated_paths);
                    // Writing content present in concatenated_paths to tar file
                    write_to_file(tarFile, concatenated_paths);

                    // Sending tar file to the client
                    send_file(clientSocketDesc, tarFile);

                    free(concatenated_paths);
                }
                else{
                    long isEmpty=0;
                    send(clientSocketDesc, &isEmpty, sizeof(long), 0);
                }
            }

            // Below if condition is used for implementing 'w24fdb'or 'w24fda' command (to find files which were created before or after target time)
            else if ((strcmp(inputArgs[0], "w24fdb") == 0 || strcmp(inputArgs[0], "w24fda") == 0) && inputArgs[1] != NULL) {
                const char *date_str = inputArgs[1]; // First argument holds target time
                const char *dir = getenv("HOME");

                struct tm tm = {0};
                if (strptime(date_str, "%Y-%m-%d", &tm) == NULL) { // Parse date string
                    fprintf(stderr, "Invalid date format\n");
                    exit(EXIT_FAILURE);
                }
                target_time = mktime(&tm); // Convert to time_t

                // Call nftw with appropriate callback function
                int (*callback_func)(const char *, const struct stat *, int, struct FTW *);
                if (strcmp(inputArgs[0], "w24fdb") == 0)
                    callback_func = fileCreatedBeforeTargetTime; // If client wants to know the files which were created before target time
                else
                    callback_func = fileCreatedAfterTargetTime; // If client wants to know the files which were created on or after target time

                if (nftw(dir, callback_func, 20, FTW_PHYS) == -1) {
                    perror("nftw");
                    exit(EXIT_FAILURE);
                }

                if (concatenated_paths != NULL) {
                    // Writing content present in concatenated_paths to tar file
                    write_to_file(tarFile, concatenated_paths);

                    // Sending tar file to the client
                    send_file(clientSocketDesc, tarFile);

                    free(concatenated_paths);
                }
                else{
                    long isEmpty=0;
                    send(clientSocketDesc, &isEmpty, sizeof(long), 0);
                }
            }

            // Below if condition is used for implementing 'dirlist -a' command (to list the directories in alphabetical order)
            else if (strcmp(inputArgs[0], "dirlist") == 0 && strcmp(inputArgs[1], "-a") == 0) {
                const char *dir = getenv("HOME");

                // Traversing the directory tree rooted at ~ and call listDirectories function for each directory
                if (nftw(dir, listDirectories, 200, FTW_PHYS) == -1) {
                    perror("nftw");
                    exit(EXIT_FAILURE);
                }

                // Sorting directories as per alphabetical order
                qsort(directories, num_dirs, sizeof(char *), compare);

                // Calculating total length including separators
                size_t total_length = 0;
                for (int i = 0; i < num_dirs; i++) {
                    total_length += strlen(directories[i]) + 1; // +1 for newline separator
                }

                // Sending the size of the data to the client
                ssize_t bytes_sent = send(clientSocketDesc, &total_length, sizeof(size_t), 0);
                if (bytes_sent == -1) {
                    perror("Error sending data size");
                    exit(EXIT_FAILURE);
                }

                // {"/sanjay/a.txt", "sanjay/b.txt"} -> "/sanjay/a.txt \n /sanjay/b.txt"
                // Concatenating directory paths into a single string
                concatenated_paths = malloc(total_length + 1); // Add 1 for null terminator
                if (concatenated_paths == NULL) {
                    perror("malloc");
                    exit(EXIT_FAILURE);
                }
                concatenated_paths[0] = '\0'; // Initialize the string

                for (int i = 0; i < num_dirs; i++) {
                    strcat(concatenated_paths, directories[i]);
                    strcat(concatenated_paths, "\n"); // Separator
                    free(directories[i]); // Free memory for each directory name
                }

                // Send concatenated_paths to the client
                bytes_sent = send(clientSocketDesc, concatenated_paths, total_length, 0);
                if (bytes_sent == -1) {
                    perror("Error sending concatenated_paths");
                    exit(EXIT_FAILURE);
                }

                free(concatenated_paths); // Free concatenated_paths string
                // free(directories);
            }

            // Below if condition is used for implementing 'dirlist -t' command (to list the directories as per created time)
            else if (strcmp(inputArgs[0], "dirlist") == 0 && strcmp(inputArgs[1], "-t") == 0) {
                const char *dir = getenv("HOME");

                // Traversing the directory tree rooted at ~ and call listDirectoriesTime function for each directory
                if (nftw(dir, listDirectoriesTime, 20, FTW_PHYS) == -1) {
                    perror("nftw");
                    exit(EXIT_FAILURE);
                }

                // Sorting directories as per directories created time
                qsort(directoriesObj, directoriesCount, sizeof(DirectoryInfo), compare_as_per_time);

                // Calculating total length including separators
                size_t total_length = 0;
                for (int i = 0; i < directoriesCount; i++) {
                    total_length += strlen(directoriesObj[i].path) + 1; // +1 for newline separator
                }

                // Sending the size of the data to the client
                ssize_t bytes_sent = send(clientSocketDesc, &total_length, sizeof(size_t), 0);
                if (bytes_sent == -1) {
                    perror("Error sending data size");
                    exit(EXIT_FAILURE);
                }

                // Concatenating directory paths into a single string
                concatenated_paths = malloc(total_length + 1); // Add 1 for null terminator
                if (concatenated_paths == NULL) {
                    perror("malloc");
                    exit(EXIT_FAILURE);
                }
                concatenated_paths[0] = '\0'; // Initialize the string

                // "/sanjay/a.txt \n /sanjay/b.txt"
                for (int i = 0; i < directoriesCount; i++) {
                    strcat(concatenated_paths, directoriesObj[i].path);
                    strcat(concatenated_paths, "\n"); // Separator
                    clearDirectoryInfo(&directoriesObj[i]); // Clear memory for directoriesObj
                }

                // Sending concatenated_paths to the client
                bytes_sent = send(clientSocketDesc, concatenated_paths, total_length + 1, 0);
                if (bytes_sent == -1) {
                    perror("Error sending concatenated_paths");
                    exit(EXIT_FAILURE);
                }

                // printf("Directories sent to client:\n%s\n", concatenated_paths);

                free(concatenated_paths); // Free concatenated_paths string
            }

            else{
                char *message = "Invalid Command";
                // printf("%s\n", message);
                send(clientSocketDesc, message, strlen(message), 0);
            }

            free(inputDataFromClient);
        }
    }

    close(clientSocketDesc); // Closing client socket
}

// Main function
void main()
{
    // Creating endpoint for the server using 'socket' system call
    int serverSD; // integer to hold server socket descriptor
    if ((serverSD = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        fprintf(stderr, "Could not create socket\n");
        exit(1);
    }

    // Creating struct for binding the socket
    struct sockaddr_in servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons((uint16_t) PORT_NUMBER);
    servAddr.sin_addr.s_addr = INADDR_ANY;

    // Binding socket to the specified IP address and port number
    if ((bind(serverSD, (struct sockaddr *) &servAddr, sizeof(servAddr))) == -1)
    {
        fprintf(stderr, "Binding Failure\n");
        exit(1);
    }

    // Listening for client connections present in the queue
    if ((listen(serverSD, NO_OF_CONNECTIONS)) == -1)
    {
        fprintf(stderr, "Listening Failure\n");
        exit(1);
    }

    // Accepting client connections
    while (1)
    {
        // Integer to hold client socket descriptor.
        int clientSD = accept(serverSD, (struct sockaddr *) NULL, NULL);
        printf("Got a client\n");

        int childProcessID = fork(); // Creating a child process
        if (childProcessID == 0)
        {
            printf("Inside Child Process\n");
            crequest(clientSD);
            // exit(100/0);
        }
        else if (childProcessID < 0) {
            perror("Fork failed");
            exit(1);
        }
    }

    // Close server socket
    close(serverSD);
}
