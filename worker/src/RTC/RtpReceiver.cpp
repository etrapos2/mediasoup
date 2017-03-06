#define MS_CLASS "RTC::RtpReceiver"
// #define MS_LOG_DEV

#include "RTC/RtpReceiver.hpp"
#include "RTC/Transport.hpp"
#include "Utils.hpp"
#include "MediaSoupError.hpp"
#include "Logger.hpp"

namespace RTC
{
	/* Class variables. */

	uint8_t RtpReceiver::rtcpBuffer[MS_RTCP_BUFFER_SIZE];

	/* Instance methods. */

	RtpReceiver::RtpReceiver(Listener* listener, Channel::Notifier* notifier, uint32_t rtpReceiverId, RTC::Media::Kind kind) :
		rtpReceiverId(rtpReceiverId),
		kind(kind),
		listener(listener),
		notifier(notifier)
	{
		MS_TRACE();

		if (this->kind == RTC::Media::Kind::AUDIO)
			this->maxRtcpInterval = RTC::RTCP::MAX_AUDIO_INTERVAL_MS;
		else
			this->maxRtcpInterval = RTC::RTCP::MAX_VIDEO_INTERVAL_MS;
	}

	RtpReceiver::~RtpReceiver()
	{
		MS_TRACE();

		if (this->rtpParameters)
			delete this->rtpParameters;

		if (this->rtpStream)
			delete this->rtpStream;
	}

	void RtpReceiver::Close()
	{
		MS_TRACE();

		static const Json::StaticString k_class("class");

		Json::Value event_data(Json::objectValue);

		// Notify.
		event_data[k_class] = "RtpReceiver";
		this->notifier->Emit(this->rtpReceiverId, "close", event_data);

		// Notify the listener.
		this->listener->onRtpReceiverClosed(this);

		delete this;
	}

	Json::Value RtpReceiver::toJson()
	{
		MS_TRACE();

		static Json::Value null_data(Json::nullValue);
		static const Json::StaticString k_rtpReceiverId("rtpReceiverId");
		static const Json::StaticString k_kind("kind");
		static const Json::StaticString k_rtpParameters("rtpParameters");
		static const Json::StaticString k_hasTransport("hasTransport");
		static const Json::StaticString k_rtpRawEventEnabled("rtpRawEventEnabled");
		static const Json::StaticString k_rtpObjectEventEnabled("rtpObjectEventEnabled");

		Json::Value json(Json::objectValue);

		json[k_rtpReceiverId] = (Json::UInt)this->rtpReceiverId;

		json[k_kind] = RTC::Media::GetJsonString(this->kind);

		if (this->rtpParameters)
			json[k_rtpParameters] = this->rtpParameters->toJson();
		else
			json[k_rtpParameters] = null_data;

		json[k_hasTransport] = this->transport ? true : false;

		json[k_rtpRawEventEnabled] = this->rtpRawEventEnabled;

		json[k_rtpObjectEventEnabled] = this->rtpObjectEventEnabled;

		return json;
	}

	void RtpReceiver::HandleRequest(Channel::Request* request)
	{
		MS_TRACE();

		switch (request->methodId)
		{
			case Channel::Request::MethodId::rtpReceiver_close:
			{
				#ifdef MS_LOG_DEV
				uint32_t rtpReceiverId = this->rtpReceiverId;
				#endif

				Close();

				MS_DEBUG_DEV("RtpReceiver closed [rtpReceiverId:%" PRIu32 "]", rtpReceiverId);

				request->Accept();

				break;
			}

			case Channel::Request::MethodId::rtpReceiver_dump:
			{
				Json::Value json = toJson();

				request->Accept(json);

				break;
			}

			case Channel::Request::MethodId::rtpReceiver_receive:
			{
				// Keep a reference to the previous rtpParameters.
				auto previousRtpParameters = this->rtpParameters;

				try
				{
					this->rtpParameters = new RTC::RtpParameters(request->data);
				}
				catch (const MediaSoupError &error)
				{
					request->Reject(error.what());

					return;
				}

				// NOTE: this may throw. If so keep the current parameters.
				try
				{
					this->listener->onRtpReceiverParameters(this);
				}
				catch (const MediaSoupError &error)
				{
					// Rollback previous parameters.
					this->rtpParameters = previousRtpParameters;

					request->Reject(error.what());

					return;
				}

				// Free the previous rtpParameters.
				if (previousRtpParameters)
					delete previousRtpParameters;

				Json::Value data = this->rtpParameters->toJson();

				request->Accept(data);

				// And notify again.
				this->listener->onRtpReceiverParametersDone(this);

				// Set the RtpStreamRecv.
				// TODO: This assumes a single receiving stream for now.
				uint32_t streamClockRate = this->rtpParameters->GetEncodingClockRate(0);

				// Create a RtpStreamRecv for receiving a media stream.
				this->rtpStream = new RTC::RtpStreamRecv(streamClockRate);

				break;
			}

			case Channel::Request::MethodId::rtpReceiver_setRtpRawEvent:
			{
				static const Json::StaticString k_enabled("enabled");

				if (!request->data[k_enabled].isBool())
				{
					request->Reject("Request has invalid data.enabled");

					return;
				}

				this->rtpRawEventEnabled = request->data[k_enabled].asBool();

				request->Accept();

				break;
			}

			case Channel::Request::MethodId::rtpReceiver_setRtpObjectEvent:
			{
				static const Json::StaticString k_enabled("enabled");

				if (!request->data[k_enabled].isBool())
				{
					request->Reject("Request has invalid data.enabled");

					return;
				}

				this->rtpObjectEventEnabled = request->data[k_enabled].asBool();

				request->Accept();

				break;
			}

			default:
			{
				MS_ERROR("unknown method");

				request->Reject("unknown method");
			}
		}
	}

	void RtpReceiver::ReceiveRtpPacket(RTC::RtpPacket* packet)
	{
		MS_TRACE();

		static const Json::StaticString k_class("class");
		static const Json::StaticString k_object("object");
		static const Json::StaticString k_payloadType("payloadType");
		static const Json::StaticString k_marker("marker");
		static const Json::StaticString k_sequenceNumber("sequenceNumber");
		static const Json::StaticString k_timestamp("timestamp");
		static const Json::StaticString k_ssrc("ssrc");

		// TODO: Check if stopped, etc (not yet done).

		// TODO: Handle multiple streams.
		MS_ASSERT(this->rtpStream, "no RtpStream set");

		// Process the packet.
		// TODO: Must check what kind of packet we are checking. For example, RTX
		// packets (once implemented) should have a different handling.
		if (!this->rtpStream->ReceivePacket(packet))
			return;

		// Notify the listener.
		this->listener->onRtpPacket(this, packet);

		// Emit "rtpraw" if enabled.
		if (this->rtpRawEventEnabled)
		{
			Json::Value event_data(Json::objectValue);

			event_data[k_class] = "RtpReceiver";

			this->notifier->EmitWithBinary(this->rtpReceiverId, "rtpraw", event_data, packet->GetData(), packet->GetSize());
		}

		// Emit "rtpobject" is enabled.
		if (this->rtpObjectEventEnabled)
		{
			Json::Value event_data(Json::objectValue);
			Json::Value json_object(Json::objectValue);

			event_data[k_class] = "RtpReceiver";

			json_object[k_payloadType] = (Json::UInt)packet->GetPayloadType();
			json_object[k_marker] = packet->HasMarker();
			json_object[k_sequenceNumber] = (Json::UInt)packet->GetSequenceNumber();
			json_object[k_timestamp] = (Json::UInt)packet->GetTimestamp();
			json_object[k_ssrc] = (Json::UInt)packet->GetSsrc();

			event_data[k_object] = json_object;

			this->notifier->EmitWithBinary(this->rtpReceiverId, "rtpobject", event_data, packet->GetPayload(), packet->GetPayloadLength());
		}
	}

	void RtpReceiver::GetRtcp(RTC::RTCP::CompoundPacket *packet, uint64_t now)
	{
		if (this->rtpStream)
		{
			if (static_cast<float>((now - this->lastRtcpSentTime) * 1.15) >= this->maxRtcpInterval)
			{
				RTC::RTCP::ReceiverReport* report = this->rtpStream->GetRtcpReceiverReport();

				// TODO: This assumes a single stream for now.
				report->SetSsrc(this->rtpParameters->GetEncodingMediaSsrc(0));
				packet->AddReceiverReport(report);

				this->lastRtcpSentTime = now;
			}
		}
	}

	void RtpReceiver::ReceiveRtcpFeedback(RTC::RTCP::FeedbackPsPacket* packet)
	{
		MS_TRACE();

		if (this->transport)
		{
			// Ensure that the RTCP packet fits into the RTCP buffer.
			if (packet->GetSize() > MS_RTCP_BUFFER_SIZE)
			{
				MS_WARN_TAG(rtcp, "cannot send RTCP packet, size too big (%zu bytes)",
					packet->GetSize());

				return;
			}

			packet->Serialize(RtpReceiver::rtcpBuffer);
			this->transport->SendRtcpPacket(packet);
		}
	}

	void RtpReceiver::ReceiveRtcpFeedback(RTC::RTCP::FeedbackRtpPacket* packet)
	{
		MS_TRACE();

		if (this->transport)
		{
			// Ensure that the RTCP packet fits into the RTCP buffer.
			if (packet->GetSize() > MS_RTCP_BUFFER_SIZE)
			{
				MS_WARN_TAG(rtcp, "cannot send RTCP packet, size too big (%zu bytes)",
					packet->GetSize());

				return;
			}

			packet->Serialize(RtpReceiver::rtcpBuffer);
			this->transport->SendRtcpPacket(packet);
		}
	}
}
