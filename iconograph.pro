QT       += core gui
QMAKE_CXXFLAGS += -std=c++14

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = iconograph
TEMPLATE = app

INCLUDEPATH += include/iconograph include
SOURCES += src/iconograph/*.cpp
HEADERS += include/iconograph/*.h
FORMS   += ui/*.ui
LIBS    += -Llib -lpsinc -lfreeimage
CONFIG  += rtti

DESTDIR = bin
OBJECTS_DIR = bin/.obj
MOC_DIR = bin/.moc
RCC_DIR = bin/.rcc
UI_DIR = bin/.ui
