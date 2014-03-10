using System;
using System.Linq;
using System.Xml.Linq;


namespace libpsinc
{
	/// <summary>
	/// A Device connected to the camera
	/// </summary>
	public class Device
	{
		/// <summary>
		/// Data direction of the device
		/// </summary>
		protected enum Direction
		{
			/// <summary>
			/// The device provides data to the camera
			/// </summary>
			Input,

			/// <summary>
			/// The device receives data from the camera
			/// </summary>
			Output,

			/// <summary>
			/// The device both provides data to the camera
			/// and receives data from the camera.
			/// </summary>
			Both
		}
	
		///<summary>
		/// The type of data provided by the device
		/// </summary>
		protected enum InputType
		{
			/// <summary>
			/// Byte array
			/// </summary>
			Default,

			/// <summary>
			/// String data
			/// </summary>
			String,

			/// <summary>
			/// Integer data
			/// </summary>
			Integer
		}

		/// <summary>
		/// Gets the name of the device
		/// </summary>
		/// <value>The name.</value>
		public string Name { get; private set; }

		/// <summary>
		/// Internal index of the device
		/// </summary>
		protected byte index;

		/// <summary>
		/// Direction of data (i.e., is this an input device or
		/// an output device?)
		/// </summary>
		protected Direction direction;

		/// <summary>
		/// Type of data provided by this device
		/// </summary>
		protected InputType type;

		/// <summary>
		/// The transport layer connection to this device
		/// </summary>
		internal Transport transport;
		
		/// <summary>
		/// Creates a new instance of the <see cref="libpsinc.Device"/> class.
		/// </summary>
		/// <param name="transport">Transport layer for this device.</param>
		/// <param name="xml">Xml description of the properties of this device.</param>
		internal Device(Transport transport, XElement xml)
		{
			this.transport		= transport;
			this.index			= (byte)(int)xml.Attribute("index");
			this.Name			= (string)xml.Attribute("name");
			
			Enum.TryParse<Direction>((string)xml.Attribute("direction"), true, out this.direction);
			Enum.TryParse<InputType>((string)(xml.Attribute("type") ?? new XAttribute("type", "default")), true, out this.type);
		}
		
		/// <summary>
		/// Initialise the device with the specified configuration
		/// </summary>
		/// <param name="configuration">Configuration parameter (device-specific).</param>
		public bool Initialise(byte configuration)
		{
			return this.transport.Command(new byte [] { (byte)Commands.InitialiseDevice, this.index, configuration }, null);
		}
		
		/// <summary>
		/// Write the specified buffer to the device
		/// </summary>
		/// <param name="buffer">Buffer to write.</param>
		public bool Write(byte [] buffer)
		{
			if (this.direction != Direction.Input && buffer != null && buffer.Length > 0)
			{
				int size			= buffer.Length;
				byte [] terminator	= new byte [] { 0xff };
				byte [] command		= new byte [] { 
					0x00, 0x00, 0x00, 0x00, 0x00,		// Header
					(byte)Commands.WriteDeviceBlock, 	// Command
					this.index, 
					(byte)(size & 0xff),
					(byte)((size >> 8) & 0xff),
					(byte)((size >> 16) & 0xff)
				};

				return this.transport.Transfer(command.Concat(buffer).Concat(terminator).ToArray(), null);
			}

			return false;
		}

		/// <summary>
		/// Write a value to the device.
		/// </summary>
		/// <param name="value">Value to write.</param>
		public bool Write(byte value)
		{
			return (this.direction == Direction.Input) ? false : this.transport.Command(new byte [] { (byte)Commands.WriteDevice, this.index, value }, null);
		}
		
		/// <summary>
		/// Read a byte array from the device
		/// </summary>
		public byte [] Read()
		{
			if (this.direction != Direction.Output)
			{
				byte [] receive = new byte[512];

				if (this.transport.CommandFlush(new byte [] { (byte)Commands.QueueDevice, this.index }, receive, false))
				{
					if (receive[0] == 0x00 && receive[1] == this.index)
					{
						int length = (receive[3] << 8) + receive[2];

						if (length > 0) return receive.Skip(4).Take(length).ToArray();
					}
				}
			}

			return null;
		}

		/// <summary>
		/// Read an integer from the device
		/// </summary>
		public uint ReadInteger()
		{
			if (this.type == InputType.Integer)
			{
				var data = this.Read();
				
				if (data != null && data.Length == 2) return (uint)(data[1] << 8) + data[0];
			}
			
			return 0;
		}

		/// <summary>
		/// Read a string from the device
		/// </summary>
		public string ReadString()
		{
			if (this.type == InputType.String)
			{
				var data = this.Read();
				
				if (data != null) return System.Text.Encoding.ASCII.GetString(data.TakeWhile(b => b > 0).ToArray());
			}
			
			return string.Empty;
		}
	}
}