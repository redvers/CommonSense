/*
 *
 * Copyright (C) 2016 DMA <dma@ya.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
#include <QMessageBox>
#include "FlightController.h"
#include "ui_FlightController.h"

#include "../c2/c2_protocol.h"
#include "../c2/nvram.h"
#include "singleton.h"
#include "MatrixMonitor.h"
#include "DeviceInterface.h"
#include "DeviceConfig.h"

FlightController::FlightController(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::FlightController)
{
    ui->setupUi(this);

    DeviceInterface &di = Singleton<DeviceInterface>::instance();

    matrixMonitor = new MatrixMonitor();
    layoutEditor = new LayoutEditor(di.config);
    connect(&di, SIGNAL(scancodeReceived(uint8_t, uint8_t, DeviceInterface::KeyStatus)),
            layoutEditor, SLOT(receiveScancode(uint8_t, uint8_t, DeviceInterface::KeyStatus)));

    thresholdEditor = new ThresholdEditor(di.config);
    connect(&di, SIGNAL(scancodeReceived(uint8_t, uint8_t, DeviceInterface::KeyStatus)),
            thresholdEditor, SLOT(receiveScancode(uint8_t, uint8_t, DeviceInterface::KeyStatus)));

    layerConditions = new LayerConditions(di.config);

    _delays = new Delays(di.config);

    _expHeader = new ExpansionHeader(di.config);

    // Must be last in chain to intercept all packets!
    loader = new FirmwareLoader();
    connect(loader, SIGNAL(switchMode(bool)), &di, SLOT(bootloaderMode(bool)));
    connect(loader, SIGNAL(sendPacket(Bootloader_packet_t *)), &di, SLOT(sendCommand(Bootloader_packet_t *)));
}

void FlightController::setup(void)
{
    DeviceInterface &di = Singleton<DeviceInterface>::instance();

    connect(ui->ClearButton, SIGNAL(clicked()), ui->LogViewport, SLOT(clearButtonClick()));
    connect(ui->CopyAllButton, SIGNAL(clicked()), ui->LogViewport, SLOT(copyAllButtonClick()));
    connect(ui->RedButton, SIGNAL(toggled(bool)), this, SLOT(redButtonToggle(bool)));

    connect(ui->actionFirmware_File, SIGNAL(triggered()), loader, SLOT(selectFile()));

    connect(ui->statusRequestButton, SIGNAL(clicked()), this, SLOT(statusRequestButtonClick()));

    connect(ui->MatrixMonitorButton, SIGNAL(clicked()), this, SLOT(showKeyMonitor()));
    connect(ui->action_Key_Monitor, SIGNAL(triggered()), this, SLOT(showKeyMonitor()));

    connect(ui->thresholdsButton, SIGNAL(clicked()), this, SLOT(editThresholdsClick()));
    connect(ui->action_Thresholds, SIGNAL(triggered()), this, SLOT(editThresholdsClick()));

    connect(ui->layerModsButton, SIGNAL(clicked()), this, SLOT(showLayerConditions()));
    connect(ui->action_Layer_mods, SIGNAL(triggered()), this, SLOT(showLayerConditions()));

    connect(ui->layoutButton, SIGNAL(clicked()), this, SLOT(editLayoutClick()));
    connect(ui->action_Layout, SIGNAL(triggered()), this, SLOT(editLayoutClick()));

    connect(ui->macrosButton, SIGNAL(clicked()), this, SLOT(editMacrosClick()));
    connect(ui->action_Macros, SIGNAL(triggered()), this, SLOT(editMacrosClick()));

    connect(ui->delaysButton, SIGNAL(clicked()), this, SLOT(editDelays()));
    connect(ui->action_Delays, SIGNAL(triggered()), this, SLOT(editDelays()));

    connect(ui->expButton, SIGNAL(clicked()), this, SLOT(editExpHeader()));
    connect(ui->action_Exp_Header, SIGNAL(triggered()), this, SLOT(editExpHeader()));

    connect(ui->BootloaderButton, SIGNAL(clicked()), loader, SLOT(start()));
    connect(ui->action_Update_Firmware, SIGNAL(triggered()), loader, SLOT(start()));

    connect(ui->action_Open, SIGNAL(triggered()), di.config, SLOT(fromFile()));
    connect(ui->action_Upload, SIGNAL(triggered()), di.config, SLOT(toDevice()));
    connect(ui->action_Download, SIGNAL(triggered()), di.config, SLOT(fromDevice()));
    connect(ui->action_Save, SIGNAL(triggered()), di.config, SLOT(toFile()));
    connect(ui->action_Commit, SIGNAL(triggered()), di.config, SLOT(commit()));
    connect(ui->action_Rollback, SIGNAL(triggered()), di.config, SLOT(rollback()));
    connect(this, SIGNAL(sendCommand(c2command, uint8_t)), &di, SLOT(sendCommand(c2command, uint8_t)));
    connect(&di, SIGNAL(deviceStatusNotification(DeviceInterface::DeviceStatus)), this, SLOT(deviceStatusNotification(DeviceInterface::DeviceStatus)));
    lockUI(true);
    di.start();
}

void FlightController::show(void)
{
    QMainWindow::show();
}

void FlightController::closeEvent (QCloseEvent *event)
{
    emit sendCommand(C2CMD_SET_MODE, C2DEVMODE_NORMAL);
    QApplication::quit();
    event->accept();
}

void FlightController::mainTabChanged(int idx)
{
    if (idx == 1) {
    }
}

FlightController::~FlightController()
{
    qInstallMessageHandler(*_oldLogger);
    delete ui;
}

LogViewer* FlightController::getLogViewport(void)
{
    return ui->LogViewport;
}

void FlightController::setOldLogger(QtMessageHandler *logger)
{
    _oldLogger = logger;
}

void FlightController::logToViewport(const QString &msg)
{
    ui->LogViewport->logMessage(msg);
}


void FlightController::showKeyMonitor(void)
{
    matrixMonitor->show();
}

void FlightController::redButtonToggle(bool state)
{
    emit sendCommand(C2CMD_EWO, state);
}

void FlightController::statusRequestButtonClick(void)
{
    emit sendCommand(C2CMD_GET_STATUS, 0);
}

void FlightController::deviceStatusNotification(DeviceInterface::DeviceStatus s)
{
    lockUI(true);
    switch (s)
    {
        case DeviceInterface::DeviceConnected:
            emit ui->action_Download->triggered();
            break;
        case DeviceInterface::DeviceDisconnected:
            break;
        case DeviceInterface::DeviceConfigChanged:
            emit sendCommand(C2CMD_SET_MODE, C2DEVMODE_SETUP);
            ui->action_Setup_mode->setChecked(true);
            layerConditions->init();
            _delays->init();
            _expHeader->init();
            lockUI(false);
            break;
        case DeviceInterface::BootloaderConnected:
            loader->load();
            break;
        default:
            qCritical() << "Unknown device status" << s << "!";
    }
}

void FlightController::lockUI(bool lock)
{
    ui->statusRequestButton->setDisabled(lock);
    ui->MatrixMonitorButton->setDisabled(lock);
    ui->thresholdsButton->setDisabled(lock);
    ui->layoutButton->setDisabled(lock);
    ui->layerModsButton->setDisabled(lock);
    ui->delaysButton->setDisabled(lock);
    ui->expButton->setDisabled(lock);
}

void FlightController::editLayoutClick(void)
{
    layoutEditor->show();
}

void FlightController::editMacrosClick(void)
{
    //macrosEditor->show();
}

void FlightController::editThresholdsClick(void)
{
    thresholdEditor->show();
}

void FlightController::showLayerConditions(void)
{
    layerConditions->show();
}

void FlightController::on_action_Setup_mode_triggered(bool bMode)
{
    emit sendCommand(C2CMD_SET_MODE, bMode ? C2DEVMODE_SETUP : C2DEVMODE_NORMAL);
}

void FlightController::editDelays()
{
    _delays->show();
}

void FlightController::editExpHeader()
{
    _expHeader->show();
}
