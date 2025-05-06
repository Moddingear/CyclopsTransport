
#include "Transport/UDPTransport.hpp"

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
#include <Transport/ConnectionToken.hpp>

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

std::shared_ptr<ConnectionToken> UDPTransport::Connect(std::string address)
{
	sockaddr_in connectionaddress;
	connectionaddress.sin_port = htons(Port);
	connectionaddress.sin_family = AF_INET;
	if (address == BroadcastClient)
	{
		address = Interface.broadcast;
	}
	inet_pton(AF_INET, address.c_str(), &connectionaddress.sin_addr);

	std::shared_ptr<ConnectionToken> token;
	for (auto &&i : connections)
	{
		if (memcmp(&i.second.address.sin_addr, &connectionaddress.sin_addr, sizeof(connectionaddress.sin_addr)) == 0)
		{
			return i.first;
		}
	}
	token = make_shared<ConnectionToken>(address, this);
	UDPConnection value;
	value.address = connectionaddress;
	connections[token] = value;
	return token;
}

bool UDPTransport::Send(const void *buffer, int length, std::shared_ptr<ConnectionToken> token)
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
	auto key = connections.find(token);
	if (key == connections.end())
	{
		return false;
	}
	const sockaddr_in &connectionaddress = key->second.address;
	
	shared_lock lock(listenmutex);
	
	int err = sendto(sockfd, buffer, length, 0, (struct sockaddr*)&connectionaddress, sizeof(sockaddr_in));
	if (err==-1 && (errno != EAGAIN && errno != EWOULDBLOCK))
	{
		cerr << "UDP Server failed to send data to " << token->GetConnectionName() << " : " << errno << "(" << strerror(errno) << ")" << endl;
	}
	return true;
}

std::optional<int> UDPTransport::Receive(void *buffer, int maxlength, std::shared_ptr<ConnectionToken> token)
{

	sockaddr_in connectionaddress;
	socklen_t clientSize = sizeof(connectionaddress);
	int n;
	std::vector<uint8_t> recvbuff(UINT16_MAX);
	while ((n = recvfrom(sockfd, recvbuff.data(), recvbuff.size(), 0, (struct sockaddr*)&connectionaddress, &clientSize)) > 0)
	{
		char ipbuf[16];
		inet_ntop(AF_INET, &connectionaddress.sin_addr, ipbuf, clientSize);
		bool found = false;
		for (auto &&i : connections)
		{
			if (memcmp(&i.second.address.sin_addr, &connectionaddress.sin_addr, sizeof(connectionaddress.sin_addr)) == 0)
			{
				found = true;
				i.second.payloads.emplace_back(recvbuff.begin(), recvbuff.begin() + n);
			}
			
		}
		if (!found)
		{
			auto token = Connect(ipbuf);
			connections[token].payloads.emplace_back(recvbuff.begin(), recvbuff.begin() + n);
			cout << "UDP Client connecting from " << ipbuf << endl;
		}
	}

	auto key = connections.find(token);
	if (key == connections.end())
	{
		return false;
	}
	if (key->second.payloads.size() >0)
	{
		auto payload = key->second.payloads.front();
		n = payload.size();
		if (payload.size() > maxlength)
		{
			cerr << "Not enough space in buffer to output UDP payload"  << endl;
			return -1;
		}
		memcpy(buffer, payload.data(), payload.size());
		key->second.payloads.pop_front();
		return n;
	}
	else
	{
		return nullopt;
	}
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
			for (auto &&i : connections)
			{
				if (memcmp(&i.second.address.sin_addr, &client.sin_addr, sizeof(client.sin_addr)) == 0)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				char buffer[100];
				inet_ntop(AF_INET, &client.sin_addr, buffer, clientSize);
				cout << "UDP Client connecting from " << buffer << endl;
				Connect(buffer);
			}
			dataReceived[n] = 0;
				
			//cout << "Received " << n << " bytes..." << endl;
			//printBuffer(dataReceived, n);
			//string rcvstr(dataReceived, n);
			//cout << rcvstr << endl;
			
		}
	}
	
}