#include "psinc/Transport.h"
#include <emergent/logger/Logger.hpp>
#include <regex>
#include <set>

using namespace std;
using namespace emergent;

#define WRITE_PIPE	0x03
#define READ_PIPE	0x81


namespace psinc
{
	Transport::Transport()
	{
		libusb_init(&this->context);

		this->legacy = !libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG);
	}


	Transport::~Transport()
	{
		this->Release();

		if (!this->legacy)
		{
			libusb_hotplug_deregister_callback(this->context, this->hotplug);
		}

		libusb_exit(this->context);
	}


	bool Transport::Initialise(uint16_t product, string serial, std::function<void(bool)> onConnection, int timeout)
	{
		this->serial		= serial;
		this->product		= product;
		this->onConnection	= onConnection;
		this->timeout		= timeout;

		if (!this->legacy)
		{
			libusb_hotplug_deregister_callback(this->context, this->hotplug);
			this->registered = false;
		}
		else Log::Info("USB hotplug not supported on this platform, running in legacy mode");

		return true;
	}


	bool Transport::Connected()
	{
		return this->handle;
	}


	int LIBUSB_CALL OnHotplug(libusb_context *context, libusb_device *device, libusb_hotplug_event event, void *data)
	{
		reinterpret_cast<Transport *>(data)->pending.enqueue({ device, event });
		return 0;
	}


	void Transport::Poll(int time)
	{
		if (this->legacy)
		{
			if (!this->handle) this->LegacyConnect();
			return;
		}

		if (!this->registered)
		{
			auto events = (libusb_hotplug_event)(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT);

			if (libusb_hotplug_register_callback(this->context, events, LIBUSB_HOTPLUG_ENUMERATE, LIBUSB_HOTPLUG_MATCH_ANY, this->product, LIBUSB_HOTPLUG_MATCH_ANY, &OnHotplug, this, &this->hotplug))
			{
				Log::Error("Unable to register USB hotplug callback");
				return;
			}

			this->registered = true;
		}

		struct timeval tv = { 0, time * 1000 };
		libusb_handle_events_timeout_completed(this->context, &tv, nullptr);

		Pending item;
		if (this->pending.try_dequeue(item))
		{
			if (Valid(item.device, this->product))
			{
				if (item.event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT && this->handle)
				{
					if (item.device == libusb_get_device(this->handle))
					{
						this->cs.lock();
							this->Release();
						this->cs.unlock();

						if (this->onConnection) this->onConnection(false);
					}
				}

				if (item.event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED && !this->handle)
				{
					if (this->Claim(item.device) && this->onConnection)
					{
						this->onConnection(true);
					}
				}
			}
		}
	}


	bool Transport::Match(libusb_device_handle *device, int index)
	{
		unsigned char data[64];

		libusb_get_string_descriptor_ascii(device, index, data, 64);

		this->id = reinterpret_cast<char *>(data);

		return this->serial.empty() ? true : regex_match(this->id, regex(this->serial));
	}


	bool Transport::Valid(libusb_device *device, uint16_t product)
	{
		static const set<uint16_t> VENDORS = { 0x0525 };
		libusb_device_descriptor descriptor;

		return libusb_get_device_descriptor(device, &descriptor) == 0
			&& VENDORS.count(descriptor.idVendor)
			&& descriptor.idProduct == product;
	}


	void Transport::LegacyConnect()
	{
		libusb_device **list;
		libusb_get_device_list(this->context, &list);

		// Loop through the list of connected USB devices
		for (libusb_device **device = list; *device; device++)
		{
			if (Valid(*device, this->product))
			{
				// If a particular device matches the known vendor, product ID then attempt to claim it.
				if (this->Claim(*device))
				{
					if (this->onConnection) this->onConnection(true);
					break;
				}
			}
		}

		libusb_free_device_list(list, 1);
	}


	bool Transport::Claim(libusb_device *device)
	{
		lock_guard<mutex> lock(this->cs);

		libusb_device_descriptor descriptor;

		if (libusb_get_device_descriptor(device, &descriptor) == 0)
		{
			// Attempt to open and claim the device
			if (libusb_open(device, &this->handle) == 0)
			{
				// If a serial number has been specified then check that this device matches it
				if (this->Match(this->handle, descriptor.iSerialNumber))
				{
					// TODO: Disabled this while PSI is solving an issue with
					// the camera that causes it to get stuck in a strange state.
					//if (libusb_set_configuration(this->handle, 1) == 0)
					{
						if (libusb_claim_interface(this->handle, 0) == 0)
						{
							Log::Info("%u: USB device claimed - %s", Timestamp::LogTime(), this->id);
							return true;
						}
					}
				}

				// Claiming was unsuccessful so clean up
				libusb_close(this->handle);
				this->handle 	= nullptr;
				this->id		= "";
			}
		}

		return false;
	}


	vector<string> Transport::List(uint16_t product)
	{
		vector<string> result;

		unsigned char data[64];
		libusb_device **list;
		libusb_device_descriptor descriptor;
		libusb_device_handle *handle;
		libusb_context *context = nullptr;

		libusb_init(&context);
		libusb_get_device_list(context, &list);

		for (libusb_device **device = list; *device; device++)
		{
			if (Valid(*device, product) && libusb_get_device_descriptor(*device, &descriptor) == 0)
			{
				if (libusb_open(*device, &handle) == 0)
				{
					libusb_get_string_descriptor_ascii(handle, descriptor.iSerialNumber, data, 64);

					result.push_back(reinterpret_cast<char *>(data));

					libusb_close(handle);
				}
			}
		}

		libusb_exit(context);

		return result;
	}


	void Transport::Disconnect()
	{
		lock_guard<mutex> lock(this->cs);

		this->Release();
	}


	void Transport::Release()
	{
		if (this->handle)
		{
			// Release the device and close the handle
			libusb_release_interface(this->handle, 0);
			libusb_close(this->handle);

			Log::Info("%u: USB deviced released - %s", Timestamp::LogTime(), this->id);

			this->handle	= nullptr;
			this->id		= "";
		}
	}



	bool Transport::Reset()
	{
		lock_guard<mutex> lock(this->cs);

		if (this->handle)
		{
			if (libusb_reset_device(this->handle) != 0)
			{
				// Something has gone wrong with the reset and so the device appears
				// as if it has been reconnected. Therefore this handle is invalid
				// and must be cleaned up.
				this->Release();

				Log::Error("Problem resetting USB device, handle has been released");
			}
		}

		return this->handle;
	}


	bool Transport::Transfer(Buffer<byte> *send, Buffer<byte> *receive, atomic<bool> &waiting, bool check, bool truncate)
	{
		lock_guard<mutex> lock(this->cs);

		return this->handle ? this->Transfer(send, true, check, false) && (waiting = true) && this->Transfer(receive, false, check, truncate) : false;
	}


	bool Transport::Transfer(Buffer<byte> *buffer, bool write, bool check, bool truncate)
	{
		if (buffer)
		{
			int transferred;
			bool result	= false;
			int err		= libusb_bulk_transfer(this->handle, write ? WRITE_PIPE : READ_PIPE, *buffer, buffer->Size(), &transferred, this->timeout);

			if (!err)
			{
				result = (write || check) ? transferred == buffer->Size() : true;

				// When requested, truncate the buffer to the size of data actually received.
				if (!write && result && truncate)
				{
					buffer->Truncate(transferred);
				}

				if (!result)
				{
					Log::Error(
						"%u: USB device %s - Incomplete transfer when %s (%d of %d bytes)",
						Timestamp::LogTime(),
						this->id,
						write ? "writing" : "reading",
						transferred,
						buffer->Size()
					);
				}
			}
			else if (this->legacy && (err == LIBUSB_ERROR_NO_DEVICE || err == LIBUSB_ERROR_IO))
			{
				Log::Error("%u: USB device %s - Device has been disconnected", Timestamp::LogTime(), this->id);
				this->Release();
			}
			else
			{
				Log::Error("%u: USB device %s - Transfer error %d when %s", Timestamp::LogTime(), this->id, err, write ? "writing" : "reading");
			}

			return result;
		}

		return true;
	}
}

