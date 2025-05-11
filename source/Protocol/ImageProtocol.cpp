#include <Protocol/ImageProtocol.hpp>
#include <string.h>
#include <Transport/ConnectionToken.hpp>
#include <iostream>
#include <array>
#include <algorithm>
#include <cassert>
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

using namespace std;

#define PROTOCOL_VERSION 0

const std::map<ImageProtocol::PacketTypes, std::string> ImageProtocol::TypeMap
{
	{ImageProtocol::PacketTypes::None, ""},
	{ImageProtocol::PacketTypes::Image, "IMAGE"},
	{ImageProtocol::PacketTypes::Configuration, "CONFIGURATION"},
	{ImageProtocol::PacketTypes::Status, "STATUS"},
	{ImageProtocol::PacketTypes::Handshake, "HANDSHAKE"},
	
};


std::map<HSteamListenSocket, ImageProtocol*> ImageProtocol::port_owner;
std::map<HSteamNetConnection, ImageProtocol*> ImageProtocol::connection_owner;



ImageProtocol::ImageProtocol(std::string InServerIP)
	:server_ip(InServerIP)
{
	socket = SteamNetworkingSockets();
	poll_group = socket->CreatePollGroup();
	if (poll_group == k_HSteamNetPollGroup_Invalid)
	{
		cerr << "Failed to create poll group !" << endl;
	}
	
	SteamNetworkingIPAddr ipaddr;
	ipaddr.Clear();
	SteamNetworkingConfigValue_t opt[2];
	opt[0].SetPtr( k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)SteamNetConnectionStatusChangedCallback );
	opt[1].SetInt32( k_ESteamNetworkingConfig_SendBufferSize, 1280*850);
	if (server_ip.size() == 0)
	{
		ipaddr.m_port = 50668;
		listener = socket->CreateListenSocketIP(ipaddr, sizeof(opt)/sizeof(opt[0]), opt);
		if (listener == k_HSteamListenSocket_Invalid)
		{
			cerr << "Failed to create listen socket !" <<endl;
		}
		
		port_owner[listener] = this;
	}
	else
	{
		ipaddr.ParseString(server_ip.c_str());
		ipaddr.m_port = 50668;
		client_connection = socket->ConnectByIPAddress(ipaddr, sizeof(opt)/sizeof(opt[0]), opt);
		if (client_connection == k_HSteamNetConnection_Invalid)
		{
			cerr << "Failed to create connection !" <<endl;
		}
		
		connection_owner[client_connection] = this;
	}
	
}

ImageProtocol::~ImageProtocol()
{
	if (listener != k_HSteamListenSocket_Invalid)
	{
		port_owner.erase(listener);
		socket->CloseListenSocket(listener);
	}
	if (client_connection != k_HSteamNetConnection_Invalid)
	{
		connection_owner.erase(client_connection);
		socket->CloseConnection(client_connection, k_ESteamNetConnectionEnd_AppException_Generic, "Disconnected", false);
	}
}

ImageProtocol::PacketTypes ImageProtocol::GetPacketType(const char buffer[8])
{
	PacketTypes type = PacketTypes::None;
	for (auto &&i : TypeMap)
	{
		int length = std::min<size_t>(i.second.length(), 8); // sizeof(Header.type)
		if (memcmp(i.second.data(), buffer, length) == 0)
		{
			type = i.first;
		}
	}
	return type;
}

ImageProtocol::Header::Header(PacketTypes InType)
{
	string TypeString = TypeMap.at(InType);
	if (TypeString.size() > sizeof(type))
	{
		memcpy(type, TypeString.data(), sizeof(type));
	}
	else
	{
		memcpy(type, TypeString.data(), TypeString.size());
		memset(type+TypeString.size(), 0, sizeof(type) - TypeString.size());
	}
	version = PROTOCOL_VERSION;
}

ImageProtocol::PacketTypes ImageProtocol::Header::GetPacketType() const
{
	return ImageProtocol::GetPacketType(type);
}

void ImageProtocol::Handshake()
{
	if (IsServer())
	{
		cout << "Can't send an handshake as server !" << endl;
		return;
	}

	std::vector<uint8_t> message(sizeof(Header));
	Header &head = *reinterpret_cast<Header*>(message.data());
	head = Header(PacketTypes::Handshake);
	socket->SendMessageToConnection(client_connection, message.data(), message.size(), 0, nullptr);
}

void ImageProtocol::SendImage(void* buffer, size_t length, ImageMetadata metadata)
{
	if (!IsServer())
	{
		cerr << "Client can't send images !" <<endl;
		return;
	}
	if (server_connections.size() == 0)
	{
		ServerReceive();
		return;
	}
	
	
	//std::array<uint8_t, sizeof(Header) + sizeof(metadata)> message;
	Header &head = *reinterpret_cast<Header*>(buffer);
	head = Header(PacketTypes::Image);
	ImageMetadata &met = *reinterpret_cast<ImageMetadata*>(((uint8_t*)buffer) + sizeof(head));
	met = metadata;
	for (auto client : server_connections)
	{
		//socket->SendMessageToConnection(client, message.data(), message.size(), k_nSteamNetworkingSend_Unreliable, nullptr);
		socket->SendMessageToConnection(client, buffer, length, k_nSteamNetworkingSend_Unreliable, nullptr);
	}
	ServerReceive();
}

void ImageProtocol::ServerReceive()
{
	socket->RunCallbacks();
	SteamNetworkingMessage_t *message;

	do
	{
		int numreceived = socket->ReceiveMessagesOnPollGroup(poll_group, &message, 1);
		//auto received = transport.ReceiveAny(message.data(), message.size());
		if (numreceived == 0)
		{
			break;
		}
		if (message->GetSize() < sizeof(Header))
		{
			cerr << "Packet too small for header, length " << message->GetSize() << endl;
			message->Release();
			break;
		}
		const Header &head = *reinterpret_cast<const Header*>(message->GetData());
		auto type = head.GetPacketType();
		if (head.version != PROTOCOL_VERSION)
		{
			cerr << "Unknown Image Protocol version " << head.version << endl;
			message->Release();
			break;
		}
		if (type == PacketTypes::None)
		{
			cerr << "Received an invalid packet type " << head.type << endl;
			message->Release();
			break;
		}
		SteamNetConnectionInfo_t info;
		char ipaddr[64];
		socket->GetConnectionInfo(message->GetConnection(), &info);
		info.m_addrRemote.ToString(ipaddr, sizeof(ipaddr), true);
		
		switch (type)
		{
		case PacketTypes::Handshake :
			cout << "Received handshake from " << ipaddr << endl;
			break;
		
		default:
			cout << "Packet type not supported yet" << endl;
			break;
		}
		message->Release();
	} while (1);
	
	

}

std::optional<ImageProtocol::Image> ImageProtocol::ReceiveImage()
{
	if (IsServer())
	{
		cerr << "Server can't receive images !" <<endl;
		return nullopt;
	}
	SteamNetworkingMessage_t *message;
	int numreceived = socket->ReceiveMessagesOnConnection(client_connection, &message, 1);
	if (numreceived == 0)
	{
		return nullopt;
	}
	const uint8_t* data = reinterpret_cast<const uint8_t*>(message->GetData());
	const Header &head = *reinterpret_cast<const Header*>(data);
	switch (head.version)
	{
	case PROTOCOL_VERSION:
		//ok
		break;
	
	default:
		cerr << "Unknown Image Protocol version " << head.version << endl;
		return nullopt;
	}
	auto type = head.GetPacketType();
	if (type != PacketTypes::Image)
	{
		cerr << "Unhandled packet type " << head.type << endl;
		return nullopt;
	}

	Image im;
	im.metadata = *reinterpret_cast<const ImageMetadata*>(data+sizeof(Header));
	im.data = std::vector<uint8_t>(data, data+message->GetSize());
	return im;
}

void ImageProtocol::SteamNetConnectionStatusChangedCallback( SteamNetConnectionStatusChangedCallback_t *pInfo )
{
	if (pInfo->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid)
	{
		port_owner.at(pInfo->m_info.m_hListenSocket)->OnSteamNetConnectionStatusChanged(pInfo);
	}
	else
	{
		connection_owner.at(pInfo->m_hConn)->OnSteamNetConnectionStatusChanged(pInfo);
	}
}

void ImageProtocol::OnSteamNetConnectionStatusChanged( SteamNetConnectionStatusChangedCallback_t *pInfo )
{
	if (IsServer())
	{
		switch (pInfo->m_info.m_eState)
		{
		case k_ESteamNetworkingConnectionState_Connecting:
			{
				char ipbuf[64];
				pInfo->m_info.m_addrRemote.ToString(ipbuf, sizeof(ipbuf), true);
				socket->AcceptConnection(pInfo->m_hConn);

				//set poll group
				if( !socket->SetConnectionPollGroup(pInfo->m_hConn, poll_group))
				{
					socket->CloseConnection(pInfo->m_hConn, 0, nullptr, false);
					cerr << "Failed to set poll group on " << ipbuf << endl;
					break;
				}

				server_connections.push_back(pInfo->m_hConn);
				cout << ipbuf << " just connected" << endl;
			}
			break;
		case k_ESteamNetworkingConnectionState_None:
			// NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
			break;
		case k_ESteamNetworkingConnectionState_ClosedByPeer:
		case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
			{
				// Ignore if they were not previously connected.  (If they disconnected
				// before we accepted the connection.)
				if ( pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connected )
				{

					// Locate the client.  Note that it should have been found, because this
					// is the only codepath where we remove clients (except on shutdown),
					// and connection change callbacks are dispatched in queue order.
					auto itClient = std::find(server_connections.begin(), server_connections.end(), pInfo->m_hConn);
					assert( itClient != server_connections.end() );

					char ipbuf[64];
					pInfo->m_info.m_addrRemote.ToString(ipbuf, sizeof(ipbuf), true);

					if ( pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally )
					{
						cout << ipbuf << " disconnected dut to problem" <<endl;
					}
					else
					{
						cout << ipbuf << " disconnected by peer" <<endl;
					}

					server_connections.erase( itClient );
				}
				else
				{
					assert( pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting );
				}

				// Clean up the connection.  This is important!
				// The connection is "closed" in the network sense, but
				// it has not been destroyed.  We must close it on our end, too
				// to finish up.  The reason information do not matter in this case,
				// and we cannot linger because it's already closed on the other end,
				// so we just pass 0's.
				socket->CloseConnection( pInfo->m_hConn, 0, nullptr, false );
				break;
			}
			break;
		default:
			break;
		}
	}
	else
	{
		assert( pInfo->m_hConn == client_connection || client_connection == k_HSteamNetConnection_Invalid );
		switch ( pInfo->m_info.m_eState )
		{
			case k_ESteamNetworkingConnectionState_None:
				// NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
				break;

			case k_ESteamNetworkingConnectionState_ClosedByPeer:
			case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
			{
				// Print an appropriate message
				if ( pInfo->m_eOldState == k_ESteamNetworkingConnectionState_Connecting )
				{
					// Note: we could distinguish between a timeout, a rejected connection,
					// or some other transport problem.
					cout << "We got rejected during connection" << endl;
				}
				else if ( pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally )
				{
					cout << "Host lost" << endl;
				}
				else
				{
					// NOTE: We could check the reason code for a normal disconnection
					cout << "Disconnected" << endl;
				}

				// Clean up the connection.  This is important!
				// The connection is "closed" in the network sense, but
				// it has not been destroyed.  We must close it on our end, too
				// to finish up.  The reason information do not matter in this case,
				// and we cannot linger because it's already closed on the other end,
				// so we just pass 0's.
				socket->CloseConnection( pInfo->m_hConn, 0, nullptr, false );
				client_connection = k_HSteamNetConnection_Invalid;
				break;
			}

			case k_ESteamNetworkingConnectionState_Connecting:
				// We will get this callback when we start connecting.
				// We can ignore this.
				break;

			case k_ESteamNetworkingConnectionState_Connected:
				cout << "Connected to server OK" << endl;
				socket->SetConnectionPollGroup(pInfo->m_hConn, poll_group);
				break;

			default:
				// Silences -Wswitch
				break;
			
		}
	}
}