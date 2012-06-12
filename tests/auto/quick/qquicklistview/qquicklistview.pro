CONFIG += testcase
testcase.timeout = 600 # this test is slow
TARGET = tst_qquicklistview
macx:CONFIG -= app_bundle

HEADERS += incrementalmodel.h
SOURCES += tst_qquicklistview.cpp \
           incrementalmodel.cpp

include (../../shared/util.pri)
include (../shared/util.pri)

TESTDATA = data/*

QT += core-private gui-private qml-private quick-private v8-private testlib
CONFIG += insignificant_test
