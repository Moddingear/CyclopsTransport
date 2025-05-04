#pragma once

#include <Transport/GenericTransport.hpp>

#include <thread>
#include <shared_mutex>
#include <vector>
#include <list>
#include <map>
#include <netinet/in.h>

//UDP transport layer

class UDPTransport : public GenericTransport
{
private:
	struct UDPConnection
	{
		sockaddr_in address;
		std::list<std::vector<uint8_t>> payloads;
	};
	
	NetworkInterface Interface;
	int Port;
	int sockfd;
	bool Connected;
	std::thread* ReceiveThreadHandle;
	mutable std::shared_mutex listenmutex;
	std::map<std::shared_ptr<ConnectionToken>, UDPConnection> connections;
public:

	UDPTransport(int inPort, NetworkInterface inInterface);

	virtual ~UDPTransport();

	std::shared_ptr<ConnectionToken> Connect(std::string address);

	//virtual std::vector<std::string> GetClients() const override;

	virtual std::optional<int> Receive(void *buffer, int maxlength, std::shared_ptr<ConnectionToken> token) override;
	
	virtual bool Send(const void* buffer, int length, std::shared_ptr<ConnectionToken> token) override;

	void receiveThread();
};
