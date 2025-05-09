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
	bool server;
	UDPTransport transport;
	std::shared_ptr<ConnectionToken> ServerToken, //Token to send data to server
		BroadcastToken;
	std::map<uint16_t, std::map<uint16_t, std::vector<uint8_t>>> partial_packets;
	std::atomic<uint16_t> index_counter = 0;
	std::vector<uint8_t> recvbuffer; //2MB, for unpacking images
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
		uint16_t num_segments;
		uint16_t segment_index;
		uint16_t index;

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
	
	

	ImageProtocol(bool InServer);
	~ImageProtocol();

	static PacketTypes GetPacketType(const char buffer[8]);

	void Handshake(std::string host);

	void SendImage(void* buffer, size_t length, ImageMetadata metadata);

	void ServerReceive();

	std::optional<Image> ReceiveImage();
};


