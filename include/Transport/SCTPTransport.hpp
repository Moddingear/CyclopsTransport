#pragma once

#include <Transport/GenericTransport.hpp>

#include <shared_mutex>
#include <vector>
#include <map>
#include <list>
#include <netinet/in.h>

#include <Transport/Task.hpp>

//TCP transport layer



class SCTPTransport : public GenericTransport
{
private:
	struct SCTPConnection
	{
		sockaddr_in address;
		std::string name;
	};

	bool Server;
	std::string IP, Interface;
	int Port;
	int sockfd;
	bool Connected;
	mutable std::shared_mutex listenmutex; //protects connections
	std::map<std::shared_ptr<ConnectionToken>, SCTPConnection> connections;
public:

	SCTPTransport(bool inServer, std::string inIP, int inPort, std::string inInterface);

	virtual ~SCTPTransport(); //frees all allocated sockets

private:
	void CreateSocket(); //create unix socket
	bool Connect(); //attempt to connect to server
	void CheckConnection(); //create socket and connect if needed
	void DeleteSocket(int fd); //free socket
public:

	std::shared_ptr<ConnectionToken> Connect(std::string address);

	virtual std::vector<std::shared_ptr<ConnectionToken>> GetClients() const override;

protected:

	virtual std::optional<int> Receive(void* buffer, int maxlength, std::shared_ptr<ConnectionToken> token) override;

	virtual bool Send(const void* buffer, int length,  std::shared_ptr<ConnectionToken> token) override;

	virtual void DisconnectClient(std::shared_ptr<ConnectionToken> token) override;
};
