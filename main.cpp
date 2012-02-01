#include "usbdriver.h"

#include <QDebug>
#include <iostream>
using namespace std;

int getOption()
{
    qDebug() << "COMMANDOS\n"
             << "\t[1] Open\n"
             << "\t[2] Close\n"
             << "\t[3] Print\n"
             << "\t[4] Get command set\n"
             << "\t[5] Get Product Info\n"
             << "\t[0] Exit\n"
             << "DIGITE:";
    int opt = 0;
    cin >> opt;
    return opt;
}

void executeOption(BematechDrv &drv, int opt)
{
    switch(opt)
    {
    case 1:
        drv.open();
        break;
    case 2:
        drv.close();
        break;
    case 3:
        drv.sendCommand("\x1B\x74\x2 Renato\x0A\x1B\x50");
        break;
    case 4:
        qDebug() << "Command set: " << drv.getCommandSet();
        break;
    case 5:
        qDebug() << "Product Info:" << drv.productInfo();
    }
}

int main(int argc, char **argv)
{
    BematechDrv drv(3);
    int opt = -1;
    while(opt != 0) {
        opt = getOption();
        executeOption(drv, opt);
    }
    return 0;
}
