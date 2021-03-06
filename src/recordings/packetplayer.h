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

#ifndef ROBOTV_PACKETPLAYER_H
#define	ROBOTV_PACKETPLAYER_H

#include "robotvdmx/demuxer.h"
#include "robotvdmx/demuxerbundle.h"

#include "recordings/recplayer.h"
#include "net/msgpacket.h"

#include "vdr/remux.h"
#include <deque>
#include <chrono>

class PacketPlayer : public RecPlayer, protected TsDemuxer::Listener {
public:

    PacketPlayer(cRecording* rec);

    virtual ~PacketPlayer();

    MsgPacket* requestPacket(bool keyFrameMode);

    int64_t seek(int64_t position);

    const std::chrono::milliseconds& startTime() const {
        return m_startTime;
    }

    const std::chrono::milliseconds& endTime() const {
        return m_endTime;
    }

    void reset();

protected:

    MsgPacket* getNextPacket();

    MsgPacket* getPacket();

    void onStreamPacket(TsDemuxer::StreamPacket *p);

    void onStreamChange();

    void clearQueue();

    int64_t filePositionFromClock(int64_t wallclockTimeMs);

private:

    StreamBundle createFromPatPmt(const cPatPmtParser* patpmt);

    cPatPmtParser m_parser;

    cIndexFile* m_index;

    cRecording* m_recording;

    DemuxerBundle m_demuxers;

    uint64_t m_position;

    bool m_requestStreamChange;

    int m_patVersion;

    int m_pmtVersion;

    std::deque<MsgPacket*> m_queue;

    MsgPacket* m_streamPacket = NULL;

    std::chrono::milliseconds m_startTime;

    std::chrono::milliseconds m_endTime;

    int64_t m_startPts;

};

#endif	// ROBOTV_PACKETPLAYER_H
