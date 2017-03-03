#define MS_CLASS "RTC::RTCP::FeedbackPsPsLeiPacket"
// #define MS_LOG_DEV

#include "RTC/RTCP/FeedbackPsLei.hpp"
#include "Logger.hpp"
#include <cstring>

namespace RTC { namespace RTCP
{
	/* Class methods. */

	PsLeiItem* PsLeiItem::Parse(const uint8_t* data, size_t len)
	{
		MS_TRACE();

		// data size must be >= header + length value.
		if (sizeof(Header) > len)
		{
			MS_WARN_TAG(rtcp, "not enough space for PsLei item, discarded");

			return nullptr;
		}

		Header* header = const_cast<Header*>(reinterpret_cast<const Header*>(data));

		return new PsLeiItem(header);
	}

	/* Instance methods. */
	PsLeiItem::PsLeiItem(uint32_t ssrc)
	{
		MS_TRACE();

		this->raw = new uint8_t[sizeof(Header)];
		this->header = reinterpret_cast<Header*>(this->raw);
		this->header->ssrc = htonl(ssrc);
	}

	size_t PsLeiItem::Serialize(uint8_t* buffer)
	{
		MS_TRACE();

		// Add minimum header.
		std::memcpy(buffer, this->header, sizeof(Header));

		return sizeof(Header);
	}

	void PsLeiItem::Dump()
	{
		MS_TRACE();

		MS_DUMP("<PsLeiItem>");
		MS_DUMP("  ssrc : %" PRIu32, this->GetSsrc());
		MS_DUMP("</PsLeiItem>");
	}
}}
