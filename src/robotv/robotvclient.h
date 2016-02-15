/*
 *      vdr-plugin-robotv - RoboTV server plugin for VDR
 *
 *      Copyright (C) 2015 Alexander Pipelka
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

#ifndef ROBOTV_CLIENT_H
#define ROBOTV_CLIENT_H

#include <list>
#include <string>
#include <queue>

#include <vdr/thread.h>
#include <vdr/tools.h>
#include <vdr/receiver.h>
#include <vdr/status.h>

#include "demuxer/streaminfo.h"
#include "recordings/artwork.h"

#include "controllers/streamcontroller.h"
#include "controllers/recordingcontroller.h"

class cChannel;
class cDevice;
class MsgPacket;
class PacketPlayer;

class RoboTvClient : public cThread, public cStatus {
private:

    unsigned int m_id;

    int m_socket;

    bool m_loggedIn;

    bool m_statusInterfaceEnabled;

    MsgPacket* m_request;

    MsgPacket* m_response;

    cCharSetConv m_toUtf8;

    uint32_t m_protocolVersion;

    cMutex m_msgLock;

    int m_compressionLevel;

    std::list<int> m_caids;

    bool m_wantFta;

    bool m_filterLanguage;

    int m_channelCount;

    int m_timeout;

    std::string m_clientName;

    std::queue<MsgPacket*> m_queue;

    cMutex m_queueLock;

    Artwork m_artwork;

    // Controllers

    StreamController m_channelController;

    RecordingController m_recordingController;

protected:

    bool processRequest();

    virtual void Action(void);

    virtual void TimerChange(const cTimer* Timer, eTimerChange Change);
    virtual void ChannelChange(const cChannel* Channel);
    virtual void Recording(const cDevice* Device, const char* Name, const char* FileName, bool On);
    virtual void OsdStatusMessage(const char* Message);

public:

    RoboTvClient(int fd, unsigned int id);

    virtual ~RoboTvClient();

    void sendMoviesChange();

    void sendTimerChange();

    void queueMessage(MsgPacket* p);

    void sendStatusMessage(const char* Message);

    unsigned int getID() {
        return m_id;
    }

    const std::string& getClientName() {
        return m_clientName;
    }

    int getSocket() {
        return m_socket;
    }

protected:

    void setLoggedIn(bool yesNo) {
        m_loggedIn = yesNo;
    }

    void enableStatusInterface(bool yesNo) {
        m_statusInterfaceEnabled = yesNo;
    }

private:

    void putTimer(cTimer* timer, MsgPacket* p);

    bool isChannelWanted(int languageIndex, cChannel* channel, int type = 0);

    int channelCount(int languageIndex);

    cString createLogoUrl(const cChannel* channel);

    cString createServiceReference(const cChannel* channel);

    void addChannelToPacket(const cChannel*, MsgPacket*);

    //

    bool processLogin();

    bool processGetTime();

    bool processEnableStatusInterface();

    bool processUpdateChannels();

    bool processChannelFilter();

    //

    bool processChannelsGetChannels();

    //

    bool processTimerGet();

    bool processTimerGetList();

    bool processTimerAdd();

    bool processTimerDelete();

    bool processTimerUpdate();

    //

    bool processMoviesGetDiskSpace();

    bool processMoviesGetList();

    bool processMoviesGetInfo();

    bool processMoviesRename();

    bool processMoviesDelete();

    bool processMoviesMove();

    bool processMoviesSetPlayCount();

    bool processMoviesSetPosition();

    bool processMoviesGetPosition();

    bool processMoviesGetMarks();

    bool processMoviesSetUrls();

    //

    bool processArtworkGet();

    bool processArtworkSet();

    //

    bool processEPG_GetForChannel();
};

#endif // ROBOTV_CLIENT_H
