/**
   @author Kenta Suzuki
*/

#include "rqt_bag_player/mainwindow.h"

#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <rosgraph_msgs/Clock.h>

#include <QAction>
#include <QBoxLayout>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QProcess>
#include <QSlider>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTimer>
#include <QToolBar>

#include <vector>

namespace rqt_bag_player {

class PlayerConfigDialog : public QDialog
{
public:
    PlayerConfigDialog(QWidget* parent = nullptr);

    void setLoopChecked(const bool& checked) { loopCheck->setChecked(checked); }
    bool isLoopChecked() const { return loopCheck->isChecked(); }
    void setClockChecked(const bool& checked) { clockCheck->setChecked(checked); }
    bool isClockChecked() const { return clockCheck->isChecked(); }
    void setRate(const double& rate) { rateSpin->setValue(rate); }
    double rate() const { return rateSpin->value(); }

private:

    QCheckBox* loopCheck;
    QCheckBox* clockCheck;
    QDoubleSpinBox* rateSpin;
    QDialogButtonBox* buttonBox;
};

class MainWindow::Impl
{
public:
    MainWindow* self;

    Impl(MainWindow* self);

    void open();
    void save();
    void record(const bool& checked);
    void clickPlay();
    void clickResume();
    void clickStop();
    void play();
    void stop();
    void config();

    void checkRecord(const bool& checked);
    void checkPlay(const bool& checked);
    void loadFile(const QString& fileName);
    void saveFile(const QString& fileName);

    void on_timer_timeout();
    void on_timeSpin_valueChanged(double value);
    void on_timeSlider_valueChanged(int value);
    void on_playTree_customContextMenuRequested(const QPoint& pos);
    void on_recordTree_customContextMenuRequested(const QPoint& pos);

    void createActions();
    void createToolBars();

    void clockCallback(const rosgraph_msgs::ClockPtr& msg);

    QAction* openAct;
    QAction* saveAct;
    QAction* recordAct;
    QAction* playAct;
    QAction* resumeAct;
    QAction* stopAct;
    QAction* configAct;
    QAction* checkPlayAct;
    QAction* uncheckPlayAct;
    QAction* checkRecordAct;
    QAction* uncheckRecordAct;

    QTimer* timer;
    QTreeWidget* playTree;
    QTreeWidget* recordTree;
    QDoubleSpinBox* beginTimeSpin;
    QDoubleSpinBox* endTimeSpin;
    QDoubleSpinBox* timeSpin;
    QSlider* timeSlider;
    QString recordNode;
    QString playNode;
    QString filePath;

    ros::NodeHandle n;
    ros::Subscriber clock_sub;
    ros::Time begin_time;
    ros::Time end_time;

    bool is_recording;
    bool is_playing;
    bool is_loop_checked;
    bool is_clock_checked;
    double rate;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    impl = new Impl(this);
}

MainWindow::Impl::Impl(MainWindow* self)
    : self(self)
    , is_recording(false)
    , is_playing(false)
    , is_loop_checked(false)
    , is_clock_checked(true)
    , rate(1.0)
{
    QWidget* widget = new QWidget;
    self->setCentralWidget(widget);

    createActions();
    createToolBars();

    self->setWindowTitle("Bag Player");

    clock_sub = n.subscribe("clock", 1000, &Impl::clockCallback, this);

    recordNode.clear();
    playNode.clear();
    filePath.clear();

    timer = new QTimer(self);
    timer->start(0.01);
    self->connect(timer, &QTimer::timeout, [&](){ on_timer_timeout(); });

    playTree = new QTreeWidget;
    playTree->setHeaderLabels(QStringList() << "Play topics");
    playTree->setContextMenuPolicy(Qt::CustomContextMenu);
    self->connect(playTree, &QTreeWidget::customContextMenuRequested,
        [&](QPoint pos){ on_playTree_customContextMenuRequested(pos); });

    recordTree = new QTreeWidget;
    recordTree->setHeaderLabels(QStringList() << "Record topics");
    recordTree->setContextMenuPolicy(Qt::CustomContextMenu);
    self->connect(recordTree, &QTreeWidget::customContextMenuRequested,
        [&](QPoint pos){ on_recordTree_customContextMenuRequested(pos); });

    auto layout = new QHBoxLayout;
    layout->addWidget(playTree);
    layout->addWidget(recordTree);
    widget->setLayout(layout);
}

MainWindow::~MainWindow()
{
    delete impl;
}

void MainWindow::Impl::open()
{
    if(is_playing) {
        stop();
    }

    timer->stop();

    static QString dir = "/home";
    QString fileName = QFileDialog::getOpenFileName(self, "Open File",
        dir,
        "Bag Files (*.bag);;All Files (*)");

    if(fileName.isEmpty()) {
        return;
    } else {
        QFileInfo info(fileName);
        dir = info.absolutePath();
        loadFile(fileName);
    }

    timer->start(0.01);
}

void MainWindow::Impl::save()
{
    if(is_playing) {
        stop();
    }

    timer->stop();

    static QString dir = "/home";
    QString fileName = QFileDialog::getSaveFileName(self, "Save File",
        dir,
        "Bag Files (*.bag);;All Files (*)");

    if(fileName.isEmpty()) {
        return;
    } else {
        QFileInfo info(fileName);
        dir = info.absolutePath();
        saveFile(fileName);
    }

    timer->start(0.01);
}

void MainWindow::Impl::record(const bool& checked)
{
    int count = recordTree->topLevelItemCount();
    if(count == 0) {
        recordAct->setChecked(false);
    }

    if(checked) {
        if(count > 0) {
            QStringList arguments;
            arguments << "record";
            for(int i = 0; i < count; ++i) {
                QTreeWidgetItem* item = recordTree->topLevelItem(i);
                if(item->checkState(0) == Qt::Checked) {
                    arguments << item->text(0);
                }
            }

            recordNode = QString("record_%1").arg(ros::Time::now().toNSec());

            arguments << QString("__name:=%1").arg(recordNode);
            QProcess::startDetached("rosbag", arguments);
            is_recording = true;
        } else {
            is_recording = false;
        }
    } else {
        if(is_recording) {
            QStringList arguments;
            arguments << "kill" << QString("/%1").arg(recordNode);
            QProcess::startDetached("rosnode", arguments);
        }
        is_recording = false;
    }
}

void MainWindow::Impl::clickPlay()
{
    timeSpin->setValue(0.0);
    play();
}

void MainWindow::Impl::clickResume()
{
    if(is_playing) {
        stop();
    } else {
        play();
    }
}

void MainWindow::Impl::clickStop()
{
    if(is_recording) {
        recordAct->setChecked(false);
    }

    stop();
}

void MainWindow::Impl::play()
{
    if(!filePath.isEmpty()) {
        QStringList arguments;
        arguments << "play" << filePath;
        arguments << "-q";

        if(is_clock_checked) {
            arguments << "--clock";
        }

        arguments << "-r" << QString("%1").arg(rate);

        double start_time = timeSpin->value();
        arguments << "-s" << QString("%1").arg(start_time);

        if(is_loop_checked) {
            arguments << "-l";
        }

        arguments << "--topics";
        for(int i = 0; i < playTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = playTree->topLevelItem(i);
            if(item->checkState(0) == Qt::Checked) {
                arguments << item->text(0);
            }
        }

        playNode = QString("play_%1").arg(ros::Time::now().toNSec());

        arguments << QString("__name:=%1").arg(playNode);
        QProcess::startDetached("rosbag", arguments);
        is_playing = true;
    } else {
        is_playing = false;
    }
}

void MainWindow::Impl::stop()
{
    if(is_playing) {
        QStringList arguments;
        arguments << "kill" << QString("/%1").arg(playNode);
        QProcess::startDetached("rosnode", arguments);
        is_playing = false;
    }
}

void MainWindow::Impl::config()
{
    PlayerConfigDialog dialog(self);
    dialog.setLoopChecked(is_loop_checked);
    dialog.setClockChecked(is_clock_checked);
    dialog.setRate(rate);

    if(dialog.exec()) {
        is_loop_checked = dialog.isLoopChecked();
        is_clock_checked = dialog.isClockChecked();
        rate = dialog.rate();
    }
}

void MainWindow::Impl::checkRecord(const bool& checked)
{
    for(int i = 0; i < recordTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = recordTree->topLevelItem(i);
        item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    }
}

void MainWindow::Impl::checkPlay(const bool& checked)
{
    for(int i = 0; i < playTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = playTree->topLevelItem(i);
        item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    }
}

void MainWindow::Impl::loadFile(const QString& fileName)
{
    filePath = fileName;

    while(playTree->topLevelItemCount() > 0) {
        playTree->takeTopLevelItem(0);
    }

    rosbag::Bag bag(fileName.toStdString().c_str());
    rosbag::View view(bag);

    begin_time = view.getBeginTime();
    end_time = view.getEndTime();
    double duration = (end_time - begin_time).toSec();
    beginTimeSpin->setValue(0.0);
    endTimeSpin->setValue(duration);

    std::vector<const rosbag::ConnectionInfo*> connections = view.getConnections();
    for(auto& info : connections) {
        QString topicName = info->topic.c_str();
        QTreeWidgetItem* item = new QTreeWidgetItem(playTree);
        item->setText(0, topicName);
        item->setCheckState(0, Qt::Checked);
    }
}

void MainWindow::Impl::saveFile(const QString& fileName)
{
    int count = playTree->topLevelItemCount();
    if(count > 0) {
        QStringList arguments;
        arguments << "filter" << filePath << fileName;
        QString option = "topic == ";
        for(int i = 0; i < count; ++i) {
            QTreeWidgetItem* item = playTree->topLevelItem(i);
            if(item->checkState(0) == Qt::Checked) {
                option += QString("'%1' or ").arg(item->text(0));
            }
        }
        option.chop(4);
        arguments << option;
        QProcess::startDetached("rosbag", arguments);
    }
}

void MainWindow::Impl::on_timer_timeout()
{
    static int numTopics = 0;

    ros::master::V_TopicInfo topics;
    if(ros::master::getTopics(topics)) {
        if(topics.size() != numTopics) {
            numTopics = topics.size();

            while(recordTree->topLevelItemCount() > 0) {
                recordTree->takeTopLevelItem(0);
            }

            for(size_t i = 0; i < topics.size(); ++i) {
                ros::master::TopicInfo info = topics[i];
                QString name = info.name.c_str();
                QString dataType = info.datatype.c_str();
                QTreeWidgetItem* item = new QTreeWidgetItem(recordTree);
                item->setText(0, name);
                item->setCheckState(0, Qt::Checked);
            }
        }
    }
}

void MainWindow::Impl::on_timeSpin_valueChanged(double value)
{
    int min = timeSlider->minimum();
    int max = timeSlider->maximum();
    double duration = endTimeSpin->value() - beginTimeSpin->value();
    double rate = value / duration;

    timeSlider->blockSignals(true);
    timeSlider->setValue((max - min) * rate);
    timeSlider->blockSignals(false);
}

void MainWindow::Impl::on_timeSlider_valueChanged(int value)
{
    int min = timeSlider->minimum();
    int max = timeSlider->maximum();
    double rate = (double)value / (double)(max - min);
    double duration = endTimeSpin->value() - beginTimeSpin->value();

    timeSpin->blockSignals(true);
    timeSpin->setValue(duration * rate);
    timeSpin->blockSignals(false);
}

void MainWindow::Impl::on_playTree_customContextMenuRequested(const QPoint& pos)
{
    QMenu menu(self);
    menu.addAction(checkPlayAct);
    menu.addAction(uncheckPlayAct);
    menu.exec(playTree->mapToGlobal(pos));

}

void MainWindow::Impl::on_recordTree_customContextMenuRequested(const QPoint& pos)
{
    QMenu menu(self);
    menu.addAction(checkRecordAct);
    menu.addAction(uncheckRecordAct);
    menu.exec(recordTree->mapToGlobal(pos));
}

void MainWindow::Impl::createActions()
{
    const QIcon openIcon = QIcon::fromTheme("document-open");
    openAct = new QAction(openIcon, "&Open...", self);
    openAct->setShortcuts(QKeySequence::Open);
    openAct->setStatusTip("Open an existing file");
    self->connect(openAct, &QAction::triggered, [&](){ open(); });

    const QIcon saveIcon = QIcon::fromTheme("document-save");
    saveAct = new QAction(saveIcon, "&Save", self);
    saveAct->setShortcuts(QKeySequence::Save);
    saveAct->setStatusTip("Save the bag to disk");
    self->connect(saveAct, &QAction::triggered, [&](){ save(); });

    const QIcon recordIcon = QIcon::fromTheme("media-record");
    recordAct = new QAction(recordIcon, "&Record", self);
    recordAct->setStatusTip("Record topics");
    recordAct->setCheckable(true);
    self->connect(recordAct, &QAction::toggled, [&](bool checked){ record(checked); });

    const QIcon playIcon = QIcon::fromTheme("media-playback-start");
    playAct = new QAction(playIcon, "&Play", self);
    playAct->setStatusTip("Play topics");
    self->connect(playAct, &QAction::triggered, [&](){ clickPlay(); });

    const QIcon resumeIcon = QIcon::fromTheme("media-playback-pause");
    resumeAct = new QAction(resumeIcon, "&Pause", self);
    resumeAct->setStatusTip("Pause topics");
    self->connect(resumeAct, &QAction::triggered, [&](){ clickResume(); });

    const QIcon stopIcon = QIcon::fromTheme("media-playback-stop");
    stopAct = new QAction(stopIcon, "&Stop", self);
    stopAct->setStatusTip("Stop topics");
    self->connect(stopAct, &QAction::triggered, [&](){ clickStop(); });

    const QIcon configIcon = QIcon::fromTheme("preferences-system");
    configAct = new QAction(configIcon, "&Config", self);
    configAct->setStatusTip("Show the config dialog");
    self->connect(configAct, &QAction::triggered, [&](){ config(); });

    checkPlayAct = new QAction("&Check All", self);
    checkPlayAct->setStatusTip("Check all play topics");
    self->connect(checkPlayAct, &QAction::triggered, [&](){ checkPlay(true); });

    uncheckPlayAct = new QAction("&Uncheck All", self);
    uncheckPlayAct->setStatusTip("Uncheck all play topics");
    self->connect(uncheckPlayAct, &QAction::triggered, [&](){ checkPlay(false); });

    checkRecordAct = new QAction("&Check All", self);
    checkRecordAct->setStatusTip("Check all record topics");
    self->connect(checkRecordAct, &QAction::triggered, [&](){ checkRecord(true); });

    uncheckRecordAct = new QAction("&Uncheck All", self);
    uncheckRecordAct->setStatusTip("Uncheck all record topics");
    self->connect(uncheckRecordAct, &QAction::triggered, [&](){ checkRecord(false); });
}

void MainWindow::Impl::createToolBars()
{
    QToolBar* playerToolBar = self->addToolBar("Bag Player");
    playerToolBar->addAction(openAct);
    playerToolBar->addAction(saveAct);
    playerToolBar->addSeparator();
    playerToolBar->addAction(recordAct);
    playerToolBar->addAction(playAct);
    playerToolBar->addAction(resumeAct);
    playerToolBar->addAction(stopAct);
    playerToolBar->addAction(configAct);

    beginTimeSpin = new QDoubleSpinBox;
    beginTimeSpin->setRange(0.0, 9999.0);
    beginTimeSpin->setEnabled(false);

    endTimeSpin = new QDoubleSpinBox;
    endTimeSpin->setRange(0.0, 9999.0);
    endTimeSpin->setEnabled(false);
    
    timeSpin = new QDoubleSpinBox;
    timeSpin->setRange(0.0, 9999.0);
    self->connect(timeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        [=](double value){ on_timeSpin_valueChanged(value); });

    timeSlider = new QSlider(Qt::Horizontal);
    timeSlider->setRange(0.0, 100.0);
    self->connect(timeSlider, QOverload<int>::of(&QSlider::valueChanged),
        [=](int value){ on_timeSlider_valueChanged(value); });

    playerToolBar->addWidget(beginTimeSpin);
    playerToolBar->addWidget(timeSlider);
    playerToolBar->addWidget(timeSpin);
    playerToolBar->addWidget(endTimeSpin);
}

void MainWindow::Impl::clockCallback(const rosgraph_msgs::ClockPtr& msg)
{
    ros::Time clock = msg->clock;
    double time = (clock - begin_time).toSec();
    timeSpin->setValue(time);
}

PlayerConfigDialog::PlayerConfigDialog(QWidget* parent)
    : QDialog(parent)
{
    loopCheck = new QCheckBox;
    loopCheck->setText("Loop");

    clockCheck = new QCheckBox;
    clockCheck->setText("Clock");

    rateSpin = new QDoubleSpinBox;

    auto gridLayout = new QGridLayout;
    gridLayout->addWidget(new QLabel("Rate"), 0, 0);
    gridLayout->addWidget(rateSpin, 0, 1);
    gridLayout->addWidget(loopCheck, 1, 0);
    gridLayout->addWidget(clockCheck, 1, 1);

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                     | QDialogButtonBox::Cancel);

    connect(buttonBox, &QDialogButtonBox::accepted, [&](){ accept(); });
    connect(buttonBox, &QDialogButtonBox::rejected, [&](){ reject(); });

    auto mainLayout = new QVBoxLayout;
    mainLayout->addLayout(gridLayout);
    mainLayout->addWidget(buttonBox);
    mainLayout->addStretch();

    setLayout(mainLayout);
    setWindowTitle("Player Config");
}

}
