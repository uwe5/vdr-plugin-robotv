/*
 *      vdr-plugin-robotv - roboTV server plugin for VDR
 *
 *      Copyright (C) 2016 Alexander Pipelka
 *
 *      https://github.com/pipelka/vdr-plugin-robotv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <live/livestreamer.h>
#include "config/config.h"
#include "packetplayer.h"
#include "tools/time.h"
#include "robotv/robotvcommand.h"

#define MIN_PACKET_SIZE (128 * 1024)

PacketPlayer::PacketPlayer(cRecording* rec) : RecPlayer(rec), m_demuxers(this) {
    m_requestStreamChange = true;
    m_index = new cIndexFile(rec->FileName(), false);
    m_recording = rec;
    m_position = 0;
    m_patVersion = -1;
    m_pmtVersion = -1;
    m_startPts = 0;

    // initial start / end time
    m_startTime = roboTV::currentTimeMillis();
    m_endTime = m_startTime + std::chrono::milliseconds(m_recording->LengthInSeconds() * 1000);
}

PacketPlayer::~PacketPlayer() {
    clearQueue();
    delete m_index;
}

void PacketPlayer::onStreamPacket(TsDemuxer::StreamPacket *p) {
    // skip non video / audio packets
    if(p->content != StreamInfo::Content::VIDEO && p->content != StreamInfo::Content::AUDIO) {
        return;
    }

    // initialise stream packet
    MsgPacket* packet = new MsgPacket(ROBOTV_STREAM_MUXPKT, ROBOTV_CHANNEL_STREAM);
    packet->disablePayloadCheckSum();

    // write stream data
    packet->put_U16(p->pid);

    packet->put_S64(p->pts);
    packet->put_S64(p->dts);
    packet->put_U32(p->duration);

    // write frame type into unused header field clientid
    packet->setClientID((uint16_t)p->frameType);

    // write payload into stream packet
    packet->put_U32(p->size);
    packet->put_Blob(p->data, p->size);

    int64_t currentTime = 0;
    int64_t currentPts = p->pts;

    // set initial pts
    if(m_startPts == 0) {
        m_startPts = currentPts;
    }

    // pts wrap ?
    if(currentPts < m_startPts - 90000) {
        currentPts += 0x200000000ULL;
    }

    currentTime = m_startTime.count() + (currentPts - m_startPts) / 90;

    // add timestamp (wallclock time in ms starting at m_startTime)
    packet->put_S64(currentTime);

    m_queue.push_back(packet);
}

void PacketPlayer::onStreamChange() {
    isyslog("stream change requested");
    m_requestStreamChange = true;
}

MsgPacket* PacketPlayer::getNextPacket() {
    int pmtVersion = 0;
    int patVersion = 0;

    int packetCount = 20;
    int packetSize = TS_SIZE * packetCount;

    unsigned char buffer[packetSize];

    // get next block (TS packets)
    int bytesRead = getBlock(buffer, m_position, packetSize);

    if(bytesRead < TS_SIZE) {
        return nullptr;
    }

    // round to TS_SIZE border
    packetCount = (bytesRead / TS_SIZE);
    packetSize = TS_SIZE * packetCount;

    // advance to next block
    m_position += packetSize;

    // new PAT / PMT found ?
    if(m_parser.ParsePatPmt(buffer, packetSize)) {
        m_parser.GetVersions(m_patVersion, pmtVersion);

        if(pmtVersion > m_pmtVersion) {
            isyslog("found new PMT version (%i)", pmtVersion);
            m_pmtVersion = pmtVersion;

            // update demuxers from new PMT
            isyslog("updating demuxers");
            StreamBundle streamBundle = createFromPatPmt(&m_parser);
            m_demuxers.updateFrom(&streamBundle);

            m_requestStreamChange = true;
        }
    }

    // put packets into demuxer
    uint8_t* p = buffer;

    for(int i = 0; i < packetCount; i++) {
        m_demuxers.processTsPacket(p);
        p += TS_SIZE;
    }

    // stream change needed / requested
    if(m_requestStreamChange) {
        // first we need valid PAT/PMT
        if(!m_parser.GetVersions(patVersion, pmtVersion)) {
            return NULL;
        }

        // demuxers need to be ready
        if(!m_demuxers.isReady()) {
            return NULL;
        }

        isyslog("demuxers ready");

        for(auto i : m_demuxers) {
            isyslog("%s", i->info().c_str());
        }

        isyslog("create streamchange packet");
        m_requestStreamChange = false;

        return LiveStreamer::createStreamChangePacket(m_demuxers);
    }

    // get next packet from queue (if any)
    if(m_queue.size() == 0) {
        return NULL;
    }

    MsgPacket* packet = m_queue.front();
    m_queue.pop_front();

    return packet;
}

MsgPacket* PacketPlayer::getPacket() {
    MsgPacket* p = NULL;

    // process data until the next packet drops out
    while(m_position < m_totalLength && p == NULL) {
        p = getNextPacket();
    }

    return p;
}

MsgPacket* PacketPlayer::requestPacket(bool keyFrameMode) {
    MsgPacket* p = NULL;

    // create payload packet
    if(m_streamPacket == NULL) {
        m_streamPacket = new MsgPacket();
        m_streamPacket->disablePayloadCheckSum();
    }

    while(p = getPacket()) {

        if(keyFrameMode && p->getClientID() != (uint16_t)StreamInfo::FrameType::IFRAME) {
            delete p;
            continue;
        }

        // recheck recording duration
        if(p->getClientID() == (uint16_t)StreamInfo::FrameType::IFRAME && update()) {
            int64_t durationMs = (int)(((double)m_index->Last() * 1000.0) / m_recording->FramesPerSecond());
            m_endTime = m_startTime + std::chrono::milliseconds(durationMs);
        }

        // add start / endtime
        if(m_streamPacket->eop()) {
            m_streamPacket->put_S64(startTime().count());
            m_streamPacket->put_S64(endTime().count());
        }

        // add data
        m_streamPacket->put_U16(p->getMsgID());
        m_streamPacket->put_U16(p->getClientID());

        // add payload
        uint8_t* data = p->getPayload();
        int length = p->getPayloadLength();
        m_streamPacket->put_Blob(data, length);

        delete p;

        // send payload packet if it's big enough
        if(m_streamPacket->getPayloadLength() >= MIN_PACKET_SIZE) {
            MsgPacket* result = m_streamPacket;
            m_streamPacket = NULL;

            return result;
        }
    }

    return NULL;
}

void PacketPlayer::clearQueue() {
    MsgPacket* p = NULL;

    while(m_queue.size() > 0) {
        p = m_queue.front();
        m_queue.pop_front();
        delete p;
    }
}

void PacketPlayer::reset() {
    // reset parser
    m_parser.Reset();
    m_demuxers.clear();
    m_requestStreamChange = true;
    m_patVersion = -1;
    m_pmtVersion = -1;

    // reset current stream packet
    delete m_streamPacket;
    m_streamPacket = nullptr;

    // remove pending packets
    clearQueue();
}

int64_t PacketPlayer::filePositionFromClock(int64_t wallclockTimeMs) {
    double offsetMs = wallclockTimeMs - m_startTime.count();
    double fps = m_recording->FramesPerSecond();
    int frames = (int)((offsetMs * fps) / 1000.0);

    int index = m_index->GetClosestIFrame(frames);

    uint16_t fileNumber = 0;
    off_t fileOffset = 0;

    m_index->Get(index, &fileNumber, &fileOffset);

    if(fileNumber == 0) {
        return 0;
    }

    return m_segments[--fileNumber]->start + fileOffset;
}

int64_t PacketPlayer::seek(int64_t wallclockTimeMs) {
    // adujst position to TS packet borders
    m_position = filePositionFromClock(wallclockTimeMs);

    // invalid position ?
    if(m_position >= m_totalLength) {
        return -1;
    }

    isyslog("seek: %lu / %lu", m_position, m_totalLength);

    // reset parser
    reset();

    return 0;
}

StreamBundle PacketPlayer::createFromPatPmt(const cPatPmtParser* patpmt) {
    StreamBundle item;
    int patVersion = 0;
    int pmtVersion = 0;

    if(!patpmt->GetVersions(patVersion, pmtVersion)) {
        return item;
    }

    // add video stream
    int vpid = patpmt->Vpid();
    int vtype = patpmt->Vtype();

    item.addStream(StreamInfo(vpid,
                              vtype == 0x02 ? StreamInfo::Type::MPEG2VIDEO :
                              vtype == 0x1b ? StreamInfo::Type::H264 :
                              vtype == 0x24 ? StreamInfo::Type::H265 :
                              StreamInfo::Type::NONE));

    // add (E)AC3 streams
    for(int i = 0; patpmt->Dpid(i) != 0; i++) {
        int dtype = patpmt->Dtype(i);
        item.addStream(StreamInfo(patpmt->Dpid(i),
                                  dtype == 0x6A ? StreamInfo::Type::AC3 :
                                  dtype == 0x7A ? StreamInfo::Type::EAC3 :
                                  StreamInfo::Type::NONE,
                                  patpmt->Dlang(i)));
    }

    // add audio streams
    for(int i = 0; patpmt->Apid(i) != 0; i++) {
        int atype = patpmt->Atype(i);
        item.addStream(StreamInfo(patpmt->Apid(i),
                                  atype == 0x04 ? StreamInfo::Type::MPEG2AUDIO :
                                  atype == 0x03 ? StreamInfo::Type::MPEG2AUDIO :
                                  atype == 0x0f ? StreamInfo::Type::AAC :
                                  atype == 0x11 ? StreamInfo::Type::LATM :
                                  StreamInfo::Type::NONE,
                                  patpmt->Alang(i)));
    }

    // add subtitle streams
    for(int i = 0; patpmt->Spid(i) != 0; i++) {
        StreamInfo stream(patpmt->Spid(i), StreamInfo::Type::DVBSUB, patpmt->Slang(i));

        stream.setSubtitlingDescriptor(
                patpmt->SubtitlingType(i),
                patpmt->CompositionPageId(i),
                patpmt->AncillaryPageId(i));

        item.addStream(stream);
    }

    return item;
}
