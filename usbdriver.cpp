/*
 * Copyright (C) 2012 Renato Araujo <renatox@gamail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "usbdriver.h"

#include <QDebug>
#include <unistd.h>

#define BEMATECH_ID_VENDOR  0x0B1B

// Device endpoint(s)
#define EP_OUT  0x01
#define EP_IN   0x82
#define DEVICE_BUFFER_SIZE 64

static bool initlialized = false;

BematechDrv::BematechDrv(int product_id)
    : m_dev(0),
      m_product_id(product_id),
      m_interfaceID(-1),
      m_endPointRead(EP_IN),
      m_endPointWrite(EP_OUT)

{
    usb_init(); /* initialize the library */
}

BematechDrv::~BematechDrv()
{
    if (m_dev)
        close();
}

bool BematechDrv::open()
{
    struct usb_bus *bus;
    struct usb_device *dev;

    if (m_dev)
        return true;

    usb_find_busses(); /* find all busses */
    usb_find_devices(); /* find all connected devices */

    for (bus = usb_get_busses(); bus; bus = bus->next) {
        for (dev = bus->devices; dev; dev = dev->next) {
            if (dev->descriptor.idVendor == BEMATECH_ID_VENDOR &&
                dev->descriptor.idProduct == m_product_id)
            {
                m_dev = usb_open(dev);
                int configId = -1;
                int interfaceId = -1;
                int altInterfaceId = -1;

                for(int i=0; i < dev->descriptor.bNumConfigurations; i++) {
                    struct usb_config_descriptor config = dev->config[i];
                    for(int y=0; y < config.bNumInterfaces; y++) {
                        for(int a=0; a < config.interface[y].num_altsetting; a++) {
                            if (config.interface[y].altsetting[a].bDescriptorType == 4) {
                                configId = config.bConfigurationValue;
                                interfaceId = config.interface[y].altsetting[a].bInterfaceNumber;
                                altInterfaceId = config.interface[y].altsetting[a].bAlternateSetting;
                            }
                        }
                    }
                }
                QByteArray functionError;
                if (usb_set_configuration(m_dev, configId) == 0) {
                    if (usb_claim_interface(m_dev, interfaceId) == 0) {
                        if (usb_set_altinterface(m_dev, altInterfaceId) == 0) {
                            m_interfaceID = interfaceId;
                            if (initialize()) {
                                return true;
                            }
                        } else {
                            functionError = "usb_set_altinterface";
                        }
                    } else {
                        functionError = "usb_claim_interface";
                    }
                } else {
                   functionError = "usb_set_configuration";
                }

                if (!functionError.isEmpty()) {
                    qWarning() << functionError << "ERROR:" << usb_strerror();
                }
            }
        }
    }
    m_dev = 0;
    qWarning() << "IMPRESSORA BEMATECH NOT FOUND";
    return false;
}

void BematechDrv::close()
{
    if (m_interfaceID == -1)
        return;

    if (m_dev == 0)
        return;

    sendPostCommand();
    sendCommand("\x1D\xf8\x46"); //reset

    if (usb_clear_halt(m_dev, m_endPointRead) < 0) {
        qWarning() << "Fail to clear end-point";
    }
    if (usb_clear_halt(m_dev, m_endPointWrite) < 0) {
        qWarning() << "Fail to clear end-point";
    }
    if (usb_clear_halt(m_dev, 0x83) < 0) {
        qWarning() << "Fail to clear end-point";
    }

    if (m_dev)
        usb_release_interface(m_dev, m_interfaceID);

    if (m_dev)
        usb_close(m_dev);

    m_dev = 0;
}

bool BematechDrv::reset()
{
    close();
    sleep(7); // wait for print reset
    return open();
}

bool BematechDrv::initialize()
{
    int ret = 0;

    ret = sendControlMessage(0x22, 0x0003, QByteArray("", 0));
    if (ret < 0)
        return false;

    return sendPreCommand();
}

bool BematechDrv::sendPreCommand()
{
    static QByteArray _command_setMode("\x1D\xF9\x35\x00", 4); // chage printer mode
    static QByteArray _command_reset("\x1B\x40", 2); // reset printer

    if (!sendCommand(_command_setMode)) {
        qWarning() << "Fail to chame printer mode:" << usb_strerror();
        return false;
    }

    if (!sendCommand(_command_reset)) {
        qWarning() << "Fail to reset printer:" << usb_strerror();
        return false;
    }
    return true;
}

bool BematechDrv::sendPostCommand()
{
    static QByteArray _command_restore("\x1D\xF9\x1F\x31", 4);
    if (!sendCommand(_command_restore)){
        qWarning() << "error sending post command:" << usb_strerror();
        return false;
    }
    return true;
}

bool BematechDrv::sendCommand(const QByteArray &data)
{
    bool error = false;

#if 1
    QByteArray buffer = data;
    while(buffer.size() > 0 ) {
        int size = buffer.size() > DEVICE_BUFFER_SIZE ? DEVICE_BUFFER_SIZE : buffer.size();
        int ret = usb_bulk_write(m_dev, EP_OUT, (char*)buffer.data(), size, 5000);
        if (ret < 0) {
            error = true;
            qWarning() << "error writing:" << usb_strerror();
            buffer.clear();
            break;
        } else {
            buffer.remove(0, ret);
        }
    }
#else
    int ret = usb_bulk_write(m_dev, EP_OUT, (char*)data.data(), data.size(), 5000);
    if (ret < 0)
        error = true;
#endif

    return error==false;
}

//API commands
int BematechDrv::getCommandSet()
{
    if (sendCommand(QByteArray("\x1D\xF9\x43\x00", 4))) {
        QByteArray result = readData(m_endPointRead);
        if (!result.isEmpty()) {
            return result.toInt();
        }
    }
    return -1;
}

QHash<QString, QByteArray> BematechDrv::productInfo()
{
    QHash<QString, QByteArray> result;

    //Product code
    if (sendCommand(QByteArray("\x1D\xF9\x27\x00", 4))) {
        QByteArray read = readData(m_endPointRead);
        if (!read.isEmpty()) {
            result["ProductCode"] = read;
        }
    }

    //Serial number
    if (sendCommand(QByteArray("\x1D\xF9\x27\x31", 4))) {
        QByteArray read = readData(m_endPointRead);
        if (!read.isEmpty()) {
            result["SerialNumber"] = read;
        }
    }

    //Manufacturing date
    if (sendCommand(QByteArray("\x1D\xF9\x27\x32", 4))) {
        QByteArray read = readData(m_endPointRead);
        if (!read.isEmpty()) {
            result["ManufacturingDate"] = read;
        }
    }

    //Firmware version
    if (sendCommand(QByteArray("\x1D\xF9\x27\x33", 4))) {
        QByteArray read = readData(m_endPointRead);
        if (!read.isEmpty()) {
            result["FirmwareVersion"] = read;
        }
    }

    //Manufacturing timestamp
    if (sendCommand(QByteArray("\x1D\xF9\x27\x35", 4))) {
        QByteArray read = readData(m_endPointRead);
        if (!read.isEmpty()) {
            result["ManufacturingTimestamp"] = read;
        }
    }

    //Interface
    if (sendCommand(QByteArray("\x1D\xF9\x27\x38", 4))) {
        QByteArray read = readData(m_endPointRead);
        if (!read.isEmpty()) {
            result["Interface"] = read;
        }
    }

    return result;
}

int BematechDrv::sendControlMessage(unsigned int bRequest,
                                    unsigned int wValue,
                                    QByteArray data)
{
    int ret = 0;
    ret = usb_control_msg(m_dev,
                          USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_ENDPOINT_OUT,
                          bRequest, /* set/get test */
                          wValue,  /* test type    */
                          m_interfaceID,  /* interface id */
                          data.data(), data.size(), 5000);
    if (ret < 0) {
        printf("error sending control message:\n%s\n", usb_strerror());
        return -1;
    } else {
        printf("success: control message send ok\n");
        return 0;
    }
}

QByteArray BematechDrv::sendInterruptlMessage(int endPoint)
{
    char data[255];
    int size=254;
    memset(data, '\0', size);

    size = usb_interrupt_read(m_dev, endPoint, data, 254, 5000);
    if (size < 0) {
        printf("error sending interrupt message:\n%s\n", usb_strerror());
        return QByteArray();
    } else {
        printf("success: interrupt message read ok: %d\n", size);
        return QByteArray(data, size);
    }
}

QByteArray BematechDrv::readData(int endPoint)
{
    char buffer[DEVICE_BUFFER_SIZE];

    int ret = usb_bulk_read(m_dev, endPoint, buffer, DEVICE_BUFFER_SIZE, 5000);
    if (ret < 0) {
        qWarning() << "error reading data:" << usb_strerror();
        return QByteArray();
    } else {
        return QByteArray(buffer, ret);
    }
}
