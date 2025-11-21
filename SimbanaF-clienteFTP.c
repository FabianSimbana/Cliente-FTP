#define _GNU_SOURCE
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>   
#include <unistd.h>       
#include <netdb.h>         


int connectTCP(const char *host, const char *service);
int errexit(const char *format, ...);
int open_port_data_connection(int ctrl_sock);

#define BUFSIZE 4096
#define REPLYLEN 2048
#define PARTS 5

/* ---------- Utilidades de protocolo ---------- */

/* send a command like "USER foo" or "PWD" (arg can be NULL).
 * response is filled (null-terminated) and integer return code is parsed (e.g., 220, 230, 226)
 */
int send_command_and_get_reply(int ctrl_sock, const char *cmd, const char *arg, char *response, size_t rsz) {
    char buf[1024];
    int n;
    if (arg)
        snprintf(buf, sizeof(buf), "%s %s\r\n", cmd, arg);
    else
        snprintf(buf, sizeof(buf), "%s\r\n", cmd);
    if (write(ctrl_sock, buf, strlen(buf)) < 0) {
        perror("write control");
        return -1;
    }
    /* read reply (simple read - assumes single-line reply fits buffer) */
    n = read(ctrl_sock, response, rsz - 1);
    if (n <= 0) {
        response[0] = '\0';
        return -1;
    }
    response[n] = '\0';
    /* print server reply */
    printf("<-- %s", response);
    /* parse code */
    if (n >= 3 && isdigit((unsigned char)response[0]) && isdigit((unsigned char)response[1]) && isdigit((unsigned char)response[2])) {
        int code = (response[0]-'0')*100 + (response[1]-'0')*10 + (response[2]-'0');
        return code;
    }
    return 0;
}

/* parse PASV response "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)." */
int parse_pasv(const char *response, char *ip_out, int *port_out) {
    const char *p = strchr(response, '(');
    if (!p) return -1;
    int h1,h2,h3,h4,p1,p2;
    if (sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2) != 6) return -1;
    snprintf(ip_out, 64, "%d.%d.%d.%d", h1,h2,h3,h4);
    *port_out = p1*256 + p2;
    return 0;
}

/* obtain size of remote file using "SIZE filename" (expect 213 <size>) */
long get_remote_size(int ctrl_sock, const char *filename) {
    char resp[REPLYLEN];
    int code = send_command_and_get_reply(ctrl_sock, "SIZE", filename, resp, sizeof(resp));
    if (code == 213) {
        long sz = atol(resp + 4);
        return sz;
    }
    return -1;
}

/* ---------- Data connection helper (PASV) ---------- */
/* send PASV and connect to data socket using connectTCP (which needs host and port as string) */
int open_pasv_data_connection(int ctrl_sock) {
    char resp[REPLYLEN];
    int code = send_command_and_get_reply(ctrl_sock, "PASV", NULL, resp, sizeof(resp));
    if (code < 0) return -1;
    char ip[64];
    int port;
    if (parse_pasv(resp, ip, &port) < 0) return -1;
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    int data_sock = connectTCP(ip, portstr);
    return data_sock;
}

/* ---------- Basic operations ---------- */

void do_pwd(int ctrl_sock) {
    char resp[REPLYLEN];
    send_command_and_get_reply(ctrl_sock, "PWD", NULL, resp, sizeof(resp));
}

void do_cwd(int ctrl_sock, const char *dir) {
    char resp[REPLYLEN];
    send_command_and_get_reply(ctrl_sock, "CWD", dir, resp, sizeof(resp));
}

void do_list(int ctrl_sock) {
    char resp[REPLYLEN];
    int data_sock = open_pasv_data_connection(ctrl_sock);
    if (data_sock < 0) { printf("Failed to open data connection (PASV)\n"); return; }
    /* send LIST */
    send_command_and_get_reply(ctrl_sock, "LIST", NULL, resp, sizeof(resp));
    /* read data */
    char buf[BUFSIZE];
    int n;
    while ((n = read(data_sock, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    close(data_sock);
    /* final reply */
    read(ctrl_sock, resp, sizeof(resp)-1);
    resp[sizeof(resp)-1] = '\0';
    printf("<-- %s", resp);
}

/* Normal download (single connection) */
void do_get(int ctrl_sock, const char *filename) {
    char resp[REPLYLEN];
    int data_sock = open_pasv_data_connection(ctrl_sock);
    if (data_sock < 0) { printf("Failed to open PASV data socket\n"); return; }
    /* send RETR */
    int code = send_command_and_get_reply(ctrl_sock, "RETR", filename, resp, sizeof(resp));
    if (code >= 400) { close(data_sock); return; }
    FILE *fp = fopen(filename, "wb");
    if (!fp) { perror("fopen"); close(data_sock); return; }
    char buf[BUFSIZE];
    int n;
    while ((n = read(data_sock, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, fp);
    }
    fclose(fp);
    close(data_sock);
    /* read final reply */
    if (read(ctrl_sock, resp, sizeof(resp)-1) > 0) {
        resp[sizeof(resp)-1] = '\0';
        printf("<-- %s", resp);
    }
}

void do_portget(int ctrl_sock, const char *filename) {
    char resp[REPLYLEN];

    int listen_sock = open_port_data_connection(ctrl_sock);
    if (listen_sock < 0) {
        printf("Error abriendo PORT\n");
        return;
    }

    int code = send_command_and_get_reply(ctrl_sock, "RETR", filename, resp, sizeof(resp));
    if (code >= 400) {
        close(listen_sock);
        return;
    }

    struct sockaddr_in client;
    socklen_t clen = sizeof(client);
    int data_sock = accept(listen_sock, (struct sockaddr*)&client, &clen);
    if (data_sock < 0) { perror("accept"); close(listen_sock); return; }

    FILE *fp = fopen(filename, "wb");
    if (!fp) { perror("fopen"); close(data_sock); close(listen_sock); return; }

    char buf[BUFSIZE];
    int n;
    while ((n = read(data_sock, buf, sizeof(buf))) > 0)
        fwrite(buf, 1, n, fp);

    fclose(fp);
    close(data_sock);
    close(listen_sock);

    if (read(ctrl_sock, resp, sizeof(resp)-1) > 0)
        printf("<-- %s", resp);
}


/* Normal upload (single connection) */
void do_put(int ctrl_sock, const char *filename) {
    char resp[REPLYLEN];
    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror("fopen"); return; }
    int data_sock = open_pasv_data_connection(ctrl_sock);
    if (data_sock < 0) { printf("Failed to open PASV data socket\n"); fclose(fp); return; }
    send_command_and_get_reply(ctrl_sock, "STOR", filename, resp, sizeof(resp));
    char buf[BUFSIZE];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), fp)) > 0) {
        ssize_t w = write(data_sock, buf, r);
        if (w < 0) { perror("write data"); break; }
    }
    fclose(fp);
    close(data_sock);
    /* final reply */
    if (read(ctrl_sock, resp, sizeof(resp)-1) > 0) {
        resp[sizeof(resp)-1] = '\0';
        printf("<-- %s", resp);
    }
}

void do_portput(int ctrl_sock, const char *filename) {
    char resp[REPLYLEN];
    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror("fopen"); return; }

    int listen_sock = open_port_data_connection(ctrl_sock);
    if (listen_sock < 0) {
        printf("Error abriendo PORT\n");
        fclose(fp);
        return;
    }

    send_command_and_get_reply(ctrl_sock, "STOR", filename, resp, sizeof(resp));

    struct sockaddr_in client;
    socklen_t clen = sizeof(client);
    int data_sock = accept(listen_sock, (struct sockaddr*)&client, &clen);
    if (data_sock < 0) { perror("accept"); close(listen_sock); fclose(fp); return; }

    char buf[BUFSIZE];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), fp)) > 0)
        write(data_sock, buf, r);

    fclose(fp);
    close(data_sock);
    close(listen_sock);

    if (read(ctrl_sock, resp, sizeof(resp)-1) > 0)
        printf("<-- %s", resp);
}

/* ---------- Concurrent download implementation (RETR in 5 parts) ---------- */

/* helper: child process downloads a segment [start, start+len-1] into temporary file part_<i> */
int child_download_part(const char *host, const char *user, const char *pass,
                        const char *remote_file, long start, long len, int part_index)
{
    char svc[] = "21";
    int ctrl = connectTCP(host, svc);
    if (ctrl < 0) return -1;

    char resp[REPLYLEN];

    /* read banner */
    if (read(ctrl, resp, sizeof(resp)-1) <= 0) { close(ctrl); return -1; }
    resp[sizeof(resp)-1] = '\0';

    /* login */
    send_command_and_get_reply(ctrl, "USER", user, resp, sizeof(resp));
    send_command_and_get_reply(ctrl, "PASS", pass, resp, sizeof(resp));

    /* set binary */
    send_command_and_get_reply(ctrl, "TYPE", "I", resp, sizeof(resp));

    /* REST offset */
    char restarg[64];
    snprintf(restarg, sizeof(restarg), "%ld", start);
    send_command_and_get_reply(ctrl, "REST", restarg, resp, sizeof(resp));

    /* PASV and connect data */
    int data_sock = open_pasv_data_connection(ctrl);
    if (data_sock < 0) { close(ctrl); return -1; }

    /* RETR after REST offset */
    int code = send_command_and_get_reply(ctrl, "RETR", remote_file, resp, sizeof(resp));
    if (code >= 400) { 
        close(data_sock); 
        close(ctrl); 
        return -1; 
    }

    /* open the partial file */
    char partname[128];
    snprintf(partname, sizeof(partname), "%s.part%d", remote_file, part_index);
    FILE *fp = fopen(partname, "wb");
    if (!fp) { perror("fopen"); close(data_sock); close(ctrl); return -1; }

    /* read ONLY this segment */
    long remaining = len;
    char buf[BUFSIZE];

    while (remaining > 0) {
        int chunk = (remaining > BUFSIZE) ? BUFSIZE : remaining;
        int n = read(data_sock, buf, chunk);
        if (n <= 0) break;
        fwrite(buf, 1, n, fp);
        remaining -= n;
    }

    fclose(fp);
    close(data_sock);


    /* read final reply */
    if (read(ctrl, resp, sizeof(resp)-1) > 0) {
        resp[sizeof(resp)-1] = '\0';
        /* print child completion message */
        printf("Child part %d finished: %s", part_index, resp);
    }
    close(ctrl);
    return 0;
}

/* Parent orchestrates: checks SIZE, divides file, forks children, waits, joins parts */
void do_get_concurrent(const char *host, int ctrl_sock, const char *user, const char *pass, const char *filename) {
    char resp[REPLYLEN];

    /* Get size using SIZE command on existing control socket */
    long total_size = get_remote_size(ctrl_sock, filename);
    if (total_size < 0) {
        printf("Server does not support SIZE or failed to get size. Concurrent download aborted.\n");
        return;
    }
    printf("Remote file size: %ld bytes\n", total_size);

    long part_size = total_size / PARTS;
    pid_t pids[PARTS];
    for (int i = 0; i < PARTS; ++i) {
        long start = i * part_size;
        long len = (i == PARTS-1) ? (total_size - start) : part_size;

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            /* if fork fails, wait for previously created and abort */
            for (int j=0;j<i;j++) waitpid(pids[j], NULL, 0);
            return;
        } else if (pid == 0) {
            /* child */
            /* Note: child creates its own control connection and logs in */
            child_download_part(host, user, pass, filename, start, len, i);
            _exit(0);
        } else {
            pids[i] = pid;
        }
    }

    /* parent waits for all children */
    for (int i = 0; i < PARTS; ++i) {
        int status;
        waitpid(pids[i], &status, 0);
    }

    /* assemble parts into final file */
    FILE *out = fopen(filename, "wb");
    if (!out) { perror("fopen assemble"); return; }
    char partname[128];
    char buf[BUFSIZE];
    for (int i = 0; i < PARTS; ++i) {
        snprintf(partname, sizeof(partname), "%s.part%d", filename, i);
        FILE *in = fopen(partname, "rb");
        if (!in) { perror("fopen part"); fclose(out); return; }
        size_t n;
        while ((n = fread(buf,1,sizeof(buf), in)) > 0) {
            fwrite(buf,1,n,out);
        }
        fclose(in);
        /* remove part file */
        remove(partname);
    }
    fclose(out);

    /* Notify server: after all parts children completed, the parent should read a final control reply if needed.
       However, each child reads its own final reply and printed it. The parent's control connection did not
       issue RETR; it only performed SIZE. So no extra reply to read here. */
    printf("Concurrent download completed and assembled into '%s'\n", filename);
}

/* ---------- Port mode ---------- */
/* --------- ACTIVE MODE (PORT) --------- */
/* Open ephemeral port locally, send PORT command, return data socket (listening) */

int open_port_data_connection(int ctrl_sock) {
    int sockfd;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return -1; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;  // ephemeral port

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if (getsockname(sockfd, (struct sockaddr*)&addr, &len) < 0) {
        perror("getsockname");
        close(sockfd);
        return -1;
    }

    int port = ntohs(addr.sin_port);
    int p1 = port / 256;
    int p2 = port % 256;

    char ip[] = "127,0,0,1";  // solo localhost por simplicidad
    char arg[128];
    snprintf(arg, sizeof(arg), "%s,%d,%d", ip, p1, p2);

    char resp[REPLYLEN];
    int code = send_command_and_get_reply(ctrl_sock, "PORT", arg, resp, sizeof(resp));
    if (code >= 400) {
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 1) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;  // listening socket
}


/* ---------- Help menu ---------- */
void print_help() {
    printf("\n===== MENU DE COMANDOS FTP =====\n");
    printf("  ayuda                 - mostrar este menú\n");
    printf("  pwd                   - mostrar directorio en servidor\n");
    printf("  cd <dir>              - cambiar directorio en servidor\n");
    printf("  ls                    - listar archivos del servidor\n");
    printf("  get <archivo>         - descargar archivo (modo PASV)\n");
    printf("  getc <archivo>        - descargar concurrente en %d partes\n", PARTS);
    printf("  put <archivo>         - subir archivo (modo PASV)\n");
    printf("  portget <archivo>     - descargar usando modo activo (PORT)\n");
    printf("  portput <archivo>     - subir usando modo activo (PORT)\n");
    printf("  lcd <dir>             - cambiar dir local\n");
    printf("  salir                 - terminar sesión\n\n");
}


/* ---------- Main ---------- */
int main(int argc, char *argv[]) {
    char *host = "127.0.0.1";
    char *service = "ftp";
    if (argc >= 2) host = argv[1];
    if (argc >= 3) service = argv[2];

    /* Connect control */
    int ctrl = connectTCP(host, service);
    if (ctrl < 0) errexit("connectTCP failed\n");

    char resp[REPLYLEN];
    if (read(ctrl, resp, sizeof(resp)-1) <= 0) errexit("Failed to read banner\n");
    printf("%s", resp);

    /* login */
    char user[128], pass[128];
    while (1) {
        printf("Username: ");
        if (scanf("%127s", user) != 1) errexit("username input\n");
        send_command_and_get_reply(ctrl, "USER", user, resp, sizeof(resp));
        /* prompt for password without echo - use getpass if available */
        char *p = getpass("Password: ");
        if (!p) errexit("getpass\n");
        strncpy(pass, p, sizeof(pass)-1); pass[sizeof(pass)-1] = '\0';
        int code = send_command_and_get_reply(ctrl, "PASS", pass, resp, sizeof(resp));
        if (code == 230) { printf("Login ok\n"); break; }
        else { printf("Login failed: %s", resp); }
    }
    /* consume newline left by scanf when reading later lines */
    int c = getchar();

    print_help();

    char line[512];
    for (;;) {
        printf("ftp> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        /* strip newline */
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        /* parse command */
        char *cmd = strtok(line, " ");
        if (!cmd) continue;

        if (strcasecmp(cmd, "help") == 0) {
            print_help();
        } else if (strcasecmp(cmd, "pwd") == 0) {
            do_pwd(ctrl);
        } else if (strcasecmp(cmd, "cd") == 0) {
            char *arg = strtok(NULL, "");
            if (!arg) { printf("usage: cd <dir>\n"); continue; }
            do_cwd(ctrl, arg);
        } else if (strcasecmp(cmd, "ls") == 0) {
            do_list(ctrl);
        } else if (strcasecmp(cmd, "portget") == 0) {
        char *arg = strtok(NULL, "");
        if (!arg) { printf("uso: portget <archivo>\n"); continue; }
        do_portget(ctrl, arg);
        } else if (strcasecmp(cmd, "portput") == 0) {
            char *arg = strtok(NULL, "");
            if (!arg) { printf("uso: portput <archivo>\n"); continue; }
            do_portput(ctrl, arg);
        } else if (strcasecmp(cmd, "get") == 0) {
            char *arg = strtok(NULL, " ");
            if (!arg) { printf("usage: get <file>\n"); continue; }
            do_get(ctrl, arg);
        } else if (strcasecmp(cmd, "getc") == 0) {
            char *arg = strtok(NULL, " ");
            if (!arg) { printf("usage: getc <file>\n"); continue; }
            do_get_concurrent(host, ctrl, user, pass, arg);
        } else if (strcasecmp(cmd, "put") == 0) {
            char *arg = strtok(NULL, " ");
            if (!arg) { printf("usage: put <file>\n"); continue; }
            do_put(ctrl, arg);
        } else if (strcasecmp(cmd, "pput") == 0) {
            /* For brevity, we keep pput experimental: we call user's existing passiveTCP /
               building of PORT would be required. Not implemented fully here. */
            printf("pput (active) is experimental and not implemented in this build.\n");
        } else if (strcasecmp(cmd, "pcd") == 0 || strcasecmp(cmd, "lcd") == 0) {
            char *arg = strtok(NULL, " ");
            if (!arg) { printf("usage: pcd <localdir>\n"); continue; }
            if (chdir(arg) == 0) printf("Local dir changed to %s\n", arg);
            else perror("chdir");
        } else if (strcasecmp(cmd, "quit") == 0) {
            send_command_and_get_reply(ctrl, "QUIT", NULL, resp, sizeof(resp));
            close(ctrl);
            break;
        } else {
            printf("Unknown command. Type 'help'.\n");
        }
    }

    return 0;
}
