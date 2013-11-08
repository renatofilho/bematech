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

#ifdef Q_WS_WIN
	#include <windows.h>
	#define sleep(x) Sleep(x*1000)
#else
	#include <unistd.h>
#endif

#include <errno.h>

#define BEMATECH_ID_VENDOR  0x0B1B

// Device endpoint(s)
#define EP_OUT  0x01
#define EP_IN   0x82
#define DEVICE_BUFFER_SIZE 1024

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
    if (m_dev)
        return true;

    usb_init(); /* initialize the library */
    usb_find_busses(); /* find all busses */
    usb_find_devices(); /* find all connected devices */

    struct usb_bus  *bus;
    struct usb_device *dev;
    struct usb_device *best_dev = 0;

    int best_conf = -1;
    int conf = -1;
    struct usb_config_descriptor *confptr;

    int best_iface = -1;
    int iface = -1;
    struct usb_interface *ifaceptr;

    int best_altset = -1;
    int altset = -1;
    struct usb_interface_descriptor *altptr;

    int best_write_ep = -1;
    int best_read_ep = -1;
    int endp;
    struct usb_endpoint_descriptor *endpptr;

    //Difent class for diferent model.
    int interfaceClas = m_product_id == 1 ? 255 : USB_CLASS_DATA;

    int protocol = -1;

    for (bus = usb_get_busses(); bus; bus = bus->next) {
        for (dev = bus->devices; dev; dev = dev->next) {
            if (dev->descriptor.idVendor != BEMATECH_ID_VENDOR
                || dev->descriptor.idProduct != m_product_id)
                continue;

            for(conf = 0, confptr = dev->config;
                conf < dev->descriptor.bNumConfigurations;
                conf++, confptr++) {
                for (iface = 0, ifaceptr = confptr->interface;
                     iface < confptr->bNumInterfaces;
                     iface++, ifaceptr++) {
                    for (altset = 0, altptr = ifaceptr->altsetting;
                         altset < ifaceptr->num_altsetting;
                         altset++, altptr++) {

                        if (altptr->bInterfaceClass != interfaceClas)
                            continue;

                        int read_endp = -1;
                        int write_endp = -1;

                        for (endp = 0, endpptr = altptr->endpoint;
                            endp < altptr->bNumEndpoints;
                            endp++, endpptr++) {

                            if ((endpptr->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK) {
                                if (endpptr->bEndpointAddress & USB_ENDPOINT_DIR_MASK) {
                                    read_endp = endp;
                                } else {
                                    write_endp = endp;
                                }
                            }

                            // Save best match
                            if (write_endp >= 0) {
                                protocol = altptr->bInterfaceProtocol;
                                best_altset = altset;
                                best_write_ep = write_endp;
                                best_read_ep = read_endp;
                            }
                        }

                        if (best_write_ep > -1) {
                            best_dev = dev;
                            best_conf = conf;
                            best_iface = iface;
                            break;
                        }
                    }
                    if (best_dev) break;
                }
                if (best_dev) break;
            }
            if (best_dev) break;
        }
        if (best_dev) break;
    }

    if (best_dev) {
        qDebug() << "Device:    " << best_dev << "\n"
                 << "Conf:      " << best_conf << "\n"
                 << "Iface:     " << best_iface << "\n"
                 << "Altset:    " << best_altset << "\n"
                 << "Write EP:  " << best_write_ep << "\n"
                 << "Read EP:   " << best_read_ep;

        m_dev = usb_open(best_dev);
        if (!m_dev) {
            qWarning() << "Fail to open device";
            return false;
        }

        if (usb_set_configuration(m_dev, best_dev->config[best_conf].bConfigurationValue) != 0) {
            qWarning() << "Fail to set configuration";
            return false;
        }

        int number = best_dev->config[best_conf].interface[best_iface].altsetting[best_altset].bInterfaceNumber;
        while(usb_claim_interface(m_dev, number)) {
            if (errno != EBUSY) {
                qWarning() << "Fail to clain interface";
                return false;
            }
        }

        number =  best_dev->config[best_conf].interface[best_iface].altsetting[best_altset].bAlternateSetting;
        while (usb_set_altinterface(m_dev, number)) {
            if (errno != EBUSY) {
                qWarning() << "Fail to set alternate interface";
                return false;
            }
        }

        m_interfaceID = best_iface;
        m_endPointWrite = best_dev->config[best_conf].interface[best_iface].altsetting[best_altset].endpoint[best_write_ep].bEndpointAddress;
        m_endPointRead = best_dev->config[best_conf].interface[best_iface].altsetting[best_altset].endpoint[best_read_ep].bEndpointAddress;

        return initialize();
    }
    qWarning() << "PRINTER NOT FOUND";
    return false;
}

void BematechDrv::close()
{
    if (m_interfaceID == -1)
        return;

    if (m_dev == 0)
        return;


    sendPostCommand();
    if (m_product_id == 3)
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
    if (m_product_id == 3)
        sleep(7); // wait for print reset
    return open();
}

bool BematechDrv::initialize()
{
    int ret = 0;

    if (m_product_id == 1) {
        ret = sendControlMessage(0x0000, 0xFFFF, QByteArray("", 0));
        if (ret < 0)
            return false;

        ret = sendControlMessage(0x01, 0x0007, QByteArray("", 0));
        if (ret < 0)
            return false;
    } else if (m_product_id == 3) {
        ret = sendControlMessage(0x22, 0x0003, QByteArray("", 0));
        if (ret < 0)
            return false;
    }

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
        int ret = usb_bulk_write(m_dev, m_endPointWrite, (char*)buffer.data(), size, 5000);
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
    int ret = usb_bulk_write(m_dev, m_endPointWrite, (char*)data.data(), data.size(), 5000);
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
