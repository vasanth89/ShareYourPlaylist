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
#define BUF_SIZE 1024
#define MAX_FILES 10
#define TRUE 1
#define FALSE 0
#define UPLOAD_PORT 8000
#define DOWNLOAD_PORT 8001
#define MAX_LENGTH 2550
#define PATH_MAX 255

//attributes to measure performance
struct timespec s_time, e_time;
struct TIME{
  int seconds;
  int minutes;
  int hours;
  long int milliseconds;
};

//method declaration
int connectTCP(int port);
void userSetup(const char *user, int socket);
void handleUpload(char *user);
void fileMetadata(int numberOfFiles, int socket);
void fileTransfer(int numberOfFiles, int socket);
void handleDownload(char *user);
void fileTransferDownload(int socket, char *send);
int selectdir() ;
void timeDifference(struct TIME t1, struct TIME t2, struct TIME *differ);
void fatal(char *string);

//global variable declaration
char *fileNameArray[MAX_FILES];
char *fileSizeArray[MAX_FILES];
char metaData[3000];

/*
 * main(): Entry point of client application for Share Your Playlist.
 * The user enters his username followed by selecting Upload or Download
 * The function calls the respective function based on user's choice.
 */
int main(int argc, char **argv) {
	char *user;
	int option;

	printf("************************************** Welcome to SHARE YOUR PLAYLIST **************************************\n\n");
	printf("Enter Username : ");
	scanf("%s", user);
	printf("\nHello %s\n", user);
	printf("\n1.Upload\n2.Download\nEnter your option : ");
	scanf("%d", &option);
	printf("\nYou selected option %d\n", option);

	//Call the appropriate function based on user's choice
	if (option == 1)
		handleUpload(user);
	else if (option == 2)
		handleDownload(user);
	else
		fatal("Invalid option");
}

/*
 * handleUpload(): This function delivers the upload functionality for the client.
 * This function co-ordinates the main functions of the client such as connection, and file transfer
 */
void handleUpload(char *user) {

	int numberOfFiles, s;
	time_t start, end;

	printf("************************************** Upload Your Playlist - START**************************************\n\n");
	//Show the start time of upload
	start = time(0);
	(void) printf("\nUpload : Start Time %s\n", ctime(&start));

	//call the function to establish connection to server on upload port
	s = connectTCP(UPLOAD_PORT);
	if (s < 0)
		fatal("Upload socket failed");
	printf("Socket number : %d\n", s);

	//Function that checks with the server by sending username and validates the user
	userSetup(user, s);

	//Request the user to mention the number of files to be uploaded
	printf("Enter number of files : ");
	scanf("%d", &numberOfFiles);
	//If there are no files to upload
	if (numberOfFiles == 0) {
		printf("No files for upload!!\n");
		exit(EXIT_SUCCESS);
	}
	//A maximum of 10 files are allowed during every upload
	if (numberOfFiles > MAX_FILES)
		fatal("Cannot transfer more than 10 files");

	//functions to handle the file transfer
	fileMetadata(numberOfFiles, s);
	fileTransfer(numberOfFiles, s);

	//Display the end time and calculate the total time taken for upload
	end = time(0);
	(void) printf("Upload : End Time %s\n", ctime(&end));
	(void) printf("Upload : Total Time %lu\n", (end - start));

	printf("************************************** Upload Your Playlist - END**************************************\n\n");
	//Close the socket and exit program
	close(s);
	exit(EXIT_SUCCESS);
}

/*
 * connectTCP(): This function is used to establish a connection to the server.
 * Returns the socket number on successful connection establishment.
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
	server_addr.sin_addr.s_addr = inet_addr("10.11.12.13");
	server_addr.sin_port = htons(port);

	c = connect(s, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (c < 0)
		fatal("connect failed");

	//Return the socket after connection establishment
	return s;
}

/*
 * userSetup(): This function is used to check validity of user on server.
 */
void userSetup(const char *user, int socket) {
	int success, w;
	char buf[2];

	//Write the username into the socket
	w = write(socket, user, strlen(user));

	//Check if username accepted and display appropriate message
	w = read(socket, buf, 1);
	buf[1] = '\0';
	if (w < 0)
		fatal("username failed");

	//Check if server sent ACK for username
	success = atoi(buf);
	if (success == TRUE)
		printf("User accept success\n");
	else
		fatal("User accept failed");
}

/*
 * fileMetadata(): This function is used to form the metadata needed for file upload.
 * It collects the required information such as filename and filesize.
 * Forms metadata and sends it to server
 */
void fileMetadata(int numberOfFiles, int socket) {

	char noOfFiles[2], fileSize[10], b[2], buf[BUF_SIZE];
	const char *delims = "~", *spaces = " ";
	char *filenames, *str1, *result, *nextString;
	int w, success, j = 0;
	long lSize;
	struct stat stbuf;

	//initialize metaData to blank
	memset(metaData, 0, 3000);
	memset(buf, 0, BUF_SIZE);
	memset(noOfFiles, 0, 2);

	//append number of files to start of metadata
	sprintf(noOfFiles, "%d", numberOfFiles);
	strcpy(metaData, noOfFiles);
	strcat(metaData, delims);

	//Used to get filenames from user
	FILE *in = fdopen(STDIN_FILENO, "r");
	printf("Enter filenames separated by spaces : ");
	fgets(buf, BUF_SIZE, in);
	fclose(in);
	buf[strlen(buf)] = '\0';

	//Loop that reads all information about the file and appends to metadata
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

		//append filename to metadata
		strcpy(fileNameArray[j], result);
		strcat(metaData, fileNameArray[j]);
		strcat(metaData, delims);
		//printf("fileNameArray[%d]: %s\n", j, fileNameArray[j]);

		int fdes;
		fdes = open(fileNameArray[j], O_RDONLY);
		if (fdes < 0)
			fatal("File des open failed");
		else
			printf("File des %d open success\n", j);
		//Use fstat to get file size as it is better for binary files
		if ((fstat(fdes, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
			fatal("Error in fstat");
		}

		lSize = stbuf.st_size;
		//printf("File Size : %ld\n", lSize);
		close(fdes);

		memset(fileSize, 0, sizeof(long));
		sprintf(fileSize, "%ld", lSize);
		fileSizeArray[j] = (char *) malloc(sizeof(lSize));

		//append filesize to metadata
		strcpy(fileSizeArray[j], fileSize);
		strcat(metaData, fileSizeArray[j]);
		strcat(metaData, delims);
	}

	metaData[strlen(metaData)] = '\0';
	printf("Metadata : %s\n", metaData);

	//Write the metadata into socket
	w = write(socket, metaData, strlen(metaData));
	w = read(socket, b, 1);
	b[1] = '\0';

	//Check if server sent ACK for metadata
	success = atoi(b);
	if (success == TRUE)
		printf("After metaData call success\n");
	else
		fatal("After metaData call failed");
}

/*
 * fileTransfer(): This function performs the important function of sending the files to the server.
 * It loops through each file and sends multilple files one after the other.
 */
void fileTransfer(int numberOfFiles, int socket) {
	int bytes, count = 0, w, success;
	char buf[BUF_SIZE], b[2];
	FILE *fd;
	time_t t_start, t_end;
	struct tm *st,*en;
	struct timeval time_ms,time_end;
	struct TIME t1,t2,diff;

	//Performance measure based on time for Client Side - START
	t_start = time(NULL);
	st = localtime(&t_start);
	t1.hours = st->tm_hour;
	t1.minutes = st->tm_min;
	t1.seconds = st->tm_sec;
	gettimeofday(&time_ms, NULL);
	t1.milliseconds =  time_ms.tv_usec/1000;
	printf("START TIME = %d:%d:%d:%03ld\n",t1.hours,t1.minutes,t1.seconds,t1.milliseconds);

	//Loop through the files and send them to server
	while (count < numberOfFiles) {
		fd = fopen(fileNameArray[count], "rb");
		if (fd < 0)
			fatal("File open failed");
		else
			printf("File open success\n");

		//loop to read the file contents 1024 bytes at a time
		while ((bytes = fread(buf, BUF_SIZE, 1, fd)) >= 0) {
			w = write(socket, buf, BUF_SIZE);
			//condition to check if end of file has been reached
			if (bytes == 0 && feof(fd)) {
				//printf("End of file reached\n");
				break;
			}
		}
		fclose(fd);
		//check for ACK from server for success of file transfer
		w = read(socket, b, 1);
		b[1] = '\0';
		success = atoi(b);
		if (success == TRUE)
			printf("File [%d] Transfer Result : %d\n", count, success);
		else
			fatal("File transfer failed");
		count++;
	}
	//Measure the end time and calculate the total time taken
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
	//Performance measure based on time for Client Side - END
}

/*
 * handleDownload(): This function is used to handle the download functionality.
 * It establishes connection to server, requests playlist information
 * and downloads the files to local storage.
 */
void handleDownload(char *user) {
	int s, w, success;
	char buf[2], send[108], buffer[BUF_SIZE];
	char *ch;

	printf("************************************** Download Playlist - START**************************************\n\n");
	//Establish connection to server on download port
	s = connectTCP(DOWNLOAD_PORT);
	if (s < 0)
		fatal("Download socket failed");
	printf("Socket number : %d\n", s);

	memset(buffer, 0, BUF_SIZE);
	//Check done to see if there are any playlists else throw error
	w = read(s, buf, BUF_SIZE);
	success = atoi(buf);
	if(success != TRUE ){
		fatal("There are no playlists!!!\n");
	}
	//Send ACK to server
	w = write(s, "1", 1);

	w = read(s, buffer, BUF_SIZE);
	//Send ACK to server
	w = write(s, "1", 1);

	//Display the playlists using the information obtained from server
	printf("The Playlist are :\n");
	ch = strtok(buffer, "~");
	while (ch != NULL) {
		printf("%s\n", ch);
		ch = strtok(NULL, "~");
	}

	//get input from user for downloading playlist
	printf("\nEnter a playlist : ");
	scanf("%s", send);
	//call the function to handle file transfer from server
	fileTransferDownload(s, send);

	printf("************************************** Download Playlist - END**************************************\n\n");
	//Close the socket and exit the program
	close(s);
	exit(EXIT_SUCCESS);
}

/*
 * fileTransferDownload():This function handles the download of files from the server.
 * It communicates with the server and handles file download.
 */
void fileTransferDownload(int connID, char *send) {
	char metadataDownload[3000], buf[2], cwd[PATH_MAX], revbuf[BUF_SIZE], backup[BUF_SIZE];
	time_t start, end;
	char *fileNameArrayDownload[MAX_FILES], *fileSizeArrayDownload[MAX_FILES];
	char *str1, *result, *nextString;
	const char *delims = "~";
	int w, rd, success, changedir, fr_block_sz = 0, remain = 0;
	int counter = 0, i = 1, noOfFiles = 0, count = 0, j, anamolyCount = 0;
	unsigned long lSize = 0L, sizeCheck = 0L;
	time_t t_start, t_end;
	struct tm *st,*en;
	struct timeval time_ms,time_end;
	struct TIME t1,t2,diff;

	//print the start time of download
	start = time(0);
	(void) printf("Download : Start Time %s\n", ctime(&start));

	send[strlen(send)] = '\0';
	//Write the playlist name to server
	w = write(connID, send, strlen(send));

	read(connID, buf, 1);
	//Server sent NACK so handle it accordingly
	success = atoi(buf);
	if (success == FALSE)
		fatal("Error in download");

	//Call this function to switch to 'Music' directory where playlist will be downloaded
	selectdir();

	getcwd(cwd, sizeof(cwd));
	strcat(cwd, "/");
	strcat(cwd, send);
	changedir = mkdir(cwd, 0777);

	//Check if directory has been created successfully and change into it
	if (0 == changedir)
		printf("Directory is created\n");
	else
		printf("Directory already Exists!!!\n");

	changedir = chdir(cwd);
	if (changedir < 0)
		fatal("Change Directory in download failed");

	//read the metadata from server for download
	memset(metadataDownload, 0, BUF_SIZE);
	rd = read(connID, metadataDownload, BUF_SIZE);
	if (rd < 0)
		fatal("Reading Failed!!! \n");

	//send ACK to server after correctly receiving metadata
	w = write(connID, "1", 1);

	metadataDownload[strlen(metadataDownload) - 1] = '\0';
	printf("Metadata : %s\n", metadataDownload);

	//loop to process metadata and populate the arrays accordingly
	for (j = 0, str1 = metadataDownload;; j++, str1 = NULL) {
		result = strtok_r(str1, delims, &nextString);
		if (result == NULL)
			break;
		if (j == 0) {
			noOfFiles = atoi(result);
			//printf("Number of files : %d\n", noOfFiles);
			continue;
		}
		if (j % 2 != 0) {
			fileNameArrayDownload[j / 2] = result;
			//printf("Filename : %s\n", fileNameArrayDownload[j / 2]);
		} else {
			fileSizeArrayDownload[j / 2 - 1] = result;
			//printf("Filesize : %s\n", fileSizeArrayDownload[j / 2 - 1]);
		}
	}

	//Performance measure based on time for Client Side - START
	t_start = time(NULL);
	st = localtime(&t_start);
	t1.hours = st->tm_hour;
	t1.minutes = st->tm_min;
	t1.seconds = st->tm_sec;
	gettimeofday(&time_ms, NULL);
	t1.milliseconds =  time_ms.tv_usec/1000;
	printf("START TIME = %d:%d:%d:%03ld\n",t1.hours,t1.minutes,t1.seconds,t1.milliseconds);

	//Loop to receive files from server
	while (count < noOfFiles) {

		//printf("Filename(%d) is :%s\n", count, fileNameArrayDownload[count]);
		//printf("Filesize(%d) is :%s\n", count, fileSizeArrayDownload[count]);

		lSize = atol(fileSizeArrayDownload[count]);

		//counter is used to store the number of times 1024 bytes are read for each file
		counter = (int) lSize / BUF_SIZE;
		//printf("Counter value : %d\n", counter);

		FILE *fr = fopen(fileNameArrayDownload[count], "ab");
		if (fr == NULL)
			fatal("File cannot be opened on server");
		printf("File %s opened\n",fileNameArrayDownload[count]);

		//loop to obtain file data from server
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
		w = write(connID, "1", 1);
		//display the anamoly count to show anomalies in file chunking.
		printf("Anamoly Count for file[%d] : %d\n",count,anamolyCount);

		//reset all variables to zero
		sizeCheck = 0L;
		lSize = 0L;
		i = 1;
		remain = 0;
		fr_block_sz = 0;
		anamolyCount = 0;
		count++;
	}

	//Measure the end time and calculate the total time taken
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
	//Performance measure based on time for Client Side - END

	//Display the end time of download
	end = time(0);
	(void) printf("Download : End Time %s\n", ctime(&end));
	(void) printf("Download : Total Time %lu\n", (end - start));
}

/*
 * selectdir(): This function is used to change into local storage to store files during download
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
 * Generic function that is used to handle all errors
 */
void fatal(char *string) {
	printf("%s\n", string);
	exit(EXIT_FAILURE);
}
