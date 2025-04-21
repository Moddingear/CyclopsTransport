
#include "Communication/Transport/UDPTransport.hpp"

#include <iostream>
#include <filesystem>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

using namespace std;

UDPTransport::UDPTransport(int inPort, NetworkInterface inInterface)
	:GenericTransport(),
	Interface(inInterface), Port(inPort)
{
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd == -1)
	{
		cerr << "Failed to create socket, port " << Port << endl;
	}

	setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, Interface.name.c_str(), Interface.name.size() );
	int broadcastEnable = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(Port);

	string ip = "0.0.0.0"; //accept all

	if (inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr) <= 0) {
		cerr << "UDP ERROR : Invalid address/ Address not supported \n" << endl;
	}

	//cout << "UDP Binding socket" << endl;
	if (bind(sockfd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1) 
	{
		cerr << "UDP Can't bind to IP/port, " << strerror(errno) << endl;
	}
	Connected = true;
	//ReceiveThreadHandle = new thread(&UDPTransport::receiveThread, this);
}

UDPTransport::~UDPTransport()
{
	if (sockfd != -1)
	{
		close(sockfd);
	}
}

bool UDPTransport::Send(const void *buffer, int length, std::string client)
{
	if (!Connected)
	{
		return false;
	}

	if (length > 1000)
	{
		cerr << "WARNING : Packet length over 1000, packet may be dropped" << endl;
	}
	//cout << "Sending " << length << " bytes..." << endl;
	//printBuffer(buffer, length);
	shared_lock lock(listenmutex);
	sockaddr_in connectionaddress;
	connectionaddress.sin_port = Port;
	connectionaddress.sin_family = AF_INET;
	if (client == BroadcastClient)
	{
		client = Interface.broadcast;
	}
	inet_pton(AF_INET, client.c_str(), &connectionaddress.sin_addr);
	
	int err = sendto(sockfd, buffer, length, 0, (struct sockaddr*)&connectionaddress, sizeof(sockaddr_in));
	if (err==-1 && (errno != EAGAIN && errno != EWOULDBLOCK))
	{
		cerr << "UDP Server failed to send data to " << client << " : " << errno << "(" << strerror(errno) << ")" << endl;
	}
	return true;
}

int UDPTransport::Receive(void *buffer, int maxlength, string client, bool blocking)
{
	(void)blocking;

	int n;
	sockaddr_in connectionaddress;
	socklen_t clientSize = sizeof(connectionaddress);
	bzero(&connectionaddress, clientSize);
	if (client == BroadcastClient)
	{
		client = "0.0.0.0";
	}
	inet_pton(AF_INET, client.c_str(), &connectionaddress.sin_addr);
	if ((n = recvfrom(sockfd, buffer, maxlength, 0, (struct sockaddr*)&connectionaddress, &clientSize)) > 0)
	{
		char ipbuf[16];
		inet_ntop(AF_INET, &connectionaddress.sin_addr, ipbuf, clientSize);
		cout << "UDP Client connecting from " << ipbuf << endl;
		return n;
		
	}
	return -1;
}
	
void UDPTransport::receiveThread()
{
	//cout << "UDP Webserver thread started" << endl;
	char dataReceived[1025];
	int n;
	while (1)
	{
		this_thread::sleep_for(chrono::milliseconds(100));
		
		sockaddr_in client;
		socklen_t clientSize = sizeof(client);
		bzero(&client, clientSize);
		shared_lock lock(listenmutex);
		while ((n = recvfrom(sockfd, dataReceived, sizeof(dataReceived)-1, 0, (struct sockaddr*)&client, &clientSize)) > 0)
		{
			bool found = false;
			for (size_t i = 0; i < connectionaddresses.size(); i++)
			{
				if (connectionaddresses[i].sin_addr.s_addr == client.sin_addr.s_addr)
				{
					found = true;
					break;
				}
				
			}
			if (!found)
			{
				connectionaddresses.push_back(client);
				char buffer[100];
				inet_ntop(AF_INET, &client.sin_addr, buffer, clientSize);
				cout << "UDP Client connecting from " << buffer << endl;
			}
			dataReceived[n] = 0;
				
			//cout << "Received " << n << " bytes..." << endl;
			//printBuffer(dataReceived, n);
			//string rcvstr(dataReceived, n);
			//cout << rcvstr << endl;
			
		}
	}
	
}