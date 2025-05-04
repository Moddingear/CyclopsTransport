#include <Protocol/ImageProtocol.hpp>
#include <string.h>
#include <Transport/ConnectionToken.hpp>
#include <iostream>

using namespace std;

#define PROTOCOL_VERSION 0

ImageProtocol::ImageProtocol(std::string host)
	:transport(host == "", host,  50668, "")
{
}

ImageProtocol::~ImageProtocol()
{
}

void ImageProtocol::SendImage(void* buffer, size_t length, ImageMetadata metadata)
{
	auto clients = transport.GetClients();
	if (clients.size() == 0)
	{
		return;
	}
	std::vector<uint8_t> message(length + sizeof(metadata) + sizeof(Header));
	Header &head = *reinterpret_cast<Header*>(message.data());
	memcpy(head.type, "IMAGE\0\0\0", sizeof(head.type));
	head.version = PROTOCOL_VERSION;
	memcpy(message.data() + sizeof(Header), &metadata, sizeof(metadata));
	memcpy(message.data() +  sizeof(metadata) + sizeof(Header), buffer, length);
	for (auto &&i : clients)
	{
		i->Send(message.data(), message.size());
	}
}

std::optional<ImageProtocol::Image> ImageProtocol::ReceiveImage()
{
	auto clients = transport.GetClients();
	if (clients.size() != 1)
	{
		return nullopt;
	}
	std::vector<uint8_t> recvbuffer(2 * 1<<20); //2MB
	auto recvlen = clients[0]->Receive(recvbuffer.data(), recvbuffer.size());
	if (!recvlen.has_value())
	{
		return nullopt;
	}
	Header &head = *reinterpret_cast<Header*>(recvbuffer.data());
	switch (head.version)
	{
	case PROTOCOL_VERSION:
		//ok
		break;
	
	default:
		cerr << "Unknown Image Protocol version " << head.version << endl;
		return nullopt;
	}

	if (head.type != string("IMAGE"))
	{
		cerr << "Unhandled packet type " << head.type << endl;
		return nullopt;
	}
	Image im;
	memcpy(&im, &recvbuffer[sizeof(Header)], recvlen.value()-sizeof(Header));
	return im;
}