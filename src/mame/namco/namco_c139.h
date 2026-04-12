// license:BSD-3-Clause
// copyright-holders:Angelo Salese
/***************************************************************************

    Namco C139 - Serial I/F Controller

    Custom serial interface controller (SIC) used across Namco System 2,
    System 21, System 22, and Super System 22 arcade hardware (1987–1997)
    for linked multi-cabinet gameplay and multi-screen displays.

    Hardware reference:
    - 9-N-1 serial UART (9-bit data, no parity, 1 stop bit), 1 or 2 Mbps
    - External 0x2000-word (9-bit) SRAM: TX area 0x0000–0x0FFF, RX area 0x1000–0x1FFF
    - Eight 16-bit registers; REG_6 (RXOFFSET) has a hardware minimum of 0x1000
    - Power-on defaults confirmed by direct hardware measurement

    Pin-compatible upgrade C422 used on System 23 (higher clock speed).

    Transport: ASIO TCP over loopback. Each instance accepts on its local port
    and connects to its remote port. Two separate unidirectional sockets per
    link direction. Port configuration via MAME's standard comm options:
        -comm_localhost / -comm_localport
        -comm_remotehost / -comm_remoteport

***************************************************************************/
#ifndef MAME_NAMCO_NAMCO_C139_H
#define MAME_NAMCO_NAMCO_C139_H

#pragma once

#include <memory>
#include <string>


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> namco_c139_device

class namco_c139_device : public device_t
{
public:
	// construction/destruction
	namco_c139_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// IRQ callback — fired when a communication event occurs (mode-dependent)
	auto irq_cb() { return m_irq_cb.bind(); }

	// Topology role — controls relay behaviour for multi-cabinet installations.
	// Must be set by the game driver in machine_start() before the first device_reset().
	// Default CENTER is correct for all single-cabinet and 2-cabinet linked games.
	//
	//   CENTER    — originates TX frames; receives RX frames (standard operation)
	//   FORWARDER — receives from CENTER and immediately relays to SLAVE (ASIO thread,
	//               no CPU involvement); also fires IRQ so its own CPU can render
	//   SLAVE     — receives only; no relay, no TX
	//
	// ridgeracf ring: Center → Forwarder (right screen) → Slave (left screen)
	enum class role_t { CENTER, FORWARDER, SLAVE };
	void set_role(role_t role) { m_role = role; }

	// Address maps for driver wiring
	void data_map(address_map &map) ATTR_COLD;   // 0x0000–0x3FFF  (0x2000 words of RAM)
	void regs_map(address_map &map) ATTR_COLD;   // 0x00–0x0F      (8 × 16-bit registers)

	// RAM access — also used by drivers that wire RAM directly (System 2, System 21)
	uint16_t ram_r(offs_t offset);
	void     ram_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	// Register access
	uint16_t reg_r(offs_t offset);
	void     reg_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_stop()  override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	devcb_write_line m_irq_cb;

	uint16_t m_ram[0x2000];
	uint16_t m_reg[8];

private:
	// network transport
	std::string m_localhost;
	std::string m_localport;
	std::string m_remotehost;
	std::string m_remoteport;
	uint8_t  m_linkid = 0;        // XOR hash of remote address, used for packet identification

	// wire-protocol work buffer (fixed 512-byte frame)
	uint8_t  m_buffer[0x200];

	// connection-establishment countdown (decremented each tick; comm begins when 0)
	uint16_t m_link_timer = 0;

	emu_timer *m_tick_timer = nullptr;

	role_t   m_role = role_t::CENTER;

	// ASIO networking context (opaque — defined in .cpp)
	class net_context;
	std::unique_ptr<net_context> m_net;

	TIMER_CALLBACK_MEMBER(tick_timer_cb);
	void     comm_tick();
	void     recv_data();
	void     send_data();
	unsigned recv_frame();
	void     send_frame();
};


// device type definition
DECLARE_DEVICE_TYPE(NAMCO_C139, namco_c139_device)


#endif // MAME_NAMCO_NAMCO_C139_H
