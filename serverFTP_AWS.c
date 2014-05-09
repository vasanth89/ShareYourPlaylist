//header files
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

//constants declaration
#define NOOFCONN 10
#define MAX_FILES 10
#define BUF_SIZE 1024
#define PATH_MAX 255
#define INTERVAL 5
#define UPLOAD_PORT 8000
#define DOWNLOAD_PORT 8001
#define INVALID_SOCKET -1
#define TRUE 1
#define FALSE 0

//attributes for measuring performance statistics
struct {
	pthread_mutex_t st_mutex;
	unsigned int st_concount;
	unsigned int st_contotal;
	unsigned long st_contime;
	unsigned long st_bytecount;
} stats;

pthread_t th;
pthread_attr_t ta;

struct TIME{
  int seconds;
  int minutes;
  int hours;
  long int milliseconds;
};

//method declaration
int passiveTCP(int port);
int network_accept_any(int fds[], unsigned int count, int **master);
int handleUpload(int connID);
void checkUserDir(int socket);
void fileTransfer(int connID);
void dataRedundancyClient(char *user);
int connectTCP(int port);
void fileTransferRedundancy(int socket, char *user);
int handleDownload(int connID);
void fileTransferDownload(int connID);
int selectdir();
void timeDifference(struct TIME t1, struct TIME t2, struct TIME *differ);
void prstats(void);
void fatal(char *string);

//global variable declaration
struct stat st;
char username[PATH_MAX];
struct sockaddr_in* pV4Addr;
int ipAddr;
char clientConn[INET_ADDRSTRLEN];
char *server_addr_list[] = { "10.11.12.13", "14.15.16.17" }; // Replace with the list of server IP addresses on AWS


/*
 * main(): This function is the entry point for the server program of Share Your Playlist application.
 * This function creates the upload and download sockets and waits for client connections.
 * We have implemented all the 3 methods used in concurrency in servers namely:
 * 1. Multi-process approach using fork() for handling upload and download functionalities
 * 2. Non-blocking I/O using select() for monitoring two ports on server
 * 3. Multi-threaded approach for data redundancy between servers
 */
int main(int argc, char **argv) {
	struct sockaddr_in sockserv;
	unsigned int alen;
	int upload_sock, download_sock, i, slave_sock, fds[2], *fd = NULL;
	struct sockaddr *addr;
	socklen_t *addrlen;

	//binds sockets for upload and download functionality
	upload_sock = passiveTCP(UPLOAD_PORT);
	download_sock = passiveTCP(DOWNLOAD_PORT);

	printf("Upload sock : %d\n", upload_sock);
	printf("Download sock : %d\n", download_sock);

	fds[0] = upload_sock;
	fds[1] = download_sock;

	while (1) {
		//calls function to identify which port client connected to
		slave_sock = network_accept_any(fds, 2, &fd);

		//Initializing thread attributes for prstats thread
		(void) pthread_attr_init(&ta);
		(void) pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
		(void) pthread_mutex_init(&stats.st_mutex, 0);

		/*if(pthread_create(&th, &ta, (void * (*)(void *))prstats, 0) < 0)
		 fatal("prstats thread failed");*/

		//Based on socket that obtained the connection, we use multi-process approach using fork() to provide concurrency
		//Based on upload/download, the respective functions are called in the child processes.
		if (*fd == upload_sock) {
			switch (fork()) {
			case 0:
				close(*fd);
				exit(handleUpload(slave_sock));
			default:
				close(slave_sock);
				break;
			}
		}
		if (*fd == download_sock) {
			switch (fork()) {
			case 0:
				close(*fd);
				exit(handleDownload(slave_sock));
			default:
				close(slave_sock);
				break;
			}
		}
	}
}

/*
 * passiveTCP(): This function is used to bind the upload and download port to new sockets.
 * This function places the socket in listen mode to wait for clients
 */
int passiveTCP(int port) {
	struct sockaddr_in sockserv, sockclient;
	int server_sock, bind_server, listen_server, connID, on = 1;

	sockserv.sin_family = AF_INET;
	sockserv.sin_addr.s_addr = INADDR_ANY;
	sockserv.sin_port = htons(port);

	server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sock < 0)
		fatal("Creation of Master Socket failed");

	//used to enable reuse of socket port
	setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));

	//bind the socket to the respective port number
	bind_server = bind(server_sock, (struct sockaddr *) &sockserv,
			sizeof(sockserv));

	if (bind_server < 0)
		fatal("Binding of Master failed");

	//Upto 10 clients can connect to the server
	listen_server = listen(server_sock, NOOFCONN);

	if (listen_server < 0)
		fatal("Listen failed");

	//return the socket that was created
	return server_sock;
}

/*
 * network_accept_any(): This function implements the select() for non-blocking I/O approach to concurrency.
 * This function loops through upload and download sock.
 * It populates the appropriate master socket and returns the socket on which connection was established.
 */
int network_accept_any(int fds[], unsigned int count, int **master) {
	fd_set readfds;
	int maxfd, fd, status, accept_sock;
	unsigned int i, alen;
	struct sockaddr_in sockserv;

	*master = malloc(sizeof(int));

	FD_ZERO(&readfds);
	maxfd = -1;
	for (i = 0; i < count; i++) {
		FD_SET(fds[i], &readfds);
		if (fds[i] > maxfd)
			maxfd = fds[i];
	}
	//wait for connection
	status = select(maxfd + 1, &readfds, NULL, NULL, NULL);
	if (status < 0)
		return INVALID_SOCKET;
	fd = INVALID_SOCKET;
	//loop through to check for socket that obtained connection from client
	for (i = 0; i < count; i++) {
		if (FD_ISSET(fds[i], &readfds)) {
			alen = sizeof(sockserv);
			fd = fds[i];
			break;
		}
	}
	if (fd == INVALID_SOCKET)
		return INVALID_SOCKET;
	else {
		**master = fd;
		accept_sock = accept(fd, (struct sockaddr *) &sockserv, &alen);
		//Code written to handle data redundancy
		//This is used later to identify if connection came from client or server and do processing accordingly
		pV4Addr = (struct sockaddr_in*) &sockserv;
		ipAddr = pV4Addr->sin_addr.s_addr;
		inet_ntop( AF_INET, &ipAddr, clientConn, INET_ADDRSTRLEN);
		//return the socket on which new client connection was established
		return accept_sock;
	}
}

/*
 * handleUpload(): This function delivers the upload functionality for the server.
 * This function co-ordinates the main functions of the server such as checking user validity, and file transfer
 */
int handleUpload(int connID) {
	int i, check = TRUE;
	time_t start, end;

	//Show the start time of upload
	start = time(0);
	(void) printf("Start Time %s\n", ctime(&start));

	//function that checks if its new user or existing user and creates directory
	checkUserDir(connID);

	//function that accepts file from client and saves to local storage
	fileTransfer(connID);

	//Display the end time and calculate the total time taken for upload
	end = time(0);
	(void) printf("End Time %s\n", ctime(&end));
	(void) printf("Total Time %lu\n", (end - start));

	//Loop through to check if connection was from client or server
	for (i = 0; i < 2; i++) {
		if (strcmp(clientConn, server_addr_list[i]) == 0) {
			check = FALSE;
			break;
		}
	}

	//if connection was from client, initiate data redundancy to send data to other servers
	if (check) {
		//start a new thread to perform data redundancy among servers.
		//Including this we have included all concurrency approaches in our server
		pthread_t thread1;
		pthread_create(&thread1, NULL,(void * (*)(void *))dataRedundancyClient, username);
		pthread_join(thread1, NULL);
	}
	return 0;
}

/*
 * checkUserDir(): This function is used to check if user is new or existing.
 * Depending on user, a directory is created for user to upload his/her playlist.
 */
void checkUserDir(int socket) {
	char newdir[PATH_MAX], cwd[PATH_MAX], check[PATH_MAX] = "/home/ubuntu/Music";
	int checkDir, changedir, r;

	//Read the username
	bzero(username, PATH_MAX);
	r = read(socket, username, sizeof(username));
	username[strlen(username)] = '\0';

	printf("Username is :%s\n", username);

	//Change into main directory and create folder for user
	changedir = chdir(check);
	if (changedir < 0)
		fatal("Change Directory 0 failed");
	checkDir = mkdir(username, 0777);
	if (0 == checkDir)
		printf("Directory is created\n");
	else
		printf("Directory already Exists!!!\n");

	getcwd(cwd, sizeof(cwd));
	strcpy(newdir, cwd);
	strcat(newdir, "/");
	strcat(newdir, username);
	changedir = chdir(newdir);
	if (changedir < 0)
		fatal("Change Directory failed");

	//send ACK to client once directory is created successfully
	r = write(socket, "1", 1);
}

/*
 * fileTransfer(): This function is used to handle file transfer from client.
 * It handles the metadata information and performs necessary upload operations for server
 */
void fileTransfer(int connID) {
	char metaData[3000], revbuf[BUF_SIZE], backup[BUF_SIZE], success[1] = "1";
	char *fileNameArray[MAX_FILES], *fileSizeArray[MAX_FILES];
	char *str1, *result, *nextString;
	const char *delims = "~";
	int anamolyCount = 0, fr_block_sz = 0, remain = 0;
	int counter = 0, i = 1, noOfFiles = 0, count = 0, w, j;
	unsigned long lSize = 0L, sizeCheck = 0L;
	time_t t_start, t_end;
	struct tm *st,*en;
	struct timeval time_ms,time_end;
	struct TIME t1,t2,diff;

	//read the metadata information sent from client
	w = read(connID, metaData, sizeof(metaData));
	metaData[strlen(metaData)] = '\0';
	printf("Metadata : %s\n", metaData);

	//loop to split the metadata and read the information
	for (j = 0, str1 = metaData;; j++, str1 = NULL) {
		result = strtok_r(str1, delims, &nextString);
		if (result == NULL)
			break;
		if (j == 0) {
			noOfFiles = atoi(result);
			continue;
		}
		if (j % 2 != 0) {
			fileNameArray[j / 2] = result;
		} else {
			fileSizeArray[j / 2 - 1] = result;
		}
	}
	//Send ACK to client on metadata success
	w = write(connID, success, 1);

	//calculate the time taken
	t_start = time(NULL);
	st = localtime(&t_start);
	t1.hours = st->tm_hour;
	t1.minutes = st->tm_min;
	t1.seconds = st->tm_sec;
	gettimeofday(&time_ms, NULL);
	t1.milliseconds =  time_ms.tv_usec/1000;
	printf("START TIME = %d:%d:%d:%03ld\n",t1.hours,t1.minutes,t1.seconds,t1.milliseconds);

	//Loop to obtain files from client
	while (count < noOfFiles) {

		lSize = atol(fileSizeArray[count]);
		counter = (int) lSize / BUF_SIZE;

		FILE *fr = fopen(fileNameArray[count], "ab");
		if (fr == NULL)
			fatal("File cannot be opened on server");
		printf("File %s opened\n",fileNameArray[count]);

		while (i <= counter) {
			fr_block_sz = read(connID, revbuf, BUF_SIZE);
			//this block is executed if the client does not recieve 1024 bytes sent from server
			//anamolyCount is used as a performance metric to analyze file chunking anomalies
			while (fr_block_sz < BUF_SIZE) {
				anamolyCount++;
				remain = BUF_SIZE - fr_block_sz;
				remain = read(connID, backup, remain);
				strcat(revbuf, backup);
				fr_block_sz += remain;
			}
			sizeCheck += (long) fr_block_sz;
			fwrite(revbuf, fr_block_sz, 1, fr);
			bzero(revbuf, BUF_SIZE);
			i++;
			remain = 0;
		}

		//Obtain last chunk of file as it may not be in the boundary of 1024 bytes
		if (sizeCheck < lSize) {
			remain = (int) lSize - (int) sizeCheck;
			fr_block_sz = read(connID, revbuf, remain);
			fwrite(revbuf, remain, 1, fr);
			bzero(revbuf, BUF_SIZE);
		}

		//File transfer success
		printf("Ok received from client!\n");
		fclose(fr);

		//send ACK to server
		w = write(connID, success, 1);
		//display the anamoly count to show anomalies in file chunking.
		printf("Anamoly Count for file[%d] : %d\n", count, anamolyCount);

		//reset all variables to zero
		sizeCheck = 0L;
		lSize = 0L;
		i = 1;
		remain = 0;
		fr_block_sz = 0;
		anamolyCount = 0;
		count++;
	}
	//measure the end time and calculate the total time taken
	t_end = time(NULL);
	en = localtime(&t_end);
	t2.hours = en->tm_hour;
	t2.minutes = en->tm_min;
	t2.seconds = en->tm_sec;
	gettimeofday(&time_end, NULL);
	t2.milliseconds = time_end.tv_usec/1000;
	printf("END TIME = %d:%d:%d:%03ld\n",t2.hours,t2.minutes,t2.seconds,  t2.milliseconds);
	timeDifference(t2,t1, &diff);
	printf("TIME = %d:%d:%d:%03ld\n",diff.hours,diff.minutes,diff.seconds,diff.milliseconds);
	//close the connection with client
	close(connID);
}

/*
 * dataRedundancyClient(): This function is used to handle data redundancy among servers.
 * It establishes connections and sends the files to other server instances.
 * It replicates the upload functionality of client and works like a client to other server instances.
 */
void dataRedundancyClient(char *user) {
	int c, w, success;
	char buf[2];
	time_t start, end;

	//Show the start time of data redundancy
	start = time(0);
	(void) printf("dataRedundancyClient : Start Time %s\n", ctime(&start));

	//function call to establish connection to back-up server for data redundancy
	c = connectTCP(UPLOAD_PORT);
	if (c < 0)
		fatal("Connection to server 2 failed");

	//write username to check for validity
	w = write(c, user, strlen(user));
	w = read(c, buf, 1);
	buf[1] = '\0';
	if (w < 0)
		fatal("dataRedundancyClient : username failed");
	success = atoi(buf);
	if (success == TRUE)
		printf("dataRedundancyClient : User accept success\n");
	else
		fatal("dataRedundancyClient : User accept failed");

	//function which performs data redundancy between servers
	fileTransferRedundancy(c, user);

	//Display the end time and calculate the total time taken for data redundancy
	end = time(0);
	(void) printf("dataRedundancyClient : End Time %s\n", ctime(&end));
	(void) printf("dataRedundancyClient : Total Time %lu\n", (end - start));
}

/*
 * connectTCP(): This function is used to establish connection to other server instances.
 * This is called as part of data redundancy operation.
 */
int connectTCP(int port) {
	int c, s;
	struct sockaddr_in server_addr;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		fatal("socket failed");

	//Mention the server details to make connection
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr("54.187.101.41");
	server_addr.sin_port = htons(port);

	c = connect(s, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (c < 0)
		fatal("connect failed");

	//Return the socket after connection establishment
	return s;
}

/*
 * fileTransferRedundancy(): This function handles the data redundancy operation among servers.
 * This opens the user playlist and sends the updated list of files to the other servers.
 */
void fileTransferRedundancy(int socket, char *user) {
	const char *delims = "~";
	int w, r, changedir, success = 0, file_count = 0, i = 0, bytes, count = 0, succ;
	char file_name[108], noOfFiles[2], dname[108] = "", check[2], buf[BUF_SIZE];
	char b[2], cwd[BUF_SIZE], metaDataDownload[3000], fileSize[sizeof(long)];
	struct stat stbuf;
	struct dirent * entry;
	char *fileNameArrayDownload[MAX_FILES], *fileSizeArrayDownload[MAX_FILES];
	long lSize = 0L;
	DIR * dirp;
	FILE *fdTx;

	//Switch to root folder containing all the playlists
	selectdir();

	//form the path to user playlist
	getcwd(cwd, sizeof(cwd));
	strcat(cwd, "/");
	strcat(cwd, user);
	changedir = chdir(cwd);
	if (changedir < 0)
		fatal("dataRedundancy : Change Directory in download failed"); // Playlist to be tranferred

	//loop through the user directory and calculate number of files to be transferred
	dirp = opendir(cwd);
	while ((entry = readdir(dirp)) != NULL) {
		if ((strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) { /* If the entry is a regular file */
			file_count++;
		}
	}

	//set the metadata to blank
	memset(metaDataDownload, 0, 3000);
	memset(noOfFiles, 0, 2);
	//append the number of files just like in client
	sprintf(noOfFiles, "%d", file_count);
	strcpy(metaDataDownload, noOfFiles);
	strcat(metaDataDownload, delims);

	//Loop through the directory and form the metadata to be used for data redundancy
	dirp = opendir(cwd);
	while ((entry = readdir(dirp)) != NULL) {
		//Check if the entry is a regular file
		if ((strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) {
			fileNameArrayDownload[i] = entry->d_name;
			strcat(metaDataDownload, fileNameArrayDownload[i]);
			strcat(metaDataDownload, delims);

			//To read the file size
			int fdes;
			fdes = open(fileNameArrayDownload[i], O_RDONLY);
			if (fdes < 0)
				fatal("File des open failed");
			else
				printf("File des %d open success\n", i);

			//Use fstat to read size as it is better for binary files
			if ((fstat(fdes, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
				fatal("Error in fstat");
			}

			lSize = stbuf.st_size;
			close(fdes);

			memset(fileSize, 0, sizeof(long));
			sprintf(fileSize, "%ld", lSize);
			fileSizeArrayDownload[i] = (char *) malloc(sizeof(lSize));
			strcpy(fileSizeArrayDownload[i], fileSize);
			strcat(metaDataDownload, fileSizeArrayDownload[i]);
			strcat(metaDataDownload, delims);
			i++;
		}
	}

	printf("The Metadata is %s : \n", metaDataDownload);

	//write the metadata to back-up server
	w = write(socket, metaDataDownload, 3000);
	if (w < 0)
		fatal("dataRedundancy : Metadata Error\n");

	//check for ACK from back-up server
	w = read(socket, check, 1);
	check[1] = '\0';
	success = atoi(check);
	if (success == TRUE)
		printf("dataRedundancy : After metaData call success\n");
	else
		fatal("dataRedundancy : After metaData call failed");

	//Loop through the files and send them one by one to back-up server
	while (count < file_count) {
		fdTx = fopen(fileNameArrayDownload[count], "rb");
		if (fdTx < 0)
			fatal("dataRedundancy : File open failed");
		else
			printf("dataRedundancy : File open success\n");

		while ((bytes = fread(buf, BUF_SIZE, 1, fdTx)) >= 0) {
			w = write(socket, buf, BUF_SIZE);
			//condition to check if end of file has been reached
			if (bytes == 0 && feof(fdTx)) {
				break;
			}
		}
		fclose(fdTx);
		//check for ACK from back-up server for success of file transfer
		w = read(socket, b, 1);
		b[1] = '\0';
		succ = atoi(b);
		if (succ > 0)
			printf("dataRedundancy : File [%d] Transfer Result : %d\n", count,
					succ);
		else
			fatal("dataRedundancy : File transfer failed");
		count++;
	}
}

/*
 * selectdir(): This function is used to change into root storage to store playlists on server
 */
int selectdir() {
	int changedir;
	char *p = getenv("USER");
	if (p == NULL)
		return -1;
	printf("%s\n", p);

	char direc[PATH_MAX] = "/home/";
	strcat(direc, p);
	strcat(direc, "/Music");
	changedir = chdir(direc);
	if (changedir < 0)
		fatal("Change Directory in download failed");
	return 0;
}

/*
 * handleDownload(): This function is used to handle the download functionality.
 * It establishes connection to client, sends playlist information
 * and sends the files from local storage.
 */
int handleDownload(int connID) {
	int r, i, len, file_count = 0;
	char cwd[PATH_MAX], b[2], dname[BUF_SIZE] = "";
	DIR * dirp;
	struct dirent * entry;

	//switch to the root directory
	selectdir();

	// Program for listing the directories
	getcwd(cwd, sizeof(cwd));

	//loop through the directory and get all the playlist names
	dirp = opendir(cwd);
	while ((entry = readdir(dirp)) != NULL) {
		//condition to check for directory
		if (entry->d_type == DT_DIR
				&& (strcmp(entry->d_name, ".") != 0
						&& strcmp(entry->d_name, "..") != 0)) {
			strcat(dname, entry->d_name);
			strcat(dname, "~");
			file_count++;

		}
	}
	//Check done to see if there are no playlists. Send appropriate error message
	if (file_count == 0){
		printf("There are no playlists. Please upload some playlist!!!");
		write(connID, "-1", 1);
		return 0;
	}
	else
		write(connID, "1", 1);

	r = read(connID, b, 1);

	dname[strlen(dname)] = '\0';
	len = strlen(dname);
	//send the list of playlists to client
	r = write(connID, dname, len);
	r = read(connID, b, 1);
	b[1] = '\0';
	//call function to send files to client
	fileTransferDownload(connID);
	return 0;
}

/*
 * fileTransferDownload():This function handles the sending of playlist to client.
 * It communicates with the client and handles file download.
 */
void fileTransferDownload(int connID) {
	char metaDataDownload[3000], fileSize[sizeof(long)];
	const char *delims = "~";
	char file_name[108], noOfFiles[2], cwd[BUF_SIZE], buf[BUF_SIZE], b[2], dname[108] = "";
	struct stat stbuf;
	DIR * dirp;
	struct dirent * entry;
	int file_count = 0, i = 0, bytes, count = 0, succ, w, r, changedir;
	char *fileNameArrayDownload[MAX_FILES], *fileSizeArrayDownload[MAX_FILES];
	long lSize = 0L;
	FILE *fd, *fdTx;
	time_t t_start, t_end;
	struct tm *st,*en;
	struct timeval time_ms,time_end;
	struct TIME t1,t2,diff;

	//get the playlist name sent by the user
	memset(file_name, 0, 108);
	r = read(connID, file_name, sizeof(file_name));
	printf("The playlist transferred : %s \n ", file_name);
	write(connID, "1", 1);
	selectdir();

	getcwd(cwd, sizeof(cwd));
	strcat(cwd, "/");
	strcat(cwd, file_name);
	changedir = chdir(cwd);
	if (changedir < 0)
		fatal("Change Directory in download failed");

	//Loop through the playlist to get the count of files
	dirp = opendir(cwd);
	while ((entry = readdir(dirp)) != NULL) {
		// If the entry is a regular file
		if ((strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) {
			file_count++;
		}
	}

	//initialize metaData to blank
	memset(metaDataDownload, 0, 3000);
	memset(noOfFiles, 0, 2);
	sprintf(noOfFiles, "%d", file_count);

	//append number of files to start of metadata
	strcpy(metaDataDownload, noOfFiles);
	strcat(metaDataDownload, delims);

	//Loop through the directory and form the metadata to be used for download
	dirp = opendir(cwd);
	while ((entry = readdir(dirp)) != NULL) {
		if ((strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) { /* If the entry is a regular file */
			fileNameArrayDownload[i] = entry->d_name;
			//append filename to metadata
			strcat(metaDataDownload, fileNameArrayDownload[i]);
			strcat(metaDataDownload, delims);

			//To read the file size
			int fdes;
			fdes = open(fileNameArrayDownload[i], O_RDONLY);
			if (fdes < 0)
				fatal("File des open failed");
			else
				printf("File des %d open success\n", i);
			//Use fstat as it is better for binary files
			if ((fstat(fdes, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
				fatal("Error in fstat");
			}

			lSize = stbuf.st_size;
			printf("File Size : %ld\n", lSize);
			close(fdes);

			memset(fileSize, 0, sizeof(long));
			sprintf(fileSize, "%ld", lSize);
			fileSizeArrayDownload[i] = (char *) malloc(sizeof(lSize));
			//append filesize to metadata
			strcpy(fileSizeArrayDownload[i], fileSize);
			strcat(metaDataDownload, fileSizeArrayDownload[i]);
			strcat(metaDataDownload, delims);
			i++;
		}
	}

	printf("The Metadata is %s : \n", metaDataDownload);

	char *success;
	//write the metadata to client
	w = write(connID, metaDataDownload, 3000);
	if (w < 0)
		printf("After metaData call failed\n ");
	//read ACK from client
	r = read(connID, success, sizeof(file_name));

	//calculate the time taken
	t_start = time(NULL);
	st = localtime(&t_start);
	t1.hours = st->tm_hour;
	t1.minutes = st->tm_min;
	t1.seconds = st->tm_sec;
	gettimeofday(&time_ms, NULL);
	t1.milliseconds =  time_ms.tv_usec/1000;
	printf("START TIME = %d:%d:%d:%03ld\n",t1.hours,t1.minutes,t1.seconds,t1.milliseconds);

	//loop through each file in the playlist and send to client
	while (count < file_count) {
		fdTx = fopen(fileNameArrayDownload[count], "rb");
		if (fdTx < 0)
			fatal("File open failed");
		else
			printf("File open success\n");

		//loop to read the file contents 1024 bytes at a time
		while ((bytes = fread(buf, BUF_SIZE, 1, fdTx)) >= 0) {
			w = write(connID, buf, BUF_SIZE);
			//condition to check if end of file has been reached
			if (bytes == 0 && feof(fdTx)) {
				break;
			}
		}
		fclose(fdTx);
		//check for ACK from client for success of file transfer
		w = read(connID, b, 1);
		b[1] = '\0';
		succ = atoi(b);
		if (succ > 0)
			printf("File [%d] Transfer Result : %d\n", count, succ);
		else
			fatal("File transfer failed");
		count++;
	}
	//calculate the end time and measure the total time taken
	t_end = time(NULL);
	en = localtime(&t_end);
	t2.hours = en->tm_hour;
	t2.minutes = en->tm_min;
	t2.seconds = en->tm_sec;
	gettimeofday(&time_end, NULL);
	t2.milliseconds = time_end.tv_usec/1000;
	printf("END TIME = %d:%d:%d:%03ld\n",t2.hours,t2.minutes,t2.seconds,  t2.milliseconds);
	timeDifference(t2,t1, &diff);
	printf("TIME = %d:%d:%d:%03ld\n",diff.hours,diff.minutes,diff.seconds,diff.milliseconds);
}

/*
 * timeDifference(): This function is used to calculate the time taken between file transfers.
 * It provides precision in the ofer of milliseconds.
 */
void timeDifference(struct TIME t1, struct TIME t2, struct TIME *differ){
	if(t2.milliseconds > t1.milliseconds){
		--t1.seconds;
		t1.milliseconds += 1000;
	}
	differ->milliseconds = t1.milliseconds-t2.milliseconds;
	if(t2.seconds>t1.seconds){
	--t1.minutes;
	t1.seconds += 60;
	}
	differ->seconds=t1.seconds-t2.seconds;
	if(t2.minutes>t1.minutes){
	--t1.hours;
	t1.minutes += 60;
	}
	differ->minutes=t1.minutes-t2.minutes;
	differ->hours=t1.hours-t2.hours;
}

/*
 * prstats(): This function is used to display the statistics at regular intervals.
 */
void prstats(void) {
	time_t now;

	while (1) {
		(void) sleep(INTERVAL);

		(void) pthread_mutex_lock(&stats.st_mutex);
		now = time(0);
		(void) printf("--- %s", ctime(&now));
		(void) printf("%-32s: %u\n", "Current connections", stats.st_concount);
		(void) printf("%-32s: %u\n", "Completed connections",
				stats.st_contotal);
		if (stats.st_contotal) {
			(void) printf("%-32s: %.2f (secs)\n",
					"Average complete connection time",
					(float) stats.st_contime / (float) stats.st_contotal);
			(void) printf("%-32s: %.2f\n", "Average byte count",
					(float) stats.st_bytecount
							/ (float) (stats.st_contotal + stats.st_concount));
		}
		(void) printf("%-32s: %lu\n\n", "Total byte count", stats.st_bytecount);
		(void) printf("%-32s: %.2f (secs)\n\n", "Total connection time",
				(float) stats.st_contime);
		(void) pthread_mutex_unlock(&stats.st_mutex);
	}
}

/*
 * Generic function that is used to handle all errors
 */
void fatal(char *string) {
	printf("%s\n", string);
	exit(EXIT_FAILURE);
}
