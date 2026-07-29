#ifndef GS_USB_H
#define GS_USB_H

#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QList>
#include <QMutex>
#include <QString>
#include <QVector>

#include <atomic>
#include <thread>

#include "canconnection.h"
#include "candle_api/candle.h"

/**
 * @brief A gs_usb device as reported by a USB bus scan
 */
struct GSUSBDeviceInfo
{
    QString name;           //!< port name shown in the UI and stored in settings, e.g. "CANable#0"
    QString path;           //!< Windows device interface path, unique per physical device
    int     numChannels;    //!< number of CAN channels the device reports

    GSUSBDeviceInfo() : numChannels(1) {}
};

/**
 * @brief A single RX frame handed from the USB reader thread to the connection thread
 */
struct GSUSBRxFrame
{
    candle_fd_frame_t frame;
    quint64           timestampUs;
    bool              timestampValid;
};

/**
 * @brief Connection to a gs_usb class device (candleLight, CANable, CANnectivity, cantact, ...)
 *
 * Windows only. On Linux and macOS these devices are handled by the kernel gs_usb driver and
 * show up as SocketCAN interfaces, so no user space driver is needed there.
 *
 * One connection owns one physical device. All CAN channels of that device are exposed as
 * SavvyCAN buses. gs_usb multiplexes every channel over a single USB bulk IN endpoint, so a
 * single reader thread reads the endpoint and fans frames out by their channel number.
 */
class GSUSBConnection : public CANConnection
{
    Q_OBJECT

public:
    GSUSBConnection(QString portName, int busSpeed, bool canFd, int dataRate);
    virtual ~GSUSBConnection();

    /**
     * @brief Scan the USB bus for gs_usb devices
     * @return one entry per physical device (not per channel)
     * @note Devices that are currently open by another connection cannot be probed for their
     *       channel count, so a previously scanned value is reused for those.
     */
    static QList<GSUSBDeviceInfo> enumerateDevices();

protected:
    virtual void piStarted();
    virtual void piStop();
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus);
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus);
    virtual void piSuspend(bool pSuspend);
    virtual bool piSendFrame(const CANFrame&);

private slots:
    /**
     * @brief Move everything the reader thread collected into the connection queue.
     * @note Runs in the connection thread.
     */
    void processRxFrames();

private:
    /* number of buses to hand to the CANConnection constructor, needs the device before we have one */
    static int channelCountForPort(const QString& portName);

    bool openDevice();
    void closeDevice();
    bool configureChannel(int pBusIdx, const CANBus& pBus);
    void stopChannel(int pBusIdx);
    void startReader();
    void stopReader();
    void readerLoop();
    bool deviceTimestampToHostUs(quint32 pRawTimestampUs, quint64& pHostTimestampUs);
    void sendDebug(const QString& pDebugText);

    candle_handle mDevice;
    QString       mDevicePath;
    QString       mDeviceBaseKey;   //!< device path with the USB interface number stripped
    int           mDeviceChannels;   //!< channels the device actually reports, <= getNumBuses()

    std::thread       mReaderThread;
    std::atomic<bool> mReaderRunning;
    std::atomic<bool> mRxNotifyPending;

    QMutex                mRxMutex;
    QVector<GSUSBRxFrame> mRxFrames;

    QMutex mTxMutex;

    QVector<bool> mChannelRunning;
    QVector<bool> mFdEnabled;

    /* device clock to host clock mapping, only written by the reader thread once it is running */
    quint64 mHostOffsetStartUs;
    quint64 mDeviceTicksStartUs;
    quint64 mDeviceTsHigh;
    quint32 mPrevDeviceTs;
    bool    mDeviceTsValid;
};

#endif // Q_OS_WIN

#endif // GS_USB_H
