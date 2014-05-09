AWS version
-----------
This code has more comments and is more clean. I rewrote the IP addresses.

clientFTP_AWS.c 
Client code that connects to server. Requests upload and download operations.
Since there are multiple server instances, the IP address of server to connect to should be mentioned in the code.

serverFTP_AWS.c 
Server code. Listens on ports 8000(upload) and 8001(download). There can be multiple instances
Performs data redundancy by sending files to other servers.

Local version
-------------
This code has lesser comments and looks kind of redundant. But this has some validations that I added.

clientFTP_local.c  
Client code that connects to server. Requests upload and download operations.
Since there are multiple server instances, the IP address of server to connect to should be mentioned in the code.

serverFTP_local.c 
Server code that acts as the main server. Listens on ports 8000(upload) and 8001(download). There can be multiple instances
Performs data redundancy by sending files to other servers.

serverFTP_bkp_local.c 
Server code that acts like the back-up server. Listens on ports 8010(upload) and 8011(download) since its the same machine. 
Performs data redundancy by sending files to other servers.

common.h
File that contains all the necessary headers and constants