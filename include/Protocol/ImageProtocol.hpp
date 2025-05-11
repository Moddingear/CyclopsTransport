#pragma once

#include <Transport/UDPTransport.hpp>
#include <string>
#include <memory>
#include <atomic>
#include <vector>
#include <map>

class ImageProtocol
{
private:
	class ISteamNetworkingSockets *socket;

	typedef uint32_t HSteamNetPollGroup;
	typedef uint32_t HSteamListenSocket;
	typedef uint32_t HSteamNetConnection;

	HSteamNetPollGroup poll_group;

	HSteamListenSocket listener = 0;
	std::vector<HSteamNetConnection> server_connections;

	std::string server_ip;
	HSteamNetConnection client_connection = 0;

	static std::map<HSteamListenSocket, ImageProtocol*> port_owner;
	static std::map<HSteamNetConnection, ImageProtocol*> connection_owner;

	std::atomic<uint16_t> index_counter = 0;

	std::vector<struct SteamNetworkingMessage_t*> pending_messages;
public:

	enum class PacketTypes
	{
		None,
		Image,
		Configuration,
		Status,
		Handshake
	};

private:
	static const std::map<PacketTypes, std::string> TypeMap;

public:

	struct __attribute__((packed)) Header
	{
		uint32_t version;
		char type[8];

		Header(PacketTypes type);
		PacketTypes GetPacketType() const;
	};

	struct __attribute__((packed)) ImageMetadata
	{
		uint64_t timestamp;
		uint16_t width, height;
		uint8_t encoding;
		uint8_t identifier;
	};

	struct Image
	{
		ImageMetadata metadata;
		std::vector<uint8_t> data;
	};
	
	

	ImageProtocol(std::string InServerIP);
	~ImageProtocol();

	bool IsServer() const
	{
		return server_ip.size() == 0;
	}

	static PacketTypes GetPacketType(const char buffer[8]);

	void Handshake();

	void SendImage(void* buffer, size_t length, ImageMetadata metadata);

	void ServerReceive();

	std::optional<Image> ReceiveImage();

private:
	static void SteamNetConnectionStatusChangedCallback( struct SteamNetConnectionStatusChangedCallback_t *pInfo );
	void OnSteamNetConnectionStatusChanged( struct SteamNetConnectionStatusChangedCallback_t *pInfo );
};


