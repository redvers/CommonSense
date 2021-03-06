#include <QCoreApplication>
#include <QMessageBox>
#include <QDebug>
#include <string>
#include <algorithm>
#include "DeviceInterface.h"


DeviceInterface::DeviceInterface(QObject *parent): QObject(parent),
    device(NULL), pollTimerId(0), mode(DeviceInterfaceNormal), currentStatus(DeviceDisconnected)
{
    config = new DeviceConfig();
    installEventFilter(config);

    connect(config, SIGNAL(changed()), this, SLOT(configChanged()));
    connect(config, SIGNAL(downloadBlock(c2command, uint8_t)), this, SLOT(sendCommand(c2command, uint8_t)));
    connect(config, SIGNAL(uploadBlock(OUT_c2packet_t)), this, SLOT(sendCommand(OUT_c2packet_t)));

    connect(config, SIGNAL(sendCommand(c2command, uint8_t)), this, SLOT(sendCommand(c2command, uint8_t)));
}

DeviceInterface::~DeviceInterface(void)
{
    _releaseDevice();
}

/**
 * This is the handler of last resort for messages from device.
 * Other modules are supposed to install the event filter and process messages of interest.
 * Default behavior is to log them as strings.
 */
bool DeviceInterface::event(QEvent* e)
{
    if (e->type() == DeviceMessage::ET) {
        QByteArray *payload = static_cast<DeviceMessage *>(e)->getPayload();
        switch (payload->at(0))
        {
        case C2RESPONSE_STATUS:
            qInfo().nospace() << "CommonSense v" << (uint8_t)payload->at(2) << "." << (uint8_t)payload->at(3)
                              << ", die temp " << (payload->at(4) == 1 ? '+' : '-') << (uint8_t)payload->at(5) << "C";
            qInfo().nospace() << "Quenched: " << (bool)(payload->at(1) & (1 << C2DEVSTATUS_EMERGENCY))
                              << ", Matrix monitoring: " << (bool)(payload->at(1) & (1 << C2DEVSTATUS_MATRIX_OUTPUT))
                              << ", setup mode: " << (bool)(payload->at(1) & (1 << C2DEVSTATUS_SETUP_MODE));
            return true;
        case C2RESPONSE_SCANCODE:
            if (!config->bValid)
            {
                return true;
            }
            uint8_t scancodeReleased, scancode, row, col;
            scancodeReleased = 0x80;
            scancode = payload->at(1);
            col = (scancode & ~scancodeReleased) % config->numCols;
            row = ((scancode & ~scancodeReleased) - col) / config->numCols;
            emit scancodeReceived(row, col, (scancode & scancodeReleased) ? KeyReleased : KeyPressed);
            qInfo().noquote() << QString((scancode & scancodeReleased) ? "+" : " -") << row+1 << col+1;
            return true;
        default:
            qInfo() << payload->constData();
            return true;
        }
    }
    return QObject::event(e);
}

void DeviceInterface::deviceMessageReceiver(void)
{
    qInfo() << "Message received";
}

void DeviceInterface::_sendPacket()
{
    //qInfo() << "Sending packet" << (uint8_t)outbox[1] << (uint8_t)outbox[2];
    if (!device) return; // TODO we should be more vocal
    outbox[0] = 0x00; // ReportID is not used.
    if (hid_write(device, outbox, sizeof outbox) == -1)
    {
        qWarning() << "Error sending to the device, will reconnect..";
        _releaseDevice();
    }
}

void DeviceInterface::sendCommand(c2command cmd, uint8_t *msg)
{
    memset(outbox, 0, sizeof(outbox));
    outbox[1] = cmd;
    memcpy(outbox+2, msg, 63);
    _sendPacket();
}

void DeviceInterface::sendCommand(c2command cmd, uint8_t msg)
{
    memset(outbox, 0, sizeof(outbox));
    outbox[1] = cmd;
    outbox[2] = msg;
    _sendPacket();
}

void DeviceInterface::sendCommand(OUT_c2packet_t cmd)
{
    memset(outbox, 0, sizeof(outbox));
    memcpy(outbox+1, cmd.raw, sizeof(cmd));
    _sendPacket();
}

void DeviceInterface::sendCommand(Bootloader_packet_t *packet)
{
    memset(outbox, 0, sizeof(outbox));
    uint8_t wire_length = packet->length + 7; // marker+cmd+len(2)+checksum(2)+marker
    memcpy_s(outbox+1, wire_length, packet->raw, wire_length);
    //qInfo() << (uint8_t)packet->raw[0] << (uint8_t)packet->raw[1] << (uint8_t)packet->raw[2] << (uint8_t)packet->raw[3]
    //        << (uint8_t)packet->raw[4] << (uint8_t)packet->raw[5] << (uint8_t)packet->raw[6] << (uint8_t)packet->raw[7]
    //        << (uint8_t)packet->raw[8];
    _sendPacket();
}

void DeviceInterface::configChanged(void)
{
    qInfo() << "Configuration changed.";
    if (currentStatus == DeviceConnected)
    {
        emit deviceStatusNotification(DeviceConfigChanged);
    }
}

void DeviceInterface::bootloaderMode(bool bEnabled)
{
    if (bEnabled)
    {
        qInfo() << "Entering firmware update mode.";
        mode = DeviceInterfaceBootloader;
        if (currentStatus == DeviceConnected)
        {
            emit sendCommand(C2CMD_ENTER_BOOTLOADER, 1);
        }
    }
    else
    {
        qInfo() << "Resuming normal operations.";
        mode = DeviceInterfaceNormal;
    }
    _resetTimer(1000);
    _releaseDevice();
}

void DeviceInterface::_resetTimer(int interval)
{
    if (pollTimerId) killTimer(pollTimerId);
    pollTimerId = startTimer(interval);
}

void DeviceInterface::timerEvent(QTimerEvent *)
{
    if (!device)
        return _initDevice();

    memset(bytesFromDevice, 0x00, sizeof(bytesFromDevice));
    int bytesRead = hid_read(device, bytesFromDevice, sizeof(bytesFromDevice));
    if (bytesRead < 0)
    {
        qInfo() << "Device went away. Reconnecting..";
        _releaseDevice();
        return;
    }
    if (bytesRead == 0)
        return;

    QCoreApplication::postEvent(this, new DeviceMessage(bytesFromDevice));
}

void DeviceInterface::_initDevice(void)
{
    device = acquireDevice();
    if (!device) {
        qInfo() << ".";
        _resetTimer(1000);
        return;
    }
    hid_set_nonblocking(device, 1);
    _updateDeviceStatus(mode == DeviceInterfaceNormal ? DeviceConnected : BootloaderConnected);
    _resetTimer(0);
    return;
}

void DeviceInterface::_updateDeviceStatus(DeviceStatus newStatus)
{
    if (newStatus != currentStatus)
    {
        currentStatus = newStatus;
        emit deviceStatusNotification(newStatus);
    }
}

hid_device* DeviceInterface::acquireDevice(void)
{
    hid_device *retval = NULL;
    if (hid_init())
    {
        qInfo() << "Cannot initialize hidapi!";
        return NULL;
    }
    hid_device_info *root = hid_enumerate(0, 0);
    if (!root) {
        qInfo() << "No HID devices on this system?";
        return NULL;
    }
    hid_device_info *d = root;
    while (d){
        bool deviceSelected = false;
        switch (mode)
        {
            case DeviceInterfaceNormal:
//        qInfo() << d->path << d->vendor_id << d->product_id;
        // Usage and usage page are win and mac only :(
#ifdef __linux__
                if (d->vendor_id == 0x4114)
#else
                if (d->usage_page == 0x6213 && d->usage == 0x88)
#endif
#ifdef __linux__
                    if (d->interface_number == 1)
#endif
                        deviceSelected = true;
                break;
            case DeviceInterfaceBootloader:
                if (d->vendor_id == 0x04b4 && d->product_id == 0xb71d)
                    deviceSelected = true;
                break;
            default:
                break;
        }
        if (deviceSelected)
        {
            qInfo() << "Found a node!";
            qInfo() << "Trying to use" << d->path;
            retval = hid_open_path(d->path);
            if (retval)
                break;
            qInfo() << "Cannot open device. Linux permissions problem?";
        }
        d = d->next;
    }
    hid_free_enumeration(root);
    return retval;
}

void DeviceInterface::_releaseDevice(void)
{
    if (device)
        qInfo("Releasing device.");
        _updateDeviceStatus(DeviceDisconnected);
        hid_close(device);
    device = NULL;
    if(hid_exit())
        qWarning("warning: error during hid_exit");
}

void DeviceInterface::start(void)
{
    qInfo() << "Acquiring device..";
    _resetTimer(0);
}
