#pragma once

#include <Communication/Transport/GenericTransport.hpp>

#include <thread>
#include <shared_mutex>
#include <vector>
#include <netinet/in.h>

//UDP transport layer

class UDPTransport : public GenericTransport
{
private:
	NetworkInterface Interface;
	int Port;
	int sockfd;
	bool Connected;
	std::thread* ReceiveThreadHandle;
	mutable std::shared_mutex listenmutex;
	std::vector<sockaddr_in> connectionaddresses;
public:

	UDPTransport(int inPort, NetworkInterface inInterface);

	virtual ~UDPTransport();

	//virtual std::vector<std::string> GetClients() const override;

	virtual int Receive(void *buffer, int maxlength, std::string client, bool blocking=false) override;
	
	virtual bool Send(const void* buffer, int length, std::string client) override;

	void receiveThread();
};
