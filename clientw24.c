#include <netinet/in.h> //structure for storing address information
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h> //for socket APIs
#include <sys/types.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h> // for close function

#define COMMAND_LENGTH 50
#define BUFFER_SIZE 256
#define MAX_FILENAME_LEN 512
#define FILENAME "connection_count.txt"
#define MAX_BUFFER_SIZE 1024
#define ARGS_LENGTH 10
#define TAR_FILE "/w24project/temp.tar.gz"

int inputArgsLength = 0;

char *concatenated_paths = NULL; // Initialize with an NULL
size_t buffer_size = 0;

char tarFile[100];
char *inputArgs[ARGS_LENGTH]; // To store each and every argument from the input data

// Structure to hold file information
struct FileInformation {
    char filename[MAX_FILENAME_LEN];
    off_t size;
    time_t created_time;
    mode_t permissions;
};

// Function to parse input command into an array of string elements
int validateInputCommand(char *inputCommand, char *inputArgs[])
{
    // Initialize inputArgsLength to 0 before parsing
    int inputArgsLength = 0;
    char *copy_inputCommand = strdup(inputCommand);
    // Breaking down the entire string into each array element using strtok
    char *token = strtok(copy_inputCommand, " ");
    int i = 0;
    while (token != NULL && i < ARGS_LENGTH - 1) { // Prevent overflow in inputArgs array
        inputArgs[i++] = strdup(token);
        token = strtok(NULL, " ");
        inputArgsLength++; // Incrementing args length
    }
    inputArgs[i] = NULL; // Mark the end of arguments

    // Validate input command
    if (strcmp(inputArgs[0], "dirlist") == 0){
        // If no options (-a or -t is passed)
        if (inputArgsLength == 1) {
             printf("Less arguments provided try again!\nl");
            return 0; // Command is invalid
        }
        else if ((strcmp(inputArgs[1], "-a") == 0) || (strcmp(inputArgs[1], "-t") == 0)) {
            return 1; // Command is valid
        } else {
            printf("Only dirlist -a and dirlist -t is supported\n");
            return 0; // Command is invalid
        }
    }
    else if (strcmp(inputArgs[0], "w24fn") == 0){
        // If filename not passed
        if (inputArgsLength == 1) {
             printf("Less arguments provided try again!\nl");
            return 0; // Command is invalid
        }
        else if (inputArgs[1] != NULL && inputArgs[1][0] != '\0' && strlen(inputArgs[1]) > 0) {
            return 1; // Command is valid
        } else {
            printf("Please send the file that has to be searched\n");
            return 0; // Command is invalid
        }
    }
    else if (strcmp(inputArgs[0], "w24fz") == 0){
        // If size 1 and size 2 both not passed
        if (inputArgsLength == 1) {
             printf("Less arguments provided try again!\nl");
            return 0; // Command is invalid
        }
        // If size 1 or size 2 only one is passed
        else if (inputArgs[1] != NULL && inputArgs[1][0] != '\0' && strlen(inputArgs[1]) > 0 && inputArgs[2] != NULL && inputArgs[2][0] != '\0' && strlen(inputArgs[2]) > 0) {
            return 1; // Command is valid
        } else {
            printf("Please pass both size1 and size2\n");
            return 0; // Command is invalid
        }
    }
    else if ((strcmp(inputArgs[0], "w24fdb") == 0) || (strcmp(inputArgs[0], "w24fda") == 0)) {
        // If date is not passed
        if (inputArgsLength == 1) {
             printf("Less arguments provided try again!\nl");
            return 0; // Command is invalid
        }
        // If date is in invalid format
        else if (inputArgs[1] != NULL && inputArgs[1][0] != '\0' && strlen(inputArgs[1]) == 10) {
            // Check if date format is "YYYY-MM-DD"
            if (inputArgs[1][4] == '-' && inputArgs[1][7] == '-') {
                return 1; // Command and date format are valid
            } else {
                printf("Date format is invalid. Plase pass in YYYY-MM-DD\n");
                return 0; // Date format is invalid
            }
        } else {
            printf("Date format is invalid. Plase pass in YYYY-MM-DD\n");
            return 0; // Command or date is invalid
        }
    }
    else if (strcmp(inputArgs[0], "w24ft") == 0){
        // If not even one extension is passed
        if (inputArgsLength == 1) {
             printf("Less arguments provided try again!\nl");
            return 0; // Command is invalid
        }
        // If more than three extensions are passed
        else if (inputArgsLength > 4){
            printf("Maximum three extensions only\n");
            return 0;
        }
        else{
            return 1;
        }

    }
    else if (strcmp(inputArgs[0], "quitc") == 0){
        return 1;
    }
    else{
        printf("Invalid command\n");
        return 0;
    }
}

// Function used to create w24project folder
void createFolder() {
    // Fork a child process
    pid_t pid = fork();

    if (pid < 0) {
        // Fork failed
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        char *home = getenv("HOME");

        // Concatenate the directory path
        char dir_path[256]; // Adjust the size if needed
        snprintf(dir_path, sizeof(dir_path), "%s/w24project", home);

        // Child process
        char *args[] = {"mkdir", "-p", dir_path, NULL};

        // Replace the child process with mkdir using execvp
        if (execvp("mkdir", args) == -1) {
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    } else {
        // Parent process
        // Wait for the child process to finish
        wait(NULL);
        // printf("w24project folder created successfully.\n");
    }

    // Changing tar file path
    char *home = getenv("HOME");
    char *path = malloc(strlen(home) + strlen(TAR_FILE) + 1);

    strcpy(path, home);
    strcat(path, TAR_FILE);
    strcpy(tarFile, path);

    // Use the concatenated path
    // printf("Path: %s\n", tarFile);

    // Free the allocated memory
    free(path);
}

// Function to receive file content over socket
void receive_file(int clientSocketDesc, const char *filename) {
    // Receive file size from server
    long file_size;
    if (recv(clientSocketDesc, &file_size, sizeof(long), 0) == -1) {
        perror("Error receiving file size");
        exit(EXIT_FAILURE);
    }

    // Check if file_size is equal to 0
    else if (file_size == 0) {
        printf("No file found\n");
    }

    else {
        FILE *fp = fopen(filename, "wb");
        if (fp == NULL) {
            perror("Error opening file");
            exit(EXIT_FAILURE);
        }

        // Receive and write file content in chunks
        char *buffer = (char *)malloc(MAX_BUFFER_SIZE);
        if (buffer == NULL) {
            perror("Error allocating memory for buffer");
            exit(EXIT_FAILURE);
        }

        size_t total_bytes_received = 0;
        while (total_bytes_received < file_size) {
            // Determine the size of the chunk to receive
            size_t chunk_size = (file_size - total_bytes_received < MAX_BUFFER_SIZE) ? (file_size - total_bytes_received) : MAX_BUFFER_SIZE;

            // Receive a chunk of data from the server
            ssize_t bytes_received = recv(clientSocketDesc, buffer, chunk_size, 0);
            if (bytes_received == -1) {
                perror("Error receiving file content");
                exit(EXIT_FAILURE);
            }

            // Write the received chunk to the file
            size_t bytes_written = fwrite(buffer, 1, bytes_received, fp);
            if (bytes_written != bytes_received) {
                perror("Error writing file content");
                exit(EXIT_FAILURE);
            }

            // Print the received chunk
            fwrite(buffer, 1, bytes_received, stdout);

            // Update the total bytes received
            total_bytes_received += bytes_received;
        }

        free(buffer);
        fclose(fp);
    }
}

// Function to read connection count from file
int read_connection_count()
{
    // Retriving the count from the file using open and read system call
    int fd = open(FILENAME, O_RDONLY | O_CREAT, 0644);
    if (fd == -1) {
        perror("Error opening file");
        return -1;
    }

    char buffer[10];
    if (read(fd, buffer, sizeof(buffer)) == -1) {
        perror("Error reading file");
        close(fd);
        return -1;
    }

    close(fd); // closing file descriptor
    return atoi(buffer); // sending the value
}

// Function to write connection count to file
void write_connection_count(int count)
{
    // Updating the count from the file using open and write system call
    int fd = open(FILENAME, O_WRONLY | O_TRUNC);
    if (fd == -1) {
        perror("Error opening file");
        return;
    }

    char buffer[10];
    sprintf(buffer, "%d", count);
    if (write(fd, buffer, strlen(buffer)) == -1) {
        perror("Error writing to file");
        close(fd);
        return;
    }

    close(fd); // closing file descriptor
}

int main()
{
    int connection_count = read_connection_count(); // getting connection count
    if (connection_count == -1) {
        printf("Error reading connection count from file\n");
        exit(EXIT_FAILURE);
    }

    // Creating a socket descriptor
    int socketDescriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDescriptor < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Enable SO_REUSEADDR option
    int optval = 1;
    if (setsockopt(socketDescriptor, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Creating struct for connecting the socket
    struct sockaddr_in servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = INADDR_ANY;

    // Set server port for the current connection
    if (connection_count < 3) {
        servAddr.sin_port = htons((uint16_t)8000); // serverw24 for first 3 connections
    } else if (connection_count < 6) {
        servAddr.sin_port = htons((uint16_t)8001); // mirror1 for connections 4-6
    } else if (connection_count < 9) {
        servAddr.sin_port = htons((uint16_t)8002); // mirror2 for connections 7-9
    } else {
        servAddr.sin_port = htons((uint16_t)(8000 + connection_count % 3)); // Alternating between port numbers for remaining connections
    }

    // Connecting to server
    int connectStatus = connect(socketDescriptor, (struct sockaddr*)&servAddr, sizeof(servAddr));
    if (connectStatus == -1) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }
    else {
        printf("Connected to port %d\n", ntohs(servAddr.sin_port));

        connection_count++; // Incrementing the connection count
        write_connection_count(connection_count); // Writing updated connection count value back to file

        // Creating w24project folder in present working directory
        createFolder();

        while (1) {
            // Inside the while loop
            char *messageRecvFromServer = malloc(BUFFER_SIZE);
            if (messageRecvFromServer == NULL) {
                perror("Memory allocation failed");
                close(socketDescriptor);
                return EXIT_FAILURE;
            }

            char *inputCommand = malloc(COMMAND_LENGTH);
            if (inputCommand == NULL) {
                perror("Memory allocation failed");
                free(messageRecvFromServer);
                close(socketDescriptor);
                return EXIT_FAILURE;
            }

            printf("clientw24$ ");
            fgets(inputCommand, COMMAND_LENGTH, stdin); // Reading input command using fgets
            inputCommand[strcspn(inputCommand, "\n")] = 0; // Removing trail newline character

            int isValidOrNot = validateInputCommand(inputCommand, inputArgs);
            if (!isValidOrNot){
                free(messageRecvFromServer);
                free(inputCommand);
                continue;
            }

            concatenated_paths=NULL;

            // Send the filename to the server
            send(socketDescriptor, inputCommand, strlen(inputCommand), 0);

            // quitc :- The command is transferred to the serverw24 and the client process is terminated 
            if (strcmp(inputCommand,"quitc") == 0){
                int bytesReceived = recv(socketDescriptor, messageRecvFromServer, BUFFER_SIZE, 0);
                messageRecvFromServer[bytesReceived] = '\0';

                // Check if the message indicates disconnection
                if (strcmp(messageRecvFromServer, "Disconnecting") == 0) {
                    free(inputCommand);
                    free(messageRecvFromServer);
                    break;
                }
            }

            // w24fn - command to list file details
            else if (strncmp(inputCommand, "w24fn", strlen("w24fn")) == 0){
                // Receiving file information from server
                struct FileInformation file_info;
                recv(socketDescriptor, &file_info, sizeof(struct FileInformation), 0);

                if (file_info.filename == NULL || file_info.filename[0] == '\0' || (strcmp(file_info.filename, "Invalid Command") == 0)) {
                    printf("File Not Found\n");
                }
                else {
                    // Print file information
                    printf("Filename: %s\n", file_info.filename);
                    printf("Size: %ld bytes\n", (long)file_info.size);
                    printf("Created: %s", ctime(&file_info.created_time));
                    printf("Permissions: %o\n", file_info.permissions);
                }
            }

            else if ((strncmp(inputCommand, "w24ft", strlen("w24ft")) == 0) || (strncmp(inputCommand, "w24fz", strlen("w24fz")) == 0) || (strncmp(inputCommand, "w24fdb", strlen("w24fdb")) == 0) || (strncmp(inputCommand, "w24fda", strlen("w24fda")) == 0)){
                // Receive file from server and save as "temp.tar.gz"
                receive_file(socketDescriptor, tarFile);
                // printf("File received successfully as temp.tar.gz.\n");

            }

            else if (strncmp(inputCommand, "dirlist", strlen("dirlist")) == 0) {
                size_t recv_size;

                // Receive the size of the buffer
                if (recv(socketDescriptor, &recv_size, sizeof(size_t), 0) == -1) {
                    perror("recv buffer size");
                    exit(EXIT_FAILURE);
                }

                // Allocate memory for concatenated_paths based on the received size
                concatenated_paths = malloc(recv_size + 1); // Add 1 for null terminator
                if (!concatenated_paths) {
                    perror("malloc");
                    exit(EXIT_FAILURE);
                }

                // Receive concatenated_paths
                if (recv(socketDescriptor, concatenated_paths, recv_size, 0) == -1) {
                    perror("recv concatenated_paths");
                    exit(EXIT_FAILURE);
                }

                // Null-terminate the string
                concatenated_paths[recv_size] = '\0'; // Null-terminate the string

                printf("Received directories from server:\n%s\n", concatenated_paths);

                // If there is any junks files then receiving that
                if (strncmp(inputCommand, "dirlist -t", strlen("dirlist -t")) == 0){  
                    // Receive concatenated_paths
                    if (recv(socketDescriptor, concatenated_paths, recv_size, 0) == -1) {
                        perror("recv concatenated_paths");
                        exit(EXIT_FAILURE);
                    }
                }

                free(concatenated_paths);
            }

            else{
                int bytesReceived = recv(socketDescriptor, messageRecvFromServer, BUFFER_SIZE, 0);
                messageRecvFromServer[bytesReceived] = '\0';
                printf("%s\n",messageRecvFromServer);
            }

            // Free memory for inputCommand and message for next iteration
            free(inputCommand);
            free(messageRecvFromServer);
        }

        // Close socket
        printf("Terminating....\n");
        close(socketDescriptor);
    }

    return EXIT_SUCCESS;
}
