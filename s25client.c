//s25client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 7348
#define BUF 4096

/* Reliable recv for fixed-size data */
ssize_t recv_all(int sock, void *buf, size_t len) {
    size_t recvd = 0;
    while (recvd < len) {
        ssize_t r = recv(sock, (char*)buf + recvd, len - recvd, 0);
        if (r <= 0) return r;
        recvd += r;
    }
    return recvd;
}

/* Check if file has valid extension */
int is_valid_extension(const char *filename) {
    char *ext = strrchr(filename, '.');
    return ext && (strcmp(ext, ".c") == 0 || strcmp(ext, ".pdf") == 0 || 
                   strcmp(ext, ".txt") == 0 || strcmp(ext, ".zip") == 0);
}

int main(){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    struct sockaddr_in a;
    a.sin_family = AF_INET;
    a.sin_port = htons(SERVER_PORT);
    a.sin_addr.s_addr = inet_addr(SERVER_IP);
    if (connect(s, (struct sockaddr*)&a, sizeof(a)) < 0) {
        perror("connect");
        return 1;
    }

    char line[BUF], file[PATH_MAX], dir[PATH_MAX];
    
    while (1) {
        printf("cmd> "); fflush(stdout);
        if (!fgets(line, BUF, stdin)) break;
        line[strcspn(line, "\n")] = 0;

        /* ===== UPLOADF ===== */
        if (strncmp(line, "uploadf", 7) == 0) {
            send(s, "uploadf", strlen("uploadf") + 1, 0);

            int n;
            printf("How many files? (1-3): ");
            if (scanf("%d", &n) != 1) break;
            getchar();
            if (n < 1 || n > 3) {
                printf("Invalid count for uploadf.\n");
                continue;
            }
            send(s, &n, sizeof(int), 0);

            printf("Dest dir (example: ~/S1/folder1): ");
            fgets(dir, BUF, stdin);
            dir[strcspn(dir, "\n")] = 0;
            send(s, dir, BUF, 0);

            for (int i = 0; i < n; i++) {
                printf("File %d: ", i+1);
                fflush(stdout);
                fgets(file, PATH_MAX, stdin);
                file[strcspn(file, "\n")] = 0;

                if (!is_valid_extension(file)) {
                    printf("Invalid file extension. Only .c, .pdf, .txt, .zip allowed.\n");
                    int zero = 0;
                    char empty[BUF] = "";
                    send(s, empty, BUF, 0);
                    send(s, &zero, sizeof(int), 0);
                    continue;
                }

                /* Send only basename, not full path */
                char *bn = strrchr(file, '/') ? strrchr(file, '/') + 1 : file;
                send(s, bn, BUF, 0);

                int f = open(file, O_RDONLY);
                if (f < 0) {
                    perror("open");
                    int zero = 0;
                    send(s, &zero, sizeof(int), 0);
                    continue;
                }
                int sz = lseek(f, 0, SEEK_END);
                lseek(f, 0, SEEK_SET);
                send(s, &sz, sizeof(int), 0);

                char b[BUF]; int rd;
                while ((rd = read(f, b, BUF)) > 0) send(s, b, rd, 0);
                close(f);
            }

        /* ===== DOWNLF ===== */
        } else if (strncmp(line, "downlf", 6) == 0) {
            send(s, "downlf", strlen("downlf") + 1, 0);

            int n;
            printf("How many? (1-2): ");
            if (scanf("%d", &n) != 1) break;
            getchar();
            if (n < 1 || n > 2) {
                printf("Invalid count for downlf.\n");
                continue;
            }
            send(s, &n, sizeof(int), 0);

            for (int i = 0; i < n; i++) {
                printf("Remote file path: ");
                fgets(file, BUF, stdin);
                file[strcspn(file, "\n")] = 0;
                send(s, file, BUF, 0);

                int sz;
                if (recv_all(s, &sz, sizeof(int)) <= 0) {
                    printf("No response or error\n");
                    continue;
                }
                if (sz <= 0) {
                    printf("File not found: %s\n", file);
                    continue;
                }

                char *bn = strrchr(file, '/') ? strrchr(file, '/') + 1 : file;
                int f = open(bn, O_CREAT|O_WRONLY, 0666);
                if (f < 0) {
                    perror("open write");
                    char tmp[BUF]; int left = sz;
                    while (left > 0) { int rd = recv(s, tmp, left>BUF?BUF:left, 0); if (rd<=0) break; left -= rd; }
                    continue;
                }

                int left = sz; char b[BUF];
                while (left > 0) {
                    int rd = recv(s, b, left>BUF?BUF:left, 0);
                    if (rd <= 0) break;
                    write(f, b, rd);
                    left -= rd;
                }
                close(f);
                printf("Downloaded: %s (%d bytes)\n", bn, sz);
            }

        /* ===== REMOVEF ===== */
        } else if (strncmp(line, "removef", 7) == 0) {
            send(s, "removef", strlen("removef") + 1, 0);
            int n;
            printf("How many? (1-2): ");
            if (scanf("%d", &n) != 1) break;
            getchar();
            if (n < 1 || n > 2) {
                printf("Invalid count for removef.\n");
                continue;
            }
            send(s, &n, sizeof(int), 0);
            for (int i = 0; i < n; i++) {
                printf("Path: ");
                fgets(file, BUF, stdin);
                file[strcspn(file, "\n")] = 0;
                send(s, file, BUF, 0);
            }

        /* ===== DOWNLTAR ===== */
        } else if (strncmp(line, "downltar", 8) == 0) {
            send(s, "downltar", strlen("downltar") + 1, 0);
            printf("Type (.c/.pdf/.txt): ");
            fgets(file, BUF, stdin);
            file[strcspn(file, "\n")] = 0;
            send(s, file, BUF, 0);

            int sz;
            if (recv_all(s, &sz, sizeof(int)) <= 0) {
                printf("No response\n");
                continue;
            }
            if (sz <= 0) {
                printf("No files of that type found\n");
                continue;
            }

            // Determine tar filename based on file type
            char tar_filename[256];
            if (strcmp(file, ".c") == 0) {
                strcpy(tar_filename, "cfiles.tar");
            } else if (strcmp(file, ".pdf") == 0) {
                strcpy(tar_filename, "pdf.tar");
            } else if (strcmp(file, ".txt") == 0) {
                strcpy(tar_filename, "text.tar");
            } else {
                strcpy(tar_filename, "out.tar"); // fallback
            }

            int f = open(tar_filename, O_CREAT|O_WRONLY|O_TRUNC, 0666);
            if (f < 0) {
                perror("open tar file");
                char tmp[BUF]; int left = sz;
                while (left > 0) { int rd = recv(s,tmp,left>BUF?BUF:left,0); if(rd<=0) break; left -= rd; }
                continue;
            }
            int left = sz; char b[BUF];
            while (left > 0) {
                int rd = recv(s, b, left>BUF?BUF:left, 0);
                if (rd <= 0) break;
                write(f, b, rd);
                left -= rd;
            }
            close(f);
            printf("Received %s (%d bytes)\n", tar_filename, sz);

        /* ===== DISP FNAMES ===== */
        } else if (strncmp(line, "dispfnames", 10) == 0) {
            send(s, "dispfnames", strlen("dispfnames") + 1, 0);
            printf("Dir (example: ~/S1/folder1): ");
            fgets(dir, BUF, stdin);
            dir[strcspn(dir, "\n")] = 0;
            send(s, dir, BUF, 0);

            char out[BUF+1];
            int r;
            while ((r = recv(s, out, BUF, 0)) > 0) {
                out[r] = '\0';
                printf("%s", out);
                if (r < BUF) break;
            }
        
        } else {
            printf("Unknown command. Supported: uploadf downlf removef downltar dispfnames\n");
        }
    }

    close(s);
    return 0;
}
