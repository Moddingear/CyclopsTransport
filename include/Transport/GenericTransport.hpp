#pragma once

#include <utility>
#include <vector>
#include <string>
#include <mutex>
#include <set>
#include <memory>
#include <optional>

class ConnectionToken;

//Generic class to send data to other programs
class GenericTransport
{
private:
	static std::mutex TransportListMutex;
	static std::set<GenericTransport*> ActiveTransportList;
public:

	GenericTransport();

	virtual ~GenericTransport();

	static void DeleteAllTransports();

	static const std::string BroadcastClient;

	static void printBuffer(const void *buffer, int length);

	virtual std::vector<std::shared_ptr<ConnectionToken>> GetClients() const;

	bool CheckToken(const std::shared_ptr<ConnectionToken> &token);

	//receive from clients
	//return number of bytes read
	virtual int Receive(void *buffer, int maxlength, std::string client, bool blocking=false);


	virtual bool Send(const void* buffer, int length, std::string client);

protected:
	//receive data using token. No return value = disconnected
	//If disconnected, the transport forgets the token
	virtual std::optional<int> Receive(void* buffer, int maxlength, std::shared_ptr<ConnectionToken> token);
	//send data using token. false = disconnected
	//If disconnected, the transport forgets the token
	virtual bool Send(const void* buffer, int length,  std::shared_ptr<ConnectionToken> token);

	//Disconnect a client : the transport forgets about the client and the token
	virtual void DisconnectClient(std::shared_ptr<ConnectionToken> token);

public:

	struct NetworkInterface
	{
		std::string name;
		std::string address;
		std::string mask;
		std::string broadcast;
	};

	static std::vector<NetworkInterface> GetInterfaces();

	friend ConnectionToken;
};
