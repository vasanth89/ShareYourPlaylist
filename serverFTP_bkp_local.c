#include "common.h"

void prstats(void);
int passiveTCP(int port);
void checkUserDir(int socket);
void fileTransfer(int connID);
void fileTransferDownload(int connID);
int handleUpload(int connID);
int handleDownload(int connID);
int selectdir();
int dataRedundancyClient(char *user);
int connectTCP(int port);
void fileTransferRedundancy(int socket, char *user);
void timeDifference(struct TIME t1, struct TIME t2, struct TIME *differ);
void fatal(char *string);
//Check for user authenticity added by Vasanth Viswanathan. class ID : 38
int checkUserAuthenticity(int socket);

struct {
	pthread_mutex_t st_mutex;
	unsigned int st_concount;
	unsigned int st_contotal;
	unsigned long st_contime;
	unsigned long st_bytecount;
} stats;

struct stat st;
char username[PATH_MAX];
char ACK[] = "1";
char NACK[] = "-1";


pthread_t th;
pthread_attr_t ta;

//TIME FUNCTION - PERFORMANCE
struct TIME{
  int seconds;
  int minutes;
  int hours;
  long int milliseconds;
};




int main() {

	struct sockaddr_in sockserv;
	unsigned int alen;
	int upload_sock, download_sock, i, slave_sock;
	int fds[2];
	int *fd = NULL;
	struct sockaddr *addr;
	socklen_t *addrlen;

	upload_sock = passiveTCP(UPLOAD_PORT_BKP);
	download_sock = passiveTCP(DOWNLOAD_PORT_BKP);

	printf("Upload sock : %d\n", upload_sock);
	printf("Download sock : %d\n", download_sock);

	//------ Thread Initialization-------------
	(void) pthread_attr_init(&ta);
	(void) pthread_attr_setdetachstate(&ta, PTHREAD_CREATE_DETACHED);
	(void) pthread_mutex_init(&stats.st_mutex, 0);

	fds[0] = upload_sock;
	fds[1] = download_sock;

	while (1) {
		slave_sock = network_accept_any(fds, 2, &fd);

		//Based on the file descriptor, Upload or Download are selected.
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

//--------------Selects the available client network connection--------------------

int network_accept_any(int fds[], unsigned int count, int **master) {
	fd_set readfds;
	int maxfd, fd;
	unsigned int i;
	int status, accept_sock;
	struct sockaddr_in sockserv;
	unsigned int alen;
	*master = malloc(sizeof(int));

	FD_ZERO(&readfds);
	maxfd = -1;
	for (i = 0; i < count; i++) {
		FD_SET(fds[i], &readfds);
		if (fds[i] > maxfd)
			maxfd = fds[i];
	}
	status = select(maxfd + 1, &readfds, NULL, NULL, NULL);
	if (status < 0)
		return INVALID_SOCKET;
	fd = INVALID_SOCKET;
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
		printf("**Master : %d\n", **master);
		accept_sock = accept(fd, (struct sockaddr *) &sockserv, &alen);
		return accept_sock;
	}
}

//---------- Connection between the port number and the service is implemented (Below function)----------------

int passiveTCP(int port) {

	/**
	 * Support for IPv6 added by Vasanth Viswanathan. class ID : 38 - START
	 */
	//struct sockaddr_in sockserv, sockclient;
	int server_sock, bind_server, listen_server, connID, on = 1;
	struct sockaddr_in6 sockserv;

	//sockserv.sin_family = AF_INET;
	sockserv.sin6_family = AF_INET6;
	//sockserv.sin_addr.s_addr = INADDR_ANY;
	sockserv.sin6_port = htons(port);
	if(inet_pton(AF_INET6, "::1" , &sockserv.sin6_addr) <=0){
		fatal("inet_pton error");
	}

	//server_sock = socket(AF_INET, SOCK_STREAM, 0);
	server_sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (server_sock < 0)
		fatal("Creation of Master Socket failed");
	/**
	 * Support for IPv6 added by Vasanth Viswanathan. class ID : 38 - END
	 */

	setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on));

	bind_server = bind(server_sock, (struct sockaddr *) &sockserv,
			sizeof(sockserv));

	if (bind_server < 0)
		fatal("Binding of Master failed");

	listen_server = listen(server_sock, NOOFCONN);

	if (listen_server < 0)
		fatal("Listen failed");

	return server_sock;
}

//----------The Upload function  ----------

int handleUpload(int connID) {
	time_t start, end;
	start = time(0);
	(void) printf("Upload : Start Time %s\n", ctime(&start));
	(void) pthread_mutex_lock(&stats.st_mutex);
	stats.st_concount++;
	(void) pthread_mutex_unlock(&stats.st_mutex);
	checkUserDir(connID);
	fileTransfer(connID);
	end = time(0);
	(void) printf("Upload : End Time %s\n", ctime(&end));
	(void) printf("Upload : Total Time %lu\n", (end - start));
	(void) pthread_mutex_lock(&stats.st_mutex);
	stats.st_contime += end - start;
	stats.st_concount--;
	stats.st_contotal++;
	(void) pthread_mutex_unlock(&stats.st_mutex);

	/*pthread_t thread1;
	pthread_create(&thread1, NULL,(void * (*)(void *))dataRedundancyClient, username);
	pthread_join(thread1, NULL);*/
	return 0;
}

//-------------Backup Server for the Upload Function-------------

int dataRedundancyClient(char *user) {
	int c, w, success;
	char buf[2];
	time_t start, end;
	start = time(0);
	(void) printf("dataRedundancyClient : Start Time %s\n", ctime(&start));
	c = connectTCP(8000);
	if (c < 0)
		fatal("Connection to server 2 failed");

	w = write(c, user, strlen(user));

	w = read(c, buf, 1);
	buf[1] = '\0';
	if (w < 0)
		fatal("dataRedundancyClient : username failed");
	success = atoi(buf);
	if (success != TRUE)
		fatal("dataRedundancyClient : User accept failed");
	fileTransferRedundancy(c, user);
	end = time(0);
	(void) printf("dataRedundancyClient : End Time %s\n", ctime(&end));
	(void) printf("dataRedundancyClient : Total Time %lu\n", (end - start));
	return 0;
}

//---------------Connection between the Two Servers------------------

int connectTCP(int port) {
	/**
	 * Support for IPv6 added by Vasanth Viswanathan. class ID : 38 - START
	 */
	//struct sockaddr_in server_addr;
	struct sockaddr_in6 server_addr;
	int c, s;
	//s = socket(AF_INET, SOCK_STREAM, 0);
	s = socket(AF_INET6, SOCK_STREAM, 0);
	if (s < 0)
		fatal("socket failed");

	memset(&server_addr, 0, sizeof(server_addr));
	//server_addr.sin_family = AF_INET;
	server_addr.sin6_family = AF_INET6;
	//server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	if(inet_pton(AF_INET6, "::1" , &server_addr.sin6_addr) <=0){
			fatal("inet_pton error");
	}
	//server_addr.sin_port = htons(port);
	server_addr.sin6_port = htons(port);
	/**
	 * Support for IPv6 added by Vasanth Viswanathan. class ID : 38 - END
	 */

	c = connect(s, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (c < 0)
		fatal("connect failed");

	return s;
}

//------------------ File Transfer to the Backup Server----------------------

void fileTransferRedundancy(int socket, char *user) {
	char metaDataDownload[3000], fileSize[sizeof(long)];
	const char *delims = "~";
	int w, r, changedir;
	char file_name[108], noOfFiles[2];
	char cwd[BUF_SIZE] = "/home/";//Music2";
	char *p = getenv("USER");
	if (p == NULL)
		fatal("No USERNAME!!!\n");
	strcat(cwd, p);
	strcat(cwd, "/Music3");
	struct stat stbuf;
	int success = 0;
	struct dirent * entry;
	char dname[108] = "";
	int file_count = 0;
	int i = 0;
	char *fileNameArrayDownload[MAX_FILES];
	char *fileSizeArrayDownload[MAX_FILES];
	long lSize = 0L;
	char check[2];

	strcat(cwd, "/");
	strcat(cwd, user);
	changedir = chdir(cwd);
	if (changedir < 0)
		fatal("dataRedundancy Server : Change Directory in download failed"); // Playlist to be tranferred


	//****************************************** - METADATA CONSTRUCTION - ******************************************************
	DIR * dirp;

	dirp = opendir(cwd); /* There should be error handling after this */
	while ((entry = readdir(dirp)) != NULL) {
		if ((strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) { /* If the entry is a regular file */
			file_count++;
		}
	}
	printf("dataRedundancy Server :The no of files inside the playlist : %d\n ", file_count);
	memset(metaDataDownload, 0, 3000);
	memset(noOfFiles, 0, 2);
	sprintf(noOfFiles, "%d", file_count);
	strcpy(metaDataDownload, noOfFiles);
	strcat(metaDataDownload, delims);

	//Find the name and the size of the file

	FILE *fd;

	dirp = opendir(cwd);
	while ((entry = readdir(dirp)) != NULL) {
		if ((strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) { /* If the entry is a regular file */
			fileNameArrayDownload[i] = entry->d_name;
			strcat(metaDataDownload, fileNameArrayDownload[i]);
			strcat(metaDataDownload, delims);

			//To read the file size
			int fdes;
			fdes = open(fileNameArrayDownload[i], O_RDONLY);
			if (fdes < 0)
				fatal("dataRedundancy Server : File cannot open failed!!!");

			//Use fstat as it is better for binary files
			if ((fstat(fdes, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
				fatal("dataRedundancy Server : Error in calculating statistics!!!");
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

	w = write(socket, metaDataDownload, 3000);
	if (w < 0)
		fatal("dataRedundancy Server : Metadata Error!!!\n");

	w = read(socket, check, 1);
	check[1] = '\0';
	success = atoi(check);

	//-----------------------------Metadata Construction ends here------------------------------------------

	//-----------------------------Opening the Directory for transferring the files---------------------------------------

	int bytes, count = 0, succ;
	char buf[BUF_SIZE], b[2];
	FILE *fdTx;

	while (count < file_count) {
		fdTx = fopen(fileNameArrayDownload[count], "rb");
		if (fdTx < 0)
			fatal("dataRedundancy Server : File open failed!!!");

		while ((bytes = fread(buf, BUF_SIZE, 1, fdTx)) >= 0) {
			w = write(socket, buf, BUF_SIZE);

			if (bytes == 0 && feof(fdTx)) {
				break;
			}
		}
		fclose(fdTx);
		w = read(socket, b, 1);
		b[1] = '\0';
		succ = atoi(b);
		if (succ > 0)
			printf("dataRedundancy Server : File [%d] Transfer Result : %d\n", count,succ);
		else
			fatal("dataRedundancy Server : File transfer failed!!!");
		count++;
	}
}

//--------------- Checks for the Existance of the User Directory else creates directory------------------
void checkUserDir(int socket) {
	char newdir[PATH_MAX], cwd[PATH_MAX];
	char check[PATH_MAX] = "/home/";
	char *p = getenv("USER");
		if (p == NULL)
			fatal("USER cannot be found");//return -1;

	strcat(check, p);
	strcat(check, "/Music3");

	int checkDir, changedir, r;
	bzero(username, PATH_MAX);
	r = read(socket, username, sizeof(username));
	username[strlen(username)] = '\0';

	printf("Username is :%s\n", username);

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
	r = write(socket, ACK, 1);

}

//------------------ File Transfer for the Upload Service---------------------
void fileTransfer(int connID) {
	char metaData[3000];
	char *fileNameArray[MAX_FILES];
	char *fileSizeArray[MAX_FILES];
	char *str1, *result, *nextString;
	const char *delims = "~";
	int anamolyCount = 0;

	char revbuf[BUF_SIZE], backup[BUF_SIZE], success[1] = "1";
	int fr_block_sz = 0, remain = 0, counter = 0, i = 1, noOfFiles = 0, count =
			0, w, j;
	unsigned long lSize = 0L, sizeCheck = 0L;

	w = read(connID, metaData, sizeof(metaData));

	metaData[strlen(metaData)] = '\0';
	printf("Metadata : %s\n", metaData);

	for (j = 0, str1 = metaData;; j++, str1 = NULL) {
		result = strtok_r(str1, delims, &nextString);
		if (result == NULL)
			break;
		if (j == 0) {
			noOfFiles = atoi(result);
			printf("Number of files : %d\n", noOfFiles);
			continue;
		}
		if (j % 2 != 0) {
			fileNameArray[j / 2] = result;
			printf("Filename : %s\n", fileNameArray[j / 2]);
		} else {
			fileSizeArray[j / 2 - 1] = result;
			printf("Filesize : %s\n", fileSizeArray[j / 2 - 1]);
		}
	}
	w = write(connID, success, 1);

	/*
	 * PERFORMANCE TIME - Starts here
	 */
	time_t t_start, t_end;
	struct tm *st,*en;
	struct timeval time_ms,time_end;
	struct TIME t1,t2,diff;


	//Performance for the Server Side
	t_start = time(NULL);
	st = localtime(&t_start);
	t1.hours = st->tm_hour;
	t1.minutes = st->tm_min;
	t1.seconds = st->tm_sec;
	gettimeofday(&time_ms, NULL);
	t1.milliseconds =  time_ms.tv_usec/1000;
	printf("START TIME = %d:%d:%d:%03ld\n",t1.hours,t1.minutes,t1.seconds,t1.milliseconds);

	while (count < noOfFiles) {

		printf("Filename(%d) is :%s\n", count, fileNameArray[count]);
		printf("Filesize(%d) is :%s\n", count, fileSizeArray[count]);

		lSize = atol(fileSizeArray[count]);
		counter = (int) lSize / BUF_SIZE;
		printf("Counter value : %d\n", counter);

		FILE *fr = fopen(fileNameArray[count], "ab");
		if (fr == NULL)
			fatal("File cannot be opened on server");
		printf("File opened\n");

		while (i <= counter) {
			fr_block_sz = read(connID, revbuf, BUF_SIZE);
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

		if (sizeCheck < lSize) {
			remain = (int) lSize - (int) sizeCheck;
			fr_block_sz = read(connID, revbuf, remain);
			fwrite(revbuf, remain, 1, fr);
			bzero(revbuf, BUF_SIZE);
		}

		fclose(fr);
		w = write(connID, success, 1);
		(void) pthread_mutex_lock(&stats.st_mutex);
		stats.st_bytecount += lSize;
		(void) pthread_mutex_unlock(&stats.st_mutex);
		printf("Anamoly Count for file[%d] : %d\n",count,anamolyCount);
		sizeCheck = 0L;
		lSize = 0L;
		i = 1;
		remain = 0;
		fr_block_sz = 0;
		anamolyCount = 0;
		count++;
	}

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
	/*
	 * PERFORMANCE TIME - ends here
	 */
	close(connID);
}

/*
 * ----------------------- selectdir function is used to select the directory ---------------------------------------
 */
int selectdir() {
	int changedir;
	char *p = getenv("USER");
	if (p == NULL)
		return -1;
	printf("The Username is %s\n", p);

	char direc[PATH_MAX] = "/home/";
	strcat(direc, p);
	strcat(direc, "/Music3");
	changedir = chdir(direc);
	if (changedir < 0)
		fatal("Change Directory in download failed");
	return 0;
}


//------------------Service : Download Function --------------------
int handleDownload(int connID) {
	int r, w, i, len, file_count = 0;
	char cwd[PATH_MAX], b[2];
	/**
	 * Check for user authenticity added by Vasanth Viswanathan. class ID : 38 - START
	 */
	int check = checkUserAuthenticity(connID);
	if (check == -1)
		return 0;
	/**
	 * Check for user authenticity added by Vasanth Viswanathan. class ID : 38 - END
	 */

	selectdir();

	getcwd(cwd, sizeof(cwd));
	printf("Current directory is %s \n ", cwd);

	DIR * dirp;
	struct dirent * entry;
	char dname[BUF_SIZE] = "";

	dirp = opendir(cwd);
	while ((entry = readdir(dirp)) != NULL) {
		if (entry->d_type == DT_DIR
				&& (strcmp(entry->d_name, ".") != 0
						&& strcmp(entry->d_name, "..") != 0)) { /* If the entry is a regular file */
			strcat(dname, entry->d_name);
			strcat(dname, "~");
			file_count++;

		}
	}
	if (file_count == 0){
		printf("There are no playlist. Please upload some playlist!!!");
		w = write(connID, "-1", 1);
		return 0;
	}
	else
		write(connID, "1", 1);

	r = read(connID, b, 1);
	dname[strlen(dname)] = '\0';
	len = strlen(dname);
	w = write(connID, dname, len);
	r = read(connID, b, 1);
	b[1] = '\0';
	fileTransferDownload(connID);
	return 0;
}

/**
 * Check for user authenticity added by Vasanth Viswanathan. class ID : 38 - START
 */
//--------------- Checks for the Existance of the User Directory------------------
int checkUserAuthenticity(int socket) {
	char newdir[PATH_MAX], cwd[PATH_MAX];
	char check[PATH_MAX] = "/home/";
	char *p = getenv("USER");
	if (p == NULL)
		fatal("USER cannot be found");//return -1;

	strcat(check, p);
	strcat(check, "/Music2");

	int changedir;
	bzero(username, PATH_MAX);
	read(socket, username, sizeof(username));
	username[strlen(username)] = '\0';

	printf("Username is :%s\n", username);

	changedir = chdir(check);
	if (changedir < 0)
		fatal("Change Directory 0 failed");

	getcwd(cwd, sizeof(cwd));

	strcpy(newdir, cwd);
	strcat(newdir, "/");
	strcat(newdir, username);

	changedir = chdir(newdir);
	if (changedir < 0){
		write(socket, NACK, 1);
		return -1;
	}
	write(socket, ACK, 1);
	return 0;

}
/**
 * Check for user authenticity added by Vasanth Viswanathan. class ID : 38 - END
 */

//--------------------File Transfer function for the Download Service-----------------------
void fileTransferDownload(int connID) {
	char metaDataDownload[3000], fileSize[sizeof(long)];
	const char *delims = "~";
	int w, r, changedir;
	char file_name[108], noOfFiles[2];
	char cwd[BUF_SIZE];
	struct stat stbuf;

	memset(file_name, 0, 108);
	r = read(connID, file_name, sizeof(file_name));
	printf("The playlist transferred : %s \n ", file_name);

	selectdir();

	getcwd(cwd, sizeof(cwd));
	strcat(cwd, "/");
	strcat(cwd, file_name);
	changedir = chdir(cwd);
	if (changedir < 0)
	{
		fatal("Change Directory in download failed");
		write(connID, "0", 1);
	}
	else{
		write(connID, ACK, 1);
	}
	printf("Address after giving the playlist is %s \n ", cwd);

	//****************************************** - METADATA CONSTRUCTION - ******************************************************
	DIR * dirp;
	struct dirent * entry;
	char dname[108] = "";
	int file_count = 0;
	dirp = opendir(cwd); /* There should be error handling after this */
	while ((entry = readdir(dirp)) != NULL) {
		if ((strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) { /* If the entry is a regular file */
			file_count++;
		}
	}
	printf("The no of files inside the playlist : %d\n ", file_count);
	memset(metaDataDownload, 0, 3000);
	memset(noOfFiles, 0, 2);
	sprintf(noOfFiles, "%d", file_count);
	strcpy(metaDataDownload, noOfFiles);
	strcat(metaDataDownload, delims);

	//Find the name and the size of the file
	int i = 0;
	char *fileNameArrayDownload[MAX_FILES];
	char *fileSizeArrayDownload[MAX_FILES];
	long lSize = 0L;
	FILE *fd;

	dirp = opendir(cwd);
	while ((entry = readdir(dirp)) != NULL) {
		if ((strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)) { /* If the entry is a regular file */
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
			strcpy(fileSizeArrayDownload[i], fileSize);
			strcat(metaDataDownload, fileSizeArrayDownload[i]);
			strcat(metaDataDownload, delims);
			i++;
		}
	}

	printf("The Metadata is %s : \n", metaDataDownload);

	char *success;
	w = write(connID, metaDataDownload, 3000);
	if (w < 0)
		printf("Error!!!\n ");
	printf("The Metadata successfully sent %d \n", r);
	r = read(connID, success, sizeof(file_name));
	printf("Bytes read %d \n", r);

	//-----------------------------Metadata ends here------------------------------------------

	//-----------------------------Opening the Directory---------------------------

	int bytes, count = 0, succ;
	char buf[BUF_SIZE], b[2];
	FILE *fdTx;

	/*
	 *-------STATISTICS - TIME Function - starts here------
	 */
	//Time Function Parameters
	time_t t_start, t_end;
	struct tm *st,*en;
	struct timeval time_ms,time_end;
	struct TIME t1,t2,diff;


	//Performance for the Server Side
	t_start = time(NULL);
	st = localtime(&t_start);
	t1.hours = st->tm_hour;
	t1.minutes = st->tm_min;
	t1.seconds = st->tm_sec;
	gettimeofday(&time_ms, NULL);
	t1.milliseconds =  time_ms.tv_usec/1000;
	printf("START TIME = %d:%d:%d:%03ld\n",t1.hours,t1.minutes,t1.seconds,t1.milliseconds);

	while (count < file_count) {
		fdTx = fopen(fileNameArrayDownload[count], "rb");
		if (fdTx < 0)
			fatal("File open failed");
		else
			printf("File open success\n");

		while ((bytes = fread(buf, BUF_SIZE, 1, fdTx)) >= 0) {
			w = write(connID, buf, BUF_SIZE);
			if (bytes == 0 && feof(fdTx)) {
				printf("End of file reached\n");
				break;
			}
		}
		fclose(fdTx);
		w = read(connID, b, 1);
		printf("->Bytes read (file) : %d\n", w);
		b[1] = '\0';
		succ = atoi(b);
		if (succ > 0)
			printf("File [%d] Transfer Result : %d\n", count, succ);
		else
			fatal("File transfer failed");
		count++;
	}

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

	/*
	 * STATISTICS - TIME Function - ends here
	 */
}

//--------------------Statistics on the server side---------------------------------
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

//----------Calculate the Time difference----------------

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

void fatal(char *string) {
	printf("%s\n", string);
	exit(EXIT_FAILURE);
}
