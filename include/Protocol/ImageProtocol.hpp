#pragma once

#include <Transport/SCTPTransport.hpp>
#include <string>
#include <memory>
#include <atomic>
#include <vector>

class ImageProtocol
{
private:
	SCTPTransport transport;
	std::shared_ptr<ConnectionToken> BroadcastToken;
	std::map<uint16_t, std::map<uint16_t, std::vector<uint8_t>>> partial_packets;
	std::atomic<uint16_t> index_counter = 0;
public:

	enum class PacketTypes
	{
		None,
		Image,
		Configuration,
		Status
	};

	struct __attribute__((packed)) Header
	{
		uint32_t version;
		char type[8];
		uint16_t num_segments;
		uint16_t segment_index;
		uint16_t index;
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
	
	

	ImageProtocol(std::string host);
	~ImageProtocol();

	void SendImage(void* buffer, size_t length, ImageMetadata metadata);

	std::optional<Image> ReceiveImage();
};


