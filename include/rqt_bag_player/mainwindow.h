/**
   @author Kenta Suzuki
*/

#ifndef rqt_bag_player__mainwindow_H
#define rqt_bag_player__mainwindow_H

#include <QMainWindow>

namespace rqt_bag_player {

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    class Impl;
    Impl* impl;
};

}

#endif // rqt_bag_player__mainwindow_H
