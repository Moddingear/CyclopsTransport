#pragma once

#include <Communication/Transport/GenericTransport.hpp>

#include <shared_mutex>
#include <vector>
#include <map>
#include <netinet/in.h>

#include <Misc/Task.hpp>

//TCP transport layer



class SCTPTransport : public GenericTransport
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

	SCTPTransport(bool inServer, std::string inIP, int inPort, std::string inInterface);

	virtual ~SCTPTransport();

private:
	void CreateSocket();
	bool Connect();
	void CheckConnection();
	void LowerLatency(int fd);
	void DeleteSocket(int fd);
public:

	virtual std::vector<std::shared_ptr<ConnectionToken>> GetClients() const override;

	virtual int Receive(void *buffer, int maxlength, std::string client, bool blocking=false) override;

	virtual bool Send(const void* buffer, int length, std::string client) override;



	std::vector<std::shared_ptr<ConnectionToken>> AcceptNewConnections();

protected:
	virtual std::optional<int> Receive(void* buffer, int maxlength, std::shared_ptr<ConnectionToken> token) override;

	virtual bool Send(const void* buffer, int length,  std::shared_ptr<ConnectionToken> token) override;

	virtual void DisconnectClient(std::shared_ptr<ConnectionToken> token) override;
};
