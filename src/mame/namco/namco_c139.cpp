// license:BSD-3-Clause
// copyright-holders:Angelo Salese
/***************************************************************************

    Namco C139 - Serial I/F Controller

    Custom serial interface controller (SIC) used across Namco System 2,
    System 21, System 22, and Super System 22 arcade hardware (1987–1997)
    for linked multi-cabinet gameplay and multi-screen displays.

    Connected to:
    - M5M5179P 9-bit SRAM via 13-bit address bus, 9-bit data bus
    - Host CPU via 14-bit address bus, 13-bit data bus
    - Two clock inputs: 16 MHz and 12 MHz
    - IRQ- output routed through C148 interrupt controller

    Known operating modes (REG_1 value):
        0x08 — ridgera2, raverace               (TX int, wait, TXSIZE==0)
        0x09 — suzuka8h, acedrive, winrungp,     (TX int, wait, TXSIZE==0, sync bit in TX)
                cybrcycc, driveyes (center), etc.
        0x0C — ridgeracf                         (RX int, wait, sync bit received)
        0x0D — finallap, driveyes (sides),       (RX int, wait, sync bit received + in TX)
                tokyowar, aircomb, dirtdash
        0x0F — configuration / reset state       (no interrupt)

    Wire format (approximated — TCP not real serial):
        Byte  0:      linkid (XOR hash of remote address string)
        Byte  1:      tx_size low byte
        Byte  2:      tx_size high byte
        Bytes 3..N:   payload — [lo_byte][0x00] per 9-bit RAM word
        Last byte |=  0x01 to set sync bit on final word
        Byte  0x1FF:  0x01 (frame sentinel)

    DONE (Phase 3): ridgeracf/ridgerac3m 3-screen topology role configuration (namcos22.cpp machine_reset)
    TODO (Phase 5): modes 0x09, 0x08, 0x0D
    TODO: C422 (System 23 pin-compatible upgrade)

***************************************************************************/

#include "emu.h"
#include "namco_c139.h"

#include "emuopts.h"
#include "asio.h"

#include <array>
#include <atomic>
#include <thread>

#define VERBOSE 0
#include "logmacro.h"


//**************************************************************************
//  REGISTER DEFINITIONS
//**************************************************************************

namespace {

// Register word offsets 0–7
constexpr int REG_0_STATUS   = 0;   // R: chip status flags / W: clear status, ack interrupt
constexpr int REG_1_MODE     = 1;   // operating mode bit-field (see header comment)
constexpr int REG_2_CONTROL  = 2;   // TX control / sync handshake
constexpr int REG_3_START    = 3;   // TX enable; bit-1 = 2 Mbps speed select
constexpr int REG_4_RXSIZE   = 4;   // received word count
constexpr int REG_5_TXSIZE   = 5;   // transmit word count (chip clears to 0 on TX complete)
constexpr int REG_6_RXOFFSET = 6;   // RX fill count (= REG_4); reg_r ORs 0x1000 → past-the-end addr
constexpr int REG_7_TXOFFSET = 7;   // TX buffer read pointer

// Hardware masks — values above the mask are discarded on write
constexpr uint16_t REG_MASKS[8] = {
	0x000f,  // REG_0 STATUS:    bits 3:0
	0x000f,  // REG_1 MODE:      bits 3:0
	0x0003,  // REG_2 CONTROL:   bits 1:0
	0x0003,  // REG_3 START:     bits 1:0
	0x00ff,  // REG_4 RXSIZE:    8-bit
	0x00ff,  // REG_5 TXSIZE:    8-bit
	0x1fff,  // REG_6 RXOFFSET:  13-bit (stores rx_size; reg_r returns value | 0x1000)
	0x1fff   // REG_7 TXOFFSET:  13-bit
};

// REG_0 STATUS bit definitions
constexpr uint16_t STATUS_ERROR   = 0x0001;  // frame error on receive
constexpr uint16_t STATUS_SYNC    = 0x0002;  // sync bit (bit-8) received in RX word
constexpr uint16_t STATUS_TXREADY = 0x0004;  // TXSIZE == 0 (TX complete or no TX pending)
constexpr uint16_t STATUS_RXREADY = 0x0008;  // RXSIZE == 0 (RX complete or no RX pending)

// Comm tick rate — frequently enough to detect TX/RX completion within a frame
// (Approximated: 800 Hz gives 1.25 ms per tick, comfortably within a 60 Hz frame)
constexpr int COMM_TICK_HZ = 800;

// Fixed wire-frame size (bytes): 3-byte header + up to 0x1FE×2 payload + 1 sentinel
constexpr unsigned FRAME_SIZE = 0x200;

// Connection establishment delay (ticks before comm begins after reset/reconnect)
constexpr uint16_t LINK_TIMER_INIT = 0x200;

} // anonymous namespace


//**************************************************************************
//  NET CONTEXT — ASIO TCP networking (emulation-thread-safe via lock-free FIFOs)
//**************************************************************************

class namco_c139_device::net_context
{
public:
	explicit net_context(namco_c139_device &device)
		: m_device(device)
		, m_acceptor(m_ioctx)
		, m_sock_rx(m_ioctx)
		, m_sock_tx(m_ioctx)
		, m_timeout_tx(m_ioctx)
		, m_stopping(false)
		, m_state_rx(0U)
		, m_state_tx(0U)
	{
	}

	void start()
	{
		m_thread = std::thread(
				[this]()
				{
					osd_printf_verbose("C139 [%s]: network thread started\n", m_device.tag());
					try {
						m_ioctx.run();
					} catch (const std::exception &e) {
						osd_printf_verbose("C139 [%s]: network thread exception: %s\n", m_device.tag(), e.what());
					} catch (...) {
						osd_printf_verbose("C139 [%s]: network thread unknown exception\n", m_device.tag());
					}
					osd_printf_verbose("C139 [%s]: network thread stopped\n", m_device.tag());
				});
	}

	void reset(
		const std::string &localhost, const std::string &localport,
		const std::string &remotehost, const std::string &remoteport,
		namco_c139_device::role_t role)
	{
		m_role = role;

		std::error_code err;
		asio::ip::tcp::resolver resolver(m_ioctx);

		for (auto &&ep : resolver.resolve(localhost, localport,
				asio::ip::tcp::resolver::flags::address_configured, err))
			m_localaddr = ep.endpoint();
		if (err)
			osd_printf_verbose("C139 [%s]: local address resolve error: %s\n", m_device.tag(), err.message().c_str());

		for (auto &&ep : resolver.resolve(remotehost, remoteport,
				asio::ip::tcp::resolver::flags::address_configured, err))
			m_remoteaddr = ep.endpoint();
		if (err)
			osd_printf_verbose("C139 [%s]: remote address resolve error: %s\n", m_device.tag(), err.message().c_str());

		m_ioctx.post(
				[this]()
				{
					std::error_code e;
					if (m_acceptor.is_open())  m_acceptor.close(e);
					if (m_sock_rx.is_open())   m_sock_rx.close(e);
					if (m_sock_tx.is_open())   m_sock_tx.close(e);
					m_timeout_tx.cancel();
					m_state_rx.store(0);
					m_state_tx.store(0);
					m_fifo_rx.clear();
					m_fifo_tx.clear();
					start_accept();
					start_connect();
				});
	}

	void set_role(namco_c139_device::role_t role) { m_role = role; }

	void stop()
	{
		m_ioctx.post(
				[this]()
				{
					m_stopping = true;
					std::error_code e;
					if (m_acceptor.is_open())  m_acceptor.close(e);
					if (m_sock_rx.is_open())   m_sock_rx.close(e);
					if (m_sock_tx.is_open())   m_sock_tx.close(e);
					m_timeout_tx.cancel();
					m_state_rx.store(0);
					m_state_tx.store(0);
					m_ioctx.stop();
				});
		m_work_guard.reset();
		if (m_thread.joinable())
			m_thread.join();
	}

	bool connected() const
	{
		return m_state_rx.load() == 2 && m_state_tx.load() == 2;
	}

	// Read up to data_size bytes from the RX FIFO into buffer.
	// Returns bytes_read, or 0 if not enough data available, or UINT_MAX on disconnect.
	unsigned receive(uint8_t *buffer, unsigned data_size)
	{
		if (m_state_rx.load() < 2)
			return UINT_MAX;
		if (m_fifo_rx.used() < data_size)
			return 0;
		return m_fifo_rx.read(buffer, data_size, false);
	}

	// Write data_size bytes from buffer to the TX FIFO.
	// Returns data_size on success, or UINT_MAX on disconnect / overflow.
	unsigned send(uint8_t *buffer, unsigned data_size)
	{
		if (m_state_tx.load() < 2)
			return UINT_MAX;
		if (m_fifo_tx.free() < data_size)
		{
			osd_printf_verbose("C139 [%s]: TX FIFO overflow\n", m_device.tag());
			return UINT_MAX;
		}
		const bool was_empty = (m_fifo_tx.used() == 0);
		m_fifo_tx.write(buffer, data_size);
		if (was_empty)
			m_ioctx.post([this]() { start_send_tx(); });
		return data_size;
	}

private:
	// Lock-free single-producer / single-consumer ring buffer.
	// The emulation thread is the sole reader/writer of the FIFO from the
	// MAME side; the ASIO thread reads/writes from the network side.
	class fifo
	{
	public:
		fifo() : m_wp(0), m_rp(0) { }

		unsigned write(uint8_t *buf, unsigned n)
		{
			unsigned rp = m_rp.load(std::memory_order_acquire);
			unsigned wp = m_wp.load(std::memory_order_relaxed);
			if ((BUFFER_SIZE + rp - wp - 1) % BUFFER_SIZE < n)
				return UINT_MAX;
			const unsigned b1 = std::min(n, BUFFER_SIZE - wp);
			std::copy_n(buf, b1, &m_buf[wp]);
			if (b1 < n)
				std::copy_n(buf + b1, n - b1, &m_buf[0]);
			m_wp.store((wp + n) % BUFFER_SIZE, std::memory_order_release);
			return n;
		}

		unsigned read(uint8_t *buf, unsigned n, bool peek)
		{
			unsigned wp = m_wp.load(std::memory_order_acquire);
			unsigned rp = m_rp.load(std::memory_order_relaxed);
			if ((BUFFER_SIZE + wp - rp) % BUFFER_SIZE < n)
				return UINT_MAX;
			const unsigned b1 = std::min(n, BUFFER_SIZE - rp);
			std::copy_n(&m_buf[rp], b1, buf);
			if (b1 < n)
				std::copy_n(&m_buf[0], n - b1, buf + b1);
			if (!peek)
				m_rp.store((rp + n) % BUFFER_SIZE, std::memory_order_release);
			return n;
		}

		void consume(unsigned n)
		{
			unsigned rp = m_rp.load(std::memory_order_relaxed);
			const unsigned avail = (BUFFER_SIZE + m_wp.load(std::memory_order_acquire) - rp) % BUFFER_SIZE;
			m_rp.store((rp + std::min(n, avail)) % BUFFER_SIZE, std::memory_order_release);
		}

		unsigned used() const
		{
			return (BUFFER_SIZE + m_wp.load(std::memory_order_acquire)
				- m_rp.load(std::memory_order_acquire)) % BUFFER_SIZE;
		}

		unsigned free() const
		{
			return (BUFFER_SIZE + m_rp.load(std::memory_order_acquire)
				- m_wp.load(std::memory_order_acquire) - 1 + BUFFER_SIZE) % BUFFER_SIZE;
		}

		void clear()
		{
			m_wp.store(0, std::memory_order_release);
			m_rp.store(0, std::memory_order_release);
		}

	private:
		static constexpr unsigned BUFFER_SIZE = 0x80000;
		std::atomic<unsigned> m_wp;
		std::atomic<unsigned> m_rp;
		std::array<uint8_t, BUFFER_SIZE> m_buf;
	};

	void start_accept()
	{
		if (m_stopping || !m_localaddr)
			return;
		std::error_code err;
		m_acceptor.open(m_localaddr->protocol(), err);
		m_acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
		if (!err) m_acceptor.bind(*m_localaddr, err);
		if (!err) m_acceptor.listen(1, err);
		if (!err)
		{
			osd_printf_verbose("C139: RX listening on %s:%d\n",
				m_localaddr->address().to_string(), m_localaddr->port());
			m_acceptor.async_accept(
					[this](std::error_code const &err, asio::ip::tcp::socket sock)
					{
						if (err)
						{
							osd_printf_verbose("C139 [%s]: RX accept error: %s\n", m_device.tag(), err.message().c_str());
							std::error_code e;
							m_acceptor.close(e);
							m_state_rx.store(0);
							start_accept();
						}
						else
						{
							std::error_code ep_err;
							auto ep = sock.remote_endpoint(ep_err);
							osd_printf_verbose("C139 [%s]: RX connected from %s:%d\n",
								m_device.tag(), ep.address().to_string().c_str(), (int)ep.port());
							std::error_code e;
							m_acceptor.close(e);
							m_sock_rx = std::move(sock);
							m_sock_rx.set_option(asio::socket_base::keep_alive(true));
							m_state_rx.store(2);
							start_receive_rx();
						}
					});
			m_state_rx.store(1);
		}
		else
		{
			osd_printf_verbose("C139: RX listen failed: %s\n", err.message());
		}
	}

	void start_connect()
	{
		if (m_stopping || !m_remoteaddr)
			return;
		std::error_code err;
		if (m_sock_tx.is_open())
			m_sock_tx.close(err);
		m_sock_tx.open(m_remoteaddr->protocol(), err);
		if (!err)
		{
			m_sock_tx.set_option(asio::ip::tcp::no_delay(true));
			m_sock_tx.set_option(asio::socket_base::keep_alive(true));
			osd_printf_verbose("C139: TX connecting to %s:%d\n",
				m_remoteaddr->address().to_string(), m_remoteaddr->port());
			m_timeout_tx.expires_after(std::chrono::seconds(10));
			m_timeout_tx.async_wait(
					[this](std::error_code const &err)
					{
						if (!err && m_state_tx.load() == 1)
						{
							osd_printf_verbose("C139: TX connect timed out, retrying\n");
							std::error_code e;
							m_sock_tx.close(e);
							m_state_tx.store(0);
							start_connect();
						}
					});
			m_sock_tx.async_connect(
					*m_remoteaddr,
					[this](std::error_code const &err)
					{
						m_timeout_tx.cancel();
						if (err)
						{
							osd_printf_verbose("C139 [%s]: TX connect error: %s\n", m_device.tag(), err.message().c_str());
							std::error_code e;
							m_sock_tx.close(e);
							m_state_tx.store(0);
							m_timeout_tx.expires_after(std::chrono::milliseconds(100));
							m_timeout_tx.async_wait([this](std::error_code const &te)
							{
								if (!te) start_connect();
							});
						}
						else
						{
							osd_printf_verbose("C139 [%s]: TX connected\n", m_device.tag());
							m_state_tx.store(2);
						}
					});
			m_state_tx.store(1);
		}
	}

	void start_receive_rx()
	{
		if (m_stopping)
			return;
		m_sock_rx.async_read_some(
				asio::buffer(m_buf_rx),
				[this](std::error_code const &err, std::size_t n)
				{
					if (err || !n)
					{
						if (err) osd_printf_verbose("C139 [%s]: RX error: %s\n", m_device.tag(), err.message().c_str());
						else     osd_printf_verbose("C139 [%s]: RX connection lost\n", m_device.tag());
						m_sock_rx.close();
						m_state_rx.store(0);
						m_fifo_rx.clear();
						start_accept();
					}
					else
					{
						if (UINT_MAX == m_fifo_rx.write(&m_buf_rx[0], n))
						{
							osd_printf_verbose("C139 [%s]: RX FIFO overflow\n", m_device.tag());
							m_sock_rx.close();
							m_state_rx.store(0);
							m_fifo_rx.clear();
							start_accept();
							return;
						}
						// FORWARDER: relay bytes to next node (left screen) in the ASIO
						// thread immediately, without involving the emulation CPU.
						// This approximates the Y-split broadcast on real hardware.
						if (m_role == namco_c139_device::role_t::FORWARDER
							&& m_state_tx.load() == 2)
						{
							if (m_fifo_tx.free() >= n)
							{
								const bool was_empty = (m_fifo_tx.used() == 0);
								m_fifo_tx.write(&m_buf_rx[0], n);
								osd_printf_verbose("C139 [%s]: relay %u bytes → TX\n", m_device.tag(), (unsigned)n);
								if (was_empty)
									start_send_tx();
							}
							else
							{
								osd_printf_verbose("C139 [%s]: relay TX FIFO overflow\n", m_device.tag());
							}
						}
						start_receive_rx();
					}
				});
	}

	void start_send_tx()
	{
		if (m_stopping)
			return;
		const unsigned n = m_fifo_tx.read(&m_buf_tx[0],
			std::min<unsigned>(m_fifo_tx.used(), m_buf_tx.size()), true);
		m_sock_tx.async_write_some(
				asio::buffer(&m_buf_tx[0], n),
				[this](std::error_code const &err, std::size_t sent)
				{
					m_fifo_tx.consume(sent);
					if (err)
					{
						osd_printf_verbose("C139 [%s]: TX error: %s\n", m_device.tag(), err.message().c_str());
						m_sock_tx.close();
						m_state_tx.store(0);
						m_fifo_tx.clear();
						start_connect();
					}
					else if (m_fifo_tx.used())
					{
						start_send_tx();
					}
				});
	}

	namco_c139_device &m_device;
	namco_c139_device::role_t m_role = namco_c139_device::role_t::CENTER;
	std::thread m_thread;
	asio::io_context m_ioctx;
	asio::executor_work_guard<asio::io_context::executor_type> m_work_guard{m_ioctx.get_executor()};
	std::optional<asio::ip::tcp::endpoint> m_localaddr;
	std::optional<asio::ip::tcp::endpoint> m_remoteaddr;
	asio::ip::tcp::acceptor m_acceptor;
	asio::ip::tcp::socket   m_sock_rx;
	asio::ip::tcp::socket   m_sock_tx;
	asio::steady_timer      m_timeout_tx;
	bool m_stopping;
	std::atomic_uint m_state_rx;
	std::atomic_uint m_state_tx;
	fifo m_fifo_rx;
	fifo m_fifo_tx;
	std::array<uint8_t, 0x400> m_buf_rx;
	std::array<uint8_t, 0x400> m_buf_tx;
};


//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************

DEFINE_DEVICE_TYPE(NAMCO_C139, namco_c139_device, "namco_c139", "Namco C139 Serial")


//**************************************************************************
//  LIVE DEVICE
//**************************************************************************

// Address map for the external 9-bit SRAM (0x2000 words)
//   0x0000–0x0FFF  TX area — CPU writes game state, C139 reads and serialises
//   0x1000–0x1FFF  RX area — C139 writes received data, CPU reads game state
void namco_c139_device::data_map(address_map &map)
{
	map(0x0000, 0x3fff).rw(FUNC(namco_c139_device::ram_r), FUNC(namco_c139_device::ram_w));
}

// Address map for the eight 16-bit registers (word offsets 0–7, byte range 0x00–0x0F)
void namco_c139_device::regs_map(address_map &map)
{
	map(0x00, 0x0f).rw(FUNC(namco_c139_device::reg_r), FUNC(namco_c139_device::reg_w));
}


//-------------------------------------------------
//  namco_c139_device - constructor
//-------------------------------------------------

namco_c139_device::namco_c139_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, NAMCO_C139, tag, owner, clock)
	, m_irq_cb(*this)
{
	// read MAME's standard comm options for port/host configuration
	auto const &opts = mconfig.options();
	m_localhost  = opts.comm_localhost();
	m_localport  = opts.comm_localport();
	m_remotehost = opts.comm_remotehost();
	m_remoteport = opts.comm_remoteport();

	// derive a link ID byte from the remote address string
	// used to identify packets originating from this instance vs relayed packets
	const std::string remote = util::string_format("%s:%s", m_remotehost, m_remoteport);
	m_linkid = 0;
	for (char c : remote)
		m_linkid ^= static_cast<uint8_t>(c);

	std::fill(std::begin(m_buffer), std::end(m_buffer), 0);
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void namco_c139_device::device_start()
{
	m_tick_timer = timer_alloc(FUNC(namco_c139_device::tick_timer_cb), this);
	m_tick_timer->adjust(attotime::never);

	m_net = std::make_unique<net_context>(*this);
	m_net->start();

	save_item(NAME(m_ram));
	save_item(NAME(m_reg));
	save_item(NAME(m_link_timer));
	save_item(NAME(m_linkid));
}


//-------------------------------------------------
//  set_role - topology role configuration
//  Called by the game driver in machine_reset() (ports are not readable
//  during machine_start(), so this cannot be called there).
//  Propagates immediately to the net_context so the relay behaviour is
//  correct before the link timer expires and networking begins.
//-------------------------------------------------

void namco_c139_device::set_role(role_t role)
{
	m_role = role;
	if (m_net)
		m_net->set_role(role);
}


//-------------------------------------------------
//  device_stop - device-specific cleanup
//-------------------------------------------------

void namco_c139_device::device_stop()
{
	m_tick_timer->adjust(attotime::never);
	m_net->stop();
	m_net.reset();
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void namco_c139_device::device_reset()
{
	std::fill(std::begin(m_ram), std::end(m_ram), 0);

	// power-on defaults confirmed by direct hardware measurement
	m_reg[REG_0_STATUS]   = STATUS_TXREADY | STATUS_RXREADY;  // 0x000C: both sizes are 0
	m_reg[REG_1_MODE]     = 0x000f;  // mode 0xF = no interrupt / reset-config state
	m_reg[REG_2_CONTROL]  = 0x0000;
	m_reg[REG_3_START]    = 0x0000;
	m_reg[REG_4_RXSIZE]   = 0x0000;
	m_reg[REG_5_TXSIZE]   = 0x0000;
	m_reg[REG_6_RXOFFSET] = 0x1000;  // hardware minimum — base of RX area
	m_reg[REG_7_TXOFFSET] = 0x0000;

	// reset connection and start the comm tick timer
	m_link_timer = LINK_TIMER_INIT;
	m_net->reset(m_localhost, m_localport, m_remotehost, m_remoteport, m_role);
	m_tick_timer->adjust(attotime::from_hz(COMM_TICK_HZ), 0, attotime::from_hz(COMM_TICK_HZ));
}


//**************************************************************************
//  READ/WRITE HANDLERS
//**************************************************************************

// RAM read — word-addressed, offset 0x0000–0x1FFF
uint16_t namco_c139_device::ram_r(offs_t offset)
{
	return m_ram[offset & 0x1fff];
}

// RAM write — COMBINE_DATA handles mem_mask correctly; no extra 9-bit masking
// (the hardware discards bits above 8, but enforcing that here would break
//  existing games that may write 16-bit values and expect them preserved
//  until Phase 5 traces confirm actual hardware behaviour)
void namco_c139_device::ram_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	COMBINE_DATA(&m_ram[offset & 0x1fff]);
}

// Register read
// REG_6 always returns value | 0x1000 regardless of stored value (confirmed hardware).
uint16_t namco_c139_device::reg_r(offs_t offset)
{
	if (offset >= 8)
		return 0;

	uint16_t result = m_reg[offset];

	if (offset == REG_6_RXOFFSET)
		result |= 0x1000;  // enforce hardware minimum on read

	if (!machine().side_effects_disabled())
		LOG("C139: reg_r[%02x] = %04x\n", offset, result);

	return result;
}

// Register write
// Values are masked to hardware width. Special handling for REG_0 (ack) and REG_6 (floor).
void namco_c139_device::reg_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (offset >= 8)
		return;

	LOG("C139: reg_w[%02x] = %04x\n", offset, data);

	data &= REG_MASKS[offset];

	switch (offset)
	{
		case REG_0_STATUS:
			// Any write to REG_0 clears the ERROR and SYNC status bits (interrupt ack).
			// TXREADY and RXREADY are hardware-computed from the size registers and
			// are not directly clearable by the CPU.
			{
				const bool had_irq = (m_reg[REG_0_STATUS] & (STATUS_ERROR | STATUS_SYNC)) != 0;
				m_reg[REG_0_STATUS] &= ~(STATUS_ERROR | STATUS_SYNC);
				if (m_reg[REG_5_TXSIZE] == 0) m_reg[REG_0_STATUS] |= STATUS_TXREADY;
				if (m_reg[REG_4_RXSIZE] == 0) m_reg[REG_0_STATUS] |= STATUS_RXREADY;
				// deassert IRQ once the status bits that caused it are cleared
				if (had_irq)
					m_irq_cb(CLEAR_LINE);
			}
			break;

		case REG_6_RXOFFSET:
			// Hardware minimum confirmed by direct chip test:
			// writing 0x0800 results in 0x1800 being stored.
			m_reg[REG_6_RXOFFSET] = std::max(data, uint16_t(0x1000));
			break;

		default:
			m_reg[offset] = data;
			break;
	}
}


//**************************************************************************
//  COMM TICK — called at COMM_TICK_HZ by the MAME timer
//**************************************************************************

TIMER_CALLBACK_MEMBER(namco_c139_device::tick_timer_cb)
{
	comm_tick();
}

void namco_c139_device::comm_tick()
{
	// count down connection establishment delay
	if (m_link_timer > 0)
	{
		m_link_timer--;
		return;
	}

	if (!m_net->connected())
		return;

	// always try to receive any incoming data
	recv_data();

	// FORWARDER relays in the ASIO thread; SLAVE never transmits.
	// Only CENTER generates its own TX frames.
	if (m_role != role_t::CENTER)
		return;

	// mode-specific TX trigger
	switch (m_reg[REG_1_MODE])
	{
		case 0x0c:
			// ridgeracf: TX triggered by REG_2==0x03, REG_3 transitioning to 0x00, REG_5>0
			// Condition approximated from ROM watchpoint trace (see technical reference §Mode 0x0C)
			if (m_reg[REG_2_CONTROL] == 0x03
				&& m_reg[REG_3_START]   == 0x00
				&& m_reg[REG_5_TXSIZE]  >  0x00)
			{
				send_data();
			}
			break;

		case 0x0f:
			// configuration / reset mode — no TX, no IRQ
			break;

		default:
			// modes 0x08, 0x09, 0x0D: deferred to Phase 5
			break;
	}
}


//**************************************************************************
//  DATA HELPERS
//**************************************************************************

// Attempt to read one fixed-size frame from the RX FIFO.
// Returns the number of bytes read (FRAME_SIZE on success, 0 if not enough data yet,
// UINT_MAX on disconnect).
unsigned namco_c139_device::recv_frame()
{
	unsigned bytes = m_net->receive(m_buffer, FRAME_SIZE);
	if (bytes == UINT_MAX)
	{
		// disconnected — reset link timer so we wait for reconnection
		m_link_timer = LINK_TIMER_INIT;
	}
	return bytes;
}

// Enqueue the current m_buffer contents as a fixed-size frame on the TX FIFO.
void namco_c139_device::send_frame()
{
	unsigned bytes = m_net->send(m_buffer, FRAME_SIZE);
	if (bytes == UINT_MAX)
		m_link_timer = LINK_TIMER_INIT;
}

// Process one received frame (if available).
// Decodes the wire format, writes payload to RX RAM, updates registers,
// relays if needed, and fires the mode-appropriate IRQ.
void namco_c139_device::recv_data()
{
	if (recv_frame() == 0)
		return;  // no complete frame yet

	const unsigned rx_size = uint16_t(m_buffer[2]) << 8 | m_buffer[1];

	LOG("C139: recv rx_size=%02x\n", rx_size);

	// Received data is always written to the fixed base of the RX area (0x1000).
	// Each frame overwrites the previous — there is no ring buffer.
	for (unsigned j = 0; j < rx_size; j++)
	{
		const unsigned buf = 3 + j * 2;
		m_ram[0x1000 + j] = uint16_t(m_buffer[buf + 1]) << 8 | m_buffer[buf];
	}

	// sync bit is signalled by bit-0 of the second byte of the last payload word
	// (also redundantly in buffer[0x1FF] as a sentinel)
	const bool sync_received =
		(rx_size > 0 && (m_buffer[3 + (rx_size - 1) * 2 + 1] & 0x01)) ||
		(m_buffer[FRAME_SIZE - 1] & 0x01);

	// REG_4 and REG_6 both hold rx_size.  reg_r ORs REG_6 with 0x1000 on read,
	// so the game sees REG_6 = 0x1000 + rx_size — the past-the-end address of
	// the just-written data.  Data is at 0x1000 .. (REG_6_read - 1).
	m_reg[REG_4_RXSIZE]   = rx_size & REG_MASKS[REG_4_RXSIZE];
	m_reg[REG_6_RXOFFSET] = rx_size & REG_MASKS[REG_6_RXOFFSET];

	// RXREADY clears while there is data to consume (RXSIZE > 0)
	if (m_reg[REG_4_RXSIZE] > 0)
		m_reg[REG_0_STATUS] &= ~STATUS_RXREADY;

	// relay: forward frames that did not originate from this instance.
	// FORWARDER skips here — the ASIO thread (start_receive_rx) already relayed
	// the raw bytes to m_fifo_tx; doing it again here would send every frame twice,
	// filling the SLAVE's RX FIFO until overflow and periodic disconnect/lag.
	if (m_buffer[0] != m_linkid && m_role != role_t::FORWARDER)
		send_frame();

	// mode 0x0C: IRQ fires when the sync bit is received
	if (m_reg[REG_1_MODE] == 0x0c && sync_received)
	{
		m_reg[REG_0_STATUS] |= STATUS_SYNC;
		m_irq_cb(ASSERT_LINE);
	}
}

// Encode the TX RAM area into a fixed-size wire frame and enqueue it.
// Clears TXSIZE on completion (marks TX done) and updates STATUS accordingly.
void namco_c139_device::send_data()
{
	const unsigned tx_offset = m_reg[REG_7_TXOFFSET] & 0x0fff;  // TX area only
	const unsigned tx_size   = m_reg[REG_5_TXSIZE];

	LOG("C139: send tx_size=%02x tx_offset=%04x\n", tx_size, tx_offset);

	if (tx_size == 0)
		return;

	// build the wire frame
	std::fill(std::begin(m_buffer), std::end(m_buffer), 0);
	m_buffer[0] = m_linkid;
	m_buffer[1] = tx_size & 0xff;
	m_buffer[2] = (tx_size >> 8) & 0xff;

	for (unsigned j = 0; j < tx_size; j++)
	{
		const unsigned buf = 3 + j * 2;
		const uint16_t word = m_ram[(tx_offset + j) & 0x0fff];
		m_buffer[buf]     = word & 0xff;
		m_buffer[buf + 1] = 0;
	}

	// set sync bit on the last word's second byte
	if (tx_size > 0)
		m_buffer[3 + (tx_size - 1) * 2 + 1] |= 0x01;

	// frame sentinel
	m_buffer[FRAME_SIZE - 1] = 0x01;

	send_frame();

	// TX complete: clear TXSIZE and update STATUS
	m_reg[REG_5_TXSIZE] = 0;
	m_reg[REG_0_STATUS] |= STATUS_TXREADY;
}
