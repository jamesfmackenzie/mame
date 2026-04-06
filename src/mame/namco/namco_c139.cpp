// license:BSD-3-Clause
// copyright-holders:John Bennett, Angelo Salese 
/***************************************************************************

    Namco C139 - Serial I/F Controller

	- Hacky Proof of Concept (TM)
	- So-far workes with RR Fullscale, Driver's Eyes, Ridge-Racer 2 and Rave Racer (both only with 2 players and the odd glitch).
	- System 22 games are implemented using MAME's LAN comms, so multiple instances of MAME are required.
		This gives proper network linkup and decent performance on Full Scale as each instance can potentially use a different CPU Core.
	    This represents multiple-cab linkups more authentically than one driver with multiple displays.
	    However transmission rate doesn't seem up to 60Hz, especially with 3+ players, so messages can be dropped - some games tolerate this, other's don't seem to.
	  
	- Device behaves like a UART controller IC. 
	- The baud rate is 1Mbit, but this is not a low-level emulation - packets bytes are moved from one game to another via MAME's LAN system (like Sega Model 1,2)
	- 8k x 9-bit output buffer and 8k x 9-bit input buffer using external 9-bit SRAM (CPU reads/writes accesses RAM through C139)
	- 8 x 16-bit control registers
	- Games are wired in a 'ring' - peer to peer in a circle
		The exception being 3-screen games like Ridge Racer, where the centre is a master and is Y'd to the two side screens (which do not transmit).
	- Transmission packet lengths vary from game to game, but are tens/hundreds of bytes, not the entire 8k buffer.
	- Games handle the 'ID' of the PCBs (either set via jumpers or NVRAM)
	- In addition to the registers, the C139 seems to look at the data in the messages to decide what to do. 
		The 9th bit might be for parity in the real IC, but seems to be used as a sort of 'go' bit to start a transmission in some games (rather than via registers)
    TODO:
	- Lots, it's a sub-optimal messy WIP.
	- This code is full of game-specific switches. These need to be consolidated into different modes for the chip, once more games downloaded and comms fully understood.
	- The 'Ring' is not fully understood, which may explain so much not working. Does the C139 send-on the data automatically, or do the games do it? 
		Does the C139 know where to put the data in the RAM for different board IDs?
    - System 2 games (Final Lap, Suzuka...) don't presently work as they use this IC in a manner not fully understood yet (investigations have shown comms
        can be emulated, but only one board ever appears in the link).
	- Frame sync'ing isn't working. Not so easy in a no 'master' setup.
        Fullscale actually has a sync line as well as the C139, so that could benefit from sync as there's a 1+ frame lag with side views
	- Drivers Eyes support has been removed for now. It will return in the next update, hopefully playable on a 2023 PC.
	- Integrate into real MAME (I'm not in the MAME team, and I'm not enduring code reviews outside work, so it'll have to be someone else if it ever happens, sorry).
***************************************************************************/


#include "emu.h"
#include "namco_c139.h"
#include "emuopts.h"

//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************


/**
	 * C139 SCI Register 
	 * Angelo's findings seem correct, but subtle differences...
	    so register descriptions removed for now as need tidying up, otherwise it might be misleading.
*/

#define REG_0_STATUS (0x0 >>1)
#define REG_2_CONTROL (0x2 >>1)
#define REG_4_START (0x4 >>1)
#define REG_6_START (0x6 >>1)
#define REG_8_RXWORDS (0x8 >>1)
#define REG_A_TX_WORDS (0xA >>1)
#define REG_C_RXWORDS (0xC >>1)
#define REG_E_TXOFFSET (0xE >>1)


/* A homemade header to package game packets into, for sending from MAME to MAME instance */
#define ID_OFFSET (1)
#define MSG_COUNTER (2)
#define SIZE_OFFSETL (4)
#define SIZE_OFFSETH (3)
#define ORIGIN (5)
#define HEADER_OFFSET (7)


// device type definition
DEFINE_DEVICE_TYPE(NAMCO_C139, namco_c139_device, "namco_c139", "Namco C139 Serial")
#define RX_BUFF_OFFSET (0x1000) /* 0x2000 bytes - halfway through the RAM*/

// MAME Comms states
enum
{
	LINK_NOT_ESTABLISHED = 0x00,
	LINK_OK = 0x01,
	LINK_FAIL = 0x02,
};

//**************************************************************************
//  LIVE DEVICE
//**************************************************************************

void namco_c139_device::data_map(address_map &map)
{
	map(0x0000, 0x3fff).ram().share("sharedram");
}


/* There are multiple variants of the register handling code, as I'm still working out all the modes of the chip
  Obviously each system doesn't use a different mode, that's just a code WIP while I work out things
  Hopefully with more games working, commonality will allow these to be tidied up.
*/

// System 2, needed to make Final Lap boot. No linkup in this code I've left in here.
void namco_c139_device::regs_map(address_map &map)
{
	map(0x00, 0x01).r(FUNC(namco_c139_device::status_r)); // WRITE clears flags
	map(0x02, 0x03).noprw(); // settings?
//  map(0x0a, 0x0b) // WRITE tx_w
//  map(0x0c, 0x0d) // READ rx_r
//  map(0x0e, 0x0f) //
}


void namco_c139_device::regs_map_System22(address_map& map)
{
	map(0x00, 0x0F).rw(FUNC(namco_c139_device::regs_r), FUNC(namco_c139_device::regs_w_System22));
}


//-------------------------------------------------
//  namco_c139_device - constructor
//-------------------------------------------------

namco_c139_device::namco_c139_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, NAMCO_C139, tag, owner, clock),
	device_memory_interface(mconfig, *this),
	m_space_config("data", ENDIANNESS_BIG, 16, 14, 0, address_map_constructor(FUNC(namco_c139_device::data_map), this))
{
#ifdef USE_GLOBAL_INPUTS
	// prepare localhost "filename"
	m_localhost[0] = 0;
	strcat(m_localhost, "socket.");
	strcat(m_localhost, mconfig.options().comm_localhost());
	strcat(m_localhost, ":");
	strcat(m_localhost, mconfig.options().comm_localport());

	// prepare remotehost "filename"
	m_remotehost[0] = 0;
	strcat(m_remotehost, "socket.");
	strcat(m_remotehost, mconfig.options().comm_remotehost());
	strcat(m_remotehost, ":");
	strcat(m_remotehost, mconfig.options().comm_remoteport());

	m_framesync = mconfig.options().comm_framesync() ? 0x01 : 0x00;
#endif
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void namco_c139_device::device_start()
{
	m_ram = (uint16_t*)memshare("sharedram")->ptr();
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void namco_c139_device::device_reset()
{
}

//-------------------------------------------------
//  memory_space_config - return a description of
//  any address spaces owned by this device
//-------------------------------------------------

device_memory_interface::space_config_vector namco_c139_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_DATA, &m_space_config)
	};
}

//**************************************************************************
//  READ/WRITE HANDLERS
//**************************************************************************


void namco_c139_device::regs0SetStatus(uint16_t dataWord)
{
	m_regs[REG_0_STATUS] = dataWord;
}

void namco_c139_device::regs_8_C_SetRxWords(uint16_t dataWord)
{
	m_regs[REG_8_RXWORDS] = dataWord;
	m_regs[REG_C_RXWORDS] = dataWord;
}

uint16_t namco_c139_device::regs_E_GetTXStartAddress(void)
{
	return m_regs[REG_E_TXOFFSET] ;
}

uint16_t namco_c139_device::regs_A_GetTXWordsToSend(void)
{
	return m_regs[REG_A_TX_WORDS] & 0x0FFF ;
}

void namco_c139_device::regs_A_SetTXWordsToSend(uint16_t dataWord)
{
	m_regs[REG_A_TX_WORDS] = dataWord ;
}

void namco_c139_device::setReceiveRam16bit(uint16_t dataByte, uint16_t target)
{
	m_ram[(target + RX_BUFF_OFFSET) ] = dataByte;
}

uint16_t namco_c139_device::getTransmitByte(uint16_t byteOffset)
{
	return m_ram[byteOffset ] & 0xFF;
}

uint16_t namco_c139_device::getTransmitWord(uint16_t byteOffset)
{
	return m_ram[byteOffset] & 0xFFFF;
}


// System 2 stub
uint16_t namco_c139_device::ram_r(offs_t offset)
{
	return m_ram[offset ];
}

// System 2 stub (Angelo's code)
void namco_c139_device::ram_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	COMBINE_DATA(&m_ram[offset]);
}

// System 2 stub (Angelo's code)
uint16_t namco_c139_device::status_r()
{
	/*
	 x-- RX READY or irq pending?
	 -x- IRQ direction: 1 RX cause - 0 TX cause
	*/
	return 4; // STATUS bit?
}

// System 2 stub (Angelo's code)
uint16_t namco_c139_device::regs_r(offs_t offset)
{
	return(m_regs[offset]); //todo:mask 1FFF
}



void namco_c139_device::ram_w_system22(offs_t offset, uint16_t data)
{
	// Look to see if bit 9 or higher is set
	if (data >> 8 == 0) 
	{
		// If yes, this is the end-of-message marker used by the C139
		m_end_of_message = offset & 0xff;
	}
	

	m_ram[offset] = data;
}


/*
	Hacky register handling. Still not 100% sure which bits do what in the chip
*/
void namco_c139_device::regs_w_System22(offs_t offset, uint16_t data)
{
	m_regs[offset] = data;

	if (m_mode == FULLSCALE)
	{
		if ((offset == REG_4_START) && (data == 0x1)) 
		{
			m_transmit_go = 1;	
		}
	}
	if (m_mode == RAVERACER)
	{
		if ((offset == REG_4_START) && (data == 0x3))
		{
			m_transmit_go = 1;
		}
	}

	//TODO: Don't do this here, do it in System22.cpp if you really have to do such a hack
	if (offset == REG_2_CONTROL)
	{
		if (data == 0xC)
		{
			m_mode = FULLSCALE;
		}

		if (data == 8)
		{
			m_mode = RAVERACER;
		}
	}

}


#ifdef USE_GLOBAL_INPUTS  

/* 
	Functions for comms using MAME's LAN/file routines that allow multi instances of
	MAME one one or multiple computers via network
*/
void namco_c139_device::transmitHandlerGlobalComms_System22()
{

	if (m_linktimer == 0)
	{
		// Initialise
		globalInputComms();
	}

	// Re-establish/check comms every 10 seconds
	if (m_linktimer++ >= 600)
	{
		m_linktimer = 0;
	}
	
	// Transmit stuff!
	if (m_transmit_go)
	{
		uint16_t gameStartAddress = regs_E_GetTXStartAddress();
		int16_t wordsToSend = regs_A_GetTXWordsToSend();

		if (m_mode == RAVERACER)
		{
			wordsToSend = m_end_of_message + 2;  //No idea, just is (for now anyway)
			gameStartAddress = 0; 
		}

		// Warning - wrap stoppers, masks and other stuff below that needs to be removed ultimately
		if (wordsToSend < 0)
		{
			wordsToSend = 0;	//Clamp
		}
	
		wordsToSend &= 0xFFF; // Wrap protection
	
		gameStartAddress &= 0x1FFF;  // Overflow

		// Wrapper to package the message
		m_buffer_tx[0] = 'S';
		m_buffer_tx[ID_OFFSET] = m_id_byte;
		m_buffer_tx[SIZE_OFFSETL] = wordsToSend;
		m_buffer_tx[SIZE_OFFSETH] = wordsToSend>>8;
		m_buffer_tx[MSG_COUNTER] = m_msg_counter++;
		int origin = gameStartAddress * 2;
		m_buffer_tx[ORIGIN] = (origin >>8) & 0xFF;
		m_buffer_tx[ORIGIN + 1] = (origin) & 0xFF;

		uint16_t x = 0;
		
		for (uint16_t count = 0; count < wordsToSend; count++)
		{
			uint16_t transmitWord = getTransmitByte((count + gameStartAddress) & 0xFFF); //????FULLSCALE is broken?
			x = HEADER_OFFSET + (2 * count);
			
			m_buffer_tx[x++] = transmitWord;
			m_buffer_tx[x] = transmitWord >> 8;
		
			if (count == wordsToSend - 1)
			{
				m_buffer_tx[x] |= 0x01;
			}
			
		}
		x++;
		m_buffer_tx[x] = 'E'; // End character for our Global Comms string

		if (wordsToSend)
		{
			x++;
			send_frame(x);
			
			regs_A_SetTXWordsToSend(0x0);
		}

		m_end_of_message = 0;
		m_transmit_go = 0;
	}
}


uint16_t namco_c139_device::receiveHandlerGlobalComms_System22()
{

	uint16_t result = 0;
	std::uint32_t received_bytes = 0;
	
	/* Retry LAN connection */
	if (m_linktimer++ >= 60 * 2)
	{
		globalInputComms();
		m_linktimer = 0;
	}

	if ((m_line_rx) && (m_rx_offset == 0)) // if globalcomms has received data and buffer is empty, read more data into the buffer
	{
		m_line_rx->read(&m_buffer_rx[m_rx_offset], 0, 0x2000, received_bytes);
	}
	
	/* Process received data, or data ununsed from last pass (starts at m_rx_offset)*/
	if ((received_bytes > 0) || (m_rx_offset > 0))
	{
		uint16_t gamePacketSize = 0;
		// Check for our MAME-added start byte
		if (m_buffer_rx[m_rx_offset] == 'S')
		{
			// My custom header inforation
			gamePacketSize = m_buffer_rx[m_rx_offset + SIZE_OFFSETH];
			gamePacketSize = gamePacketSize << 8;
			gamePacketSize |= m_buffer_rx[m_rx_offset + SIZE_OFFSETL];
		}
		else
		{  // bad message
			m_rx_offset = 0;
			return 0;
		}

		// End byte should be an 'E' in my footer
		if (m_buffer_rx[m_rx_offset + HEADER_OFFSET + gamePacketSize * 2] != 'E')
		{
			// bad message
			m_rx_offset = 0;
			return (0);
		}
		

		// Loop received messages onto next PCB in chain (unless it's this PCB, then don't as it's been around the ring once).
		if (m_buffer_rx[m_rx_offset + ID_OFFSET] != m_id_byte)
		{
		    loop_frame(1 + HEADER_OFFSET + gamePacketSize * 2, m_rx_offset);
		}
		
		//Update the buffer offset to next message 
		m_rx_offset += 1 + m_rx_offset + HEADER_OFFSET + gamePacketSize * 2;
		
		// overflow protection. Should never get here unless something screwy with the scanline calls causes a buffer back-up.
		if (m_rx_offset > 10000)
		{
			m_rx_offset = 0;
		}


		// If buffer isn't empty
		if (received_bytes > m_rx_offset)
		{
			// Could set a fault
		}
		else
		{
			// Reset for next pass
			m_rx_offset = 0;
		}

		if (gamePacketSize != 0)
		{
			// Handle the data.
			// Lots of hacks in here for different games
			// These should untimately disappear as more stuff is fixed and the modes of the IC are better understood.

			{
				// Ridge Racer, Rave Racer, Fullscale...
				for (uint16_t count = 0; count < gamePacketSize; count++)
				{
					uint16_t inWord = m_buffer_rx[(2 * count) + 1 + HEADER_OFFSET + m_rx_offset];
					inWord = inWord << 8;
					inWord |= m_buffer_rx[(2 * count) + HEADER_OFFSET + m_rx_offset];

					// Write RX data into to RAM (upper 8K RX buffer)
					setReceiveRam16bit(inWord, count);
				}
				//Write to regs
				regs_8_C_SetRxWords(gamePacketSize);
				regs0SetStatus(0x2);
				//Fire interrupt
				result = 1;
				m_interrupt_set = 1;
			}


		}
		else
		{
			result = 0;
		}
	}
	else
	{
		regs0SetStatus(0x4);
	}
	
	return (result);

}
#endif

void namco_c139_device::globalInputComms()
{
	// check rx socket
	if (!m_line_rx)
	{
		osd_printf_verbose("C139COMM: listen on %d\n", m_localhost);
		uint64_t filesize; // unused
		osd_file::open(m_localhost, OPEN_FLAG_CREATE, m_line_rx, filesize);

		// Create a unique ID byte for each instange of the game, based on the LAN address
		if (m_line_rx)
		{
			uint8_t x;
			for (x = 0; x < sizeof(m_localhost) && m_localhost[x] != 0; x++)
			{

				m_id_byte ^= m_localhost[x];
			}

			osd_printf_verbose("C139COMM: ID byte = %02d\n", m_id_byte);
		}

	}

	// check tx socket
	if (!m_line_tx)
	{
		osd_printf_verbose("C139COMM: connect to %s\n", m_remotehost);
		uint64_t filesize; // unused
		osd_file::open(m_remotehost, 0, m_line_tx, filesize);

	}

}



void namco_c139_device::send_frame(int dataSize) {
	if (!m_line_tx)
		return;

	std::uint32_t written;
	const std::error_condition err = m_line_tx->write(&m_buffer_tx, 0, dataSize, written);

	if (err || (written != dataSize))
	{
		osd_printf_verbose("C139COMM: TX problem\n");
	}
}



void namco_c139_device::loop_frame(int dataSize, int offset) 
{
	if (!m_line_tx)
		return;

	std::uint32_t written;
	const std::error_condition err = m_line_tx->write(&m_buffer_rx, offset, dataSize, written);
	if (err || (written != dataSize))
		osd_printf_verbose("C139COMM: loop TX problem\n");
}
