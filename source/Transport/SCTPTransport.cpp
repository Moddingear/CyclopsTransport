#include "Transport/SCTPTransport.hpp"
#include <Transport/ConnectionToken.hpp>

#include <iostream>
#include <filesystem>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <mutex>
#include <Transport/thread-rename.hpp>

using namespace std;

SCTPTransport::SCTPTransport(bool inServer, string inIP, int inPort, string inInterface)
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

	cout << "Created SCTP transport " << IP << ":" << Port << " @ " << Interface <<endl;
}

SCTPTransport::~SCTPTransport()
{
	cout << "Destroying SCTP transport " << IP << ":" << Port << " @ " << Interface <<endl;
	if (Server)
	{
		for (auto &connection : connections)
		{
			shutdown(connection.second.filedescriptor, SHUT_RDWR);
			close(connection.second.filedescriptor);
			connection.first->Disconnect();
		}
	}
	if (sockfd != -1)
	{
		shutdown(sockfd, SHUT_RDWR);
		close(sockfd);
	}
}

void SCTPTransport::CreateSocket()
{
	if (sockfd != -1)
	{
		return;
	}
	int type = Server ? SOCK_SEQPACKET | SOCK_NONBLOCK : SOCK_SEQPACKET;
	sockfd = socket(PF_INET, type, IPPROTO_SCTP);
	if (sockfd == -1)
	{
		cerr << "SCTP Failed to create socket, port " << Port << endl;
	}
	
	/*if(setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, Interface.c_str(), Interface.size()))
	{
		cerr << "SCTP Failed to bind to interface : " << strerror(errno) << endl;
	}*/

	const int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
	{
		cerr << "setsockopt(SO_REUSEADDR) failed" << endl;
	}
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
	{
		cerr << "setsockopt(SO_REUSEPORT) failed" << endl;
	}
	
}

bool SCTPTransport::Connect()
{
	struct sockaddr_in serverAddress;
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(Port);

	string ip = Server ? "0.0.0.0" : IP;

	if (inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr) <= 0) {
		cerr << "SCTP ERROR : Invalid address/ Address not supported \n" << endl;
	}

	if (Server)
	{
		//cout << "SCTP Binding socket..." << endl;
		if (bind(sockfd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1) 
		{
			cerr << "SCTP Can't bind to IP/port, " << strerror(errno) << endl;
		}
		//cout << "SCTP Marking socket for listening" << endl;
		if (listen(sockfd, SOMAXCONN) == -1)
		{
			cerr << "SCTP Can't listen !" << endl;
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
		cout << "SCTP connected to server" << endl;
		Connected = true;
		SCTPConnection connection;
		connection.name = ip;
		connection.address = serverAddress;
		connection.filedescriptor = sockfd;
		auto token = make_shared<ConnectionToken>(ip, this);
		connections[token] = connection;
		return true;
	}
}

void SCTPTransport::CheckConnection()
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

void SCTPTransport::DeleteSocket(int fd)
{
	close(fd);
}

vector<shared_ptr<ConnectionToken>> SCTPTransport::GetClients() const
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

std::optional<int> SCTPTransport::Receive(void* buffer, int maxlength, std::shared_ptr<ConnectionToken> token)
{
	if (!Server)
	{
		CheckConnection();
	}
	if (!CheckToken(token))
	{
		return nullopt;
	}
	int fd;
	{
		shared_lock lock(listenmutex);
		auto value = connections.find(token);
		if (value == connections.end())
		{
			cerr << "Token not found in connections while receiving !" << endl;
			return nullopt;
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
	if (!token->IsConnected()) //disconnected after receiving
	{
		return nullopt;
	}
	return numreceived;
}


bool SCTPTransport::Send(const void* buffer, int length,  std::shared_ptr<ConnectionToken> token)
{
	if (!Server)
	{
		CheckConnection();
	}
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


vector<shared_ptr<ConnectionToken>> SCTPTransport::AcceptNewConnections()
{
	if (!Server)
	{
		return {};	
	}
	vector<shared_ptr<ConnectionToken>> newconnections;
	CheckConnection();
	while (1)
	{
		SCTPConnection connection;
		socklen_t clientSize = sizeof(connection.address);
		bzero(&connection.address, clientSize);
		connection.filedescriptor = accept4(sockfd, (struct sockaddr *)&connection.address, &clientSize, 0);
		if (connection.filedescriptor > 0)
		{
			char buffer[16];
			inet_ntop(AF_INET, &connection.address.sin_addr, buffer, sizeof(buffer));
			buffer[sizeof(buffer)-1] = 0;
			connection.name = string(buffer, strlen(buffer));
			cout << "SCTP Client connecting from " << connection.name << " fd=" << connection.filedescriptor << endl;
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
				cerr << "SCTP Unhandled error on accept: " << strerror(errno) << endl;
				break;
			}
			break;
		}
	}
	return newconnections;
}

void SCTPTransport::DisconnectClient(std::shared_ptr<ConnectionToken> token)
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
		cout << "SCTP Client " << value->second.name << "@fd" << value->second.filedescriptor << " disconnected." <<endl;
	}
	else
	{
		cout << "SCTP Server " << value->second.name << "@fd" << value->second.filedescriptor << " disconnected." <<endl;
		sockfd = -1; //in the case of the client, the sockfd is that of the root socket
	}
	connections.erase(value);
}