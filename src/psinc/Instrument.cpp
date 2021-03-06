#include "psinc/Instrument.h"
#include "psinc/driver/Commands.h"

#include <emergent/logger/Logger.hpp>
#include <future>

using namespace std;
using namespace emergent;


namespace psinc
{
	const set<uint16_t> Instrument::Vendors::All = { 0x2dd8, 0x0525 };
	const set<uint16_t> Instrument::Vendors::PSI = { 0x2dd8 };


	Instrument::~Instrument()
	{
		if (this->initialised)
		{
			this->run = false;

			// Notify the thread to wake so that it can then exit
			this->condition.notify_one();
			this->_thread.join();
		}
	}


	Device Instrument::CustomDevice(byte index)
	{
		return { &this->transport, "", index };
	}


	// vector<string> Instrument::List(Type product)
	// {
	// 	return Transport::List((uint16_t)product);
	// }

	map<string, string> Instrument::List(Type product, const set<uint16_t> &vendors)
	{
		return Transport::List(vendors, (uint16_t)product);
	}


	void Instrument::Initialise(Type product, string serial, std::function<void(bool)> onConnection, int timeout, const set<uint16_t> &vendors)
	{
		this->onConnection = onConnection;

		// Common devices for all instrument types
		this->devices = {
			{ "Error",				{ &transport, "Error", 				0x04, Device::Direction::Input }},	// Error reporting
			{ "Serial",				{ &transport, "Serial", 			0x05, Device::Direction::Both }},	// Serial number of the camera (16 bytes)
			{ "Storage0",			{ &transport, "Storage0", 			0x06, Device::Direction::Both }},	// Storage block 0 (free for use - 502 bytes)
			{ "Name",				{ &transport, "Name", 				0x07, Device::Direction::Both }},	// User-settable name of the camera.
			{ "Storage1",			{ &transport, "Storage1", 			0x08, Device::Direction::Both }},	// Storage block 1 (free for use - 127 bytes)
			{ "Watchdog",			{ &transport, "Watchdog",			0x0d, Device::Direction::Both }},	// Watchdog time configuration (ms)
			{ "HardwareVersion",	{ &transport, "HardwareVersion",	0x0b, Device::Direction::Input }},
			{ "FirmwareVersion",	{ &transport, "FirmwareVersion",	0x0f, Device::Direction::Input }},
		};

		if (product == Type::Camera)
		{
			this->devices.insert({
				{ "Prox",		{ &this->transport, "Prox", 		0x00, Device::Direction::Input }},	// Prox reader device
				{ "Lock",		{ &this->transport, "Lock",			0x01, Device::Direction::Output }},	// Electronic lock control
				{ "LEDArray",	{ &this->transport, "LEDArray",		0x02, Device::Direction::Output }},	// LED array
				{ "SecureLock",	{ &this->transport, "SecureLock",	0x03, Device::Direction::Both }},	// Encrypted lock control
				{ "Defaults",	{ &this->transport, "Defaults", 	0x09, Device::Direction::Both }},	// Default settings for this device. Modify with care.
				{ "LEDPair",	{ &this->transport, "LEDPair",		0x0e, Device::Direction::Output }},	// Simple LED pair
				{ "Count",		{ &this->transport, "Count",		0x13, Device::Direction::Input }},	// 64-bit counter
				{ "Query",		{ &this->transport, "Query", 		0xff, Device::Direction::Input }}	// Query the camera for a list of available devices and chip type
			});
		}

		if (product == Type::Odometer)
		{
			this->devices.insert({
				{ "Count", { &this->transport, "Count", 0x00, Device::Direction::Input }}		// The odometer count value
			});
		}

		this->transport.Initialise(vendors, (uint16_t)product, serial, [&](bool connected) {
			this->configured = false;

			if (this->onConnection && !connected)
			{
				this->onConnection(false);
			}

		}, timeout);

		if (!this->initialised)
		{
			this->run			= true;
			this->_thread		= thread(&Instrument::Entry, this);
			this->initialised	= true;
		}
	}


	void Instrument::Entry()
	{
		unique_lock<mutex> lock(this->cs);

		while (this->run)
		{
			this->transport.Poll(0);

			if (this->transport.Connected() && !this->configured)
			{
				this->configured = this->Configure();

				if (this->onConnection && this->configured)
				{
					this->onConnection(true);
				}
			}

			if (this->Main())
			{
				this->condition.wait_for(lock, 50ms);
			}
		}
	}


	bool Instrument::Connected()
	{
		return this->transport.Connected() && this->configured;
	}


	bool Instrument::Reset(byte level)
	{
		if (level == 0)
		{
			return this->transport.Reset();
		}

		atomic<bool> waiting(false);
		Buffer<byte> command = {
			0x00, 0x00, 0x00, 0x00, 0x00,								// Header
			Commands::ResetChip, (byte)(level - 1), 0x00, 0x00, 0x00,	// Command
			0xff														// Terminator
		};

		return level < 3 ? this->transport.Transfer(&command, nullptr, waiting) : false;
	}
}

