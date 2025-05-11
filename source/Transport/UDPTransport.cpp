
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

UDPTransport::UDPTransport(int inPort, optional<NetworkInterface> inInterface)
	:GenericTransport(),
	Interface(inInterface), Port(inPort)
{
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd == -1)
	{
		cerr << "Failed to create socket, port " << Port << endl;
	}

	if (Interface.has_value())
	{
		setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, Interface.value().name.c_str(), Interface.value().name.size() );
		int broadcastEnable = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
	}
	
	
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
	unique_lock lock(listenmutex);
	sockaddr_in connectionaddress;
	connectionaddress.sin_port = htons(Port);
	connectionaddress.sin_family = AF_INET;
	if (address == BroadcastClient)
	{
		if (Interface.has_value())
		{
			address = Interface.value().broadcast;
		}
		else
		{
			address = "0.0.0.0";
		}
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

std::shared_ptr<ConnectionToken> UDPTransport::Connect(sockaddr_in address)
{
	unique_lock lock(listenmutex);
	char ipbuf[16];
	inet_ntop(AF_INET, &address.sin_addr, ipbuf, sizeof(ipbuf));

	std::shared_ptr<ConnectionToken> token;
	for (auto &&i : connections)
	{
		if (memcmp(&i.second.address.sin_addr, &address.sin_addr, sizeof(address.sin_addr)) == 0)
		{
			return i.first;
		}
	}
	token = make_shared<ConnectionToken>(string(ipbuf), this);
	UDPConnection value;
	value.address = address;
	connections[token] = value;
	return token;
}

std::vector<std::shared_ptr<ConnectionToken>> UDPTransport::GetClients() const
{
	vector<shared_ptr<ConnectionToken>> clients;
	shared_lock lock(listenmutex);
	clients.reserve(connections.size());
	for (auto &connection : connections)
	{
		clients.push_back(connection.first);
	}
	return clients;
}

std::pair<int, std::shared_ptr<ConnectionToken>> UDPTransport::ReceiveBacklog(void *buffer, int maxlength)
{
	shared_lock lock(listenmutex);
	for (auto &&i : connections)
	{
		if (i.second.payloads.size() > 0)
		{
			auto payload = i.second.payloads.front();
			if (payload.size() > maxlength)
			{
				cerr << "UDP receive : Not enough space to evacuate past payload ! Truncating !" << endl;
			}
			size_t size = std::min<size_t>(payload.size(), maxlength);
			memcpy(buffer, payload.data(), size);
			i.second.payloads.pop_front();
			return {size, i.first};
		}
	}
	return {0, nullptr};
}

std::pair<int, std::shared_ptr<ConnectionToken>> UDPTransport::ReceiveFresh(void *buffer, int maxlength)
{
	shared_lock lock(listenmutex);
	sockaddr_in connectionaddress;
	socklen_t clientSize = sizeof(connectionaddress);
	int n;
	if ((n = recvfrom(sockfd, buffer, maxlength, MSG_DONTWAIT, (struct sockaddr*)&connectionaddress, &clientSize)) > 0)
	{
		for (auto &&i : connections)
		{
			if (memcmp(&i.second.address.sin_addr, &connectionaddress.sin_addr, sizeof(connectionaddress.sin_addr)) == 0)
			{
				return {n, i.first};
			}
		}
		//release shared lock for an unique lock on connect
		lock.unlock();
		lock.release();
		char ipbuf[16];
		inet_ntop(AF_INET, &connectionaddress.sin_addr, ipbuf, clientSize);
		auto token = Connect(connectionaddress);
		cout << "UDP Client connecting from " << ipbuf << endl;
		return {n, token};
	}
	return {0, nullptr};
}

std::pair<int, std::shared_ptr<ConnectionToken>> UDPTransport::ReceiveAny(void *buffer, int maxlength)
{
	auto Backlog = ReceiveBacklog(buffer, maxlength);
	if (Backlog.second != nullptr)
	{
		return Backlog;
	}
	return ReceiveFresh(buffer, maxlength);
}

std::optional<int> UDPTransport::Receive(void *buffer, int maxlength, std::shared_ptr<ConnectionToken> token)
{
	//try to dig stuff out of the backlog
	{
		shared_lock lock(listenmutex);
		auto entry = connections.find(token);
		if (entry == connections.end())
		{
			cerr << "UDP Receive : token unknown" << endl;
		}
		else if (entry->second.payloads.size() > 0)
		{
			auto payload = entry->second.payloads.front();
			if (payload.size() > maxlength)
			{
				cerr << "UDP receive : Not enough space to evacuate past payload ! Truncating !" << endl;
			}
			size_t size = std::min<size_t>(payload.size(), maxlength);
			memcpy(buffer, payload.data(), size);
			entry->second.payloads.pop_front();
			return size;
		}
	}
	
	
	std::vector<uint8_t> recvbuff(UINT16_MAX);
	std::pair<int, std::shared_ptr<ConnectionToken>> recv;
	while ((recv=ReceiveFresh(recvbuff.data(), recvbuff.size())).first > 0)
	{
		if (recv.second == token)
		{
			memcpy(buffer, recvbuff.data(), recv.first);
			return recv.first;
		}
		else
		{
			shared_lock lock(listenmutex);
			connections[recv.second].payloads.emplace_back(recvbuff.begin(), recvbuff.begin() + recv.first);
		}
	}
	return nullopt;
}

bool UDPTransport::Send(const void *buffer, int length, std::shared_ptr<ConnectionToken> token)
{
	if (!Connected)
	{
		return false;
	}

	if (length > 1500)
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
				char buffer[16];
				inet_ntop(AF_INET, &client.sin_addr, buffer, sizeof(buffer));
				cout << "UDP Client connecting from " << buffer << endl;
				Connect(client);
			}
			dataReceived[n] = 0;
				
			//cout << "Received " << n << " bytes..." << endl;
			//printBuffer(dataReceived, n);
			//string rcvstr(dataReceived, n);
			//cout << rcvstr << endl;
			
		}
	}
	
}