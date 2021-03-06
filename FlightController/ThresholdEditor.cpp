#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>

#include "ThresholdEditor.h"
#include "ui_ThresholdEditor.h"
#include "DeviceInterface.h"
#include "singleton.h"
#include "../c2/c2_protocol.h"

ThresholdEditor::ThresholdEditor(DeviceConfig *config, QWidget *parent) :
    QFrame(parent),
    ui(new Ui::ThresholdEditor),
    grid(new QGridLayout())
{
    ui->setupUi(this);
    deviceConfig = config;
    initDisplay();
    connect(ui->applyButton, SIGNAL(clicked()), this, SLOT(applyThresholds()));
    connect(ui->revertButton, SIGNAL(clicked()), this, SLOT(resetThresholds()));
    connect(ui->loPlusButton, SIGNAL(clicked()), this, SLOT(updateLows()));
    connect(ui->hiPlusButton, SIGNAL(clicked()), this, SLOT(updateHighs()));
}

void ThresholdEditor::show(void)
{
    if (deviceConfig->bValid)
    {
        updateDisplaySize(deviceConfig->numRows, deviceConfig->numCols);
        resetThresholds();
        QWidget::show();
    }
    else
    {
        QMessageBox::critical(this, "Error", "Matrix not configured - cannot edit");
    }
}

ThresholdEditor::~ThresholdEditor()
{
    delete ui;
}


void ThresholdEditor::initDisplay()
{
    for (uint8_t i = 1; i<=ABSOLUTE_MAX_COLS; i++)
    {
        grid->addWidget(new QLabel(QString("%1").arg(i)), 0, i, 1, 1, Qt::AlignRight);
        if (i <= ABSOLUTE_MAX_ROWS) {
            grid->addWidget(new QLabel(QString("%1").arg(i)), i, 0, 1, 1, Qt::AlignRight);
        }
    }
    for (uint8_t i = 0; i<ABSOLUTE_MAX_ROWS; i++)
    {
        for (uint8_t j = 0; j<ABSOLUTE_MAX_COLS; j++)
        {
            QWidget *l = new QWidget();
            l->setStyleSheet("background-color: #cccccc;");
            QBoxLayout *ll = new QBoxLayout(QBoxLayout::BottomToTop);
            l->setLayout(ll);
            display[i][j] = l;
            lo[i][j] = new QSpinBox();
            lo[i][j]->setStyleSheet("background-color: #ffffff;");
            lo[i][j]->setToolTip("Low deadband boundary");
            ll->addWidget(lo[i][j]);
            skip[i][j] = new QCheckBox();
            skip[i][j]->setToolTip("Skip update");
            ll->addWidget(skip[i][j]);
            hi[i][j] = new QSpinBox();
            hi[i][j]->setStyleSheet("background-color: #ffffff;");
            hi[i][j]->setToolTip("High deadband boundary");
            ll->addWidget(hi[i][j]);
            grid->addWidget(l, i+1, j+1, 1, 1);
        }
    }
    ui->Dashboard->setLayout(grid);
}

void ThresholdEditor::updateDisplaySize(uint8_t rows, uint8_t cols)
{
    for (uint8_t i = 1; i<=ABSOLUTE_MAX_COLS; i++)
    {
        if (i <= ABSOLUTE_MAX_ROWS)
            grid->itemAtPosition(i, 0)->widget()->setVisible(i<=rows);
        grid->itemAtPosition(0, i)->widget()->setVisible(i<=cols);
    }
    for (uint8_t i = 0; i<ABSOLUTE_MAX_ROWS; i++)
    {
        for (uint8_t j = 0; j<ABSOLUTE_MAX_COLS; j++)
        {
            display[i][j]->setVisible((i < rows) & (j < cols));
        }
    }
    adjustSize();
}

void ThresholdEditor::updateLows()
{
    for (uint8_t i = 0; i < deviceConfig->numRows; i++)
    {
        for (uint8_t j = 0; j < deviceConfig->numCols; j++)
        {
            if (!skip[i][j]->isChecked())
            {
                lo[i][j]->setValue(lo[i][j]->value() + ui->loStepSpinbox->value());
            }
        }
    }
}

void ThresholdEditor::updateHighs()
{
    for (uint8_t i = 0; i < deviceConfig->numRows; i++)
    {
        for (uint8_t j = 0; j < deviceConfig->numCols; j++)
        {
            if (!skip[i][j]->isChecked())
            {
                hi[i][j]->setValue(hi[i][j]->value() + ui->hiStepSpinbox->value());
            }
        }
    }
}

void ThresholdEditor::applyThresholds()
{
    deviceConfig->guardLo = ui->loGuardSpinbox->value();
    deviceConfig->guardHi = ui->hiGuardSpinbox->value();
    for (uint8_t i = 0; i < deviceConfig->numRows; i++)
    {
        for (uint8_t j = 0; j < deviceConfig->numCols; j++)
        {
            deviceConfig->deadBandLo[i][j] = lo[i][j]->value();
            deviceConfig->deadBandHi[i][j] = hi[i][j]->value();
        }
    }
}

void ThresholdEditor::resetThresholds()
{
    ui->loGuardSpinbox->setValue(deviceConfig->guardLo);
    ui->hiGuardSpinbox->setValue(deviceConfig->guardHi);
    for (uint8_t i = 0; i < deviceConfig->numRows; i++)
    {
        for (uint8_t j = 0; j < deviceConfig->numCols; j++)
        {
            lo[i][j]->setValue(deviceConfig->deadBandLo[i][j]);
            hi[i][j]->setValue(deviceConfig->deadBandHi[i][j]);
            skip[i][j]->setChecked(deviceConfig->deadBandLo[i][j] > deviceConfig->deadBandHi[i][j]);
        }
    }
    qInfo() << "Loaded threshold map";
}

void ThresholdEditor::receiveScancode(uint8_t row, uint8_t col, DeviceInterface::KeyStatus status)
{
    if (status == DeviceInterface::KeyPressed)
    {
        display[row][col]->setStyleSheet("background-color: #ffffff;");
    }
    else
    {
        display[row][col]->setStyleSheet("background-color: #cccccc;");
    }
}

void ThresholdEditor::on_closeButton_clicked()
{
    this->close();
}
