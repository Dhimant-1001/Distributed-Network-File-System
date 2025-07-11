
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>

// #define NS_PORT 8090
#define BUFFER_SIZE 40960
#define TIMEOUT_SECONDS 5

int ns_sock;

int receiving_list = 0;
bool running = true;

typedef struct
{
    pthread_mutex_t lock; // Mutex to protect shared state
    pthread_cond_t cond;  // Condition variable for signaling
    int reader_count;     // Number of active readers
    bool is_async_write;  // Flag to indicate if an async write is ongoing
    bool writer_present;  // Flag to indicate if a writer is present
} FileLock;

#define MAX_FILES 1000
// #define BUFFER_SIZE 999999

typedef struct
{
    char path[BUFFER_SIZE];
    FileLock lock;
} FileLockEntry;

FileLockEntry file_lock_map[MAX_FILES];
int file_lock_count = 0;
pthread_mutex_t file_map_lock = PTHREAD_MUTEX_INITIALIZER;

FileLock *get_file_lock(const char *path)
{
    pthread_mutex_lock(&file_map_lock);

    // Search for the file lock in the existing entries
    for (int i = 0; i < file_lock_count; i++)
    {
        if (strcmp(file_lock_map[i].path, path) == 0)
        {
            pthread_mutex_unlock(&file_map_lock);
            return &file_lock_map[i].lock;
        }
    }

    // If not found, create a new entry
    if (file_lock_count < MAX_FILES)
    {
        strcpy(file_lock_map[file_lock_count].path, path);
        FileLock *new_lock = &file_lock_map[file_lock_count].lock;
        pthread_mutex_init(&new_lock->lock, NULL);
        pthread_cond_init(&new_lock->cond, NULL);
        new_lock->reader_count = 0;
        new_lock->is_async_write = false;
        new_lock->writer_present = false;
        file_lock_count++;
        pthread_mutex_unlock(&file_map_lock);
        return new_lock;
    }

    pthread_mutex_unlock(&file_map_lock);
    return NULL; // Map is full
}

void read_file(const char *path)
{
    FileLock *file_lock = get_file_lock(path);
    if (!file_lock)
    {
        printf("Error: Unable to get lock for file %s\n", path);
        return;
    }

    pthread_mutex_lock(&file_lock->lock);

    // Wait if there's an ongoing async write or a writer is present
    while (file_lock->is_async_write || file_lock->writer_present)
    {
        pthread_cond_wait(&file_lock->cond, &file_lock->lock);
    }

    // Increment reader count
    file_lock->reader_count++;

    pthread_mutex_unlock(&file_lock->lock);

    // Perform read operation (simulated)
    printf("Reading file: %s\n", path);
    sleep(1); // Simulate reading

    pthread_mutex_lock(&file_lock->lock);

    // Decrement reader count and signal if necessary
    file_lock->reader_count--;
    if (file_lock->reader_count == 0)
    {
        pthread_cond_signal(&file_lock->cond);
    }

    pthread_mutex_unlock(&file_lock->lock);
}

char critical_response_buffer[BUFFER_SIZE];
pthread_mutex_t response_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t response_cond = PTHREAD_COND_INITIALIZER;
bool critical_response_received = false;

void *listen_to_ns(void *arg);
void connect_and_read_from_ss(const char *ss_ip, int ss_port, const char *file_path);
void connect_and_write_to_ss(const char *ss_ip, int ss_port, const char *file_path, const char *data);
void connect_and_get_file_info(const char *ss_ip, int ss_port, const char *file_path);

char latest_IP_and_things_recieved_from_the_ns[BUFFER_SIZE];

void stream_from_server(const char *ss_ip, int ss_port, const char *file_path)
{
    int ss_sock;
    struct sockaddr_in ss_addr;
    char buffer[BUFFER_SIZE] = {0};

    // Create socket for Storage Server
    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return;
    }

    // Set up Storage Server address
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);
    printf("IP: %s %d\n", ss_ip, ss_port);
    inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr);

    // Connect to the Storage Server
    if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
    {
        perror("Storage Server Connection failed");
        close(ss_sock);
        return;
    }

    // Send the STREAM command to Storage Server
    snprintf(buffer, sizeof(buffer), "STREAM %s", file_path);
    send(ss_sock, buffer, strlen(buffer), 0);
    printf("\nRequest sent to Storage Server to stream: %s\n", buffer);

    // Use mpv to play audio data received in real-time (macOS compatible)
    FILE *audio_pipe = popen("mpv --no-terminal --ao=coreaudio -", "w");
    if (!audio_pipe)
    {
        // Fallback to other audio outputs
        audio_pipe = popen("mpv --no-terminal -", "w");
        if (!audio_pipe)
        {
            perror("Error opening pipe to mpv");
            close(ss_sock);
            return;
        }
    }
    int bytes_read;
    while ((bytes_read = recv(ss_sock, buffer, BUFFER_SIZE, 0)) > 0)
    {
        if (bytes_read == 3 && strncmp(buffer, "EOF", 3) == 0)
        {
            break; // End of stream
        }
        fwrite(buffer, 1, bytes_read, audio_pipe);
        // printf("Received audio data\n");
    }

    // printf("Streaming ended.\n");
    if (bytes_read < 0)
    {
        perror("Error receiving audio data");
    }

    // Cleanup
    pclose(audio_pipe);
    close(ss_sock);
    printf("Streaming ended.\n");
}

void *listen_to_ns(void *arg)
{
    char buffer[BUFFER_SIZE] = {0};
    int flag = 0;
    while (running)
    {

        memset(buffer, 0, BUFFER_SIZE); // Clear buffer before reading
        int bytes_read = read(ns_sock, buffer, BUFFER_SIZE - 1);

        if (bytes_read > 0)
        {
            buffer[bytes_read] = '\0'; // Null-terminate the received string

            if (strstr(buffer, "File: ") != 0 || strstr(buffer, "Directory: ") != 0)
            {
                printf("%s", buffer);
                receiving_list = 1;

                continue;
            }

            else if (strncmp(buffer, "EOF", 3) == 0)
            {
                printf("End of list.\n");
                receiving_list = 0;
                // continue;
            }

            else if (strncmp("IP", buffer, 2) == 0 ||
                     strncmp("File not found in any storage server", buffer, 36) == 0 || strncmp("Unknown command", buffer, 15) == 0)
            {
                strcpy(latest_IP_and_things_recieved_from_the_ns, buffer);
                continue;
            }
            else
            {
                printf("\n%s\n", buffer);
                // printf("\n[From Naming Server]: %s\n", buffer);
            }

            if (flag == 0)
            {
                // printf("Enter command (READ/WRITE/INFO/STREAM/EXIT): "); // Re-prompt user for input
                printf("Enter a command: ");
            }
            flag = 1;
            fflush(stdout); // Ensure the prompt is displayed immediately
        }
        else if (bytes_read == 0)
        {
            printf("Naming Server connection closed.\n");
            running = false;

            break;
        }
        else
        {
            perror("Error reading from Naming Server");
            running = false;
            pthread_mutex_unlock(&response_mutex);

            break;
        }
    }
    return NULL;
}

pthread_t listener_thread;

void send_request_to_ns(const char *naming_server_ip, int ns_port, const char *command, const char *file_path)
{
    struct sockaddr_in ns_addr;
    char buffer[BUFFER_SIZE] = {0};

    // Create socket for Naming Server
    if ((ns_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return;
    }

    // Set up Naming Server address
    ns_addr.sin_family = AF_INET;
    ns_addr.sin_port = htons(ns_port);
    ns_addr.sin_addr.s_addr = inet_addr(naming_server_ip);
    // if (inet_pton(AF_INET, naming_server_ip, &ns_addr.sin_addr) <= 0)
    // {
    //     perror("Invalid address/ Address not supported");
    //     return;
    // }

    // Connect to the Naming Server
    if (connect(ns_sock, (struct sockaddr *)&ns_addr, sizeof(ns_addr)) < 0)
    {
        perror("NS Connection failed");
        close(ns_sock);
        return;
    }

    // snprintf(buffer, sizeof(buffer), "CLIENT");
    snprintf(buffer, sizeof(buffer), "CLIENT CONNECTING TO NAMING SERVER ...");

    send(ns_sock, buffer, strlen(buffer), 0);
    printf("Connecting to Naming Server as client ...\n");

    sleep(1);
    // printf("Connected to Naming Server as client.\n");

    pthread_create(&listener_thread, NULL, listen_to_ns, NULL);

    // Continuously prompt user for commands
    int flag = 0;
    while (running)
    {
        char command[BUFFER_SIZE];
        char file_path[BUFFER_SIZE];
        int is_sync = 0; // Default is asynchronous
        char file_name[BUFFER_SIZE];
        // char* latest_IP_and_things_recieved_from_the_ns

        // Prompt for command type
        if (flag == 1)
        {
            // printf("\nEnter command (READ/WRITE/INFO/STREAM/EXIT): ");
            printf("\nEnter a command: ");
        }
        flag = 1;
        fgets(command, BUFFER_SIZE, stdin);
        if (strcmp(command, "\n") == 0 || strcmp(command, " ") == 0)
        {
            printf("Invalid Command\n", command);
            continue;
        }

        command[strcspn(command, "\n")] = 0; // Remove newline

        // Exit if user types "EXIT"
        if (strcmp(command, "EXIT") == 0)
        {
            printf("Exiting.\n");
            running = false; // Stop the listener thread
            break;
        }

        // Prompt for file path
        printf("Enter file path: ");
        fgets(file_path, BUFFER_SIZE, stdin);
        file_path[strcspn(file_path, "\n")] = 0; // Remove newline

        /////////////////////////////////////////////

        // printf("Buffer: %s\n", buffer);

        if (strcmp(command, "CREATE_DIC") == 0)
        {
            printf("Enter name of directory (without ./): ");
            fgets(file_name, BUFFER_SIZE, stdin);
            file_name[strcspn(file_name, "\n")] = 0; // Remove newline
            strcat(file_path, "/");
            strcat(file_path, file_name);
        }
        else if (strcmp(command, "CREATE_F") == 0)
        {
            printf("Enter name of file (without ./): ");
            fgets(file_name, BUFFER_SIZE, stdin);
            file_name[strcspn(file_name, "\n")] = 0; // Remove newline
            strcat(file_path, "/");
            strcat(file_path, file_name);
        }

        /////////////////////////////////////////////

        // Send the command to the Naming Server
        snprintf(buffer, sizeof(buffer), "%s %s", command, file_path);
        send(ns_sock, buffer, strlen(buffer), 0);

        printf("Request sent to Naming Server: %s\n", buffer);

        sleep(0.7);
        if (strstr(buffer, "Unknown command") != 0)
        {
            printf("Unknown command: %s\n", command);
            continue;
        }

        if (strcmp(command, "LIST") == 0 || strcmp(command, "CREATE_DIC") == 0 || strcmp(command, "CREATE_F") == 0 || strcmp(command, "DELETE") == 0 || strcmp(command, "COPY") == 0)
        {
            continue;
        }

        if (strstr(buffer, "File not found in any storage server") != 0)
        {
            continue;
        }

        int bytes_read = 0;

        // if (strcmp(command, "CREATE_DIC") != 0 && strcmp(command, "DELETE") != 0 && strcmp(command, "CREATE_F") != 0)
        //     bytes_read = read(ns_sock, buffer, BUFFER_SIZE - 1);

        // Wait for the Naming Server to respond
        sleep(1);
        int byts_read = strlen(latest_IP_and_things_recieved_from_the_ns);

        if (strncmp(latest_IP_and_things_recieved_from_the_ns, "Unknown command", strlen("Unknown command")) == 0)
        {
            // printf("Came here\n");
            printf("Unknown command: %s\n", command);
            continue;
        }

        // printf("Received from Naming Server: %s\n", latest_IP_and_things_recieved_from_the_ns);
        printf("Naming Server sent the details of the storage server:\n%s\n", latest_IP_and_things_recieved_from_the_ns);

        bytes_read = 1;

        // byts_read = strlen(buffer);

        if (byts_read > 0)
        {
            buffer[byts_read] = '\0';
            // printf("Received from Naming Server: %s\n", latest_IP_and_things_recieved_from_the_ns);

            if (strstr(latest_IP_and_things_recieved_from_the_ns, "File not found in any storage server") != 0)
            {
                continue;
            }

            // Parse IP and Port from NS response
            char ss_ip[INET_ADDRSTRLEN];
            int ss_port;

            // printf("STRING: %s\n", latest_IP_and_things_recieved_from_the_ns);
            // sscanf(latest_IP_and_things_recieved_from_the_ns, "IP: %s Port: %d", ss_ip, &ss_port);

            sscanf(latest_IP_and_things_recieved_from_the_ns, " IP: %s Port: %d", ss_ip, &ss_port);
            printf("Connecting to Storage Server at IP: %s, Port: %d\n", ss_ip, ss_port);

            // printf("Connecting to Storage Server at IP: %s, Port: %d\n", ss_ip, ss_port);

            // Connect to Storage Server based on the command
            if (strcmp(command, "READ") == 0)
            {

                connect_and_read_from_ss(ss_ip, ss_port, file_path);
            }
            else if (strcmp(command, "WRITE") == 0)
            {
                char sync_flag[BUFFER_SIZE];
                printf("Do you want to write synchronously irrespective of time overhead? (yes/no): ");
                fgets(sync_flag, BUFFER_SIZE, stdin);
                sync_flag[strcspn(sync_flag, "\n")] = 0;

                if (strcmp(sync_flag, "yes") == 0)
                    is_sync = 1;

                memset(buffer, 0, sizeof(buffer));
                if (is_sync)
                {
                    snprintf(buffer, sizeof(buffer), "--SYNC");
                }

                printf("Enter data to write: ");
                fgets(buffer + strlen(buffer), BUFFER_SIZE - strlen(buffer), stdin);
                connect_and_write_to_ss(ss_ip, ss_port, file_path, buffer);
            }
            else if (strcmp(command, "INFO") == 0)
                connect_and_get_file_info(ss_ip, ss_port, file_path);
            else if (strcmp(command, "STREAM") == 0)
            {
                // check if the extension is .mp3
                char *ext = strrchr(file_path, '.');
                if (ext == NULL || strcmp(ext, ".mp3") != 0)
                {
                    printf("Invalid file extension. Only .mp3 files are supported for streaming.\n");
                    continue;
                }
                stream_from_server(ss_ip, ss_port, file_path);
            }
            else if (strcmp(command, "LIST") == 0)
            {

                printf("List of files in the directory:\n%s\n", latest_IP_and_things_recieved_from_the_ns);

                send(ns_sock, "LIST", strlen("LIST"), 0);

                sleep(1);
                // while (receiving_list)
                // {
                // };
            }
            else
            {
                printf("Wrong input command: %s\n", command);
            }
        }
        // else
        // {
        //     printf("No response from Naming Server.\n");
        // }
    }

    // pthread_join(listener_thread, NULL);
    close(ns_sock);
}

void connect_and_read_from_ss(const char *ss_ip, int ss_port, const char *file_path)
{
    int ss_sock;
    struct sockaddr_in ss_addr;
    char buffer[BUFFER_SIZE] = {0};

    // Create socket for Storage Server
    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return;
    }

    // Set up Storage Server address
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);

    ss_addr.sin_addr.s_addr = inet_addr(ss_ip);

    // printf("IP: %s %d\n", ss_ip, ss_port);

    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid address or address not supported\n");
        close(ss_sock);
        return;
    }

    // printf("IP: %s %d\n", ss_ip, ss_port);

    // Connect to the Storage Server
    if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
    {
        perror("SS Connection failed");
        close(ss_sock);
        return;
    }

    // Send READ request
    snprintf(buffer, sizeof(buffer), "READ %s", file_path);
    send(ss_sock, buffer, strlen(buffer), 0);

    // Receive file content
    printf("File content:\n");
    while (1)
    {
        int bytes_read = read(ss_sock, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0)
        {
            break; // Handle connection closing or error
        }
        buffer[bytes_read] = '\0'; // Null terminate the received data

        if (strcmp(buffer, "Write in progress. Cannot read the file right now. Please try again later.\n") == 0)
        {
            printf("Write in progress. Cannot read the file right now. Please try again later.\n");
            break;
        }

        // Check for EOF marker and break if received
        if (strcmp(buffer, "EOF\n") == 0)
        {
            break;
        }

        printf("%s", buffer);
    }

    // pthread_join(listener_thread, NULL);
    close(ss_sock); // Close the connection after EOF
}

void connect_and_write_to_ss(const char *ss_ip, int ss_port, const char *file_path, const char *data)
{
    int ss_sock;
    struct sockaddr_in ss_addr;
    char buffer[BUFFER_SIZE] = {0};

    // Create socket for Storage Server
    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return;
    }

    // Set up Storage Server address
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid address or address not supported\n");
        close(ss_sock);
        return;
    }

    // Connect to the Storage Server
    if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
    {
        perror("SS Connection failed");
        close(ss_sock);
        return;
    }

    // Send WRITE request with file path and data
    snprintf(buffer, sizeof(buffer), "WRITE %s %s", file_path, data);
    send(ss_sock, buffer, strlen(buffer), 0);

    sleep(1);

    // printf("CAME HERE\n");

    if (strncmp(data, "--SYNC", 6) != 0)
    {
        // Send data in chunks
        size_t data_len = strlen(data);
        size_t sent_bytes = 0;
        while (sent_bytes < data_len)
        {
            size_t chunk_size = (data_len - sent_bytes > BUFFER_SIZE) ? BUFFER_SIZE : data_len - sent_bytes;
            ssize_t bytes_sent = send(ss_sock, data + sent_bytes, chunk_size, 0);
            if (bytes_sent < 0)
            {
                perror("Error sending data");
                break;
            }
            sent_bytes += bytes_sent;
        }
    }
    else
    {
        send(ss_sock, data, strlen(data), 0);
    }

    sleep(0.4);
    snprintf(data, sizeof(data), "EOF");
    send(ss_sock, data, strlen(data), 0);

    // Receive server confirmation
    int bytes_read = read(ss_sock, buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0)
    {
        buffer[bytes_read] = '\0'; // Null terminate the received data
        printf("Storage Server response: %s\n", buffer);
    }

    close(ss_sock);
}

void connect_and_get_file_info(const char *ss_ip, int ss_port, const char *file_path)
{
    int ss_sock;
    struct sockaddr_in ss_addr;
    char buffer[BUFFER_SIZE] = {0};

    // Create socket for Storage Server
    if ((ss_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return;
    }

    // Set up Storage Server address
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid address or address not supported\n");
        close(ss_sock);
        return;
    }

    // Connect to the Storage Server
    if (connect(ss_sock, (struct sockaddr *)&ss_addr, sizeof(ss_addr)) < 0)
    {
        perror("SS Connection failed");
        close(ss_sock);
        return;
    }

    // Send INFO request with file path
    snprintf(buffer, sizeof(buffer), "INFO %s", file_path);
    send(ss_sock, buffer, strlen(buffer), 0);

    // Receive file info
    int bytes_read = read(ss_sock, buffer, BUFFER_SIZE - 1);
    if (bytes_read > 0)
    {
        buffer[bytes_read] = '\0'; // Null terminate the received data
        printf("File info from Storage Server: %s\n", buffer);
    }

    close(ss_sock);
}

int main(int argc, char *argv[])
{
    char naming_server_ip[BUFFER_SIZE];
    int ns_port;

    if (argc < 3)
    {
        printf("Usage: %s <naming_server_ip> <ns_port>\n", argv[0]);
        return 1;
    }

    strcpy(naming_server_ip, argv[1]);
    ns_port = atoi(argv[2]);

    send_request_to_ns(naming_server_ip, ns_port, "", "");
    return 0;
}
