# Network File System (NFS) Implementation

A distributed file system implementation in C that allows multiple clients to access files stored across multiple storage servers through a centralized naming server.

## 🎯 Project Overview

This NFS implementation consists of three main components:
- **Naming Server**: Central coordinator that manages file location mapping and client-server communication
- **Storage Servers**: Handle actual file storage, retrieval, and operations
- **Clients**: User interfaces for file operations like read, write, create, delete, and stream

## 🏗️ Architecture

```
┌─────────────┐    ┌─────────────────┐    ┌─────────────┐
│   Client    │◄──►│  Naming Server  │◄──►│   Storage   │
│             │    │                 │    │   Server    │
└─────────────┘    └─────────────────┘    └─────────────┘
                           │
                           ▼
                   ┌─────────────┐
                   │   Storage   │
                   │   Server    │
                   └─────────────┘
```

### Core Components

#### 1. Naming Server (`naming.c`)
- **Central Directory Service**: Maintains a global file system tree using Trie data structure
- **Load Balancing**: Distributes files across multiple storage servers
- **Failure Detection**: Monitors storage server health and handles failures
- **Backup Management**: Implements replication with configurable replication factor
- **Caching**: LRU cache for frequently accessed paths
- **Concurrent Access**: Handles multiple client requests simultaneously

#### 2. Storage Server (`storage.c`)
- **File Operations**: Create, read, write, delete files and directories
- **Concurrent Access Control**: Reader-writer locks for thread-safe operations
- **Asynchronous Writing**: Large file operations with chunked flushing
- **Audio Streaming**: Binary file streaming for media files
- **Dynamic Registration**: Can join/leave the network at runtime
- **Cross-platform IP Detection**: Works on both Linux and macOS

#### 3. Client (`client.c`)
- **File Operations**: Full CRUD operations on files and directories
- **Streaming Support**: Audio file streaming with real-time playback
- **Synchronous/Asynchronous Writes**: Configurable write modes
- **Interactive Interface**: Command-line interface for user operations
- **Error Handling**: Comprehensive error reporting and recovery

## 🚀 Features

### File Operations
- ✅ **CREATE_F**: Create new files
- ✅ **CREATE_DIC**: Create new directories
- ✅ **READ**: Read file contents
- ✅ **WRITE**: Write data to files (sync/async)
- ✅ **DELETE**: Delete files and directories recursively
- ✅ **INFO**: Get file metadata (size, permissions, timestamps)
- ✅ **LIST**: List all accessible files and directories
- ✅ **COPY**: Copy files between storage servers
- ✅ **STREAM**: Stream audio files in real-time

### Advanced Features
- 🔄 **Asynchronous Writing**: Large files written in chunks with immediate acknowledgment
- 🔒 **Concurrent Access Control**: Multiple readers, single writer per file
- 📦 **Data Replication**: Configurable replication factor for fault tolerance
- ⚡ **Caching**: LRU cache for improved performance
- 🎵 **Audio Streaming**: Real-time audio file streaming
- 🌐 **Cross-platform**: Works on Linux and macOS
- 📊 **Logging**: Comprehensive logging for debugging and monitoring

> **Note**: This implementation was developed and tested on macOS, with cross-platform compatibility for Linux systems.

## 🎮 Usage

### 1. Start the Naming Server
```bash
./naming <port>
# Example: ./naming 8090
```

### 2. Start Storage Servers
```bash
./storage <naming_server_ip> <naming_server_port> <storage_port> <folder_path>
# Example: ./storage 127.0.0.1 8090 9091 ./storage1
# Example: ./storage 127.0.0.1 8090 9092 ./storage2
```

### 3. Start Client
```bash
./client <naming_server_ip> <naming_server_port>
# Example: ./client 127.0.0.1 8090
```

## 📖 Client Commands

Once connected, you can use these commands:

### File Operations
```
READ <file_path>              # Read file content
WRITE <file_path>             # Write to file (with sync option)
CREATE_F <directory_path>     # Create a new file
CREATE_DIC <directory_path>   # Create a new directory
DELETE <file_path>            # Delete file or directory
INFO <file_path>              # Get file information
COPY <source> <destination>   # Copy files between servers
```

### Directory Operations
```
LIST <directory_path>         # List all files in directory
```

### Advanced Operations
```
STREAM <audio_file_path>      # Stream audio files
EXIT                          # Disconnect from server
```

### Example Session
```
Enter a command: READ
Enter file path: ./test/sample.txt

Enter a command: CREATE_F
Enter file path: ./test
Enter name of file (without ./): newfile.txt

Enter a command: WRITE
Enter file path: ./test/newfile.txt
Do you want to write synchronously? (yes/no): no

Enter a command: STREAM
Enter file path: ./audio/song.mp3
```

## ⚙️ Configuration

### Replication Factor
The system uses a default replication factor of 3. Modify in `naming.c`:
```c
#define REPLICATION_FACTOR 3
```

### Buffer Sizes
Adjust buffer sizes for performance:
```c
#define BUFFER_SIZE 40960      // 40KB buffer
#define ASYNC_THRESHOLD 10     // Async write threshold
#define CHUNK_SIZE 2           // Chunk size for flushing
```

### Cache Settings
Configure LRU cache size:
```c
#define CACHE_SIZE 5           // Number of cached entries
```

## 🧪 Testing

### Basic Functionality Test
```bash
# Terminal 1: Start naming server
./naming 8090

# Terminal 2: Start storage server
./storage 127.0.0.1 8090 9091 ./test_storage

# Terminal 3: Start client
./client 127.0.0.1 8090
```

### Multi-Storage Test
```bash
# Start multiple storage servers
./storage 127.0.0.1 8090 9091 ./storage1
./storage 127.0.0.1 8090 9092 ./storage2
./storage 127.0.0.1 8090 9093 ./storage3
```

### Concurrent Client Test
```bash
# Start multiple clients
./client 127.0.0.1 8090  # Terminal 1
./client 127.0.0.1 8090  # Terminal 2
./client 127.0.0.1 8090  # Terminal 3
```

##  Technical Details

### Data Structures
- **Trie**: Efficient path storage and lookup
- **LRU Cache**: Recent search optimization
- **Reader-Writer Locks**: Concurrent access control
- **Dynamic Arrays**: Flexible storage management

### Threading Model
- **Naming Server**: Multi-threaded request handling
- **Storage Server**: Thread-per-client model
- **Client**: Separate thread for server communication

### Network Protocol
- **TCP Sockets**: Reliable communication
- **Custom Protocol**: Simple text-based commands
- **Binary Streaming**: Efficient file transfer