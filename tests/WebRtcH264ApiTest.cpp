#include <QtTest>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>

namespace {

rtc::binary makeAnnexBFrame()
{
    rtc::binary frame;
    const auto append = [&frame](std::initializer_list<std::uint8_t> bytes) {
        for (const std::uint8_t value : bytes) {
            frame.push_back(static_cast<std::byte>(value));
        }
    };
    append({0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1f});
    append({0x00, 0x00, 0x00, 0x01, 0x65});
    for (int index = 0; index < 3000; ++index) {
        frame.push_back(static_cast<std::byte>((index % 251) + 1));
    }
    return frame;
}

rtc::message_vector packetize(const rtc::binary &frame)
{
    constexpr rtc::SSRC ssrc = 0x10203040;
    constexpr std::uint8_t payloadType = 102;
    constexpr std::uint32_t timestamp = 9000;
    auto config = std::make_shared<rtc::RtpPacketizationConfig>(
        ssrc, "week2", payloadType, rtc::H264RtpPacketizer::ClockRate
    );
    rtc::H264RtpPacketizer packetizer(
        rtc::NalUnit::Separator::StartSequence, config, 1200
    );
    rtc::message_vector messages {
        rtc::make_message(
            frame.begin(), frame.end(), std::make_shared<rtc::FrameInfo>(timestamp)
        )
    };
    packetizer.outgoing(messages, [](rtc::message_ptr) {});
    return messages;
}

} // namespace

class WebRtcH264ApiTest final : public QObject
{
    Q_OBJECT

private slots:
    void packetizerUsesExpectedRtpFields()
    {
        const rtc::message_vector messages = packetize(makeAnnexBFrame());
        QVERIFY(messages.size() >= 4);
        for (std::size_t index = 0; index < messages.size(); ++index) {
            const rtc::message_ptr &message = messages[index];
            QVERIFY(message->size() > sizeof(rtc::RtpHeader));
            const auto *header = reinterpret_cast<const rtc::RtpHeader *>(
                message->data()
            );
            QVERIFY(header->version() == 2);
            QVERIFY(header->payloadType() == 102);
            QVERIFY(header->ssrc() == 0x10203040U);
            QVERIFY(header->timestamp() == 9000U);
            QVERIFY(message->size() <= 1200 + sizeof(rtc::RtpHeader));
            QVERIFY((header->marker() != 0) == (index + 1 == messages.size()));
        }
        QVERIFY(rtc::H264RtpPacketizer::ClockRate == 90000U);
    }

    void depacketizerRestoresAnnexBAndTimestamp()
    {
        const rtc::binary frame = makeAnnexBFrame();
        rtc::message_vector messages = packetize(frame);
        rtc::H264RtpDepacketizer depacketizer(
            rtc::NalUnit::Separator::StartSequence
        );
        depacketizer.incomingChain(messages, [](rtc::message_ptr) {});
        QVERIFY(messages.size() == 1);
        QVERIFY(messages.front()->size() == frame.size());
        QVERIFY(std::equal(
            messages.front()->cbegin(), messages.front()->cend(), frame.cbegin()
        ));
        QVERIFY(messages.front()->frameInfo != nullptr);
        QVERIFY(messages.front()->frameInfo->timestamp == 9000U);
    }

    void missingFuFragmentProducesNoFrame()
    {
        const rtc::binary original = makeAnnexBFrame();
        rtc::message_vector messages = packetize(original);
        const std::size_t packetCount = messages.size();
        QVERIFY(messages.size() >= 4);
        messages.erase(messages.begin() + 2);
        rtc::H264RtpDepacketizer depacketizer(
            rtc::NalUnit::Separator::StartSequence
        );
        depacketizer.incomingChain(messages, [](rtc::message_ptr) {});
        QVERIFY(messages.size() <= packetCount);
        QVERIFY(std::none_of(
            messages.cbegin(), messages.cend(),
            [&original](const rtc::message_ptr &message) {
                return message->size() == original.size();
            }
        ));
    }
};

QTEST_GUILESS_MAIN(WebRtcH264ApiTest)
#include "WebRtcH264ApiTest.moc"
