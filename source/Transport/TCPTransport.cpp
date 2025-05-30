#include "Transport/TCPTransport.hpp"
#include <Transport/ConnectionToken.hpp>

#include <iostream>
#include <filesystem>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <mutex>
#include <Transport/thread-rename.hpp>

using namespace std;

TCPTransport::TCPTransport(bool inServer, string inIP, int inPort, string inInterface)
	: GenericTransport()
{	
	Server = inServer;
	IP = inIP;
	Port = inPort;
	Interface = inInterface;
	sockfd = -1;
	Connected = false;
	CreateSocket();
	Connect();

	cout << "Created TCP transport " << IP << ":" << Port << " @ " << Interface <<endl;
}

TCPTransport::~TCPTransport()
{
	cout << "Destroying TCP transport " << IP << ":" << Port << " @ " << Interface <<endl;
	for (auto &connection : connections)
	{
		shutdown(connection.second.filedescriptor, SHUT_RDWR);
		close(connection.second.filedescriptor);
		connection.first->Disconnect();
	}
	if (sockfd != -1)
	{
		shutdown(sockfd, SHUT_RDWR);
		close(sockfd);
	}
}

void TCPTransport::CreateSocket()
{
	if (sockfd != -1)
	{
		return;
	}
	int type = Server ? SOCK_STREAM | SOCK_NONBLOCK : SOCK_STREAM;
	sockfd = socket(AF_INET, type, 0);
	if (sockfd == -1)
	{
		cerr << "TCP Failed to create socket, port " << Port << endl;
	}
	
	if (Interface.size())
	{
		if(setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, Interface.c_str(), Interface.size()))
		{
			cerr << "TCP Failed to bind to interface : " << strerror(errno) << endl;
		}
	}

	const int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
	{
		cerr << "setsockopt(SO_REUSEADDR) failed" << endl;
	}
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
	{
		cerr << "setsockopt(SO_REUSEPORT) failed" << endl;
	}
	//LowerLatency(sockfd);
	
}

bool TCPTransport::Connect()
{
	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(Port);

	string ip = Server ? "0.0.0.0" : IP;

	if (inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr) <= 0) {
		cerr << "TCP ERROR : Invalid address/ Address not supported \n" << endl;
	}

	if (Server)
	{
		//cout << "TCP Binding socket..." << endl;
		if (bind(sockfd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1) 
		{
			cerr << "TCP Can't bind to IP/port, " << strerror(errno) << endl;
		}
		//cout << "TCP Marking socket for listening" << endl;
		if (listen(sockfd, SOMAXCONN) == -1)
		{
			cerr << "TCP Can't listen !" << endl;
		}
		Connected = true;
		return true;
	}
	else
	{
		// communicates with listen
		if(connect(sockfd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1)
		{
			//cerr << "Failed to connect to server" << endl;
			return false;
		}
		cout << "TCP connected to server" << endl;
		Connected = true;
		TCPConnection connection;
		connection.name = ip;
		connection.address = serverAddress;
		connection.filedescriptor = sockfd;
		auto token = make_shared<ConnectionToken>(ip, this);
		connections[token] = connection;
		return true;
	}
}

void TCPTransport::CheckConnection()
{
	if (sockfd == -1)
	{
		CreateSocket();
	}
	if (!Connected)
	{
		Connect();
	}
}

void TCPTransport::LowerLatency(int fd)
{
	int corking = 0;
	if (setsockopt(fd, IPPROTO_TCP, TCP_CORK, &corking, sizeof(corking)))
	{
		cerr << "TCP Failed to disable corking : " << errno << endl;
	}
	int nodelay = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)))
	{
		cerr << "TCP Failed to disable corking : " << errno << endl;
	}
}

void TCPTransport::DeleteSocket(int fd)
{
	close(fd);
}

vector<shared_ptr<ConnectionToken>> TCPTransport::GetClients() const
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

std::optional<int> TCPTransport::Receive(void* buffer, int maxlength, std::shared_ptr<ConnectionToken> token)
{
	if (!CheckToken(token))
	{
		return false;
	}
	int fd;
	{
		shared_lock lock(listenmutex);
		auto value = connections.find(token);
		if (value == connections.end())
		{
			cerr << "Token not found in connections while receiving !" << endl;
			return false;
		}
		fd = value->second.filedescriptor;
	}
	int numreceived = recv(fd, buffer, maxlength, MSG_DONTWAIT);
	if (numreceived <= 0)
	{
		if (numreceived == 0)
		{
			//got disconnected
			token->Disconnect();
		}
		else if (numreceived == -1)
		{
			//do nothing || errno == EWOULDBLOCK || errno == EAGAIN
			numreceived = 0;
		}
	}
	if (!token->IsConnected())
	{
		return nullopt;
	}
	return numreceived;
}


bool TCPTransport::Send(const void* buffer, int length,  std::shared_ptr<ConnectionToken> token)
{
	if (!CheckToken(token))
	{
		return false;
	}
	int fd = 0;
	{
		shared_lock lock(listenmutex);
		auto value = connections.find(token);
		if (value == connections.end())
		{
			cerr << "Token not found in connections while sending !" << endl;
			return false;
		}
		fd = value->second.filedescriptor;
	}
	int numsent = send(fd, buffer, length, MSG_NOSIGNAL);
	int errnocp = errno;
	if (numsent == -1 && (errnocp != EAGAIN && errnocp != EWOULDBLOCK))
	{
		//got disconnected
		token->Disconnect();
	}
	return token->IsConnected();
}


vector<shared_ptr<ConnectionToken>> TCPTransport::AcceptNewConnections()
{
	if (!Server)
	{
		return {};	
	}
	vector<shared_ptr<ConnectionToken>> newconnections;
	CheckConnection();
	while (1)
	{
		TCPConnection connection;
		socklen_t clientSize = sizeof(connection.address);
		bzero(&connection.address, clientSize);
		connection.filedescriptor = accept4(sockfd, (struct sockaddr *)&connection.address, &clientSize, 0);
		if (connection.filedescriptor > 0)
		{
			//LowerLatency(ret);
			char buffer[16];
			inet_ntop(AF_INET, &connection.address.sin_addr, buffer, sizeof(buffer));
			buffer[sizeof(buffer)-1] = 0;
			connection.name = string(buffer, strlen(buffer));
			cout << "TCP Client connecting from " << connection.name << " fd=" << connection.filedescriptor << endl;
			int num_connections_from_same_ip = 0;
			{
				shared_lock lock(listenmutex);
				for (auto &already : connections)
				{
					if (already.second.name == connection.name)
					{
						num_connections_from_same_ip++;
					}
				}
				if (num_connections_from_same_ip > 0)
				{
					cerr << "Warning: " << connection.name << " is already connected " << num_connections_from_same_ip << " times" << endl;
				}
			}
			
			unique_lock lock(listenmutex);
			auto token = make_shared<ConnectionToken>(connection.name, this);
			connections[token] = connection;
			newconnections.push_back(token);
		}
		else 
		{
			switch (errno)
			{
			case EAGAIN:
				return newconnections;
			
			default:
				cerr << "TCP Unhandled error on accept: " << strerror(errno) << endl;
				break;
			}
			break;
		}
	}
	return newconnections;
}

void TCPTransport::DisconnectClient(std::shared_ptr<ConnectionToken> token)
{
	unique_lock lock(listenmutex);
	auto value = connections.find(token);
	if (value == connections.end())
	{
		cerr << "Token not found in connections while disconnecting !" << endl;
		return;
	}
	
	DeleteSocket(value->second.filedescriptor);
	if (Server)
	{
		cout << "TCP Client " << value->second.name << "@fd" << value->second.filedescriptor << " disconnected." <<endl;
	}
	else
	{
		cout << "TCP Server " << value->second.name << "@fd" << value->second.filedescriptor << " disconnected." <<endl;
		sockfd = -1;
	}
	
	connections.erase(value);
}