#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/select.h>


static long int MAX_QUEUE_LIMIT = 10000;
static long int MAX_BUFFER_SIZE = 999999;

struct client_request
{
	char hostname[100];
	char port_number[10];
	char username[256];
	char file_path[1000];
	char password[100];
	int  isFTP;
};

// Terminates the proxy indicating error
void closeWithError(char *error);

// Function accepts client socket and exhchanges data with the client and the server
void exchangeDataWithClient(int client_socket);

// It is used to parse the client request and obtain the server hostname and port
int parseRequest(char *requestString, struct client_request *request) ;

//Trims the request
char * removeExtraSpaces(char* string);

//Exchange HTTP Data with client and server
void exchangeHTTPWithServerAndClient(int client_socket, int server_socket,char *buffer, int client_request_length);

//Exchange FTP Data with client and server
void exchangeFTPWithServerAndHTTPWithClient(int client_socket, int server_socket,struct client_request *request);

//Send Bad Response
void sendBadResponse(int client_socket);

//Send Quit
void sendQuitToServer(int server_socket);

//Check if ftp response is incomplete
int isFTPResponseIncomplete(char * response);

//Take PASV Response and give IP and PORT
void getPASVIPAndPort(char * response, char *ip, int *port);

int isSocketActiveForRead(int socket);

int
main(int argc, char **argv)
{

  //Initializations
	int listenfd, connectionfd;
	struct sockaddr_in  cliaddr, servaddr;
	socklen_t client_length;

    // Open a socket
	listenfd = socket(AF_INET, SOCK_STREAM, 0);

    // Setup Proxy address
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(atoi(argv[1]));

    //Bind the Socket
	if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
		closeWithError("Bind Error for the request!");
	}

    // Mark the port as accepting connections
	if (listen(listenfd,MAX_QUEUE_LIMIT) == -1)
	{
		closeWithError("Listen Error for the request!");
	}

    //Accept connections infinitely
	for (;;)
	{
		client_length = sizeof(cliaddr);
		connectionfd = accept(listenfd, (struct sockaddr *)&cliaddr,&client_length);
		if (connectionfd == -1)
		{
			closeWithError("Accept failed.\n");

		}

		printf("Connection opened.\n");

        //Communicate with the client
		exchangeDataWithClient(connectionfd);

		printf("Connection closed.\n");
        //Close the connection
		close(connectionfd);

        //Reset the client address
		bzero(&cliaddr, sizeof(cliaddr));

	}
}

void exchangeDataWithClient(int client_socket) {

    //Initialize the data
	char buffer[MAX_BUFFER_SIZE], raw_buffer[MAX_BUFFER_SIZE];
	int receivedMessageSize;
	struct client_request request;
	struct sockaddr_in  proxy_server_socket;
	int read_connection;
	struct hostent *host_entity;

    // printf("Bad Message string: %s\n", badMessage);

	memset(buffer,0,MAX_BUFFER_SIZE);

    //Revceive the request from the client
	if ( (receivedMessageSize = recv(client_socket,&buffer,MAX_BUFFER_SIZE,0)) != 0)
	{
		memset(raw_buffer,0,MAX_BUFFER_SIZE);

		if (receivedMessageSize) {
			memcpy(&raw_buffer,&buffer,strlen(buffer));

			read_connection = socket(AF_INET, SOCK_STREAM, 0);
			bzero(&proxy_server_socket, sizeof(proxy_server_socket));
			bzero(&request, sizeof(request));

            //Parse the request
			if(parseRequest(raw_buffer,&request) == -1){
				printf("Return as the request is not parsed.\n");

				sendBadResponse(client_socket);
				return;
			}
			printf("Request is: %s\n", buffer);

            //Processing address for server connection
			proxy_server_socket.sin_family = AF_INET;

            //Check if it is an IP address or a host name.
			if ((request.hostname[0] >= 'a' && request.hostname[0] <= 'z') ||  (request.hostname[0] >= 'A' && request.hostname[0] <= 'z'))
			{
				printf("Parsing for host name:%s\n", request.hostname);
				if ((host_entity = gethostbyname(request.hostname)) != NULL)
				{
					memcpy(&proxy_server_socket.sin_addr, (struct in_addr*)host_entity->h_addr,host_entity->h_length);
				}
				else {
					printf("Could not resolve host.\n");
					sendBadResponse(client_socket);
					return;
				}

				printf("s_addr = %s\n", inet_ntoa((struct in_addr)proxy_server_socket.sin_addr));
			}
			else {
				printf("Parsing for IP:%s\n", request.hostname);
				if(inet_aton(request.hostname,&proxy_server_socket.sin_addr) == 0){
					printf("Could not resolve host.\n");
					sendBadResponse(client_socket);
					return;
				}
			}

            //Set the port
			proxy_server_socket.sin_port = htons(atoi(request.port_number));

            // Open a connection
			int connectionResult = connect(read_connection, (struct sockaddr *) &proxy_server_socket, sizeof(proxy_server_socket));

			if (connectionResult == -1)
			{
				printf("Could not connect to web.\n");
				sendBadResponse(client_socket);
				return;
			}

			printf("Connected to the web\n");

			if (request.isFTP == 1)
			{
				printf("Exchanging FTP Data\n");
				exchangeFTPWithServerAndHTTPWithClient(client_socket,read_connection,&request);
			}
			else {
				printf("Exchanging HTTP Data\n");
				exchangeHTTPWithServerAndClient(client_socket,read_connection,buffer,receivedMessageSize);
			}

            // Close connection with the server
			close(read_connection);
			memset(buffer,0,MAX_BUFFER_SIZE);

		}
	}
	else {
		printf("Error while receiving from client\n");
	}

}

void exchangeHTTPWithServerAndClient(int client_socket, int server_socket,char *buffer, int client_request_length){

	int serverCommunicationSize;
	char server_buffer[MAX_BUFFER_SIZE];

	memset(server_buffer,0,MAX_BUFFER_SIZE);

	printf("Request: %s\n", buffer);

    // Send data to server
	int sent_to_server_size, sent_to_client_size;
	if((sent_to_server_size = send(server_socket,buffer,client_request_length,0)) == client_request_length) {

		printf("Sent request to web. Bytes = %d \n",sent_to_server_size);

        //CHeck the client socket capacity.
		int client_capacity = 0, buffer_capacity = 0,error =0;
		unsigned int length_of_cap = sizeof(client_capacity);
		unsigned int length_of_err = sizeof(error);
		if(getsockopt(client_socket,SOL_SOCKET,SO_RCVBUF,(void *)&client_capacity, &length_of_cap) != 0) {
			return;
		}

		printf("Max buffer capacity of client:%d\n", client_capacity);

		if (client_capacity < MAX_BUFFER_SIZE)
		{
			buffer_capacity = client_capacity;
		}
		else {
			buffer_capacity = MAX_BUFFER_SIZE;
		}

        // Receive response from server
		serverCommunicationSize = recv(server_socket,&server_buffer,buffer_capacity,0);
		do 
		{
			printf("Sending response to client  Bytes: %d\n",serverCommunicationSize);

             //Check any error in client socket
			if(getsockopt(client_socket,SOL_SOCKET,SO_ERROR,(void *)&error, &length_of_err) != 0) {
				printf("Could not check if socket is connected.\n");
				return;
			}

			if (error != 0)
			{
				printf("Error in client socket:%s\n", strerror(error));
				return;
			}

            // Send response back to client
			if ((sent_to_client_size = send(client_socket,&server_buffer,serverCommunicationSize,0)) != serverCommunicationSize) {
				printf("Could not write to client socket.\n");
				return;
			}
             // printf("%s\n",server_buffer);
			memset(server_buffer,0,MAX_BUFFER_SIZE);
			printf("Sent response to client, Bytes: %d \n",sent_to_client_size);

			int client_capacity_in = 0, buffer_capacity_in = 0;
			unsigned int length_of_cap_in = sizeof(client_capacity_in);
			if (getsockopt(client_socket,SOL_SOCKET,SO_RCVBUF,(void *)&client_capacity_in, &length_of_cap_in) != 0) {
				return;
			}

			printf("Max buffer capacity of client:%d\n", client_capacity_in);


			if (client_capacity_in < MAX_BUFFER_SIZE)
			{
				buffer_capacity_in = client_capacity_in;
			}
			else {
				buffer_capacity_in = MAX_BUFFER_SIZE;
			}


            if (isSocketActiveForRead(server_socket) > 0)
            {
                
                // Receive response from server and check if all the response is received.
                serverCommunicationSize = recv(server_socket,&server_buffer,buffer_capacity_in,0);
            }

            // Receive response from server and check if all the response is received.
		} while(serverCommunicationSize > 0);
	}
	else {
		printf("Error while receiving from server\n");
		sendBadResponse(client_socket);
	}
	printf("Out of server loop\n");

}

void exchangeFTPWithServerAndHTTPWithClient(int client_socket, int server_socket,struct client_request *request) {
	char server_buffer[MAX_BUFFER_SIZE], client_response_buffer[MAX_BUFFER_SIZE];
	struct sockaddr_in  ftp_server_socket;

	memset(server_buffer,0,MAX_BUFFER_SIZE);
    int sent_to_client_size;
	char data_transfer_ip[50];
	int data_transfer_port;
	memset(data_transfer_ip,0,50);

	char proxy_request[1200];
	memset(proxy_request,0,1200);

	int data_connection;

    // //Initiate a connection
	int response_size = 0;

	//Keep receiving until end of line is received from the server.
	while ((response_size = recv(server_socket,&server_buffer,MAX_BUFFER_SIZE,0))!= 0) {

		if (!isFTPResponseIncomplete(server_buffer))
			break; 
		memset(server_buffer,0,MAX_BUFFER_SIZE);

	}

	if (response_size <= 0)
	{
        //Return Response as Bad Reques
		sendBadResponse(client_socket);
		sendQuitToServer(server_socket);
		return;
	}
	printf("Server Response for initiating connection:%s\n", server_buffer);

	int response = strtol(server_buffer,NULL,10);
	if (response == 220)
	{
		printf("Server Ready\n");
	}
	else  {
        //Return Response as Bad Request
		sendBadResponse(client_socket);
		sendQuitToServer(server_socket);
		return;
	}

    //Send the username
	memset(proxy_request,0,1200);
	memset(server_buffer,0,MAX_BUFFER_SIZE);
	strcpy(proxy_request,"USER ");
	strcat(proxy_request,request->username);
	strcat(proxy_request,"\r\n");

    // //Send Username
	if (send(server_socket,proxy_request,strlen(proxy_request),0) == strlen(proxy_request))
	{
		int response_size = 0;

		//Keep receiving until end of line is received from the server.
		while ((response_size = recv(server_socket,&server_buffer,MAX_BUFFER_SIZE,0))!= 0) {

			if (!isFTPResponseIncomplete(server_buffer))
				break; 
			memset(server_buffer,0,MAX_BUFFER_SIZE);

		}

		if (response_size <= 0)
		{
            //Return Response as Bad Reques
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
		}

		int response = strtol(server_buffer,NULL,10);
		printf("Server Response for Username:%s\n", server_buffer);

		if (response == 331)
		{
			printf("Username OK!\n");
		}
		else if (response == 230)
		{
			goto passive;
		}
		else  {
            //Return Response as Bad Request
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
		}
	}
	else {
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
	}

    //Send the password
	memset(server_buffer,0,MAX_BUFFER_SIZE);
	memset(proxy_request,0,1200);
	strcpy(proxy_request,"PASS ");
	strcat(proxy_request,request->password);
	strcat(proxy_request,"\r\n");

	printf("Username & Password request:%s\nLength:%d\n", proxy_request,strlen(proxy_request));

	if (send(server_socket,proxy_request,strlen(proxy_request),0) == strlen(proxy_request))
	{
		int response_size = 0;
		while ((response_size = recv(server_socket,&server_buffer,MAX_BUFFER_SIZE,0))!= 0) {

			if (!isFTPResponseIncomplete(server_buffer))
				break; 
			memset(server_buffer,0,MAX_BUFFER_SIZE);

		}

		if (response_size <= 0)
		{
            //Return Response as Bad Request
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
		}

		int response = strtol(server_buffer,NULL,10);
		printf("Server Response for Password:%s\n", server_buffer);
		if (response == 230)
		{
			printf("Password OK!\n");
		}
		else {
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
		}
	}
	else {
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
	}

    //Enter Passive Mode
	passive:    
	memset(proxy_request,0,1200);
	memset(server_buffer,0,MAX_BUFFER_SIZE);
	strcpy(proxy_request,"PASV\r\n");

	if (send(server_socket,proxy_request,strlen(proxy_request),0) == strlen(proxy_request))
	{
		int response_size = 0;
		while ((response_size = recv(server_socket,&server_buffer,MAX_BUFFER_SIZE,0))!= 0) {

			if (!isFTPResponseIncomplete(server_buffer))
				break; 
			memset(server_buffer,0,MAX_BUFFER_SIZE);

		}

		if (response_size <= 0)
		{
            //Return Response as Bad Request
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
		}

		int response = strtol(server_buffer,NULL,10);
		printf("Server Response for Passive Mode:%s\n", server_buffer);

		if (response == 227)
		{
			printf("Entered Passive Mode!\n");
			getPASVIPAndPort(server_buffer,data_transfer_ip,&data_transfer_port);

			if (!strlen(data_transfer_ip) || !data_transfer_port)
			{
                //Return Response as Bad Request
				sendBadResponse(client_socket);
				sendQuitToServer(server_socket);
				return;
			}

		}
		else {
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
		}
	}
	else {
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
	}


	printf("Parsed IP:%s\nPORT:%d\n", data_transfer_ip,data_transfer_port);

	// Setup Proxy address
	bzero(&ftp_server_socket, sizeof(ftp_server_socket));
	ftp_server_socket.sin_family      = AF_INET;
	ftp_server_socket.sin_port        = htons(data_transfer_port);

	if(inet_aton(data_transfer_ip,&ftp_server_socket.sin_addr) == 0){
		printf("Could not resolve host.\n");
		sendBadResponse(client_socket);
		return;
	}


	data_connection = socket(AF_INET, SOCK_STREAM, 0);

    // Open a connection
	int connectionResult = connect(data_connection, (struct sockaddr *) &ftp_server_socket, sizeof(ftp_server_socket));

	if (connectionResult == -1)
	{
		printf("Could not connect to web.\n");
		sendBadResponse(client_socket);
		return;
	}
    //Retrieve the file

	memset(proxy_request,0,1200);
	memset(server_buffer,0,MAX_BUFFER_SIZE);
	strcpy(proxy_request,"RETR ");
	strcat(proxy_request,request->file_path);
	strcat(proxy_request,"\r\n");

	printf("Proxy Request for RETR:%s\n", proxy_request);
	if (send(server_socket,proxy_request,strlen(proxy_request),0) == strlen(proxy_request))
	{
		int response_size = 0;

		response_size = recv(server_socket,&server_buffer,MAX_BUFFER_SIZE,0);

		if (response_size <= 0)
		{
            //Return Response as Bad Request
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
		}

		int response = strtol(server_buffer,NULL,10);
		printf("Server Response for Retrieve:%s\n", server_buffer);

		if (response == 150 || response == 125)
		{
			printf("Opened connection to retrieve file.\n");
		}
		else {
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
		}
	}
	else {
			sendBadResponse(client_socket);
			sendQuitToServer(server_socket);
			return;
	}



	memset(server_buffer,0,MAX_BUFFER_SIZE);
	long data_response_size = 0;
	 //CHeck the client socket capacity.
	int client_capacity = 0, buffer_capacity = 0,error =0;
	unsigned int length_of_cap = sizeof(client_capacity);
	unsigned int length_of_err = sizeof(error);
	if(getsockopt(client_socket,SOL_SOCKET,SO_RCVBUF,(void *)&client_capacity, &length_of_cap) != 0) {
		sendQuitToServer(server_socket);
	    close(data_connection);
		return;
	}

	printf("Max buffer capacity of client:%d\n", client_capacity);

	if (client_capacity < MAX_BUFFER_SIZE)
	{
		buffer_capacity = client_capacity;
	}
	else {
		buffer_capacity = MAX_BUFFER_SIZE;
	}

    char* okMessage = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nConnection: Keep-Alive\r\n\r\n";


    //Remove the size of header The char* okMessage
    buffer_capacity -= strlen(okMessage);

    // Receive response from server
	data_response_size = recv(data_connection,&server_buffer,buffer_capacity,0);
	int is_http_response_sent = 0;
	do 
	{
		printf("Sending response to client  Bytes: %ld\n",data_response_size);


        if (is_http_response_sent)
        {

        	memset(client_response_buffer,0,MAX_BUFFER_SIZE);
	        memcpy(client_response_buffer,server_buffer,data_response_size);
        	
        }
        else {
        	memset(client_response_buffer,0,MAX_BUFFER_SIZE);
	        memcpy(client_response_buffer,okMessage,strlen(okMessage));
	        memcpy(client_response_buffer+strlen(okMessage),server_buffer,data_response_size);
            data_response_size += strlen(okMessage);
		}


         //Check any error in client socket
		if(getsockopt(client_socket,SOL_SOCKET,SO_ERROR,(void *)&error, &length_of_err) != 0) {
			printf("Could not check if socket is connected.\n");
			sendQuitToServer(server_socket);
		    close(data_connection);
			return;
		}

		if (error != 0)
		{
			printf("Error in client socket:%s\n", strerror(error));
			sendQuitToServer(server_socket);
		    close(data_connection);
			return;
		}

        is_http_response_sent = 1;
        // Send response back to client
		if ((sent_to_client_size = send(client_socket,&client_response_buffer,data_response_size,0)) != data_response_size) {
			printf("Could not write to client socket.\n");
			sendQuitToServer(server_socket);
		    close(data_connection);
			return;
		}
         // printf("%s\n",server_buffer);
		memset(server_buffer,0,MAX_BUFFER_SIZE);
		printf("Sent response to client, Bytes: %d \n",sent_to_client_size);

		int client_capacity_in = 0, buffer_capacity_in = 0;
		unsigned int length_of_cap_in = sizeof(client_capacity_in);
		if (getsockopt(client_socket,SOL_SOCKET,SO_RCVBUF,(void *)&client_capacity_in, &length_of_cap_in) != 0) {
			sendQuitToServer(server_socket);
		    close(data_connection);
			return;
		}

		printf("Max buffer capacity of client:%d\n", client_capacity_in);


		if (client_capacity_in < MAX_BUFFER_SIZE)
		{
			buffer_capacity_in = client_capacity_in;
		}
		else {
			buffer_capacity_in = MAX_BUFFER_SIZE;
		}

        //Remove the size of header The char* okMessage
        buffer_capacity -= strlen(okMessage);

		if (isSocketActiveForRead(data_connection) > 0)
		{
			
	        // Receive response from server and check if all the response is received.
			data_response_size = recv(data_connection,&server_buffer,buffer_capacity_in,0);
		}

	} while(data_response_size > 0);
	sendQuitToServer(server_socket);
    close(data_connection);
}


int parseRequest(char *requestString, struct client_request *request) {


    //Check if the request is GET else return to close the connection
	if (strstr(requestString,"GET") != NULL)
	{
		requestString = removeExtraSpaces(requestString);
		printf("It is a get request\n");
        printf("Request:%s\n", requestString);

		if (strstr(requestString,"ftp://") != NULL && strstr(requestString,"HTTP/1.1") != NULL)
		{
			printf("It is a FTP request\n");
			request->isFTP = 1;
		}
		else if (strstr(requestString,"HTTP/1.1") != NULL)
		{
			printf("It is ONLY a HTTP/1.1 request\n");
			request->isFTP = 0;
		}
		else {
			return -1;
		}

		if (request->isFTP)
		{

			char *hostStart;

			if ((hostStart = strstr(requestString,"ftp://")) == NULL)
			{
				printf("Could not retrieve host for FTP\n");
				return -1;
			}

			if (strlen(hostStart) <= 6)
			{
				printf("Could not parse host from the request for FTP.\n");
				return -1;
			}
            //Find the Host
			hostStart += 6;
			hostStart = removeExtraSpaces(hostStart);


   //          //Separate username-password and hostname-path

			// char at_the_rate = '@';
   //          //Get the first and last occurence of '@'
			// char *first_occurence = strchr(hostStart,at_the_rate);
			// char *last_occurence = strrchr(hostStart,at_the_rate);

   //          //Check if there is any occurence
			// if (first_occurence == NULL)
			// {
                //No Username and password provided
			strncpy(request->username,"anonymous",strlen("anonymous"));
			strncpy(request->password,"klr24@njit.edu",strlen("klr24@njit.edu"));

			// }
			// else {
   //              //There is a username and password
			// 	if (strcmp(first_occurence,last_occurence) == 0)
			// 	{
   //                  //Username does not contain domain address
			// 	}
			// 	else {
   //                  //Username conatins domain address.
			// 	}
			// }

			char* start_of_file_path = strdup(hostStart);

			hostStart = strsep(&start_of_file_path,"/");
			start_of_file_path = removeExtraSpaces(start_of_file_path);

			int i = 0, didFindNewLineCharacters = 0;
			if (strlen(start_of_file_path) <= 0)
			{
				return -1;
			}

			while(start_of_file_path[i] != 13 && start_of_file_path[i] != 10 && start_of_file_path[i] != ' ') 
			{
				i++;
				didFindNewLineCharacters = 1;
			} 

			if (i<strlen(start_of_file_path) && didFindNewLineCharacters)
			{
				start_of_file_path[i] = 0;
			}

			if (strlen(start_of_file_path) == 0)
			{
				return -1;
			}

			memcpy(request->file_path, start_of_file_path,strlen(start_of_file_path));

			hostStart[start_of_file_path - hostStart] = 0;
			if (strstr(hostStart,":"))
			{
				printf("Reached for host with port,%s\n", hostStart);
				strncpy(request->hostname,hostStart,strstr(hostStart,":")-hostStart);
				char * port_number = strstr(hostStart,":") + 1;

				if (strlen(port_number) == 0) 
				{   
					return -1;
				}

				strncpy(request->port_number,port_number,strlen(port_number));
			}
			else {

				strncpy(request->hostname,hostStart,strlen(hostStart));
				strncpy(request->port_number, "21",2);
			}

			printf("File Path:%s\n", request->file_path);
			printf("Username:%s\n", request->username);
			printf("Password:%s\n", request->password);

		}
		else {

			char *hostStart;

			if ((hostStart = strstr(requestString,"Host:")) == NULL)
			{
				printf("Could not retrieve host for HTTP/1.1\n");
				return -1;
			}

			if (strlen(hostStart) <= 5)
			{
				printf("Could not parse host from the request.\n");
				return -1;
			}

			printf("We have successfully parsed host.\n");


            //Find the Host
			hostStart += 5;
			hostStart = removeExtraSpaces(hostStart);

			int i = 0, didFindNewLineCharacters = 0;
			while(hostStart[i] != 13 && hostStart[i] != 10 && hostStart[i] != ' ') 
			{
				i++;
				didFindNewLineCharacters = 1;
			} 

			if (i<strlen(hostStart) && didFindNewLineCharacters)
			{
				hostStart[i] = 0;
			}

			if (strlen(hostStart) == 0)
			{
				return -1;
			}

            //Check if there is any host port specified.
			if (strstr(hostStart,":"))
			{
				printf("Reached for host with port,%s\n", hostStart);
				strncpy(request->hostname,hostStart,strstr(hostStart,":")-hostStart);
				char * port_number = strstr(hostStart,":") + 1;

				if (strlen(port_number) == 0) 
				{   
					return -1;
				}

				strncpy(request->port_number,port_number,strlen(port_number));
			}
			else {

				strncpy(request->hostname,hostStart,strlen(hostStart));
				strncpy(request->port_number, "80",2);
			}

		}

		printf("Host is:%s\n", request->hostname);
		printf("Port is:%s\n", request->port_number);
	}
	else {
		return -1;
	}

	return 0;

}

char * removeExtraSpaces(char* string) {

    //trim from front
	int count = strlen(string);
	int i;
	for (i = 0; i < count; ++i)
	{
        /* code */
		if (string [i] == ' ')
		{
			continue;
		}
		else {
			break;
		}
	}

	string = string + i;

    //trim from back
	while(string[strlen(string) -1 ] == ' ' || string[strlen(string) -1 ] == '\n') {
		string[strlen(string) -1 ] = '\0';
	}


	int length = strlen(string);
	char * outputString = string;

	int j=0;
	for (i = 0; i < length; ++i)
	{
		if (string[i] == ' ' && string[i-1] == ' ' )
		{
			continue;
		}
		else {
			outputString[j] = string[i];
			j++;
		}
	}

	return outputString;

}


void closeWithError(char *error) {

	printf("%s\n",error);
	exit(1);
}

void sendBadResponse(int client_socket){
	char *badMessage="HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n<html><head><h1>Error 400. Bad Request</h1></head><body>Error 400. Bad Request</body></html>";
	int error =0;
	unsigned int length_of_err = sizeof(error);

	if(getsockopt(client_socket,SOL_SOCKET,SO_ERROR,(void *)&error, &length_of_err) != 0) {
		printf("Could not check if socket is connected.\n");
		return;
	}

	if (error != 0)
	{
		printf("Error in client socket:%s\n", strerror(error));
		return;
	}
	send(client_socket,badMessage,strlen(badMessage),0);
}

void sendQuitToServer(int server_socket) {
	printf("Sending Quit to Server\n");
	char *quitMessage = "QUIT\r\n";
	char server_buffer[1000];
	memset(server_buffer,0,1000);

	if (send(server_socket,quitMessage,strlen(quitMessage),0) == strlen(quitMessage)) {
		printf("Sent quit\n");
		if (isSocketActiveForRead(server_socket) > 0)
		{
			
	        // Receive response from server and check if all the response is received.
			recv(server_socket,&server_buffer,MAX_BUFFER_SIZE,0);
		}
		printf("Server Response for Quit:%s\n", server_buffer);
	}
	else {
		printf("Couldnt Send quit\n");
	}

}

int isFTPResponseIncomplete(char * response) {
	char* myResponse = strdup(response);

	if (strlen(myResponse) <= 2)
	{
		return 0;
	}

    //Assuming the response ends with \r\n remove the last 2 letters
	myResponse[strlen(myResponse) - 2] = 0;

	//Get the last \r\n
	while(strstr(myResponse,"\r\n") != NULL) {
		myResponse  = strstr(myResponse,"\r\n") + 2;
	}

	if (strlen(myResponse))
	{
		int index = 0;
		while(1) {
			if (myResponse[index] >= '0' && myResponse[index] <= '9')
			{
				index++;

			}
			else if (myResponse[index] == '-')
			{
				return 1;
			}
			else {
				break;
			}
		}
	}
	return 0;

}

void getPASVIPAndPort(char * response, char *ip, int *port) {
	char ip_address[50];
	memset(ip_address,0,50);

	int port_number = 0;

	char *start_of_ip = strchr(response,'(') + 1;
	char *end_of_ip = strchr(response,')');

    //Place a null at the end of IP String
	start_of_ip[end_of_ip - start_of_ip] = 0;

	int count = 0;
	while(count <= 5 && start_of_ip != NULL) {
		if (count <= 3)
		{
			int my_int_ip = atoi(start_of_ip);
			char tmp[10];
			memset(tmp,0,10);
			sprintf(tmp,"%d",my_int_ip);
			strcat(ip_address,tmp);
			if (count < 3)
			{
				strcat(ip_address,".");
			}
		}
		else if (count == 4)
		{
			port_number =  atoi(start_of_ip) * 256;
		} 
		else {
			port_number +=  atoi(start_of_ip);
		}
		start_of_ip = strstr(start_of_ip,",") + 1;
		count++;
	}

	memcpy(ip,&ip_address,strlen(ip_address));
	*port = port_number;
}

int isSocketActiveForRead(int socket) {


    fd_set sockets;
    struct timeval timeout;

    FD_ZERO(&sockets);

    timeout.tv_sec = 1;
    timeout.tv_usec = 10000;


    FD_ZERO(&sockets);
    FD_SET(socket,&sockets);

    return select(FD_SETSIZE,&sockets,(fd_set *) 0,(fd_set *) 0, &timeout);

}






