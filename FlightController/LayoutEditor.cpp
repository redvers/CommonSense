#include <QLabel>
#include <QMessageBox>
#include <QComboBox>
#include <QFileDialog>
#include <QTextStream>

#include "settings.h"
#include "LayoutEditor.h"
#include "ui_LayoutEditor.h"
#include "DeviceInterface.h"
#include "singleton.h"
#include "ScancodeList.h"

LayoutEditor::LayoutEditor(DeviceConfig *config, QWidget *parent) :
    QFrame(parent),
    ui(new Ui::LayoutEditor),
    grid(new QGridLayout()),
    currentLayer(0)
{
    ui->setupUi(this);
    deviceConfig = config;
    initDisplay();
}

void LayoutEditor::show(void)
{
    if (deviceConfig->bValid)
    {
        qInfo() << "Loading layer editor, please wait..";
        sizeDisplay(deviceConfig->numRows, deviceConfig->numCols);
        connect(ui->importButton, SIGNAL(clicked()), this, SLOT(importLayout()));
        connect(ui->exportButton, SIGNAL(clicked()), this, SLOT(exportLayout()));
        connect(ui->applyButton, SIGNAL(clicked()), this, SLOT(applyLayout()));
        connect(ui->revertButton, SIGNAL(clicked()), this, SLOT(resetLayout()));
        connect(ui->switchButton, SIGNAL(clicked()), this, SLOT(switchLayer()));
        QWidget::show();
    } else
        QMessageBox::critical(this, "Error", "Matrix not configured - cannot edit layout");
}

LayoutEditor::~LayoutEditor()
{
    delete ui;
}

void LayoutEditor::initDisplay(void)
{
    ui->Dashboard->setLayout(grid);
    for (uint8_t i = 1; i<=ABSOLUTE_MAX_COLS; i++)
    {
        grid->addWidget(new QLabel(QString("%1").arg(i)), 0, i, 1, 1, Qt::AlignRight);
        if (i <= ABSOLUTE_MAX_ROWS) {
            grid->addWidget(new QLabel(QString("%1").arg(i)), i, 0, 1, 1, Qt::AlignRight);
        }
    }
    // Do not populate lists here - makes startup SLOW
    for (uint8_t i = 0; i<ABSOLUTE_MAX_ROWS; i++)
    {
        for (uint8_t j = 0; j<ABSOLUTE_MAX_COLS; j++)
        {
            display[i][j] = NULL;
        }
    }
}

void LayoutEditor::sizeDisplay(uint8_t rows, uint8_t cols)
{
    ui->layerCombo->clear();
    for(uint8_t i = 0; i < deviceConfig->numLayers; i++)
    {
        ui->layerCombo->addItem(QString("Layer %1").arg(i));
    }
    ui->layerCombo->setCurrentIndex(currentLayer);
    for (uint8_t i = 1; i<=ABSOLUTE_MAX_COLS; i++)
    {
        grid->itemAtPosition(0, i)->widget()->setVisible(i<=cols);
        if (i <= ABSOLUTE_MAX_ROWS) {
            grid->itemAtPosition(i, 0)->widget()->setVisible(i<=rows);
        }
    }
    ScancodeList scancodes;
    for (uint8_t i = 0; i<ABSOLUTE_MAX_ROWS; i++)
    {
        for (uint8_t j = 0; j<ABSOLUTE_MAX_COLS; j++)
        {
            if (!display[i][j])
            {
                QComboBox *l = new QComboBox();
                l->addItems(scancodes.list);
                l->setStyleSheet("background-color: #dddddd;");
                display[i][j] = l;
                grid->addWidget(l, i+1, j+1, 1, 1);
                if (j == 0)
                {
                    qInfo() << ".";
                }
            }
            display[i][j]->setVisible((i < rows) && (j < cols));
        }
    }
    resetLayout();
    adjustSize();
}

void LayoutEditor::importLayout()
{
    QSettings settings;
    QFileDialog fd(this, "Choose one file to import from");
    fd.setDirectory(settings.value(LAYOUTS_DIR_KEY).toString());
    fd.setNameFilter(tr("Layout files(*.l)"));
    fd.setDefaultSuffix(QString("l"));
    fd.setFileMode(QFileDialog::ExistingFile);
    if (fd.exec())
    {
        QStringList fns = fd.selectedFiles();
        QFile f(fns.at(0));
        f.open(QIODevice::ReadOnly);
        QTextStream ds(&f);
        for (uint8_t l = 0; l<deviceConfig->numLayers; l++)
        {
            for (uint8_t i = 0; i<deviceConfig->numRows; i++)
            {
                for (uint8_t j = 0; j<deviceConfig->numCols; j++)
                {
                    qint32 buf;
                    ds >> buf;
                    deviceConfig->layouts[l][i][j] = (uint8_t)buf;
                }
            }
        }
        f.close();
        setDisplay();
        qInfo() << "Imported layout from" << fns.at(0);
        settings.setValue(LAYOUTS_DIR_KEY, QFileInfo(fns.at(0)).canonicalPath());
    }
}

void LayoutEditor::exportLayout()
{
    QSettings settings;
    QFileDialog fd(this, "Choose one file to export to");
    fd.setDirectory(settings.value(LAYOUTS_DIR_KEY).toString());
    fd.setNameFilter(tr("layout files(*.l)"));
    fd.setDefaultSuffix(QString("l"));
    fd.setAcceptMode(QFileDialog::AcceptSave);
    if (fd.exec())
    {
        applyLayout();
        QStringList fns = fd.selectedFiles();
        QFile f(fns.at(0));
        f.open(QIODevice::WriteOnly);
        QTextStream ts (&f);
        ts.setIntegerBase(16);
        ts.setFieldAlignment(QTextStream::AlignRight);
        ts.setPadChar('0');
        for (uint8_t l = 0; l<deviceConfig->numLayers; l++)
        {
            for (uint8_t i = 0; i<deviceConfig->numRows; i++)
            {
                QByteArray buf;
                for (uint8_t j = 0; j<deviceConfig->numCols; j++)
                {
                    ts << qSetFieldWidth(1) << (j ? ' ' : '\n');
                    ts << "0x";
                    ts.setFieldWidth(2);
                    ts << deviceConfig->layouts[l][i][j];

                }
            }
            ts << qSetFieldWidth(1) << '\n';
        }
        ts << qSetFieldWidth(1) << '\n';
        f.close();
        qInfo() << "Exported layout to" << fns.at(0);
        settings.setValue(LAYOUTS_DIR_KEY, QFileInfo(fns.at(0)).canonicalPath());
    }
}

void LayoutEditor::applyLayout()
{
    for (uint8_t i = 0; i<deviceConfig->numRows; i++)
    {
        for (uint8_t j = 0; j<deviceConfig->numCols; j++)
        {
            deviceConfig->layouts[currentLayer][i][j] = display[i][j]->currentIndex();
        }
    }
}

void LayoutEditor::setDisplay()
{
    for (uint8_t i = 0; i<deviceConfig->numRows; i++)
    {
        for (uint8_t j = 0; j<deviceConfig->numCols; j++)
        {
            display[i][j]->setCurrentIndex(deviceConfig->layouts[currentLayer][i][j]);
        }
    }
}

void LayoutEditor::resetLayout()
{
    setDisplay();
    qInfo() << "Loaded layout from device";
}

void LayoutEditor::switchLayer()
{
    currentLayer = ui->layerCombo->currentIndex();
    setDisplay();
}

void LayoutEditor::receiveScancode(uint8_t row, uint8_t col, DeviceInterface::KeyStatus status)
{
    if (!display[row][col])
        return;
    if (status == DeviceInterface::KeyPressed)
    {
        display[row][col]->setStyleSheet("background-color: #ffffff;");
    }
    else
    {
        display[row][col]->setStyleSheet("background-color: #dddddd;");
    }
}

void LayoutEditor::on_closeButton_clicked()
{
    this->close();
}
