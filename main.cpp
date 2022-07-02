#include <QCoreApplication>
#include <QTimer>
#include <QCommandLineParser>
#include <QThread>
#include "tcouponsupdater.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setApplicationName("Coupons");
    QCoreApplication::setOrganizationName("OOO SA");
    QCoreApplication::setApplicationVersion("0.1");

    setlocale(LC_CTYPE, "");

    QCommandLineParser parser;
    parser.setApplicationDescription("Program for synchronizing data about coupons."
                                     "While the program is running, you can send a TEST command to test the current state or QUIT to shut down"
                                     "or START/STOP to start/stop send data");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption Config(QStringList() << "config", "Config file name", "ConfigFileNameValue", "Coupons.ini");
    parser.addOption(Config);

    parser.process(a);

    QString ConfigFileName = parser.value(Config);
    if (!parser.isSet(Config))
        ConfigFileName = a.applicationDirPath() +"/" + parser.value(Config);

    qDebug() << "Reading configuration from " +  ConfigFileName;

    //настраиваем таймер
    QTimer Timer;
    Timer.setInterval(0);       //таймер сработает так быстро как только это возможно
    Timer.setSingleShot(true);  //таймер сработает 1 раз

    TCouponsUpdater CouponsUpdater(ConfigFileName, &a);

    QObject::connect(&Timer, SIGNAL(timeout()), &CouponsUpdater, SLOT(onStart()));
    //Для завершения работы необходимоподать сигнал Finished
    QObject::connect(&CouponsUpdater, SIGNAL(Finished()), &a, SLOT(quit()));

    Timer.start();

    return a.exec();
}
