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

std::shared_ptr<ConnectionToken> SCTPTransport::Connect(std::string address)
{
	sockaddr_in connectionaddress;
	connectionaddress.sin_port = Port;
	connectionaddress.sin_family = AF_INET;
	if (address == BroadcastClient)
	{
		address = "0.0.0.0";
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
	SCTPConnection value;
	value.address = connectionaddress;
	connections[token] = value;
	return token;
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
	struct sockaddr_in dest_addr;
	{
		shared_lock lock(listenmutex);
		auto value = connections.find(token);
		if (value == connections.end())
		{
			cerr << "Token not found in connections while receiving !" << endl;
			return nullopt;
		}
		dest_addr = value->second.address;
	}
	bool broadcast = dest_addr.sin_addr.s_addr == 0;
	

	struct iovec io_buf;
	io_buf.iov_base = buffer;
	io_buf.iov_len = maxlength;

	struct msghdr msg;
	memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_iov = &io_buf;
	msg.msg_iovlen = 1;
	msg.msg_name = &dest_addr;
	msg.msg_namelen = sizeof(struct sockaddr_in);

	int numreceived = recvmsg(sockfd, &msg, MSG_DONTWAIT);

	if (broadcast)
	{
		if (numreceived > 0)
		{
			char ipbuf[16];
			inet_ntop(AF_INET, &dest_addr.sin_addr, ipbuf, sizeof(dest_addr));
			bool found = false;
			for (auto &&i : connections)
			{
				if (memcmp(&i.second.address.sin_addr, &dest_addr.sin_addr, sizeof(dest_addr.sin_addr)) == 0)
				{
					found = true;
					//i.second.payloads.emplace_back(recvbuff.begin(), recvbuff.begin() + n);
				}
				
			}
			if (!found)
			{
				auto token = Connect(ipbuf);
				//connections[token].payloads.emplace_back(recvbuff.begin(), recvbuff.begin() + n);
				cout << "SCTP Client connecting from " << ipbuf << endl;
			}
		}
		
	}
	
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
	struct sockaddr_in dest_addr;
	{
		shared_lock lock(listenmutex);
		auto value = connections.find(token);
		if (value == connections.end())
		{
			cerr << "Token not found in connections while sending !" << endl;
			return false;
		}
		dest_addr = value->second.address;
	}
	struct iovec io_buf;
    io_buf.iov_base = const_cast<void*>(buffer);
    io_buf.iov_len = length; //max 213000

    struct msghdr msg;
    memset(&msg, 0, sizeof(struct msghdr));
    msg.msg_iov = &io_buf;
    msg.msg_iovlen = 1;
    msg.msg_name = &dest_addr;
    msg.msg_namelen = sizeof(struct sockaddr_in);

	int numsent = sendmsg(sockfd, &msg, MSG_NOSIGNAL);
	int errnocp = errno;
	if (numsent == -1 && (errnocp != EAGAIN && errnocp != EWOULDBLOCK))
	{
		switch (errno)
		{
		case EAGAIN:
		#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
		#endif
			break;
		
		default:
			//got disconnected
			token->Disconnect();
			break;
		}
		
	}
	return token->IsConnected();
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
	
	if (Server)
	{
		cout << "SCTP Client " << token->GetConnectionName() << " disconnected." <<endl;
	}
	else
	{
		cout << "SCTP Server " << token->GetConnectionName() << " disconnected." <<endl;
		sockfd = -1; //in the case of the client, the sockfd is that of the root socket
	}
	connections.erase(value);
}