#include "common.h"

int connectTCP(int port, int choice);
void userSetup(const char *user, int socket);
void fileMetadata(int numberOfFiles, int socket);
void fileTransfer(int numberOfFiles, int socket);
void fileTransferDownload(int socket, char *send);
void fatal(char *string);
void handleDownload(char *user);
void handleUpload(char *user);
void timeDifference(struct TIME t1, struct TIME t2, struct TIME *differ);

struct timespec s_time, e_time;
char ACK[] = "1";
char NACK[] = "-1";

//TIME FUNCTION - PERFORMANCE
struct TIME{
  int seconds;
  int minutes;
  int hours;
  long int milliseconds;
};

char *fileNameArray[MAX_FILES];
char *fileSizeArray[MAX_FILES];
char metaData[3000];

int main(int argc, char **argv) {
	char *user;
	int option;

	//------------------Enter the service---------------------

	printf("Username : ");
	scanf("%s", user);
	printf("Hello %s\n", user);
	printf("1.Upload\n2.Download\nEnter your option : ");
	scanf("%d", &option);
	printf("You selected option %d\n", option);

	if (option == 1)
		handleUpload(user);
	else if (option == 2)
		handleDownload(user);
	else
		fatal("Invalid option");

}


//----------The Upload Service  ----------

void handleUpload(char *user) {

	int numberOfFiles, s;
	time_t start, end;
	int choice = 1;

	start = time(0);
	(void) printf("Upload : Start Time %s\n", ctime(&start));

	s = connectTCP(UPLOAD_PORT,choice);
	if (s < 0)
		fatal("Upload socket failed");
	userSetup(user, s);

	printf("Enter number of files : ");
	scanf("%d", &numberOfFiles);
	if (numberOfFiles == 0) {
		printf("No files for upload!!\n");
		exit(EXIT_SUCCESS);
	}
	if (numberOfFiles > MAX_FILES)
		fatal("Cannot transfer more than 10 files");

	fileMetadata(numberOfFiles, s);
	fileTransfer(numberOfFiles, s);
	end = time(0);
	(void) printf("Upload : End Time %s\n", ctime(&end));
	(void) printf("Upload : Total Time %lu\n", (end - start));
	close(s);
	exit(EXIT_SUCCESS);
}

//---------------Connection between the Two Servers------------------

int connectTCP(int port, int choice) {

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

	/**
	 * Support for connecting to back-up server added by Vasanth Viswanathan. class ID : 38 - START
	 */
	c = connect(s, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (c < 0){
		printf("Connect Failed!!! Trying backup server\n");
		if(choice == 1)
			server_addr.sin6_port = htons(UPLOAD_PORT_BKP);
		else
			server_addr.sin6_port = htons(DOWNLOAD_PORT_BKP);

		c = connect(s, (struct sockaddr *) &server_addr, sizeof(server_addr));
		if (c < 0)
			fatal("Connect Failed to back-up server as well\n");
	}
	/**
	 * Support for connecting to back-up server added by Vasanth Viswanathan. class ID : 38 - END
	 */

	return s;
}

//---------------------------User Setup----------------------------------

void userSetup(const char *user, int socket) {
	int success, w;
	char buf[2];
	w = write(socket, user, strlen(user));

	w = read(socket, buf, 1);
	buf[1] = '\0';
	if (w < 0)
		fatal("Username failed!!!\n");
	success = atoi(buf);
	if (success == TRUE)
		printf("User accept success!!!\n");
	else
		fatal("User accept failed!!!\n");
}


//-------------------------METADATA CONSTRUCTION----------------------------
void fileMetadata(int numberOfFiles, int socket) {

	char noOfFiles[2], fileSize[10], b[2];
	const char *delims = "~";
	int w, success, j = 0;
	long lSize;
	char *filenames, *str1, *result, *nextString;
	const char *spaces = " ";
	char buf[BUF_SIZE];
	struct stat stbuf;

	memset(metaData, 0, 3000);
	memset(buf, 0, BUF_SIZE);
	memset(noOfFiles, 0, 2);
	sprintf(noOfFiles, "%d", numberOfFiles);
	strcpy(metaData, noOfFiles);
	strcat(metaData, delims);
	FILE *in = fdopen(STDIN_FILENO, "r");
	printf("Enter filenames separated by spaces : ");
	fgets(buf, BUF_SIZE, in);
	fclose(in);
	buf[strlen(buf)] = '\0';

	for (j = 0, str1 = buf; j < numberOfFiles; j++, str1 = NULL) {
		result = strtok_r(str1, spaces, &nextString);
		if (result == NULL)
			break;

		//Written to handle last filename read issue because of new line character
		if (result[strlen(result) - 1] == '\n') {
			result[strlen(result) - 1] = '\0';
		}

		fileNameArray[j] = (char *) malloc(strlen(result));
		if (strlen(result) > 255)
			fatal("Filename is longer than 255 bytes");
		strcpy(fileNameArray[j], result);
		strcat(metaData, fileNameArray[j]);
		strcat(metaData, delims);

		//check for directory - If there is any Fault
		int fdes;
		fdes = open(fileNameArray[j], O_RDONLY);

		if (fdes < 0)
			fatal("File destination open failed");

		//Use fstat as it is better for binary files
		if ((fstat(fdes, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
			fatal("Error in fstat");
		}

		lSize = stbuf.st_size;
		close(fdes);

		memset(fileSize, 0, sizeof(long));
		sprintf(fileSize, "%ld", lSize);
		fileSizeArray[j] = (char *) malloc(sizeof(lSize));
		strcpy(fileSizeArray[j], fileSize);
		strcat(metaData, fileSizeArray[j]);
		strcat(metaData, delims);
	}

	metaData[strlen(metaData)] = '\0';
	printf("The Metadata is : %s\n", metaData);

	w = write(socket, metaData, strlen(metaData));
	w = read(socket, b, 1);
	b[1] = '\0';
	success = atoi(b);
	if (success == TRUE)
		printf("\n");
	else
		fatal("After metaData call failed");
}

//--------------------- File transfer for the Upload function------------------------
void fileTransfer(int numberOfFiles, int socket) {
	int bytes, count = 0, w, success;
	char buf[BUF_SIZE], b[2];
	FILE *fd;

	//Time Function Parameters
	time_t t_start, t_end;
	struct tm *st,*en;
	struct timeval time_ms,time_end;
	struct TIME t1,t2,diff;


	//Performance for the Client Side
	t_start = time(NULL);
	st = localtime(&t_start);
	t1.hours = st->tm_hour;
	t1.minutes = st->tm_min;
	t1.seconds = st->tm_sec;
	gettimeofday(&time_ms, NULL);
	t1.milliseconds =  time_ms.tv_usec/1000;
	printf("START TIME = %d:%d:%d:%03ld\n",t1.hours,t1.minutes,t1.seconds,t1.milliseconds);


	while (count < numberOfFiles) {
		fd = fopen(fileNameArray[count], "rb");
		if (fd < 0)
			fatal("File open failed");
		else
			printf("File open success\n");

		while ((bytes = fread(buf, BUF_SIZE, 1, fd)) >= 0) {
			w = write(socket, buf, BUF_SIZE);
			if (bytes == 0 && feof(fd)) {
				break;
			}
		}
		fclose(fd);
		w = read(socket, b, 1);
		b[1] = '\0';
		success = atoi(b);
		if (success == TRUE)
			printf("File [%d] Transfer Result : %d - Successful\n", count, success);
		else
			fatal("File transfer failed");
		count++;
	}
	//TIME GOES HERE --------------- Performance Calculation-----------------
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
	//TIME ENDS HERE
}


//------------------Service : Download Function --------------------
void handleDownload(char *user) {
	int s, w, success;
	char buf[2], send[108];
	char buffer[BUF_SIZE];
	char *ch;
	int choice = 2;
	s = connectTCP(DOWNLOAD_PORT, choice);
	if (s < 0)
		fatal("Download socket failed");

 	//Check authenticity of user added by Vasanth Viswanathan. class ID : 38
	userSetup(user, s);

	memset(buffer, 0, BUF_SIZE);
	w = read(s, buf, BUF_SIZE);
	success = atoi(buf);

	if(success != TRUE ){
		fatal("There are no playlists!!!\n");
	}
	w = write(s, ACK, 1);
	w = read(s, buffer, BUF_SIZE);
	w = write(s, ACK, 1);

	printf("The Metadata (Playlist names) is %s \n", buffer);
	printf("The Playlist are :\n");

	ch = strtok(buffer, "~");
	while (ch != NULL) {
		printf("%s\n", ch);
		ch = strtok(NULL, "~");
	}
	printf("Enter a playlist : ");
	scanf("%s", send);
	fileTransferDownload(s, send);
	close(s);
	exit(EXIT_SUCCESS);
}

//----------------------- selectdir function is used to select the directory ----------------------------
int selectdir() {
	int changedir;
	char *p = getenv("USER");
	if (p == NULL)
		return -1;

	char direc[PATH_MAX] = "/home/";
	strcat(direc, p);
	strcat(direc, "/Music");
	changedir = chdir(direc);
	if (changedir < 0)
		fatal("Change Directory in download failed");
	return 0;
}


//--------------------File Transfer function for the Download Service-----------------------
void fileTransferDownload(int connID, char *send) {
	int w, rd, success, changedir;
	char metadataDownload[3000];
	char buf[2], cwd[PATH_MAX];
	int anamolyCount = 0;
	time_t start, end;

	start = time(0);
	(void) printf("download : Start Time %s\n", ctime(&start));

	send[strlen(send)] = '\0';
	w = write(connID, send, strlen(send));

	read(connID, buf, 1);
	success = atoi(buf);
	if (success == FALSE)
		fatal("Error in download. No Playlist Exists!!! \n Please Enter a Valid Playlist. \n");


	//----------------------------------Getting the Metadata------------------------------
	selectdir();

	getcwd(cwd, sizeof(cwd));
	printf("Current directory is %s \n ", cwd);
	strcat(cwd, "/");
	strcat(cwd, send);
	printf("CWD: %s\n",cwd);
	changedir = mkdir(cwd, 0777);
	if (0 == changedir)
		printf("Directory is created\n");
	else
		printf("Directory already Exists!!!\n");

	changedir = chdir(cwd);
	if (changedir < 0)
		fatal("Change Directory in download failed");

	memset(metadataDownload, 0, BUF_SIZE);
	rd = read(connID, metadataDownload, BUF_SIZE);
	if (rd < 0)
		fatal("Reading Failed!!! \n");
	w = write(connID, ACK, 1);

	//----------------------------------The filetransfer for download-----------------------
	char *fileNameArrayDownload[MAX_FILES];
	char *fileSizeArrayDownload[MAX_FILES];
	char *str1, *result, *nextString;
	const char *delims = "~";

	char revbuf[BUF_SIZE], backup[BUF_SIZE], succ[1] = "1";
	int fr_block_sz = 0, remain = 0, counter = 0, i = 1, noOfFiles = 0, count =
			0, j;
	unsigned long lSize = 0L, sizeCheck = 0L;

	metadataDownload[strlen(metadataDownload) - 1] = '\0';

	for (j = 0, str1 = metadataDownload;; j++, str1 = NULL) {
		result = strtok_r(str1, delims, &nextString);
		if (result == NULL)
			break;
		if (j == 0) {
			noOfFiles = atoi(result);
			printf("Number of files : %d\n", noOfFiles);
			continue;
		}
		if (j % 2 != 0) {
			fileNameArrayDownload[j / 2] = result;
			printf("Filename : %s\n", fileNameArrayDownload[j / 2]);
		} else {
			fileSizeArrayDownload[j / 2 - 1] = result;
		}
	}


	/*
	 *--------------------------------PERFORMANCE - TIME Function----------------------------------------
	 */
	time_t t_start, t_end;
	struct tm *st,*en;
	struct timeval time_ms,time_end;
	struct TIME t1,t2,diff;


	//Performance for the Client Side
	t_start = time(NULL);
	st = localtime(&t_start);
	t1.hours = st->tm_hour;
	t1.minutes = st->tm_min;
	t1.seconds = st->tm_sec;
	gettimeofday(&time_ms, NULL);
	t1.milliseconds =  time_ms.tv_usec/1000;
	printf("START TIME = %d:%d:%d:%03ld\n",t1.hours,t1.minutes,t1.seconds,t1.milliseconds);

	while (count < noOfFiles) {

		lSize = atol(fileSizeArrayDownload[count]);
		counter = (int) lSize / BUF_SIZE;

		FILE *fr = fopen(fileNameArrayDownload[count], "ab");
		if (fr == NULL)
			fatal("File cannot be opened on server");

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
		w = write(connID, succ, 1);
		printf("Bytes written (file) : %d\n", w);
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
	 *TIME function PERFORMANCE----------------------------ENDS HERE
	 */
	end = time(0);
	(void) printf("download : End Time %s\n", ctime(&end));
	(void) printf("download : Total Time %lu\n", (end - start));

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
