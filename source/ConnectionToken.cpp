#include <Communication/Transport/ConnectionToken.hpp>
#include <Communication/Transport/GenericTransport.hpp>
#include <iostream>

using namespace std;

ConnectionToken::ConnectionToken(std::string InConnectionName, GenericTransport* InParent)
	:ConnectionName(InConnectionName), connected(InParent), Parent(InParent)
{
	//cout << "Token " << ConnectionName << " created" <<endl;
}

ConnectionToken::~ConnectionToken()
{
	//cout << "Token " << ConnectionName << " cleaned" <<endl;
}

std::optional<int> ConnectionToken::Receive(void* buffer, int maxlength)
{
	if (!Parent || !connected)
	{
		return nullopt;
	}
	
	return Parent->Receive(buffer, maxlength, shared_from_this());
}

bool ConnectionToken::Send(const void* buffer, int length)
{
	if (!Parent || !connected)
	{
		return false;
	} 
	return Parent->Send(buffer, length, shared_from_this());
}

void ConnectionToken::Disconnect()
{
	if (connected)
	{
		connected = false;
		Parent->DisconnectClient(shared_from_this());
	}
}