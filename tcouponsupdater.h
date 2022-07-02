#ifndef TCOUPONSUPDATER_H
#define TCOUPONSUPDATER_H

#include <QObject>
#include <QSettings>
#include <QtSql/QSqlDatabase>
#include <QDateTime>
#include <QQueue>
#include <QPair>
#include <QtNetwork/QNetworkAccessManager>
#include <QTimer>
#include <thttpquery.h>

class TCouponsUpdater : public QObject
{
    Q_OBJECT
public:
    typedef enum {CODE_OK, CODE_ERROR, CODE_INFORMATION} MSG_CODE;

private:
    typedef struct {
        QString Url;
        QString AZSCode;
        uint16_t MaxRecord;
        QDateTime LastUpdateLocal2Server;
        QDateTime CurrentUpdateLocal2Server;
        uint64_t LastUpdateServer2LocalID;
        uint64_t CurrentUpdateServer2LocalID;
        uint32_t TryCount;
    } THTTPServerInfo;

    typedef struct {
        QString Code;
        uint8_t Active;
        QDateTime DateTime;
    } TCouponsData;



   // typedef QQueue<TQueueCouponsData> TQueueCoupons;

public:
    explicit TCouponsUpdater(const QString &ConfigFileName, QObject *parent = nullptr);
    ~TCouponsUpdater();

private:
    QSettings *Config;  //настроки
    THTTPServerInfo HTTPServerInfo;  //информация о HTTP сервере и параметрах подключния к нему
    QSqlDatabase DB; //База данных мониторинга
    QSqlDatabase CouponsDB; //база данных купонов
    THTTPQuery *HTTPQuery; //запросы к HTTP серверу
    QTimer UpdateTimer; //основной таймер программы
    bool Sending = false; //флаг текущей передачи данных
    bool DebugMode = false;//если истина то выводим отладочную информацию в консоль
    bool ManyDataLocal = false;
    QByteArray XMLStr;
    QList<TCouponsData> SendedTalonCode;
    bool TransferMode = false; //Режим отправки данных Итина - отправляем локальные талоны, Лож - запрашиваем талоны с сервера
    QTime TimeOfRun = QTime::currentTime();

  //  TQueueCoupons QueueCouponsLocal; //очередь купонов для отправки
  //  TQueueCoupons QueueCouponsServer; //очередь принятых купонов

    void SendLogMsg(uint16_t Category, const QString &Msg);
    void SaveDateTimeLastUpdate(); //сохраняет последние обработтынные ID
    void SendLocalTalons();//получает и отправляет список локальных изменившихся талонов на сервер
    void SendServerRequest();
   // void SendToHTTPServer(); //отправляет данные на сервер

public slots:
    void onStart(); //запуск работы
private slots:
    void onHTTPGetAnswer(const QByteArray &Answer); //получен ответ от сервеера
    void onStartGetData(); //запуск отправки данны[
    void onHTTPError(); //ошибка передачи данных на сервер
    void onSendLogMsg(uint16_t Category, const QString &Msg); //записать сообщение в лог

signals:
    void Finished(); //завершение работы
};

#endif // TCOUPONSUPDATER_H
