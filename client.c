#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <dirent.h>

//#define DATASIZE 1024
#define DATASIZE 1000000
#define BUFSIZE 1000000
//#define BUFSIZE 1024
#define PORT 10000

void quit();
void cd();
void pwd(int num, char *com[]);
void dir(int num, char *com[]);
void lpwd();
void lcd();
void ldir(int num, char *com[]);
void get(int num, char *com[]);
void put(int argc, char *argv[]);
void send_filepath(int num, char *com[]);

int sd;
FILE *fp;
int file_close = 1;

struct command_table {
	char *cmd;
	void (*func)();
} cmd_tbl[] = {
	{"quit", quit},
	{"pwd", pwd},
	{"cd", cd},
	{"dir", dir},
	{"lpwd", lpwd},
	{"lcd", lcd},
	{"ldir", ldir},
	{"get", get},
	{"put", send_filepath},
	{NULL, NULL}
};

struct ftp_header {
	uint8_t type;
	uint8_t code;
	uint16_t length;
};

void lcd(int num, char *com[])
{
	if (chdir(com[1]) < 0) {
		perror("chdir");
	}
}

/* send file content */
void put(int num, char *com[])
{
	FILE *fp;
	char buf[BUFSIZE];
	char *p;
	int i, k, n = 0, len = 0, time;
	uint16_t length;
	int flag = 0;
    struct ftp_header *send_header;
    uint8_t *send_pay;
    uint8_t *data;
    int wtime = 0;

	if ((fp = fopen(com[1], "r")) == NULL) {
		perror("fopen");
		exit(EXIT_FAILURE);
	} else {
		/* content of the file */
		n = fread(buf, sizeof(char), BUFSIZE, fp);
		len = n;

        if (n < BUFSIZE) {   /// filesize is under BUFSIZE
           data = (uint8_t *)malloc(sizeof(struct ftp_header) + sizeof(uint8_t) * n);
           send_header = (struct ftp_header *)data;
           send_pay = data + sizeof(struct ftp_header);
           memcpy(send_pay, buf, n);
           /* send file content */
           send_header->type = 0x20;
           send_header->code = 0x00;
           send_header->length = htons(len);
           if (send(sd, data, sizeof(struct ftp_header) + len, 0) < 0) {
               perror("send");
               exit(EXIT_FAILURE);
           }
           free(data);
        } else {          /// filesize if over BUFSIZE
            while (1) {
                data = (uint8_t *)malloc(sizeof(struct ftp_header) + sizeof(uint8_t) * n);
                send_header = (struct ftp_header *)data;
                send_pay = data + sizeof(struct ftp_header);
                memcpy(send_pay, buf, n);
                /* send file content */
                send_header->type = 0x20;
                send_header->code = 0x01;
                send_header->length = htons(n);
                if (send(sd, data, sizeof(struct ftp_header) + n, 0) < 0) {
                    perror("send");
                    exit(EXIT_FAILURE);
                }
                free(data);
                
                memset(buf, 0, BUFSIZE);
                n = fread(buf, sizeof(char), BUFSIZE, fp);
                len += n;
                if (n < BUFSIZE) {
                    flag++;
                    break;
                }
                wtime++;
            }
            /* send the rest of the file */
            data = (uint8_t *)malloc(sizeof(struct ftp_header) + sizeof(uint8_t) * n);
            send_header = (struct ftp_header *)data;
            send_pay = data + sizeof(struct ftp_header);
            memcpy(send_pay, buf, n);
            /* send file content */
            send_header->type = 0x20;
            send_header->code = 0x00;
            send_header->length = htons(n);
            printf("going to send last data"); 
            if (send(sd, data, sizeof(struct ftp_header) + n, 0) < 0) {
                perror("send");
                exit(EXIT_FAILURE);
            }
            free(data);
        }
	}
}


/* send file path */
void send_filepath(int num, char *com[])
{
  char pkt_data[sizeof(struct ftp_header) + DATASIZE];
  struct ftp_header *send_header = (struct ftp_header *)pkt_data;
  char *data = pkt_data + sizeof(struct ftp_header);
  int len;
  struct ftp_header recv_header;
  uint16_t data_len = 0;
  uint8_t type, code;

  len = strlen(com[1]);
  strcpy(data, com[1]);
  //printf("file path = %s, length = %d\n", data, len);

  send_header->type = 0x06;
  send_header->code = 0x00;
  send_header->length = htons(len - 1);
  
  if (send(sd, pkt_data, sizeof(struct ftp_header) + len, 0) < 0) {
    perror("send");
    exit(EXIT_FAILURE);
  }
  /* receive only the header part */
  if (recv(sd, &recv_header, sizeof(struct ftp_header), 0) < 0) {
    perror("recv");
    exit(EXIT_FAILURE);
  }
  /* save the header info */
  data_len = ntohs(recv_header.length);
  type = recv_header.type;
  code = recv_header.code;

  if (type == 0x10) {
    put(num, com);        ////// call put function to send file content
  }
}

void get(int num, char *com[])
{
	char pkt_data[sizeof(struct ftp_header) + DATASIZE];
	struct ftp_header *send_header = (struct ftp_header *)pkt_data;
	char *data = pkt_data + sizeof(struct ftp_header);
	int len, data_len, n, size;
	struct ftp_header recv_header;
	struct ftp_header recv_header_length;
	char file_data[DATASIZE];
	uint8_t code;

	len = strlen(com[1]);
	strcpy(data, com[1]);
	//printf("file path = %s, length = %d\n", data, len);

    // send file path to the server
	send_header->type = 0x05;
	send_header->code = 0x00;
	send_header->length = htons(len);
	if (send(sd, pkt_data, sizeof(struct ftp_header) + len, 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
	/* receive ack from server */
	if (recv(sd, &recv_header, sizeof(struct ftp_header), 0) < 0) {
		perror("recv");
		exit(EXIT_FAILURE);
	}
	/* receive ftp_header and get data length */
	if (recv(sd, &recv_header_length, sizeof(struct ftp_header), 0) < 0) {
		perror("recv");
		exit(EXIT_FAILURE);
	}
	data_len = (int)ntohs(recv_header_length.length);
	code = recv_header_length.code;

    if (file_close == 1) { /// if file pointer is not open yet
        fp = fopen(data, "w");
        file_close = 0; /// file pointer is set
    }
    if (code == 0x00) {
        while (data_len > 0) {
            if ((n = read(sd, file_data, DATASIZE)) > 0) {
                file_data[n] = '\0';
                size = fwrite(file_data, sizeof(char), n, fp);
                if (size < n) {
                    if (!feof(fp)) {
                        perror("fwrite");
                    }
                    break;
                }
                data_len = data_len - size;
            }
        }
        fclose(fp);
        file_close = 1;
    } else {
        while (code != 0x00) {
            // receive the first part of the file
            while (data_len > 0) {
                if ((n = read(sd, file_data, DATASIZE)) > 0) {
                    file_data[n] = '\0';
                    size = fwrite(file_data, sizeof(char), n, fp);
                    if (size < n) {
                        if (!feof(fp)) {
                            perror("fwrite");
                        }
                        break;
                    }
                    data_len = data_len - size;
                }
            }
            /* receive ftp_header and get data length */
            if (recv(sd, &recv_header_length, sizeof(struct ftp_header), 0) < 0) {
                perror("recv");
                exit(EXIT_FAILURE);
            }
            data_len = (int)ntohs(recv_header_length.length);
            code = recv_header_length.code;
        }
        // receive the rest of the file
        while (data_len > 0) {
            if ((n = read(sd, file_data, DATASIZE)) > 0) {
                file_data[n] = '\0';
                size = fwrite(file_data, sizeof(char), n, fp);
                if (size < n) {
                    if (!feof(fp)) {
                        perror("fwrite");
                    }
                    break;
                }
                data_len = data_len - size;
            }
        }
        fclose(fp);
        file_close = 1;
    }
}

void pwd(int num, char *com[])
{
	char pkt_data[sizeof(struct ftp_header) + DATASIZE];
	struct ftp_header *send_header = (struct ftp_header *)pkt_data;
	char *data = pkt_data + sizeof(struct ftp_header);
	int len;
	struct ftp_header recv_header;
	uint16_t data_len = 0;
	uint8_t type, code;
	char dir[DATASIZE];
	int n;

	send_header->type = 0x02;
	send_header->code = 0x00;
	send_header->length = 0;

	if (send(sd, send_header, sizeof(struct ftp_header) + 0, 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}

	if (recv(sd, &recv_header, sizeof(struct ftp_header), 0) < 0) {
		perror("recv");
		exit(EXIT_FAILURE);
	}
	data_len = ntohs(recv_header.length);
	type = recv_header.type;
	code  =recv_header.code;

	if ((n = read(sd, dir, data_len)) > 0) {
		dir[n] = '\n';
		dir[n + 1] = '\0';
		//fwrite(data, sizeof(char), n, stdout);
		fputs(dir, stdout);
	}
}

void lpwd()
{
	char data[DATASIZE];
	FILE *fp;
	
	fp = popen("pwd", "r");
	while (fgets(data, DATASIZE - 1, fp) != NULL) {
		fputs(data, stdout);
	}
	fclose(fp);
}

void ldir(int num, char *com[])
{
	char info[DATASIZE];
	int result, dir_len, k;
	struct stat st;
	DIR *dir;
	struct dirent *dp;
	char *p;
	char data[DATASIZE];
	
	if (num == 0) {
		info[0] = '.';
		info[1] = '\0';
	} else {
		strcpy(info, com[1]);
	}

	result = stat(info, &st);
	if (result != 0) {
        fprintf(stderr, "%s is not found.\n", info);
        exit(EXIT_FAILURE);
    }
	if ((st.st_mode & S_IFMT) == S_IFDIR) {
        //printf("%s is directory.\n", info);
		if ((dir = opendir(info)) == NULL) {
			perror("opendir");
			exit(EXIT_FAILURE);
		}
		p = data;
		for (dp = readdir(dir); dp != NULL; dp = readdir(dir)) {
			printf("%s\n", dp->d_name);
		}
    } else {
        printf("%s is not directory.\n", info);
    }
}

void quit()
{
	struct ftp_header send_header;
	send_header.type = 0x01;
	send_header.code = 0x00;
	send_header.length = 0;
	if (send(sd, &send_header, sizeof(struct ftp_header), 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
	close(sd);
	exit(0);
}

void dir(int num, char *com[])
{
	char pkt_data[sizeof(struct ftp_header) + DATASIZE];
	struct ftp_header *send_header = (struct ftp_header *)pkt_data;
	char *data = pkt_data + sizeof(struct ftp_header);
	struct ftp_header recv_header;
	uint16_t data_len = 0;
	uint8_t type, code;
	char dir[DATASIZE];
	int n, len;

	//printf("num ==  %d\n", num);

	if (num == 1) {
		len = strlen(com[1]);
		strcpy(data, com[1]);
		//printf("file path = %s, length = %d\n", data, len);

		send_header->type = 0x04;
		send_header->code = 0x00;
		send_header->length = htons(len);
	} else {
		len = 0;
		send_header->type = 0x04;
		send_header->code = 0x00;
		send_header->length = htons(0);
	}

	if (send(sd, pkt_data, sizeof(struct ftp_header) + len, 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}

	if (recv(sd, &recv_header, sizeof(struct ftp_header), 0) < 0) {
		perror("recv");
		exit(EXIT_FAILURE);
	}
	data_len = ntohs(recv_header.length);
	type = recv_header.type;
	code  =recv_header.code;

	if ((n = read(sd, dir, data_len)) > 0) {
		dir[n] = '\n';
		dir[n + 1] = '\0';
		//fwrite(data, sizeof(char), n, stdout);
		fputs(dir, stdout);
	}
}

void cd(int num, char *com[])
{
	char pkt_data[sizeof(struct ftp_header) + DATASIZE];
	struct ftp_header *send_header = (struct ftp_header *)pkt_data;
	char *data = pkt_data + sizeof(struct ftp_header);
	struct ftp_header recv_header;
	uint16_t data_len = 0;
	uint8_t type, code;
	char dir[DATASIZE];
	int n, len;

	//len = strlen(com[1]);
	strcpy(data, com[1]);
	len = strlen(data);
	//printf("data = %s, length = %d\n", data, len);

	send_header->type = 0x03;
	send_header->code = 0x00;
	send_header->length = htons(len);

	if (send(sd, pkt_data, sizeof(struct ftp_header) + len, 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
	if (recv(sd, &recv_header, sizeof(struct ftp_header), 0) < 0) {
		perror("recv");
		exit(EXIT_FAILURE);
	}
	if (recv_header.type == 0x10) {
		puts("succesfully moved the directory in server");
	}
}


void getargs(char *cp, char *argv[], int *argc)
{
  char *p;
  int i = 1, len, k;
  argv[0] = cp;
  len = strlen(cp);
  *argc = 0;

  for (p = cp + 1, k = 1; k < len; p++, k++) {
    if ((*p != ' ') && (*(p - 1) == '\0')) {
      argv[i] = p;
      i++;
      (*argc)++;
    } else if ((*p != ' ') && (*(p - 1) != ' ')) {
      continue;
    } else if (*p == ' '){
      *p = '\0';
    }
  }
}


int main (int argc, char *argv[])
{
  char buf[100];
  int num, n;
  char *com[5];
  struct sockaddr_in s_addr;
  char *server_ip;
  struct command_table *p;
  fd_set readfds;
  

  server_ip = argv[1];

  /* create client socket */
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  memset(&s_addr, 0, sizeof(struct sockaddr_in));
  s_addr.sin_family = AF_INET;
  s_addr.sin_port = htons(PORT);
  s_addr.sin_addr.s_addr = inet_addr(server_ip);
  /* connect to the server */
  if (connect(sd, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
    perror("connect");
    exit(EXIT_FAILURE);
  }
  
  
  for (;;) {
    fprintf(stdout, "FTP$ ");
    if (fgets(buf, sizeof(buf) - 1, stdin) == NULL) {
      fprintf(stdout, "\n");
      continue;
    }
    buf[strlen(buf) - 1] = '\0';
    if (*buf == '\0')
      continue;
    getargs(buf, com, &num);
    //printf("com[0] = %s\n", com[0]);
	if (strncmp(com[0], "lcd", 3) == 0) {
		chdir(com[1]);
		continue;
	}
    for (p = cmd_tbl; p->cmd != NULL; p++) {
      if (strcmp(com[0], p->cmd) == 0) {
	(*p->func)(num, com);
	break;
      }
    }
    if (p->cmd == NULL)
      fprintf(stderr, "nuknown command\n");
  }
}

