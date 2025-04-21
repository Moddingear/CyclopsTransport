#include "Communication/Transport/GenericTransport.hpp"
#include <Communication/Transport/ConnectionToken.hpp>

#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;

const string GenericTransport::BroadcastClient = "all";
std::mutex GenericTransport::TransportListMutex;
std::set<GenericTransport*> GenericTransport::ActiveTransportList = {};

GenericTransport::GenericTransport()
{
	unique_lock mutlock(TransportListMutex);
	ActiveTransportList.emplace(this);
}

GenericTransport::~GenericTransport()
{
	unique_lock mutlock(TransportListMutex);
	ActiveTransportList.erase(this);
}

void GenericTransport::DeleteAllTransports()
{
	while (ActiveTransportList.size() > 0)
	{
		cout << "Removing transport " << *ActiveTransportList.begin() << endl;
		delete *ActiveTransportList.begin();
	}
}

void GenericTransport::printBuffer(const void* buffer, int length)
{
	ostringstream stream;
	stream << std::setfill ('0') << std::setw(3) 
		<< std::hex;
	int i;
	for (i = 0; i < 8; i++)
	{
		stream << std::setw(2) << (unsigned int)i << " ";
	}
	
	for (i = 0; i < length; i++)
	{
		if (i%8 == 0)
		{
			stream << endl;
		}
		stream << std::setw(2) << (unsigned int)((uint8_t*)buffer)[i] << " ";
	}
	stream << endl;
	
	cout << stream.str();
}

vector<shared_ptr<ConnectionToken>> GenericTransport::GetClients() const
{
	return {};
}

bool GenericTransport::CheckToken(const std::shared_ptr<ConnectionToken> &token)
{
	if (!token)
	{
		return false;
	}
	if (!token->IsConnected())
	{
		return false;
	}
	if (!token->GetParent())
	{
		return false;
	}
	return true;
}

int GenericTransport::Receive(void *buffer, int maxlength, string client, bool blocking)
{
	(void)buffer;
	(void)maxlength;
	(void)client;
	(void)blocking;
	cerr << "Called receive on base transport class" << endl;
	return -1;
}

bool GenericTransport::Send(const void* buffer, int length, string client)
{
	(void)buffer;
	(void)length;
	(void)client;
	cerr << "Called Send on base transport class" << endl;
	return false;
}



optional<int> GenericTransport::Receive(void *buffer, int maxlength, std::shared_ptr<ConnectionToken> token)
{
	int numreceived = Receive(buffer, maxlength, token->GetConnectionName(), false);
	if (numreceived == 0)
	{
		return nullopt;
	}
	return numreceived;
}


bool GenericTransport::Send(const void* buffer, int length, std::shared_ptr<ConnectionToken> token)
{
	return Send(buffer, length, token->GetConnectionName());
}

void GenericTransport::DisconnectClient(std::shared_ptr<ConnectionToken> token)
{
	(void) token;
}

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

vector<GenericTransport::NetworkInterface> GenericTransport::GetInterfaces()
{
	struct ifaddrs *ifaddr, *ifa;
	int s;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) == -1) 
	{
		perror("getifaddrs");
		return {};
	}

	vector<NetworkInterface> Interfaces;

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
	{
		if (ifa->ifa_addr == NULL)
		{
			continue;
		}
		if(ifa->ifa_addr->sa_family!=AF_INET)
		{
			continue;
		}
		NetworkInterface ni;
		ni.name = string(ifa->ifa_name);

		s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		if (s == 0)
		{
			ni.address = string(host);
		}
		
		s=getnameinfo(ifa->ifa_netmask,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		if (s == 0)
		{
			ni.mask = string(host);
		}

		s=getnameinfo(ifa->ifa_ifu.ifu_broadaddr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		if (s == 0)
		{
			ni.broadcast = string(host);
		}
		Interfaces.push_back(ni);
	}

	freeifaddrs(ifaddr);
	return Interfaces;
}