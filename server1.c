//s1.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <limits.h>
#include <libgen.h>
#include <errno.h>

#define PORT 7348
#define BUF 4096

// Function prototypes
void prcclient(int client_sock);
void send_to_backend(const char *src_path, const char *dest_dir, int port);
void get_from_backend(int port, const char *path, int client);
void remove_on_backend(int port, const char *path);
void list_from_backend(int port, const char *dir, char *result);
void mkdir_p(const char *path);
void normalize_s1_path(const char *in, char *out, size_t outlen);
void map_dir_for_backend(const char *s1_dir, const char *backend_base, char *out, size_t outlen);
ssize_t recv_all(int sock, void *buf, size_t len);
void remove_extension(char *filename);

int main() {
    int sockfd, newsock;
    struct sockaddr_in serv, cli;
    socklen_t clen;
    pid_t pid;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){ 
        perror("socket"); 
        return 1; 
    }
    
    // Allow socket reuse to avoid "Address already in use" error
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    serv.sin_family = AF_INET;
    serv.sin_port = htons(PORT);
    serv.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0){ 
        perror("bind"); 
        return 1; 
    }
    
    listen(sockfd, 10);
    printf("S1 (Main server) running on port %d\n", PORT);

    // Ensure base folder exists
    char home[PATH_MAX];
    snprintf(home, sizeof(home), "%s/S1", getenv("HOME"));
    mkdir_p(home);

    while(1) {
        clen = sizeof(cli);
        newsock = accept(sockfd, (struct sockaddr*)&cli, &clen);
        if(newsock < 0){ 
            perror("accept"); 
            continue; 
        }
        
        // Fork a child process to handle the client
        pid = fork();
        if(pid == 0) {
            // Child process
            close(sockfd); // Close listening socket in child
            prcclient(newsock);
            close(newsock);
            exit(0);
        } else if(pid > 0) {
            // Parent process
            close(newsock); // Close client socket in parent
            // Clean up zombie processes
            while(waitpid(-1, NULL, WNOHANG) > 0);
        } else {
            perror("fork");
            close(newsock);
        }
    }
    return 0;
}

/* Recv helper to ensure we read full len bytes */
ssize_t recv_all(int sock, void *buf, size_t len){
    size_t recvd = 0;
    while(recvd < len){
        ssize_t r = recv(sock, (char*)buf + recvd, len - recvd, 0);
        if(r <= 0) return r;
        recvd += r;
    }
    return recvd;
}

/* mkdir -p implementation */
void mkdir_p(const char *path){
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp));
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

/* Normalize path so it's always under ~/S1 */
void normalize_s1_path(const char *in, char *out, size_t outlen){
    const char *home_env = getenv("HOME");
    if(!home_env) home_env = "/tmp";

    if(strncmp(in, "~S1", 3) == 0){
        snprintf(out, outlen, "%s/S1%s", home_env, in+3);
    } else if(strncmp(in, "~/S1", 4) == 0){
        snprintf(out, outlen, "%s%s", home_env, in+1);
    } else if(in[0] == '/'){
        strncpy(out, in, outlen);
        out[outlen-1] = 0;
    } else {
        snprintf(out, outlen, "%s/S1/%s", home_env, in);
    }
}

/* Map ~/S1 path to ~/S2 or ~/S3 etc. */
void map_dir_for_backend(const char *s1_dir, const char *backend_base, char *out, size_t outlen){
    const char *home_env = getenv("HOME");
    if(!home_env) home_env = "/tmp";

    char prefix[PATH_MAX];
    snprintf(prefix, sizeof(prefix), "%s/S1", home_env);
    if(strncmp(s1_dir, prefix, strlen(prefix)) == 0){
        snprintf(out, outlen, "%s%s", backend_base, s1_dir + strlen(prefix));
    } else {
        snprintf(out, outlen, "%s/%s", backend_base,
                 strrchr(s1_dir,'/') ? strrchr(s1_dir,'/')+1 : "");
    }
}

/* Remove file extension from filename */
void remove_extension(char *filename) {
    char *dot = strrchr(filename, '.');
    if(dot) {
        *dot = '\0';
    }
}

/* Client handler function as specified in requirements */
void prcclient(int client) {
    char cmd[BUF], fname[BUF], dir[BUF], filetype[BUF];

    // Enter infinite loop waiting for client commands
    while(1) {
        memset(cmd, 0, BUF);
        int r = recv(client, cmd, BUF, 0);
        if(r <= 0) {
            break;
        }

        // ======== uploadf ========
        if(strncmp(cmd, "uploadf", 7) == 0) {
            int count;
            recv_all(client, &count, sizeof(int));
            recv_all(client, dir, BUF);

            char norm_dir[PATH_MAX];
            normalize_s1_path(dir, norm_dir, sizeof(norm_dir));
            mkdir_p(norm_dir);

            for(int i=0;i<count;i++){
                recv_all(client, fname, BUF);

                char path[PATH_MAX];
                snprintf(path, sizeof(path), "%s/%s", norm_dir, fname);

                char *tmpdup = strdup(path);
                mkdir_p(dirname(tmpdup));
                free(tmpdup);

                int size;
                recv_all(client, &size, sizeof(int));
                
                if(size <= 0) {
                    continue;
                }

                int f = open(path, O_CREAT|O_WRONLY|O_TRUNC, 0666);
                if(f < 0) {
                    // Still need to receive the data to keep protocol in sync
                    char tmp[BUF];
                    int left = size;
                    while(left > 0) {
                        int chunk = recv(client, tmp, left>BUF?BUF:left, 0);
                        if(chunk <= 0) break;
                        left -= chunk;
                    }
                    continue;
                }

                int left = size; 
                char buf[BUF];
                while(left > 0){
                    int chunk = recv(client, buf, left>BUF?BUF:left, 0);
                    if(chunk <= 0) break;
                    write(f, buf, chunk);
                    left -= chunk;
                }
                close(f);

                // Forward non-.c files to backend servers
                char *dot = strrchr(fname, '.');
                if(dot && strcmp(dot, ".c") != 0){
                    char backend_base[PATH_MAX];
                    int port = 0;
                    if(strcmp(dot, ".pdf")==0){ 
                        snprintf(backend_base, sizeof(backend_base), "%s/S2", getenv("HOME"));
                        port = 2202;
                    } 
                    else if(strcmp(dot, ".txt")==0){ 
                        snprintf(backend_base, sizeof(backend_base), "%s/S3", getenv("HOME"));
                        port = 3303;
                    }
                    else if(strcmp(dot, ".zip")==0){ 
                        snprintf(backend_base, sizeof(backend_base), "%s/S4", getenv("HOME"));
                        port = 4404;
                    }
                    
                    if(port){
                        char backend_dir[PATH_MAX];
                        map_dir_for_backend(norm_dir, backend_base, backend_dir, sizeof(backend_dir));
                        send_to_backend(path, backend_dir, port);
                        remove(path);
                    }
                }
            }
        }
        // ======== downlf ========
        else if(strncmp(cmd, "downlf", 6) == 0) {
            int count;
            recv_all(client, &count, sizeof(int));
            
            for(int i=0;i<count;i++){
                recv_all(client, fname, BUF);
                
                char *dot = strrchr(fname, '.');
                if(dot && strcmp(dot, ".c")==0){
                    char norm[PATH_MAX];
                    normalize_s1_path(fname, norm, sizeof(norm));
                    int f = open(norm, O_RDONLY);
                    if(f<0){ 
                        int z=0; send(client,&z,sizeof(int),0); 
                        continue; 
                    }
                    int size = lseek(f,0,SEEK_END);
                    lseek(f,0,SEEK_SET);
                    send(client,&size,sizeof(int),0);
                    char b[BUF]; int rd;
                    while((rd=read(f,b,BUF))>0) send(client,b,rd,0);
                    close(f);
                } else if(dot && strcmp(dot, ".pdf")==0){
                    get_from_backend(2202, fname, client);
                } else if(dot && strcmp(dot, ".txt")==0){
                    get_from_backend(3303, fname, client);
                } else if(dot && strcmp(dot, ".zip")==0){
                    get_from_backend(4404, fname, client);
                } else {
                    int z=0; send(client,&z,sizeof(int),0);
                }
            }
        }
        // ======== removef ========
        else if(strncmp(cmd, "removef", 7)==0) {
            int count;
            recv_all(client, &count, sizeof(int));
            for(int i=0;i<count;i++){
                recv_all(client, fname, BUF);
                char *dot = strrchr(fname, '.');
                if(dot && strcmp(dot, ".c")==0){
                    char norm[PATH_MAX];
                    normalize_s1_path(fname, norm, sizeof(norm));
                    remove(norm);
                } else if(dot && strcmp(dot, ".pdf")==0){
                    remove_on_backend(2202, fname);
                } else if(dot && strcmp(dot, ".txt")==0){
                    remove_on_backend(3303, fname);
                } else if(dot && strcmp(dot, ".zip")==0){
                    remove_on_backend(4404, fname);
                }
            }
        }
        // ======== downltar ========
        else if(strncmp(cmd, "downltar", 8)==0) {
            recv_all(client, filetype, BUF);
            
            if(strcmp(filetype, ".c")==0){
                char cmdline[PATH_MAX*2];
                snprintf(cmdline, sizeof(cmdline), "find %s/S1 -type f -name \"*.c\" -print0 | tar -cf /tmp/c.tar --null -T -", getenv("HOME"));
                system(cmdline);
                int f = open("/tmp/c.tar",O_RDONLY);
                if(f < 0) {
                    int z=0; send(client,&z,sizeof(int),0);
                    continue;
                }
                int size=lseek(f,0,SEEK_END);
                lseek(f,0,SEEK_SET);
                send(client,&size,sizeof(int),0);
                char b[BUF];int rd;
                while((rd=read(f,b,BUF))>0) send(client,b,rd,0);
                close(f); remove("/tmp/c.tar");
            } else if(strcmp(filetype, ".pdf")==0){
                get_from_backend(2202, "TAR", client);
            } else if(strcmp(filetype, ".txt")==0){
                get_from_backend(3303, "TAR", client);
            } else {
                int z=0; send(client,&z,sizeof(int),0);
            }
        }
        // ======== dispfnames ========
        else if(strncmp(cmd, "dispfnames", 10)==0) {
            recv_all(client, dir, BUF);
            char norm_dir[PATH_MAX];
            normalize_s1_path(dir, norm_dir, sizeof(norm_dir));

            char result[16384] = "";

            // Collect local .c files (names only, no extensions)
            char *local_files[1024];
            int count = 0;
            DIR *d = opendir(norm_dir);
            if(d){
                struct dirent *de;
                while((de=readdir(d))!=NULL){
                    if(de->d_type == DT_REG && strstr(de->d_name,".c")){
                        char *name_copy = strdup(de->d_name);
                        remove_extension(name_copy);
                        local_files[count++] = name_copy;
                    }
                }
                closedir(d);
                // Sort alphabetically
                for(int i = 0; i < count-1; i++) {
                    for(int j = i+1; j < count; j++) {
                        if(strcmp(local_files[i], local_files[j]) > 0) {
                            char *temp = local_files[i];
                            local_files[i] = local_files[j];
                            local_files[j] = temp;
                        }
                    }
                }
                for(int i=0;i<count;i++){ 
                    strcat(result, local_files[i]); 
                    strcat(result,"\n"); 
                    free(local_files[i]); 
                }
            }
            
            // Get files from backend servers (they will also return names without extensions)
            list_from_backend(2202, norm_dir, result);
            list_from_backend(3303, norm_dir, result);
            list_from_backend(4404, norm_dir, result);
            
            send(client, result, strlen(result), 0);
        }
    }
}

/* Send file to backend */
void send_to_backend(const char *src_path, const char *dest_dir, int port){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; 
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(connect(s, (struct sockaddr*)&a, sizeof(a)) < 0){
        close(s);
        return;
    }

    // Send command with proper buffer size
    char cmd[BUF];
    memset(cmd, 0, BUF);
    strcpy(cmd, "upload");
    send(s, cmd, BUF, 0);
    
    // Send destination directory
    char dest_buf[BUF];
    memset(dest_buf, 0, BUF);
    strncpy(dest_buf, dest_dir, BUF-1);
    send(s, dest_buf, BUF, 0);

    // Send filename
    char *filename = strrchr(src_path, '/');
    filename = (filename) ? filename + 1 : (char*)src_path;
    
    char fname_buf[BUF];
    memset(fname_buf, 0, BUF);
    strncpy(fname_buf, filename, BUF-1);
    send(s, fname_buf, BUF, 0);

    // Send file content
    int f = open(src_path, O_RDONLY);
    if(f < 0){
        close(s);
        return;
    }
    
    int filesize = lseek(f, 0, SEEK_END);
    lseek(f, 0, SEEK_SET);
    send(s, &filesize, sizeof(filesize), 0);

    char buf[BUF];
    int rd;
    while((rd = read(f, buf, BUF)) > 0){
        send(s, buf, rd, 0);
    }

    close(f);
    close(s);
}

void get_from_backend(int port, const char *path, int client){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; 
    a.sin_family = AF_INET; 
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if(connect(s, (struct sockaddr*)&a, sizeof(a)) < 0){ 
        int z = 0; 
        send(client, &z, sizeof(int), 0); 
        close(s); 
        return; 
    }

    // Send command
    char cmd[BUF];
    memset(cmd, 0, BUF);
    strcpy(cmd, "get");
    send(s, cmd, BUF, 0);
    
    // Convert S1 path to backend path
    char backend_path[BUF];
    memset(backend_path, 0, BUF);
    
    if(strcmp(path, "TAR") == 0) {
        strcpy(backend_path, "TAR");
    } else {
        // Convert ~/S1/... to ~/S2/... (or S3/S4)
        char norm_path[PATH_MAX];
        normalize_s1_path(path, norm_path, sizeof(norm_path));
        
        const char *home = getenv("HOME");
        char s1_prefix[PATH_MAX];
        snprintf(s1_prefix, sizeof(s1_prefix), "%s/S1", home);
        
        if(strncmp(norm_path, s1_prefix, strlen(s1_prefix)) == 0) {
            char backend_base[10];
            if(port == 2202) strcpy(backend_base, "S2");
            else if(port == 3303) strcpy(backend_base, "S3");
            else if(port == 4404) strcpy(backend_base, "S4");
            else strcpy(backend_base, "S2");
            
            snprintf(backend_path, sizeof(backend_path), "%s/%s%s", 
                     home, backend_base, norm_path + strlen(s1_prefix));
        } else {
            strncpy(backend_path, norm_path, sizeof(backend_path)-1);
        }
    }
    
    send(s, backend_path, BUF, 0);
    
    int sz;
    if(recv_all(s, &sz, sizeof(int)) <= 0){ 
        int z = 0; 
        send(client, &z, sizeof(int), 0); 
        close(s); 
        return; 
    }
    
    send(client, &sz, sizeof(int), 0);
    
    char b[BUF]; 
    int rd, total = 0;
    while(total < sz){
        rd = recv(s, b, BUF, 0);
        if(rd <= 0) break;
        send(client, b, rd, 0);
        total += rd;
    }
    close(s);
}

void remove_on_backend(int port, const char *path){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; 
    a.sin_family = AF_INET; 
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if(connect(s, (struct sockaddr*)&a, sizeof(a)) < 0){ 
        close(s); 
        return; 
    }
    
    // Send command
    char cmd[BUF];
    memset(cmd, 0, BUF);
    strcpy(cmd, "remove");
    send(s, cmd, BUF, 0);
    
    // Convert S1 path to backend path
    char backend_path[BUF];
    memset(backend_path, 0, BUF);
    char norm_path[PATH_MAX];
    normalize_s1_path(path, norm_path, sizeof(norm_path));
    
    const char *home = getenv("HOME");
    char s1_prefix[PATH_MAX];
    snprintf(s1_prefix, sizeof(s1_prefix), "%s/S1", home);
    
    if(strncmp(norm_path, s1_prefix, strlen(s1_prefix)) == 0) {
        char backend_base[10];
        if(port == 2202) strcpy(backend_base, "S2");
        else if(port == 3303) strcpy(backend_base, "S3");
        else if(port == 4404) strcpy(backend_base, "S4");
        else strcpy(backend_base, "S2");
        
        snprintf(backend_path, sizeof(backend_path), "%s/%s%s", 
                 home, backend_base, norm_path + strlen(s1_prefix));
    } else {
        strncpy(backend_path, norm_path, sizeof(backend_path)-1);
    }
    
    send(s, backend_path, BUF, 0);
    close(s);
}

void list_from_backend(int port, const char *dir, char *result){
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; 
    a.sin_family = AF_INET; 
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if(connect(s, (struct sockaddr*)&a, sizeof(a)) < 0){ 
        close(s); 
        return; 
    }
    
    char cmd[BUF];
    memset(cmd, 0, BUF);
    strcpy(cmd, "list");
    send(s, cmd, BUF, 0);
    
    char dir_buf[BUF];
    memset(dir_buf, 0, BUF);
    strncpy(dir_buf, dir, BUF-1);
    send(s, dir_buf, BUF, 0);
    
    char b[BUF]; 
    int r = recv(s, b, BUF-1, 0);
    if(r > 0){ 
        b[r] = 0; 
        strcat(result, b); 
    }
    close(s);
}
