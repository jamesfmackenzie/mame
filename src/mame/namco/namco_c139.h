// license:BSD-3-Clause
// copyright-holders: John Bennett, Angelo Salese
/***************************************************************************

    Namco C139 - Serial I/F Controller

***************************************************************************/
#ifndef MAME_MACHINE_NAMCO_C139_H
#define MAME_MACHINE_NAMCO_C139_H


#pragma once

#define USE_GLOBAL_INPUTS


//**************************************************************************
//  INTERFACE CONFIGURATION MACROS
//**************************************************************************

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> namco_c139_device

class namco_c139_device : public device_t,
						  public device_memory_interface
{
public:

	// WIP - find what works for each game, then one day make it universal
	enum
	{
		FULLSCALE,
		RAVERACER,
		ACE_DRIVER,
		CYBER,
		CCYCLES,
		TEST
	}m_mode;


	// construction/destruction
	namco_c139_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	void data_map(address_map& map);

	/* Legacy functions, no linkup - lets System 2 compile */
	void regs_map(address_map &map);
	uint16_t status_r();

	uint16_t ram_r(offs_t offset);
	void ram_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	
	/* New stuff below */
	void ram_w_system22(offs_t offset, uint16_t data);
	void regs_map_System22(address_map &map);	
	uint16_t regs_r(offs_t offset);
	void regs_w_System22(offs_t offset, uint16_t data);
	
	void transmitHandlerGlobalComms_System22(void);
	uint16_t receiveHandlerGlobalComms_System22(void);
	
	uint8_t isInterruptReady(void)
	{
		return(m_interrupt_set);
	}

	void clearInterrupt(void)
	{
		m_interrupt_set = 0;
	}


protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;
	virtual space_config_vector memory_space_config() const override;
private:
	void setReceiveRam16bit(uint16_t dataByte, uint16_t target);
	uint16_t getTransmitByte(uint16_t byteOffset);
	uint16_t getTransmitWord(uint16_t byteOffset); // TODO - consolidate with above 
	void globalInputComms(void);

	void regs0SetStatus(uint16_t dataWord);
	void regs_8_C_SetRxWords(uint16_t dataWord);
	void regs_A_SetTXWordsToSend(uint16_t dataWord);
	uint16_t regs_E_GetTXStartAddress(void);
	uint16_t regs_A_GetTXWordsToSend(void);

	const address_space_config m_space_config;
	uint16_t* m_ram = nullptr;
	uint16_t m_regs[8] = { 0 };
	uint16_t m_transmit_go{ 0 };
	uint16_t m_end_of_message{ 0 };
	uint16_t m_wordsToSend{ 0 };
	int m_txpackets{ 0 };
	uint8_t m_interrupt_set{ 0 };
		
	
#ifdef USE_GLOBAL_INPUTS

	void send_frame(int dataSize);
	void loop_frame(int dataSize, int offset);

	osd_file::ptr m_line_rx{ nullptr };  // rx line - can be either differential, simple serial or toslink
	osd_file::ptr m_line_tx{ nullptr };  // tx line - is differential, simple serial and toslink
	char m_localhost[256]{ 0 };
	char m_remotehost[256]{ 0 };

	uint8_t m_buffer_tx[0x10000]{ 0 };
	uint8_t m_buffer_rx[0x10000]{ 0 };
	uint8_t m_framesync{ 0 };

	uint16_t m_linktimer{ 0 };
	uint8_t m_id_byte{ 0 };
	uint16_t m_rx_offset{ 0 };
	uint8_t m_msg_counter{ 0 };


#endif
};


// device type definition
DECLARE_DEVICE_TYPE(NAMCO_C139, namco_c139_device)



//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************



#endif // MAME_MACHINE_NAMCO_C139_H
