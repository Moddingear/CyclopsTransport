#include <Protocol/ImageProtocol.hpp>
#include <string.h>
#include <Transport/ConnectionToken.hpp>
#include <iostream>

using namespace std;

#define PROTOCOL_VERSION 0

ImageProtocol::ImageProtocol(std::string host)
	:transport(host == "", host,  50668, "")
{
	if (host ==  "")
	{
		BroadcastToken = transport.Connect(GenericTransport::BroadcastClient);
	}
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
	if (BroadcastToken)
	{
		char recvbuffer[1024];
		BroadcastToken->Receive(recvbuffer, sizeof(recvbuffer));
	}
	else
	{
		cerr << "Missing broadcast client !" << endl;
	}
	
	static const int max_slice_len = 1024;
	size_t total_send_length = length + sizeof(metadata);
	int num_slices = total_send_length / max_slice_len;
	if (total_send_length % max_slice_len > 0)
	{
		num_slices++;
	}
	
	
	std::vector<uint8_t> message(max_slice_len + sizeof(Header));
	Header &head = *reinterpret_cast<Header*>(message.data());
	memcpy(head.type, "IMAGE\0\0\0", sizeof(head.type));
	head.version = PROTOCOL_VERSION;
	head.num_segments = num_slices;
	head.index = index_counter++;
	uint8_t *readptr = reinterpret_cast<uint8_t*>(buffer);
	uint8_t *readptr_end = readptr + length;
	uint8_t *writeptr_end = message.data() + message.size();
	for (auto &&client : clients)
	{
		if (client == BroadcastToken)
		{
			continue;
		}

		for (head.segment_index = 0; head.segment_index < num_slices; head.segment_index++)
		{
			uint8_t *writeptr = message.data() + sizeof(Header);
			if (head.segment_index == 0)
			{
				memcpy(message.data() + sizeof(Header), &metadata, sizeof(metadata));
				writeptr += sizeof(metadata);
			}
			size_t write_len = min(writeptr_end - writeptr, readptr_end - readptr);
			memcpy(writeptr, readptr, write_len);
			readptr += write_len;
			client->Send(message.data(), message.size());
		}
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
	if (recvlen.value() < sizeof(Header))
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
	partial_packets[head.index][head.segment_index] = vector<uint8_t>(recvbuffer.begin()+sizeof(Header), recvbuffer.end());

	if (partial_packets[head.index].size() == head.num_segments)
	{
		Image im;
		auto &packets = partial_packets[head.index];
		size_t acc_size = 0;
		for (auto &&i : packets)
		{
			acc_size += i.second.size();
		}
		im.data.resize(acc_size-sizeof(ImageMetadata));
		uint8_t *wrptr = im.data.data();
		for (size_t segment_idx = 0; segment_idx < head.num_segments; segment_idx++)
		{
			auto key = packets.find(segment_idx);
			if (key == packets.end())
			{
				cerr << "Missing partial packet " << segment_idx << " of " << head.num_segments << "!" << endl;
				partial_packets.erase(head.index);
				return nullopt;
			}
			
			uint8_t *readptr = key->second.data();
			uint8_t readlen = key->second.size();
			if (segment_idx == 0)
			{
				if (readlen < sizeof(ImageMetadata))
				{
					cerr << "Packet 0 not big enough for image metadata !" <<endl;
					partial_packets.erase(head.index);
					return nullopt;
				}
				memcpy(&im.metadata, readptr, sizeof(ImageMetadata));
				readptr += sizeof(ImageMetadata);
				readlen -= sizeof(ImageMetadata);
			}
			
			memcpy(wrptr, readptr, readlen);
			wrptr += readlen;
			
		}
		partial_packets.erase(head.index);
		return im;
	}

	return nullopt;
}