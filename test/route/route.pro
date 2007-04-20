
TEMPLATE = app
TARGET = routetest
DESTDIR = .
LIBS += -L../.. -lsst_test -lsst
DEPENDPATH += . ../../lib ../lib
INCLUDEPATH += . ../../lib ../lib
QT = core network gui
POST_TARGETDEPS += ../../libsst.a ../../libsst_test.a
#CONFIG -= app_bundle

# Include variables filled in by the configure script
!include(../../top.pri) {
        error("top.pri not found - please run configure at top level.")
}

# Input sources
HEADERS += main.h route.h view.h
SOURCES += main.cc route.cc view.cc

