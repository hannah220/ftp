#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

//#define DATASIZE 1024
#define DATASIZE 1000000
#define BUFSIZE 1000000
//#define BUFSIZE 1024
#define PORT 10000

struct ftp_header {
  uint8_t type;
  uint8_t code;
  uint16_t length;
};

FILE *exe_stor(int sd, int code, int data_len)
{
  char *file_path;
  char data[DATASIZE];
  int i = 0, size = 0, n = 0;
  struct ftp_header send_header;
  FILE *fp;
  
  /* fill out file path to char array[] */
  while (data_len > 0) {
    if ((n = read(sd, data, DATASIZE)) > 0) {
      if (i == 0) {
	file_path = data;
	i++;
      } else {
	strcpy(&file_path[size], data);
      }
      size += n;
      data_len = data_len - n;
    }
  }
  file_path[size] = '\0';
  //printf("file_path = %s\n", file_path);

  if ((fp =fopen(file_path, "w")) != NULL) {
    send_header.type = 0x10;
    send_header.code = 0x02;
  } else {
    perror("fopen");
    send_header.type = 0x10;
    send_header.code = 0x01;      // not authorized to access to the file
  }
  send_header.length = htons(0);

  if (send(sd, &send_header, sizeof(struct ftp_header), 0) < 0) {
    perror("send");
    exit(EXIT_FAILURE);
  }
  return fp;
}

FILE *receive_file(int sd, FILE *fp, int data_len, uint8_t code)
{
  char data[DATASIZE];
  int n = 0, size = 0;

  printf("receive file: data_len = %d\n", data_len);
  
  /* receive the data of file and write to the file */
  while (data_len > 0) {
    if ((n = read(sd, data, DATASIZE)) > 0) {
      //data[n] = '\0';
      printf("code = %d, n = %d\n", code, n);
	  size = fwrite(data, sizeof(char), n, fp);
      if (size < n) {
	if (!feof(fp)) {
	  perror("fwrite");
	}
	break;
      }
      data_len = data_len - size;
    }
  }
  if (code == 0x00) {
    fclose(fp);
    return NULL;
  } else {
    return fp;
  }
  return NULL;
}

////////////// send file to the client ///////////////////////////////////////
void exe_retr(int sd, int data_len)
{
	struct ftp_header send_header_first;
	char info[DATASIZE];
	int n, i, len, flag = 0, sucess = 0, size;
    char buf[BUFSIZE];
	FILE *fp;
	char *p;
    struct ftp_header *send_header;
    uint8_t *send_pay;
    uint8_t *data;

    memset(info, 0, DATASIZE);
	if (recv(sd, info, data_len, 0) < 0) {
		perror("recv");
		exit(EXIT_FAILURE);
    }
    
	//printf("file_path = %s\n", info);
	if ((fp = fopen(info, "r")) == NULL) {
		perror("fopen");
		send_header_first.type = 0x12;
		send_header_first.code = 0x00;
	} else {
		sucess = 1;
		send_header_first.type = 0x10;
		send_header_first.code = 0x01;
	}
	send_header_first.length = 0;
	if (send(sd, &send_header_first, sizeof(struct ftp_header), 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
    // read the content of the file 
    n = fread(buf, sizeof(char), BUFSIZE, fp);

    if (n < BUFSIZE) {
        data = (uint8_t *)malloc(sizeof(struct ftp_header) + sizeof(uint8_t) * n);
        send_header = (struct ftp_header *)data;
        send_pay = data + sizeof(struct ftp_header);
        memcpy(send_pay, buf, n);
        // send file content
        send_header->type = 0x20;
        send_header->code = 0x00;
        send_header->length = htons(n);
        if (send(sd, data, sizeof(struct ftp_header) + n, 0) < 0) {
            perror("send");
            exit(EXIT_FAILURE);
        }
        free(data);
    } else {
        while (1) {
            data = (uint8_t *)malloc(sizeof(struct ftp_header) + sizeof(uint8_t) * n);
            send_header = (struct ftp_header *)data;
            send_pay = data + sizeof(struct ftp_header);
            memcpy(send_pay, buf, n);
            // send file content
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
            if (n < BUFSIZE) {
                break;
            }
        }
        // send the rest of the file
        //printf("last %d bytes\n", n);
        data = (uint8_t *)malloc(sizeof(struct ftp_header) + sizeof(uint8_t) * n);
            send_header = (struct ftp_header *)data;
            send_pay = data + sizeof(struct ftp_header);
            memcpy(send_pay, buf, n);
            // send file content
            send_header->type = 0x20;
            send_header->code = 0x00;
            send_header->length = htons(n);
            if (send(sd, data, sizeof(struct ftp_header) + n, 0) < 0) {
                perror("send");
                exit(EXIT_FAILURE);
            }
            free(data); 
    }
}

void exe_pwd(int sd)
{
	char pkt_data[sizeof(struct ftp_header) + DATASIZE];
	struct ftp_header *send_header = (struct ftp_header *)pkt_data;
	char *data = pkt_data + sizeof(struct ftp_header);
	FILE *fp;
	int len = 0;

	fp = popen("pwd", "r");
	while (fgets(data, DATASIZE -1, fp) != NULL);
	fclose(fp);
	len = strlen(data) - 1;

	send_header->length = htons((uint16_t)len);
	send_header->type = 0x10;
	send_header->code = 0x00;
	if (send(sd, pkt_data, sizeof(struct ftp_header) + len, 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
}

void exe_list(int sd, int data_len)
{
	char pkt_data[sizeof(struct ftp_header) + DATASIZE];
	struct ftp_header *send_header = (struct ftp_header *)pkt_data;
	char *data = pkt_data + sizeof(struct ftp_header), *p;
	char *file_path;
	int i = 0, k, len, dir_len, n, size;
	struct stat st;
	int result;
	DIR *dir;
	struct dirent *dp;
	char info[DATASIZE];

	//printf("data_len = %d\n", data_len);

	if (data_len > 0) {
		/* fill out file path to char array[] */
		if ((n = read(sd, info, data_len)) < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		info[n] = '\0';
	} else {
		info[0] = '.';
		info[1] = '\0';
	}
	
	result = stat(info, &st);
	if (result != 0) {
        //fprintf(stderr, "%s is not found.\n", info);
        send_header->length = 0;
		send_header->type = 0x12;
		send_header->code = 0x00;
    }
	if ((st.st_mode & S_IFMT) == S_IFDIR) {
        //printf("%s is directory.\n", info);
		if ((dir = opendir(info)) == NULL) {
			perror("opendir");
			exit(EXIT_FAILURE);
		}
		p = data;
		for (dp = readdir(dir); dp != NULL; dp = readdir(dir)) {
			//printf("%s\n", dp->d_name);
			strcpy(p, dp->d_name);
			dir_len = strlen(dp->d_name);
			for (k = 0; k < dir_len; k++) {
				p++;		
			}
			*p = '\n';
			p++;
		}
		*p = '\0';
		//printf("data = %s\n;", data);
		len = strlen(data) - 1;

		send_header->length = htons((uint16_t)len);
		send_header->type = 0x10;
		send_header->code = 0x01;
    } else {
        //printf("%s is not directory.\n", info);
		send_header->length = 0;
		send_header->type = 0x12;
		send_header->code = 0x01;
    }
	if (send(sd, pkt_data, sizeof(struct ftp_header) + len, 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
}

void exe_cwd(int sd, int data_len)
{
	struct ftp_header *send_header;
	char info[DATASIZE];
	int n;

	//printf("data_len = %d\n", data_len);
	/* fill out file path to char array[] */
	if ((n = recv(sd, info, data_len, 0)) < 0) {
		perror("read");
		exit(EXIT_FAILURE);
	}
	info[n] = '\0';
	//printf("n = %d, strlen(info) = %d\n", n, strlen(info));
	//printf("info = %s, length = %d\n", info, strlen(info));

	if (chdir(info) < 0) {
		//puts("failed");
		send_header->type = 0x12;
		if (errno == ENOENT) {
			send_header->code = 0x00;
		} else {
			send_header->code = 0x10;
		}
	} else {
		//printf(" chdir succesfully executed");
		send_header->type = 0x10;
		send_header->code = 0x00;
	}
	send_header->length = 0;

	if (send(sd, send_header, sizeof(struct ftp_header) + 0, 0) < 0) {
		perror("send");
		exit(EXIT_FAILURE);
	}
}

void exe_quit(int sd)
{
	close(sd);
	exit(0);
}


void execute_com(int sd)
{
  //char pkt_data[sizeof(ftph_header) + DATASIZE];
  //char pkt_data1[sizeof(ftph_header) + DATASIZE];
  struct execute_table *p;
  struct ftp_header recv_header;
  //char *data;
  char data[DATASIZE];
  uint16_t data_len = 0;
  uint8_t type, code;
  FILE *fp = NULL;

  for (;;) {
    /* receive only the header part */
    if (recv(sd, &recv_header, sizeof(struct ftp_header), 0) < 0) {
      perror("recv");
      exit(EXIT_FAILURE);
    }
    /* save the header info */
    data_len = ntohs(recv_header.length);
    type = recv_header.type;
    code = recv_header.code;

    switch (type) {
    case 0x01:
      exe_quit(sd);
      exit(0);
    case 0x02:
      exe_pwd(sd);
      break;
    case 0x03:
		exe_cwd(sd, data_len);
      break;
    case 0x04:
		exe_list(sd, data_len);
      break;
    case 0x05: // requested to send file to the client
		exe_retr(sd, data_len);
      break;
    case 0x06:
      fp = exe_stor(sd, code, data_len);
      break;
    case 0x20:
      fp = receive_file(sd, fp, data_len, code);
      break;
    }
  }
}


int main(int argc, char *argv[])
{
  int master_sock, client_sock;
  struct sockaddr_in s_addr;
  struct sockaddr_in c_addr;
  int pid;
  int yes = 1;

  if (argc != 3) {
      printf("put <directory> <IPaddress>\n");
      exit(0);
  }

  if (chdir(argv[1]) < 0) {
	  perror("chdir");
	  exit(EXIT_FAILURE);
  }
	
  /* create master socket */
  if ((master_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }
  /* set up master socket */
  s_addr.sin_family = AF_INET;
  //s_addr.sin_addr.s_addr = INADDR_ANY;
  s_addr.sin_addr.s_addr = inet_addr(argv[2]);
  s_addr.sin_port = htons(PORT);

  setsockopt(master_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
  /* bind master socket to localhost */
  if (bind(master_sock, (struct sockaddr *)&s_addr, sizeof(s_addr)) < 0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }
  /* set pending connection up to 5 */
  if (listen(master_sock, 5) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  for (;;) {
    if ((client_sock = accept(master_sock, NULL, NULL)) < 0) {
      perror("accept");
      exit(EXIT_FAILURE);
    }
    if ((pid = fork()) < 0) {
      perror("fork");
      exit(EXIT_FAILURE);
    } else if (pid == 0) {   /* child process */
      execute_com(client_sock);
    } else {                 /* parent process */
      close(client_sock);
    }
		
  }
  return 0;
}
