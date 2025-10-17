//s3.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#define PORT 3303
#define BUF 4096

void mkdir_p(const char *path);
ssize_t recv_all(int sock, void *buf, size_t len);
void remove_extension(char *filename);

int main(){
    int s = socket(AF_INET, SOCK_STREAM, 0), c;
    struct sockaddr_in a, b;
    socklen_t blen;
    
    // Allow socket reuse
    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(s, (struct sockaddr*)&a, sizeof(a)) < 0){ 
        perror("S3 bind failed"); 
        return 1; 
    }
    
    listen(s, 5);
    printf("S3 (TXT server) running on port %d\n", PORT);

    // ensure base dir exists
    char base[PATH_MAX]; 
    snprintf(base, sizeof(base), "%s/S3", getenv("HOME"));
    mkdir_p(base);

    while(1){
        blen = sizeof(b);
        c = accept(s, (struct sockaddr*)&b, &blen);
        if(c < 0){ 
            perror("S3 accept failed"); 
            continue; 
        }

        char cmd[BUF], path[BUF], dir[BUF], buf[BUF];
        memset(cmd, 0, BUF);
        memset(path, 0, BUF);
        memset(dir, 0, BUF);
        
        if(recv_all(c, cmd, BUF) <= 0) {
            close(c);
            continue;
        }

        // ========= upload =========
        if(strncmp(cmd, "upload", 6) == 0) {
            // receive destination directory
            if(recv_all(c, dir, BUF) <= 0) {
                close(c);
                continue;
            }
            mkdir_p(dir);

            // receive filename
            if(recv_all(c, path, BUF) <= 0) {
                close(c);
                continue;
            }

            // receive file size
            int sz;
            if(recv_all(c, &sz, sizeof(int)) <= 0) { 
                close(c); 
                continue; 
            }

            if(sz <= 0) {
                close(c);
                continue;
            }

            // build destination full path
            char dest[PATH_MAX]; 
            snprintf(dest, sizeof(dest), "%s/%s", dir, path);

            int f = open(dest, O_CREAT | O_WRONLY | O_TRUNC, 0666);
            if(f < 0) {
                // Still need to receive the data to keep protocol in sync
                char tmp[BUF];
                int left = sz;
                while(left > 0) {
                    int n = recv(c, tmp, left > BUF ? BUF : left, 0);
                    if(n <= 0) break;
                    left -= n;
                }
                close(c);
                continue;
            }

            int left = sz;
            while(left > 0){
                int n = recv(c, buf, left > BUF ? BUF : left, 0);
                if(n <= 0) break;
                write(f, buf, n);
                left -= n;
            }
            close(f);
        }
        // ========= get =========
        else if(strncmp(cmd, "get", 3) == 0) {
            if(recv_all(c, path, BUF) <= 0) {
                close(c);
                continue;
            }
            
            if(strcmp(path, "TAR") == 0) {
                char cmdline[PATH_MAX*2];
                snprintf(cmdline, sizeof(cmdline),
                         "find %s/S3 -type f -name \"*.txt\" -print0 2>/dev/null | tar -cf /tmp/text.tar --null -T - 2>/dev/null",
                         getenv("HOME"));
                system(cmdline);
                
                int f = open("/tmp/text.tar", O_RDONLY);
                if(f < 0){ 
                    int z=0; 
                    send(c, &z, sizeof(int), 0); 
                    close(c); 
                    continue; 
                }
                int sz = lseek(f, 0, SEEK_END); 
                lseek(f, 0, SEEK_SET);
                send(c, &sz, sizeof(int), 0);
                
                int rd;
                while((rd = read(f, buf, BUF)) > 0) {
                    send(c, buf, rd, 0);
                }
                close(f); 
                remove("/tmp/text.tar");
            } else {
                int f = open(path, O_RDONLY);
                if(f < 0){ 
                    int z=0; 
                    send(c, &z, sizeof(int), 0); 
                    close(c); 
                    continue; 
                }
                int sz = lseek(f, 0, SEEK_END); 
                lseek(f, 0, SEEK_SET);
                send(c, &sz, sizeof(int), 0);
                
                int rd;
                while((rd = read(f, buf, BUF)) > 0) {
                    send(c, buf, rd, 0);
                }
                close(f);
            }
        }
        // ========= remove =========
        else if(strncmp(cmd, "remove", 6) == 0) {
            if(recv_all(c, path, BUF) <= 0) {
                close(c);
                continue;
            }
            
            remove(path);
        }
        // ========= list =========
        else if(strncmp(cmd, "list", 4) == 0) {
            if(recv_all(c, dir, BUF) <= 0) {
                close(c);
                continue;
            }
            
            const char *home = getenv("HOME");
            char s1_prefix[PATH_MAX];
            snprintf(s1_prefix, sizeof(s1_prefix), "%s/S1", home);

            char backend_dir[PATH_MAX];
            if(strncmp(dir, s1_prefix, strlen(s1_prefix)) == 0) {
                snprintf(backend_dir, sizeof(backend_dir), "%s/S3%s",
                         home, dir + strlen(s1_prefix));
            } else {
                snprintf(backend_dir, sizeof(backend_dir), "%s/S3", home);
            }

            // Collect .txt files (names only, no extensions)
            DIR *d = opendir(backend_dir);
            char *files[1024];
            int count = 0;
            char tmp[BUF] = "";
            
            if(d) {
                struct dirent *de;
                while((de = readdir(d)) != NULL) {
                    if(de->d_type == DT_REG && strstr(de->d_name, ".txt")) {
                        char *name_copy = strdup(de->d_name);
                        remove_extension(name_copy);
                        files[count++] = name_copy;
                        if(count >= 1024) break; // prevent overflow
                    }
                }
                closedir(d);
                
                // sort alphabetically
                for(int i = 0; i < count-1; i++) {
                    for(int j = i+1; j < count; j++) {
                        if(strcmp(files[i], files[j]) > 0) {
                            char *temp = files[i];
                            files[i] = files[j];
                            files[j] = temp;
                        }
                    }
                }
                      
                for(int i=0; i<count; i++){
                    strcat(tmp, files[i]);
                    strcat(tmp, "\n");
                    free(files[i]);
                }
            }
            
            send(c, tmp, strlen(tmp), 0);
        }
        
        close(c);
    }
    return 0;
}

/* Remove file extension from filename */
void remove_extension(char *filename) {
    char *dot = strrchr(filename, '.');
    if(dot) {
        *dot = '\0';
    }
}

/* Reliable recv for fixed-size data */
ssize_t recv_all(int sock, void *buf, size_t len){
    size_t recvd = 0;
    while(recvd < len){
        ssize_t r = recv(sock, (char*)buf + recvd, len - recvd, 0);
        if(r <= 0) return r;
        recvd += r;
    }
    return recvd;
}

/* mkdir -p equivalent */
void mkdir_p(const char *path){
    char tmp[PATH_MAX];
    strncpy(tmp, path, PATH_MAX);
    tmp[PATH_MAX-1] = 0;
    
    for(char *p = tmp + 1; *p; p++){
        if(*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}
