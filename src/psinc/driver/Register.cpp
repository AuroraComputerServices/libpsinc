#include "psinc/driver/Register.h"
#include "psinc/driver/Commands.h"
#include <atomic>

using namespace std;
using namespace pugi;
using namespace emergent;


namespace psinc
{
	Register::Register(xml_node configuration, Transport *transport, int addressSize)
	{
		// A page is 512 bytes, therefore with byte-level addressing the mask
		// should be 0x1ff. For chips with 16-bit addressing (v024) the mask
		// will be 0xff (since addressSize is 2).
		int mask		= 0x1ff / addressSize;
		this->address	= strtol(configuration.attribute("address").as_string("0x00"), nullptr, 0);
		this->offset 	= addressSize * (address & mask);
		this->page		= (address & ~mask) >> 8;
		this->value		= 0;
		this->transport	= transport;
	}


	void Register::Initialise(int offset, int mask, int value)
	{
		this->value = (this->value & ~mask) | ((value << offset) & mask);
	}


	byte Register::Page()
	{
		return this->page;
	}


	int Register::Get()
	{
		return this->value;
	}


	int Register::Address()
	{
		return this->address;
	}


	bool Register::Set(int offset, int mask, int value)
	{
		atomic<bool> waiting(false);

		int updated = (this->value & ~mask) | ((value << offset) & mask);

		if (this->value != updated)
		{
			this->value = updated;

			Buffer<byte> data = {
				0x00, 0x00, 0x00, 0x00, 0x00, 			// Header
				Commands::WriteRegister, 				// Command
				(byte)(this->address & 0xff),
				(byte)((this->address >> 8) & 0xff),
				(byte)(this->value & 0xff),
				(byte)((this->value >> 8) & 0xff),
				0xff									// Terminator
			};

			return this->transport->Transfer(&data, nullptr, waiting);
		}

		return true;
	}


	bool Register::SetBit(int offset, bool value)
	{
		atomic<bool> waiting(false);

		int updated = value ? (this->value | (1 << offset)) : this->value & ~(1 << offset);

		if (this->value != updated)
		{
			this->value = updated;

			Buffer<byte> data = {
				0x00, 0x00, 0x00, 0x00, 0x00,
				Commands::WriteBit, (byte)(this->address & 0xff), (byte)((this->address >> 8) & 0xff), (byte)offset, (byte)((value ? 1 : 0)),
				0xff
			};

			return this->transport->Transfer(&data, nullptr, waiting);
		}

		return true;
	}


	bool Register::Refresh()
	{
		atomic<bool> waiting(false);
		Buffer<byte> receive(5);
		Buffer<byte> command = {
			0x00, 0x00, 0x00, 0x00, 0x00,																				// Header
			0x03, 0x00, 0x00, 0x00, 0x00, 																				// Flush command
			Commands::QueueRegister, (byte)(this->address & 0xff), (byte)((this->address >> 8) & 0xff), 0x00, 0x00, 	// Command
			0xff 																										// Terminator
		};

		if (this->transport->Transfer(&command, &receive, waiting))
		{
			this->value = (receive[4] << 8) + receive[3];

			return true;
		}

		return false;
	}


	void Register::Refresh(Buffer<byte> &data)
	{
		if (this->offset < data.Size() - 1)
		{
			this->value = (data[this->offset] << 8) + data[this->offset + 1];
		}
	}
}
