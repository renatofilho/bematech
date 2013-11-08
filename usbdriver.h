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


#ifndef USBDRIVER_H
#define USBDRIVER_H

#include <usb.h>
#include <QByteArray>
#include <QHash>

class BematechDrv
{
public:
    BematechDrv(int product_id);
    ~BematechDrv();
    bool isOpen() const;
    bool open();
    void close();
    bool reset();
    bool sendCommand(const QByteArray &data);
    void setProductId(int id) { m_product_id = id; }
    int productId() { return m_product_id; }

    int getCommandSet();
    QHash<QString, QByteArray> productInfo();


private:
    struct usb_dev_handle *m_dev;
    int m_product_id;
    int m_interfaceID;
    int m_endPointWrite;
    int m_endPointRead;

    bool initialize();
    bool sendPreCommand();
    bool sendPostCommand();
    QByteArray readData(int endPoint);

    int sendControlMessage(unsigned int bRequest,
                           unsigned int wValue,
                           QByteArray data);
    QByteArray sendInterruptlMessage(int endPoint);
};

#endif
