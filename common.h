/**
 * Separate header file to group all .h files and constants added by Vasanth Viswanathan. class ID : 38 - START
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <netinet/in.h>

#define MAX_LENGTH 2550
#define NOOFCONN 10
#define MAX_FILES 10
#define BUF_SIZE 1024
#define PATH_MAX 255
#define INTERVAL 5
#define UPLOAD_PORT 8000
#define DOWNLOAD_PORT 8001
#define UPLOAD_PORT_BKP 8010
#define DOWNLOAD_PORT_BKP 8011
#define INVALID_SOCKET -1
#define TRUE 1
#define FALSE 0
/**
 * Separate header file to group all .h files and constants added by Vasanth Viswanathan. class ID : 38 - END
 */
