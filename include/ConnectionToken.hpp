#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>

class GenericTransport;

class ConnectionToken : public std::enable_shared_from_this<ConnectionToken>
{
private:
	std::string ConnectionName;
	bool connected;
	GenericTransport* Parent;
public:
	ConnectionToken(std::string InConnectionName, GenericTransport* InParent = nullptr);
	~ConnectionToken();

	std::shared_ptr<ConnectionToken> getptr()
	{
		return shared_from_this();
	}

	bool IsConnected()
	{
		return connected;
	}
	void Disconnect();

	GenericTransport* GetParent()
	{
		return Parent;
	}

	const std::string& GetConnectionName() const
	{
		return ConnectionName;
	}


	//receive data using token. No return value = disconnected
	//If disconnected, the transport forgets the token
	std::optional<int> Receive(void* buffer, int maxlength);

	//send data using token. false = disconnected
	//If disconnected, the transport forgets the token
	bool Send(const void* buffer, int length);
};