#include "gs_usb.h"

#ifdef Q_OS_WIN

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QStringBuilder>

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>

/* how long the reader thread blocks on the USB endpoint before re-checking whether it should exit */
static const uint32_t GSUSB_READ_TIMEOUT_MS = 50;

/* gs_usb devices are limited to 8 channels by the candle API */
static const int GSUSB_MAX_CHANNELS = 8;

/* SavvyCAN has no sample point selector, these are the values the CAN and CAN FD specs recommend */
static const quint32 GSUSB_NOMINAL_SAMPLE_POINT = 875;
static const quint32 GSUSB_DATA_SAMPLE_POINT = 800;

/*****************************************************************************/
/* bit timing                                                                */
/*****************************************************************************/

/**
 * @brief A known good bit timing for one device clock / bitrate combination
 * @note The candle API always sends prop_seg = 1 and sjw = 1, so one bit is
 *       1 (sync) + 1 (prop) + phaseSeg1 + phaseSeg2 time quanta.
 */
struct GSUSBTiming
{
    quint32 fclk;
    quint32 bitrate;
    quint32 brp;
    quint32 phaseSeg1;
    quint32 phaseSeg2;
};

/* Nominal (arbitration phase) timings at ~87.5% sample point, taken from cangaroo's
 * CandleApiInterface. Clocks not listed here fall through to calcBitTiming(). */
static const GSUSBTiming GSUSB_NOMINAL_TIMINGS[] = {
    /* 170 MHz - CANable 2.0 (STM32G0B1) */
    { 170000000,   10000,  68, 217, 31 },
    { 170000000,   20000,  34, 217, 31 },
    { 170000000,   50000,  17, 173, 25 },
    { 170000000,   83333,   8, 221, 32 },
    { 170000000,  100000,  10, 147, 21 },
    { 170000000,  125000,   8, 147, 21 },
    { 170000000,  250000,   4, 147, 21 },
    { 170000000,  500000,   2, 147, 21 },
    { 170000000, 1000000,   1, 147, 21 },

    /* 160 MHz - CANable 2.5 (STM32G4) */
    { 160000000,   10000,  80, 173, 25 },
    { 160000000,   20000,  40, 173, 25 },
    { 160000000,   50000,  16, 173, 25 },
    { 160000000,   83333,   8, 208, 30 },
    { 160000000,  100000,  10, 138, 20 },
    { 160000000,  125000,   8, 138, 20 },
    { 160000000,  250000,   4, 138, 20 },
    { 160000000,  500000,   2, 138, 20 },
    { 160000000, 1000000,   1, 138, 20 },

    /* 80 MHz - CANnectivity */
    {  80000000,   10000,  80,  85, 13 },
    {  80000000,   20000,  40,  85, 13 },
    {  80000000,   50000,  16,  85, 13 },
    {  80000000,  100000,   8,  85, 13 },
    {  80000000,  125000,   5, 110, 16 },
    {  80000000,  250000,   2, 138, 20 },
    {  80000000,  500000,   1, 138, 20 },
    {  80000000,  800000,   1,  85, 13 },
    {  80000000, 1000000,   1,  68, 10 },

    /* 48 MHz - candleLight / CANable 0.x (STM32F072) */
    {  48000000,   10000, 300,  12,  2 },
    {  48000000,   20000, 150,  12,  2 },
    {  48000000,   50000,  60,  12,  2 },
    {  48000000,   83333,  36,  12,  2 },
    {  48000000,  100000,  30,  12,  2 },
    {  48000000,  125000,  24,  12,  2 },
    {  48000000,  250000,  12,  12,  2 },
    {  48000000,  500000,   6,  12,  2 },
    {  48000000,  800000,   4,  11,  2 },
    {  48000000, 1000000,   3,  12,  2 },

    /* 16 MHz */
    {  16000000,   20000,  50,  12,  2 },
    {  16000000,   50000,  20,  12,  2 },
    {  16000000,   83333,  12,  12,  2 },
    {  16000000,  100000,  10,  12,  2 },
    {  16000000,  125000,   8,  12,  2 },
    {  16000000,  250000,   4,  12,  2 },
    {  16000000,  500000,   2,  12,  2 },
    {  16000000,  800000,   1,  16,  2 },
    {  16000000, 1000000,   1,  12,  2 },
};

/* CAN FD data phase timings. The data phase has much tighter hardware limits than the
 * arbitration phase (tseg1 <= 31, tseg2 <= 16, brp <= 32) which is why the low data rates
 * need a larger prescaler. */
static const GSUSBTiming GSUSB_DATA_TIMINGS[] = {
    /* 170 MHz */
    { 170000000,  1000000,  5, 25,  7 },
    { 170000000,  2000000,  5, 12,  3 },
    { 170000000,  5000000,  1, 25,  7 },
    { 170000000, 10000000,  1, 10,  5 },

    /* 160 MHz */
    { 160000000,  1000000,  4, 30,  8 },
    { 160000000,  2000000,  2, 30,  8 },
    { 160000000,  4000000,  1, 30,  8 },
    { 160000000,  5000000,  1, 24,  6 },
    { 160000000,  8000000,  1, 14,  4 },

    /* 80 MHz */
    {  80000000,  1000000,  2, 30,  8 },
    {  80000000,  2000000,  1, 30,  8 },
    {  80000000,  4000000,  1, 14,  4 },
    {  80000000,  5000000,  1, 11,  3 },
    {  80000000,  8000000,  1,  6,  2 },

    /* 48 MHz */
    {  48000000,  1000000,  2, 17,  5 },
    {  48000000,  2000000,  1, 17,  5 },
    {  48000000,  3000000,  1, 11,  3 },
    {  48000000,  4000000,  1,  8,  2 },

    /* 16 MHz */
    {  16000000,  1000000,  1, 10,  4 },
    {  16000000,  2000000,  1,  4,  2 },
};

/**
 * @brief Look up a known good timing for a device clock / bitrate pair
 * @return true if an entry exists, false if the caller has to compute one
 */
static bool lookupBitTiming(const GSUSBTiming* pTable, size_t pCount, quint32 pFclk,
                            quint32 pBitrate, candle_bittiming_t& pTiming)
{
    for (size_t i = 0; i < pCount; i++)
    {
        if (pTable[i].fclk != pFclk || pTable[i].bitrate != pBitrate) continue;

        pTiming.prop_seg   = 1;
        pTiming.sjw        = 1;
        pTiming.phase_seg1 = pTable[i].phaseSeg1;
        pTiming.phase_seg2 = pTable[i].phaseSeg2;
        pTiming.brp        = pTable[i].brp;
        return true;
    }
    return false;
}

/**
 * @brief Compute a bit timing for clocks and bitrates the tables do not cover
 * @param pCaps: capabilities as reported by the device, supplies the clock and the segment limits
 * @param pDataPhase: true when computing the CAN FD data phase, which has tighter fixed limits
 * @note This mirrors what the Linux kernel does in can_calc_bittiming(): walk the prescaler
 *       range and keep the candidate with the smallest bitrate error, breaking ties on how
 *       close the sample point lands to the requested one.
 */
static bool calcBitTiming(const candle_capability_t& pCaps, quint32 pBitrate,
                          quint32 pSamplePoint, bool pDataPhase, candle_bittiming_t& pTiming)
{
    if (pBitrate == 0 || pCaps.fclk_can == 0) return false;

    /* the device only reports arbitration phase limits, the data phase limits are fixed by the spec */
    const quint32 tseg1Min = pDataPhase ? 2u  : std::max(pCaps.tseg1_min, 2u);
    const quint32 tseg1Max = pDataPhase ? 31u : std::max(pCaps.tseg1_max, 2u);
    const quint32 tseg2Min = pDataPhase ? 1u  : std::max(pCaps.tseg2_min, 1u);
    const quint32 tseg2Max = pDataPhase ? 16u : std::max(pCaps.tseg2_max, 1u);
    const quint32 brpMin   = std::max(pCaps.brp_min, 1u);
    const quint32 brpMax   = pDataPhase ? std::min(std::max(pCaps.brp_max, 1u), 32u)
                                        : std::max(pCaps.brp_max, 1u);
    const quint32 brpInc   = std::max(pCaps.brp_inc, 1u);

    bool    found = false;
    quint64 bestScore = 0;

    for (quint32 brp = brpMin; brp <= brpMax; brp += brpInc)
    {
        const quint32 divisor = brp * pBitrate;
        if (divisor == 0) continue;

        /* time quanta per bit, rounded to nearest */
        quint32 tqPerBit = (pCaps.fclk_can + divisor / 2) / divisor;
        if (tqPerBit < (1 + tseg1Min + tseg2Min)) break;         /* larger prescalers only get worse */
        if (tqPerBit > (1 + tseg1Max + tseg2Max)) continue;

        /* sync segment is always one time quantum */
        quint32 tseg1 = (tqPerBit * pSamplePoint + 500) / 1000;
        if (tseg1 < 1) tseg1 = 1;
        tseg1 -= 1;
        tseg1 = std::min(std::max(tseg1, tseg1Min), tseg1Max);

        if (tqPerBit <= 1 + tseg1) continue;
        quint32 tseg2 = tqPerBit - 1 - tseg1;
        tseg2 = std::min(std::max(tseg2, tseg2Min), tseg2Max);

        /* the clamp above may have moved tseg2, give the slack back to tseg1 */
        if (tqPerBit <= 1 + tseg2) continue;
        tseg1 = tqPerBit - 1 - tseg2;
        if (tseg1 < tseg1Min || tseg1 > tseg1Max) continue;

        const quint32 actualBitrate = pCaps.fclk_can / (brp * tqPerBit);
        const quint32 actualSp      = ((1 + tseg1) * 1000) / tqPerBit;

        const quint64 bitrateErr = (actualBitrate > pBitrate) ? (actualBitrate - pBitrate)
                                                              : (pBitrate - actualBitrate);
        const quint64 spErr      = (actualSp > pSamplePoint) ? (actualSp - pSamplePoint)
                                                             : (pSamplePoint - actualSp);
        /* bitrate accuracy dominates, the sample point only breaks ties */
        const quint64 score = bitrateErr * 1000 + spErr;

        if (!found || score < bestScore)
        {
            found     = true;
            bestScore = score;

            pTiming.prop_seg   = 1;
            pTiming.phase_seg1 = tseg1 - 1;
            pTiming.phase_seg2 = tseg2;
            pTiming.sjw        = 1;
            pTiming.brp        = brp;
        }
    }

    return found;
}

/*****************************************************************************/
/* device enumeration                                                        */
/*****************************************************************************/

/**
 * @brief Strip the USB interface number out of a Windows device path
 *
 * Composite gs_usb devices expose every CAN channel as its own Windows device path
 * containing "&mi_XX". They all reach the same physical device and the same USB endpoints,
 * so they have to collapse into one entry. Ported from cangaroo's CandleApiDriver.
 */
static std::wstring gsusbBaseDevicePath(const std::wstring& pPath)
{
    /* Windows reports paths in lower case already, normalise anyway so the key is stable */
    std::wstring result = pPath;
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);

    /* &mi_NN shows up in both the hardware ID and the instance ID part of the path,
     * so one pass is not enough */
    const std::wstring miTag = L"&mi_";
    auto pos = result.find(miTag);
    while (pos != std::wstring::npos)
    {
        auto end = pos + miTag.size();
        while (end < result.size() && iswxdigit(result[end])) ++end;
        result.erase(pos, end - pos);
        pos = result.find(miTag, pos);
    }

    /* devices without a USB serial number get a location based instance ID where the
     * interface number is the last four hex digits before the interface GUID */
    const auto guidPos = result.rfind(L"#{");
    if (guidPos != std::wstring::npos && guidPos >= 5 && result[guidPos - 5] == L'&')
    {
        bool allHex = true;
        for (std::size_t k = guidPos - 4; k < guidPos; ++k)
        {
            if (!iswxdigit(result[k])) { allHex = false; break; }
        }
        if (allHex) result.erase(guidPos - 5, 5);
    }

    return result;
}

/**
 * @brief Derive a readable product name from the VID/PID in the device path
 */
static QString gsusbProductName(const std::wstring& pPath)
{
    std::wstring lower = pPath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    auto extract = [&](const wchar_t* pTag) -> quint16 {
        auto pos = lower.find(pTag);
        if (pos == std::wstring::npos) return 0;
        return static_cast<quint16>(wcstoul(lower.c_str() + pos + wcslen(pTag), nullptr, 16));
    };

    const quint16 vid = extract(L"vid_");
    const quint16 pid = extract(L"pid_");

    if (vid == 0x1D50 && pid == 0x606F) return QStringLiteral("candleLight");
    if (vid == 0x1209 && pid == 0x2323) return QStringLiteral("CANable");
    if (vid == 0x1209 && pid == 0xCA01) return QStringLiteral("CANnectivity");
    if (vid == 0x1CD2 && pid == 0x606F) return QStringLiteral("CANable");
    if (vid == 0x16D0 && pid == 0x117E) return QStringLiteral("cantact");

    return QStringLiteral("gs_usb");
}

/* Channel counts seen in earlier scans, keyed by port name. A device that one of our own
 * connections currently holds must not be probed again, so remember what it reported while
 * it was still free.
 *
 * The probe matters: candle_dev_open() opens the device with FILE_SHARE_READ | FILE_SHARE_WRITE
 * and immediately queues read URBs, so a second open on a device that is in use would consume
 * frames the live connection is waiting for. gGSUSBOpenPaths tracks which base device paths are
 * taken so enumerateDevices() can skip them. */
static QMutex gGSUSBCacheMutex;
static QHash<QString, GSUSBDeviceInfo> gGSUSBCache;
static QSet<QString> gGSUSBOpenPaths;

QList<GSUSBDeviceInfo> GSUSBConnection::enumerateDevices()
{
    QList<GSUSBDeviceInfo> devices;

    candle_list_handle list;
    if (!candle_list_scan(&list)) return devices;

    uint8_t numDevices = 0;
    if (!candle_list_length(list, &numDevices))
    {
        candle_list_free(list);
        return devices;
    }

    QSet<QString> seenPaths;
    QHash<QString, int> nameCounts;

    for (uint8_t i = 0; i < numDevices; i++)
    {
        candle_handle dev;
        if (!candle_dev_get(list, i, &dev)) continue;

        const std::wstring devPath(candle_dev_get_path(dev));
        const QString baseKey = QString::fromWCharArray(gsusbBaseDevicePath(devPath).c_str());

        /* additional USB interfaces of a device we already listed */
        if (seenPaths.contains(baseKey))
        {
            candle_dev_free(dev);
            continue;
        }
        seenPaths.insert(baseKey);

        GSUSBDeviceInfo info;
        info.path = QString::fromWCharArray(devPath.c_str());

        const QString product = gsusbProductName(devPath);
        info.name = product % "#" % QString::number(nameCounts[product]++);

        bool inUse;
        {
            QMutexLocker locker(&gGSUSBCacheMutex);
            inUse = gGSUSBOpenPaths.contains(baseKey);
        }

        /* the channel count only becomes readable once the device is open */
        if (!inUse && candle_dev_open(dev))
        {
            uint8_t numChannels = 0;
            if (candle_channel_count(dev, &numChannels) && numChannels > 0)
                info.numChannels = std::min<int>(numChannels, GSUSB_MAX_CHANNELS);
            candle_dev_close(dev);
        }
        else
        {
            /* held by one of our connections, or the open failed, so reuse the earlier answer */
            QMutexLocker locker(&gGSUSBCacheMutex);
            if (gGSUSBCache.contains(info.name)) info.numChannels = gGSUSBCache[info.name].numChannels;
        }
        candle_dev_free(dev);

        devices.append(info);
    }

    candle_list_free(list);

    {
        QMutexLocker locker(&gGSUSBCacheMutex);
        foreach (const GSUSBDeviceInfo& info, devices) gGSUSBCache[info.name] = info;
    }

    return devices;
}

int GSUSBConnection::channelCountForPort(const QString& portName)
{
    {
        QMutexLocker locker(&gGSUSBCacheMutex);
        if (gGSUSBCache.contains(portName)) return gGSUSBCache[portName].numChannels;
    }

    foreach (const GSUSBDeviceInfo& info, enumerateDevices())
    {
        if (info.name == portName) return info.numChannels;
    }

    /* device not plugged in right now, assume a single channel so the connection can still be
     * created and shown as disconnected */
    return 1;
}

/*****************************************************************************/
/* connection                                                                */
/*****************************************************************************/

GSUSBConnection::GSUSBConnection(QString portName, int busSpeed, bool canFd, int dataRate) :
    CANConnection(portName, "GS_USB", CANCon::GS_USB, 0, busSpeed, canFd, dataRate,
                  channelCountForPort(portName), 4000, true),
    mDevice(nullptr),
    mDeviceChannels(0),
    mReaderRunning(false),
    mRxNotifyPending(false),
    mHostOffsetStartUs(0),
    mDeviceTicksStartUs(0),
    mDeviceTsHigh(0),
    mPrevDeviceTs(0),
    mDeviceTsValid(false)
{
    sendDebug("GSUSBConnection()");

    mChannelRunning.fill(false, getNumBuses());
    mFdEnabled.fill(false, getNumBuses());

    /* The connection window only ever persists the settings of bus 0 and refuses to show a bus
     * that was never configured, so give every channel a usable default up front. */
    for (int i = 0; i < getNumBuses(); i++)
    {
        CANBus bus;
        bus.setSpeed(busSpeed > 0 ? busSpeed : 500000);
        bus.setDataRate(dataRate > 0 ? dataRate : 2000000);
        bus.setCanFD(canFd);
        bus.setListenOnly(false);
        bus.setSingleWire(false);
        bus.setActive(true);
        setBusConfig(i, bus);
    }
}

GSUSBConnection::~GSUSBConnection()
{
    stop();
    sendDebug("~GSUSBConnection()");
}

void GSUSBConnection::sendDebug(const QString& pDebugText)
{
    qDebug() << pDebugText;
    emit debugOutput(pDebugText);
}

void GSUSBConnection::piStarted()
{
    if (!openDevice())
    {
        setStatus(CANCon::NOT_CONNECTED);
        CANConStatus stats;
        stats.conStatus = getStatus();
        stats.numHardwareBuses = getNumBuses();
        emit status(stats);
        return;
    }

    for (int i = 0; i < getNumBuses(); i++)
    {
        CANBus bus;
        if (!getBusConfig(i, bus)) continue;
        configureChannel(i, bus);
    }

    startReader();

    setStatus(CANCon::CONNECTED);
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = getNumBuses();
    emit status(stats);
}

void GSUSBConnection::piStop()
{
    stopReader();

    for (int i = 0; i < getNumBuses(); i++) stopChannel(i);

    closeDevice();

    setStatus(CANCon::NOT_CONNECTED);
    CANConStatus stats;
    stats.conStatus = getStatus();
    stats.numHardwareBuses = getNumBuses();
    emit status(stats);
}

void GSUSBConnection::piSuspend(bool pSuspend)
{
    setCapSuspended(pSuspend);

    if (isCapSuspended())
    {
        QMutexLocker locker(&mRxMutex);
        mRxFrames.clear();
        getQueue().flush();
    }
}

bool GSUSBConnection::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}

void GSUSBConnection::piSetBusSettings(int pBusIdx, CANBus bus)
{
    if (pBusIdx < 0 || pBusIdx >= getNumBuses()) return;

    setBusConfig(pBusIdx, bus);

    if (!mDevice) return;

    /* gs_usb only accepts a new bit timing while the channel is stopped */
    stopChannel(pBusIdx);
    configureChannel(pBusIdx, bus);
}

bool GSUSBConnection::piSendFrame(const CANFrame& frame)
{
    if (!mDevice) return false;

    const int bus = frame.bus;
    if (bus < 0 || bus >= getNumBuses()) return false;
    if (!mChannelRunning[bus]) return false;

    /* error frames are a reporting mechanism, they cannot be put on the wire */
    if (frame.frameId() & 0x20000000) return true;

    const QByteArray payload = frame.payload();
    const bool sendAsFd = mFdEnabled[bus] && (frame.hasFlexibleDataRateFormat() || payload.length() > 8);

    QMutexLocker locker(&mTxMutex);
    bool ok = false;

    if (sendAsFd)
    {
        candle_fd_frame_t out;
        memset(&out, 0, sizeof(out));

        out.can_id = frame.frameId();
        if (frame.hasExtendedFrameFormat()) out.can_id |= CANDLE_ID_EXTENDED;

        out.flags = CANDLE_FRAME_FLAG_FD;
        if (frame.hasBitrateSwitch()) out.flags |= CANDLE_FRAME_FLAG_BRS;

        const int len = std::min<int>(payload.length(), 64);
        out.can_dlc = candle_len_to_dlc(static_cast<uint8_t>(len));
        /* a DLC always maps to a fixed length, pad the rest with zeroes */
        for (int i = 0; i < len; i++) out.data[i] = static_cast<uint8_t>(payload[i]);

        ok = candle_fd_frame_send(mDevice, static_cast<uint8_t>(bus), &out);
    }
    else
    {
        candle_frame_t out;
        memset(&out, 0, sizeof(out));

        out.can_id = frame.frameId();
        if (frame.hasExtendedFrameFormat()) out.can_id |= CANDLE_ID_EXTENDED;
        if (frame.frameType() == QCanBusFrame::RemoteRequestFrame) out.can_id |= CANDLE_ID_RTR;

        const int len = std::min<int>(payload.length(), 8);
        out.can_dlc = static_cast<uint8_t>(len);
        for (int i = 0; i < len; i++) out.data[i] = static_cast<uint8_t>(payload[i]);

        ok = candle_frame_send(mDevice, static_cast<uint8_t>(bus), &out);
    }

    if (!ok) sendDebug("GS_USB: frame send failed with error " % QString::number(static_cast<int>(candle_dev_last_error(mDevice))));

    return ok;
}

/*****************************************************************************/
/* device handling                                                           */
/*****************************************************************************/

bool GSUSBConnection::openDevice()
{
    if (mDevice) return true;

    candle_list_handle list;
    if (!candle_list_scan(&list))
    {
        sendDebug("GS_USB: could not scan the USB bus");
        return false;
    }

    uint8_t numDevices = 0;
    if (!candle_list_length(list, &numDevices))
    {
        candle_list_free(list);
        return false;
    }

    /* Resolve the stored port name back to a device. The name carries the index among all
     * devices of the same product, so rebuild the same numbering the scan produced. */
    QSet<QString> seenPaths;
    QHash<QString, int> nameCounts;
    candle_handle match = nullptr;
    std::wstring matchPath;
    QString matchBaseKey;

    for (uint8_t i = 0; i < numDevices; i++)
    {
        candle_handle dev;
        if (!candle_dev_get(list, i, &dev)) continue;

        const std::wstring devPath(candle_dev_get_path(dev));
        const QString baseKey = QString::fromWCharArray(gsusbBaseDevicePath(devPath).c_str());
        if (seenPaths.contains(baseKey))
        {
            candle_dev_free(dev);
            continue;
        }
        seenPaths.insert(baseKey);

        const QString product = gsusbProductName(devPath);
        const QString name = product % "#" % QString::number(nameCounts[product]++);

        if (name == getPort() && !match)
        {
            match = dev;
            matchPath = devPath;
            matchBaseKey = baseKey;
        }
        else candle_dev_free(dev);
    }

    candle_list_free(list);

    if (!match)
    {
        sendDebug("GS_USB: device " % getPort() % " not found");
        return false;
    }

    if (!candle_dev_open(match))
    {
        sendDebug("GS_USB: could not open " % getPort() % ", error " % QString::number(static_cast<int>(candle_dev_last_error(match))));
        candle_dev_free(match);
        return false;
    }

    /* The number of buses was fixed when the connection was constructed. If the device now
     * reports fewer channels (a different device took over the name) only drive what exists. */
    uint8_t numChannels = 0;
    mDeviceChannels = getNumBuses();
    if (candle_channel_count(match, &numChannels) && numChannels > 0)
        mDeviceChannels = std::min<int>(numChannels, getNumBuses());

    if (mDeviceChannels < getNumBuses())
    {
        sendDebug("GS_USB: device reports " % QString::number(numChannels) %
                  " channels but the connection was created with " % QString::number(getNumBuses()));
    }

    mDevice = match;
    mDevicePath = QString::fromWCharArray(matchPath.c_str());
    mDeviceBaseKey = matchBaseKey;

    {
        /* keep device scans from opening this device behind our back */
        QMutexLocker locker(&gGSUSBCacheMutex);
        gGSUSBOpenPaths.insert(mDeviceBaseKey);
    }

    /* Anchor the device clock to the host clock so frame timestamps line up with the rest of
     * SavvyCAN. Both are microseconds since the epoch after this. */
    const quint64 hostNowUs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ull;
    uint32_t deviceTicks = 0;
    mHostOffsetStartUs  = hostNowUs;
    mDeviceTicksStartUs = 0;
    mDeviceTsHigh       = 0;
    mPrevDeviceTs       = 0;
    mDeviceTsValid      = false;

    if (candle_dev_get_timestamp_us(mDevice, &deviceTicks))
    {
        mDeviceTicksStartUs = deviceTicks;
        mDeviceTsValid = true;
    }
    else sendDebug("GS_USB: no hardware timestamps, falling back to the host clock");

    sendDebug("GS_USB: opened " % getPort() % " with " % QString::number(getNumBuses()) % " channel(s)");

    return true;
}

void GSUSBConnection::closeDevice()
{
    if (!mDevice) return;

    {
        QMutexLocker locker(&gGSUSBCacheMutex);
        gGSUSBOpenPaths.remove(mDeviceBaseKey);
    }
    mDeviceBaseKey.clear();

    candle_dev_close(mDevice);
    candle_dev_free(mDevice);
    mDevice = nullptr;
}

bool GSUSBConnection::configureChannel(int pBusIdx, const CANBus& pBus)
{
    if (!mDevice) return false;
    if (pBusIdx < 0 || pBusIdx >= getNumBuses()) return false;

    mFdEnabled[pBusIdx] = false;

    if (pBusIdx >= mDeviceChannels) return false;

    /* bits 3, 5 and 6 tell the connection window that the enabled, listen only and speed
     * values in this update are meaningful */
    const int busStatValid = 0x08 | 0x20 | 0x40;

    if (!pBus.isActive())
    {
        emit busStatus(pBusIdx, pBus.getSpeed(), busStatValid);
        return false;
    }

    const uint8_t channel = static_cast<uint8_t>(pBusIdx);

    candle_capability_t caps;
    if (!candle_channel_get_capabilities(mDevice, channel, &caps))
    {
        sendDebug("GS_USB: could not read capabilities of channel " % QString::number(pBusIdx));
        return false;
    }

    candle_bittiming_t timing;
    if (!lookupBitTiming(GSUSB_NOMINAL_TIMINGS,
                         sizeof(GSUSB_NOMINAL_TIMINGS) / sizeof(GSUSB_NOMINAL_TIMINGS[0]),
                         caps.fclk_can, static_cast<quint32>(pBus.getSpeed()), timing)
        && !calcBitTiming(caps, static_cast<quint32>(pBus.getSpeed()), GSUSB_NOMINAL_SAMPLE_POINT, false, timing))
    {
        sendDebug("GS_USB: no bit timing for " % QString::number(pBus.getSpeed()) %
                  " bit/s at a " % QString::number(caps.fclk_can) % " Hz clock");
        return false;
    }

    if (!candle_channel_set_timing(mDevice, channel, &timing))
    {
        sendDebug("GS_USB: setting the bit timing of channel " % QString::number(pBusIdx) % " failed");
        return false;
    }

    uint32_t flags = 0;
    if (pBus.isListenOnly()) flags |= CANDLE_MODE_LISTEN_ONLY;
    if (caps.feature & CANDLE_FEATURE_HW_TIMESTAMP) flags |= CANDLE_MODE_HW_TIMESTAMP;

    if (pBus.isCanFD())
    {
        if (!(caps.feature & CANDLE_FEATURE_FD))
        {
            sendDebug("GS_USB: channel " % QString::number(pBusIdx) % " does not support CAN FD");
        }
        else
        {
            const quint32 dataRate = pBus.getDataRate() > 0 ? static_cast<quint32>(pBus.getDataRate())
                                                            : 2000000u;
            candle_bittiming_t dataTiming;
            if (lookupBitTiming(GSUSB_DATA_TIMINGS,
                                sizeof(GSUSB_DATA_TIMINGS) / sizeof(GSUSB_DATA_TIMINGS[0]),
                                caps.fclk_can, dataRate, dataTiming)
                || calcBitTiming(caps, dataRate, GSUSB_DATA_SAMPLE_POINT, true, dataTiming))
            {
                if (candle_channel_set_data_timing(mDevice, channel, &dataTiming))
                {
                    flags |= CANDLE_MODE_FD;
                    mFdEnabled[pBusIdx] = true;
                }
                else sendDebug("GS_USB: setting the data bit timing failed, staying on classic CAN");
            }
            else
            {
                sendDebug("GS_USB: no data bit timing for " % QString::number(dataRate) %
                          " bit/s, staying on classic CAN");
            }
        }
    }

    if (!candle_channel_start(mDevice, channel, flags))
    {
        sendDebug("GS_USB: starting channel " % QString::number(pBusIdx) % " failed, error " %
                  QString::number(static_cast<int>(candle_dev_last_error(mDevice))));
        mFdEnabled[pBusIdx] = false;
        return false;
    }

    mChannelRunning[pBusIdx] = true;

    int busStat = busStatValid | 1;
    if (pBus.isListenOnly()) busStat |= 4;
    emit busStatus(pBusIdx, pBus.getSpeed(), busStat);

    return true;
}

void GSUSBConnection::stopChannel(int pBusIdx)
{
    if (!mDevice) return;
    if (pBusIdx < 0 || pBusIdx >= getNumBuses()) return;
    if (!mChannelRunning[pBusIdx]) return;


    candle_channel_stop(mDevice, static_cast<uint8_t>(pBusIdx));
    mChannelRunning[pBusIdx] = false;
    mFdEnabled[pBusIdx] = false;
}

/*****************************************************************************/
/* receive path                                                              */
/*****************************************************************************/

void GSUSBConnection::startReader()
{
    if (mReaderRunning.load()) return;

    /* a notification that was posted but never delivered would otherwise block every
     * later one, so clear it before the thread starts producing again */
    mRxNotifyPending.store(false);
    mReaderRunning.store(true);
    mReaderThread = std::thread(&GSUSBConnection::readerLoop, this);
}

void GSUSBConnection::stopReader()
{
    mReaderRunning.store(false);
    if (mReaderThread.joinable()) mReaderThread.join();
    mRxNotifyPending.store(false);

    QMutexLocker locker(&mRxMutex);
    mRxFrames.clear();
}

void GSUSBConnection::readerLoop()
{
    while (mReaderRunning.load())
    {
        candle_fd_frame_t frame;
        if (!candle_fd_frame_read(mDevice, &frame, GSUSB_READ_TIMEOUT_MS)) continue;

        const candle_frametype_t frameType = candle_fd_frame_type(&frame);
        if (frameType != CANDLE_FRAMETYPE_RECEIVE && frameType != CANDLE_FRAMETYPE_ERROR) continue;
        if (static_cast<int>(frame.channel) >= getNumBuses()) continue;

        GSUSBRxFrame rx;
        rx.frame = frame;
        rx.timestampUs = 0;
        rx.timestampValid = deviceTimestampToHostUs(candle_fd_frame_timestamp_us(&frame), rx.timestampUs);

        {
            QMutexLocker locker(&mRxMutex);
            mRxFrames.append(rx);
        }

        /* Hand over to the connection thread. Only post a new event when the previous one has
         * not been processed yet, otherwise a busy bus floods the event loop. */
        if (!mRxNotifyPending.exchange(true))
            QMetaObject::invokeMethod(this, "processRxFrames", Qt::QueuedConnection);
    }
}

bool GSUSBConnection::deviceTimestampToHostUs(quint32 pRawTimestampUs, quint64& pHostTimestampUs)
{
    /* the firmware counter is 32 bit microseconds, so it wraps roughly every 71 minutes */
    static const quint32 wrapThreshold = 0x80000000u;

    if (!mDeviceTsValid) return false;

    if (mPrevDeviceTs != 0 && pRawTimestampUs < mPrevDeviceTs
            && (mPrevDeviceTs - pRawTimestampUs) > wrapThreshold)
    {
        mDeviceTsHigh += (1ull << 32);
    }

    quint64 absTimestampUs = static_cast<quint64>(pRawTimestampUs) + mDeviceTsHigh;

    /* the first frame after opening may predate the epoch sample, either because it was
     * buffered in the device or because the counter wrapped in between */
    if (mPrevDeviceTs == 0 && absTimestampUs < mDeviceTicksStartUs)
    {
        if ((mDeviceTicksStartUs - absTimestampUs) > wrapThreshold)
        {
            mDeviceTsHigh += (1ull << 32);
            absTimestampUs += (1ull << 32);
        }
        else return false;
    }

    if (absTimestampUs < mDeviceTicksStartUs) return false;

    mPrevDeviceTs = pRawTimestampUs;
    pHostTimestampUs = mHostOffsetStartUs + (absTimestampUs - mDeviceTicksStartUs);
    return true;
}

void GSUSBConnection::processRxFrames()
{
    mRxNotifyPending.store(false);

    QVector<GSUSBRxFrame> frames;
    {
        QMutexLocker locker(&mRxMutex);
        frames.swap(mRxFrames);
    }

    if (isCapSuspended()) return;

    foreach (const GSUSBRxFrame& rx, frames)
    {
        candle_fd_frame_t frame = rx.frame;

        CANFrame buildFrame;
        buildFrame.bus = frame.channel;
        buildFrame.isReceived = true;
        buildFrame.setFrameId(candle_fd_frame_id(&frame));
        buildFrame.setExtendedFrameFormat(candle_fd_frame_is_extended_id(&frame));

        if (candle_fd_frame_type(&frame) == CANDLE_FRAMETYPE_ERROR)
            buildFrame.setFrameType(QCanBusFrame::ErrorFrame);
        else if (candle_fd_frame_is_rtr(&frame))
            buildFrame.setFrameType(QCanBusFrame::RemoteRequestFrame);
        else
            buildFrame.setFrameType(QCanBusFrame::DataFrame);

        const bool isFd = candle_fd_frame_is_fd(&frame);
        buildFrame.setFlexibleDataRateFormat(isFd);
        if (isFd) buildFrame.setBitrateSwitch(candle_fd_frame_is_brs(&frame));

        const uint8_t rawDlc = candle_fd_frame_dlc(&frame);
        const int len = isFd ? candle_dlc_to_len(rawDlc) : std::min<int>(rawDlc, 8);
        const uint8_t* data = candle_fd_frame_data(&frame);

        QByteArray buildData;
        buildData.resize(len);
        for (int i = 0; i < len; i++) buildData[i] = static_cast<char>(data[i]);
        buildFrame.setPayload(buildData);

        const quint64 timestampUs = (useSystemTime || !rx.timestampValid)
                ? static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ull
                : rx.timestampUs;
        buildFrame.setTimeStamp(QCanBusFrame::TimeStamp(0, static_cast<qint64>(timestampUs)));

        CANFrame* frame_p = getQueue().get();
        if (!frame_p)
        {
            qDebug() << "GS_USB: can't get a frame, ERROR";
            break;
        }

        *frame_p = buildFrame;
        checkTargettedFrame(buildFrame);
        getQueue().queue();
    }
}

#endif // Q_OS_WIN
