#pragma once

#include <Transport/GenericTransport.hpp>

#include <shared_mutex>
#include <vector>
#include <map>
#include <netinet/in.h>

#include <Transport/Task.hpp>

//TCP transport layer



class TCPTransport : public GenericTransport
{
private:
	struct TCPConnection
	{
		int filedescriptor;
		sockaddr_in address;
		std::string name;
	};

	bool Server;
	std::string IP, Interface;
	int Port;
	int sockfd;
	bool Connected;
	mutable std::shared_mutex listenmutex; //protects connections
	std::map<std::shared_ptr<ConnectionToken>, TCPConnection> connections;
public:

	TCPTransport(bool inServer, std::string inIP, int inPort, std::string inInterface);

	virtual ~TCPTransport();

private:
	void CreateSocket();
	bool Connect();
	void CheckConnection();
	void LowerLatency(int fd);
	void DeleteSocket(int fd);
public:

	virtual std::vector<std::shared_ptr<ConnectionToken>> GetClients() const override;

	std::vector<std::shared_ptr<ConnectionToken>> AcceptNewConnections();

protected:
	virtual std::optional<int> Receive(void* buffer, int maxlength, std::shared_ptr<ConnectionToken> token) override;

	virtual bool Send(const void* buffer, int length,  std::shared_ptr<ConnectionToken> token) override;

	virtual void DisconnectClient(std::shared_ptr<ConnectionToken> token) override;
};
