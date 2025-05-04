#pragma once

#include <Transport/SCTPTransport.hpp>
#include <string>
#include <memory>
#include <vector>

class ImageProtocol
{
private:
	SCTPTransport transport;
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


