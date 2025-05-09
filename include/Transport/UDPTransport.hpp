#pragma once

#include <Transport/GenericTransport.hpp>

#include <thread>
#include <shared_mutex>
#include <vector>
#include <list>
#include <map>
#include <netinet/in.h>
#include <optional>

//UDP transport layer

class UDPTransport : public GenericTransport
{
private:
	struct UDPConnection
	{
		sockaddr_in address;
		std::list<std::vector<uint8_t>> payloads;
	};
	
	std::optional<NetworkInterface> Interface;
	int Port;
	int sockfd;
	bool Connected;
	std::thread* ReceiveThreadHandle;
	mutable std::shared_mutex listenmutex;
	std::map<std::shared_ptr<ConnectionToken>, UDPConnection> connections;
public:

	UDPTransport(int inPort, std::optional<NetworkInterface> inInterface);

	virtual ~UDPTransport();

	std::shared_ptr<ConnectionToken> Connect(std::string address);
	std::shared_ptr<ConnectionToken> Connect(sockaddr_in address);

	virtual std::vector<std::shared_ptr<ConnectionToken>> GetClients() const override;

	//Receive any data accumulated in the connections
	std::pair<int, std::shared_ptr<ConnectionToken>> ReceiveBacklog(void *buffer, int maxlength);
	//Receive data fresh from the socket
	std::pair<int, std::shared_ptr<ConnectionToken>> ReceiveFresh(void *buffer, int maxlength);
	//Receive old or new data, don't care
	std::pair<int, std::shared_ptr<ConnectionToken>> ReceiveAny(void *buffer, int maxlength);

	virtual std::optional<int> Receive(void *buffer, int maxlength, std::shared_ptr<ConnectionToken> token) override;
	
	virtual bool Send(const void* buffer, int length, std::shared_ptr<ConnectionToken> token) override;

	void receiveThread();
};
