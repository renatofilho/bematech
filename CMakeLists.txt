PROJECT(usbtest)

cmake_minimum_required (VERSION 2.6)
find_package (Qt4 4.5.0 REQUIRED)
include (FindPkgConfig)
pkg_check_modules(LIBUSB REQUIRED libusb)

set (usbtest_SRC
usbdriver.cpp
main.cpp)

ADD_EXECUTABLE (usbtest ${usbtest_SRC})
LINK_DIRECTORIES(${LIBUSB_LIBRARY_DIR})

TARGET_LINK_LIBRARIES (usbtest
                       ${LIBUSB_LDFLAGS}
                       ${QT_QTCORE_LIBRARY})

INCLUDE_DIRECTORIES (${main_SOURCE_DIR}
                     ${LIBUSB_INCLUDE_DIRS}
                     ${QT_INCLUDE_DIR}
                     ${QT_QTCORE_INCLUDE_DIR})

