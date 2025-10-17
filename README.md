# Distributed File System (Multi-Server Architecture)

This project implements a **multi-server distributed file system** using **C socket programming**.  
The client interacts with multiple servers for file upload, download, removal, and type-based archiving.

---

## 🧠 Overview

- **Client:** `s25client.c`
- **Servers:** `s25s1.c`, `s25s2.c`, `s25s3.c`, `s25s4.c`
- **Communication:** TCP sockets  
- **Core Idea:** The main server (S1) receives client requests and distributes files based on their type:
  - `.c` files → S1  
  - `.pdf` files → S2  
  - `.txt` files → S3  
  - `.zip` files → S4  

---

## ⚙️ Features

### ✅ `uploadf`
Upload 1–3 files to a specific directory. Supports `.c`, `.pdf`, `.txt`, `.zip`.

### ✅ `downlf`
Download 1–2 files from the server to the client machine.

### ✅ `removef`
Remove 1–2 files from server directories.

### ✅ `downltar`
Download a tar archive of all files of a specific type (`.c`, `.pdf`, `.txt`).

### ✅ `dispfnames`
Display filenames in a given directory.

---

## 🧩 Technical Highlights

- Socket-based client-server communication using `AF_INET`, `SOCK_STREAM`
- File handling with `open()`, `read()`, `write()`, and `lseek()`
- Reliable data transfer using custom `recv_all()` function
- Extension validation to ensure correct file routing
- Modular multi-server design for distributed storage

---

## 🧠 How to Run

1. **Compile each file**:
   ```bash
   gcc s25client.c -o s25client
   gcc s25s1.c -o s25s1
   gcc s25s2.c -o s25s2
   gcc s25s3.c -o s25s3
   gcc s25s4.c -o s25s4
