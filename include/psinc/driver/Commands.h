#pragma once


namespace psinc
{
	/// Camera commands
	enum Commands
	{
		Capture				= 0x00,
		SlaveCaptureRising	= 0x01,
		ResetChip			= 0x02,
		Flush				= 0x03,
		MasterCapture		= 0x04,
		SlaveCaptureFalling	= 0x05,

		WriteRegister		= 0x10,
		WriteBit			= 0x11,
		QueueRegister		= 0x12,
		ReadRegisterPage	= 0x13,

		WriteDevice			= 0x20,
		QueueDevice			= 0x21,
		InitialiseDevice	= 0x22,
		WriteBlock			= 0x23,
		ReadBlock			= 0x24,

		Channel				= 0x25,

		Query				= 0x2f
	};
}
