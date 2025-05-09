#include <Protocol/ImageProtocol.hpp>
#include <string.h>
#include <Transport/ConnectionToken.hpp>
#include <iostream>
#include <array>

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

ImageProtocol::ImageProtocol(bool InServer)
	:server(InServer), transport(50668, nullopt), recvbuffer(1<<21)
{
	if (server)
	{
		BroadcastToken = transport.Connect(GenericTransport::BroadcastClient);
	}
	else
	{
	}
	
}

ImageProtocol::~ImageProtocol()
{
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

void ImageProtocol::Handshake(std::string host)
{
	if (server)
	{
		cout << "Can't send an handshake as server !" << endl;
		return;
	}
	if (ServerToken && ServerToken->IsConnected())
	{
		cout << "Already connected !" << endl;
	}
	ServerToken = transport.Connect(host);

	std::vector<uint8_t> message(sizeof(Header));
	Header &head = *reinterpret_cast<Header*>(message.data());
	head = Header(PacketTypes::Handshake);
	head.num_segments = 1;
	head.index = index_counter++;
	ServerToken->Send(message.data(), message.size());
}

void ImageProtocol::SendImage(void* buffer, size_t length, ImageMetadata metadata)
{
	if (!server)
	{
		cerr << "Client can't send images !" <<endl;
		return;
	}
	auto clients = transport.GetClients();
	if (clients.size() == 0)
	{
		ServerReceive();
		return;
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
	head = Header(PacketTypes::Image);
	head.num_segments = num_slices;
	head.index = index_counter++;
	uint8_t *readptr = reinterpret_cast<uint8_t*>(buffer);
	uint8_t *readptr_end = readptr + length;
	uint8_t *writeptr_end = message.data() + message.size();
	for (auto &&client : clients)
	{
		if (client == ServerToken)
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
	ServerReceive();
}

void ImageProtocol::ServerReceive()
{
	std::array<uint8_t, 2048> message;
	do
	{
		auto received = transport.ReceiveAny(message.data(), message.size());
		if (received.second == nullptr)
		{
			break;
		}
		if (received.first < sizeof(Header))
		{
			cerr << "Packet too small for header, length " << received.first << endl;
			continue;
		}
		Header &head = *reinterpret_cast<Header*>(message.data());
		auto type = head.GetPacketType();
		if (head.version != PROTOCOL_VERSION)
		{
			cerr << "Unknown Image Protocol version " << head.version << endl;
			continue;
		}
		if (type == PacketTypes::None)
		{
			cerr << "Received an invalid packet type " << head.type << endl;
			continue;
		}
		
		switch (type)
		{
		case PacketTypes::Handshake :
			cout << "Received handshake from " << received.second->GetConnectionName() << endl;
			break;
		
		default:
			cout << "Packet type not supported yet" << endl;
			break;
		}
	} while (1);
	
	

}

std::optional<ImageProtocol::Image> ImageProtocol::ReceiveImage()
{
	if (server)
	{
		cerr << "Server can't receive images !" <<endl;
		return nullopt;
	}
	if (!ServerToken || !ServerToken->IsConnected())
	{
		cerr << "Server token invalid !" << endl;
		return nullopt;
	}
	
	auto recvlen = ServerToken->Receive(recvbuffer.data(), recvbuffer.size());
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
	auto type = head.GetPacketType();
	if (type != PacketTypes::Image)
	{
		cerr << "Unhandled packet type " << head.type << endl;
		return nullopt;
	}
	partial_packets[head.index][head.segment_index] = vector<uint8_t>(recvbuffer.begin()+sizeof(Header), recvbuffer.begin()+recvlen.value()-sizeof(Header));

	//maybe recompose the image if possible
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