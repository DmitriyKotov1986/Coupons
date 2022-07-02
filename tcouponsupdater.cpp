#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QList>
#include <QVector>
#include "tcouponsupdater.h"
#include "thttpquery.h"

TCouponsUpdater::TCouponsUpdater(const QString &ConfigFileName, QObject *parent)
    : QObject(parent)
{

    Config = new QSettings(ConfigFileName, QSettings::IniFormat);

    Config->beginGroup("DATABASE");
    DB = QSqlDatabase::addDatabase(Config->value("Driver", "QODBC").toString(), "MainDB");
    DB.setDatabaseName(Config->value("DataBase", "SystemMonitorDB").toString());
    DB.setUserName(Config->value("UID", "SYSDBA").toString());
    DB.setPassword(Config->value("PWD", "MASTERKEY").toString());
    DB.setConnectOptions(Config->value("ConnectionOprions", "").toString());
    DB.setPort(Config->value("Port", "3051").toUInt());
    DB.setHostName(Config->value("Host", "localhost").toString());
    Config->endGroup();

    Config->beginGroup("COUPONSDATABASE");
    CouponsDB = QSqlDatabase::addDatabase(Config->value("Driver", "QODBC").toString(), "CouponsDB");
    CouponsDB.setDatabaseName(Config->value("DataBase", "CouponsDB").toString());
    CouponsDB.setUserName(Config->value("UID", "SYSDBA").toString());
    CouponsDB.setPassword(Config->value("PWD", "MASTERKEY").toString());
    CouponsDB.setConnectOptions(Config->value("ConnectionOprions", "").toString());
    CouponsDB.setPort(Config->value("Port", "3051").toUInt());
    CouponsDB.setHostName(Config->value("Host", "localhost").toString());
    Config->endGroup();

    Config->beginGroup("SYSTEM");
    UpdateTimer.setInterval(Config->value("Interval", "60000").toInt());
    UpdateTimer.setSingleShot(false);
    DebugMode = Config->value("DebugMode", "0").toBool();
    Config->endGroup();
    QObject::connect(&UpdateTimer, SIGNAL(timeout()), this, SLOT(onStartGetData()));

    Config->beginGroup("SERVER");
    HTTPServerInfo.AZSCode = Config->value("UID", "n/a").toString();
    //формируем URL
    HTTPServerInfo.Url = "http://" + Config->value("Host", "localhost").toString() + ":" + Config->value("Port", "80").toString() +
                  "/CGI/COUPONS&" + HTTPServerInfo.AZSCode +"&" + Config->value("PWD", "123456").toString();
    HTTPServerInfo.TryCount = Config->value("TryCount", "0").toUInt();
    HTTPServerInfo.LastUpdateLocal2Server = Config->value("LastUpdateLocal2Server", "2000-01-01 00:00:00.000").toDateTime();
    HTTPServerInfo.LastUpdateServer2LocalID = Config->value("LastUpdateServer2LocalID", 0).toULongLong();
    HTTPServerInfo.MaxRecord = Config->value("MaxRecord", "10").toUInt();
    Config->endGroup();

    HTTPQuery = new THTTPQuery(HTTPServerInfo.Url, this);
    QObject::connect(HTTPQuery, SIGNAL(GetAnswer(const QByteArray &)), this, SLOT(onHTTPGetAnswer(const QByteArray &)));
    QObject::connect(HTTPQuery, SIGNAL(SendLogMsg(uint16_t, const QString &)), this, SLOT(onSendLogMsg(uint16_t, const QString &)));
    QObject::connect(HTTPQuery, SIGNAL(ErrorOccurred()), this, SLOT(onHTTPError()));
}

TCouponsUpdater::~TCouponsUpdater()
{
    SaveDateTimeLastUpdate();
    CouponsDB.close();
    SendLogMsg(MSG_CODE::CODE_OK, "Finished");
    DB.close();

    if (DebugMode) qDebug() << "Total work time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";
}

void TCouponsUpdater::onStart()
{
    if (!DB.open()) {
        qCritical() << "FAIL. Cannot connect to database. Error: " << DB.lastError().text();
        exit(-1);
    };

    if (!CouponsDB.open()) {
        qCritical() << "FAIL. Cannot connect to database with coupons. Error: " << DB.lastError().text();
        exit(-2);
    };

    SendLogMsg(MSG_CODE::CODE_OK, "Start is succesfull");

    onStartGetData();

    UpdateTimer.start();
}

void TCouponsUpdater::SendLogMsg(uint16_t Category, const QString &Msg)
{
    QString Str(Msg);
    Str.replace(QRegularExpression("'"), "''");
    if (DebugMode) qDebug() << "LOG:" << Str;
    QSqlQuery QueryLog(DB);
    DB.transaction();
    QString QueryText = "INSERT INTO LOG (CATEGORY, SENDER, MSG) VALUES ( "
                        + QString::number(Category) + ", "
                        "\'Coupons\', "
                        "\'" + Str +"\'"
                        ")";

    if (!QueryLog.exec(QueryText)) {
       qCritical() << "FAIL Cannot execute query. Error: " << QueryLog.lastError().text() << " Query: "<< QueryLog.lastQuery();
       DB.rollback();
       exit(-1);
   }
   if (!DB.commit()) {
       qCritical() << "FAIL Cannot commit transation. Error: " << DB.lastError().text();
       DB.rollback();
       exit(-2);
   };
}

void TCouponsUpdater::SaveDateTimeLastUpdate()
{
    //if (DebugMode) qDebug() << "New LastUpdateLocal2Server: " << HTTPServerInfo.LastUpdateLocal2Server.toString("yyyy-MM-dd hh:mm:ss.zzz") <<
    //         "LastUpdateServer2LocalID:" << HTTPServerInfo.LastUpdateServer2LocalID;
    Config->beginGroup("SERVER");
    Config->setValue("LastUpdateLocal2Server", HTTPServerInfo.LastUpdateLocal2Server.toString("yyyy-MM-dd hh:mm:ss.zzz"));
    Config->setValue("LastUpdateServer2LocalID", HTTPServerInfo.LastUpdateServer2LocalID);
    Config->endGroup();
    Config->sync();
}

void TCouponsUpdater::SendLocalTalons()
{
    Sending = true;
    HTTPServerInfo.CurrentUpdateLocal2Server = HTTPServerInfo.LastUpdateLocal2Server;
    SendedTalonCode.clear();

    //формаируем XML документ
    XMLStr.clear();
    QXmlStreamWriter XMLWriter(&XMLStr);
    XMLWriter.setAutoFormatting(true);
    XMLWriter.writeStartDocument("1.0");
    XMLWriter.writeStartElement("Root");
    XMLWriter.writeTextElement("AZSCode", HTTPServerInfo.AZSCode);
    XMLWriter.writeTextElement("ClientVersion", QCoreApplication::applicationVersion());
    XMLWriter.writeTextElement("LastID", QString::number(HTTPServerInfo.LastUpdateServer2LocalID)); //время запроса
    XMLWriter.writeTextElement("MaxCoupons", 0/*QString::number(HTTPServerInfo.MaxRecord)*/); //время запроса
    if (DebugMode) {
        qDebug() << "Execute query DB time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";
    }

    QSqlQuery Query(CouponsDB);
    CouponsDB.transaction();
    Query.setForwardOnly(true);
    //получаем данные об изменившихся талонах
    //if (DebugMode)  qDebug() << HTTPServerInfo.LastUpdateLocal2Server.toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString QueryText = "SELECT FIRST " + QString::number(HTTPServerInfo.MaxRecord) + " \"Code\", \"Active\", \"UpdateDate\", \"CreateDate\", \"SENT\" "
                        "FROM \"Coupons\" "
                        "WHERE ( \"UpdateDate\" >= CAST('" + HTTPServerInfo.LastUpdateLocal2Server.toString("yyyy-MM-dd hh:mm:ss.zzz") + "' AS TIMESTAMP)) AND (\"SENT\" = 0) "
                        "ORDER BY \"UpdateDate\" ";
    if (!Query.exec(QueryText)) {
        CouponsDB.rollback();
        QString LastError = "Cannot execute query. Error: " + Query.lastError().text() + " Query: " + QueryText;
        qDebug() << LastError;
        exit(-1);
    }

    //формируем список отправленных талонов
    while (Query.next()) {
        XMLWriter.writeStartElement("Coupon");
        XMLWriter.writeTextElement("Code", Query.value("Code").toString());
        XMLWriter.writeTextElement("Active", Query.value("Active").toString());
        XMLWriter.writeTextElement("UpdateDateTime", Query.value("UpdateDate").toDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"));
        XMLWriter.writeEndElement();
        //сохраняем дата-время последней обновленной записи
        HTTPServerInfo.CurrentUpdateLocal2Server = Query.value("UpdateDate").toDateTime();
        //сохраняем список отправляемых талонов
        TCouponsData tmp;
        tmp.Code = Query.value("Code").toString();
        tmp.Active = Query.value("Active").toUInt();
        tmp.DateTime = Query.value("CreateDate").toDateTime();
        SendedTalonCode.push_back(tmp);
    }

    if (!CouponsDB.commit()) {
       CouponsDB.rollback();
       QString LastError = "Cannot commit transation. Error: " + DB.lastError().text();
       qDebug() << LastError;
       exit(-2);
    };

    XMLWriter.writeEndElement();
    XMLWriter.writeEndDocument();

    ManyDataLocal = (SendedTalonCode.size() == HTTPServerInfo.MaxRecord); //если количество записей в очереде равно MaxRecord то скорее всего есть еще завис для выгрузки

    if (DebugMode) qDebug() << "Get data from DB. Send request. Time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";

    HTTPQuery->Run(XMLStr); //Отправляем запрос

   // qDebug() << "Send data time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";
}

void TCouponsUpdater::SendServerRequest()
{
    Sending = true;
    //формаируем XML документ
    XMLStr.clear();
    QXmlStreamWriter XMLWriter(&XMLStr);
    XMLWriter.setAutoFormatting(true);
    XMLWriter.writeStartDocument("1.0");
    XMLWriter.writeStartElement("Root");
    XMLWriter.writeTextElement("AZSCode", HTTPServerInfo.AZSCode);
    XMLWriter.writeTextElement("ClientVersion", QCoreApplication::applicationVersion());
    XMLWriter.writeTextElement("LastID", QString::number(HTTPServerInfo.LastUpdateServer2LocalID)); //время запроса
    XMLWriter.writeTextElement("MaxCoupons", QString::number(HTTPServerInfo.MaxRecord)); //время запроса
    XMLWriter.writeEndElement();
    XMLWriter.writeEndDocument();

    if (DebugMode) qDebug() << "Send request. Time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";

    HTTPQuery->Run(XMLStr); //Отправляем запрос
}

void TCouponsUpdater::onHTTPGetAnswer(const QByteArray &Answer)
{
    //qDebug() << Answer;
   // qDebug() << "Get answer time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";
   // qDebug() << "Total get byte: " << Answer.size();
    //Sending = false;

    if (DebugMode) qDebug() << "Get answer from HTTP Server. Time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";

    QVector<TCouponsData> ListCouponsOnServer;

    QXmlStreamReader XMLReader(Answer);
    while ((!XMLReader.atEnd()) && (!XMLReader.hasError())) {
        QXmlStreamReader::TokenType Token = XMLReader.readNext();
        if (Token == QXmlStreamReader::StartDocument) continue;
        if (Token == QXmlStreamReader::EndDocument) break;
        if (Token == QXmlStreamReader::StartElement) {
            if (XMLReader.name().toString()  == "ProtocolVersion") continue; //пока версию протокола не проверяем
            else if (XMLReader.name().toString()  == "LastID") HTTPServerInfo.CurrentUpdateServer2LocalID = XMLReader.readElementText().toLongLong();//
            else if (XMLReader.name().toString()  == "Coupon") {
                TCouponsData tmp;
                while (XMLReader.readNext() != QXmlStreamReader::EndElement) {
                    if (XMLReader.name().toString()  == "Code") tmp.Code = XMLReader.readElementText();
                    if (XMLReader.name().toString()  == "Active") tmp.Active = XMLReader.readElementText().toUInt();
                    if (XMLReader.name().toString()  == "UpdateDateTime") tmp.DateTime = QDateTime::fromString(XMLReader.readElementText(), "yyyy-MM-dd hh:mm:ss.zzz");
                }
                if (tmp.Code != "") ListCouponsOnServer.push_back(tmp);
            }
        }
    }

    if (XMLReader.hasError()) { //неудалось распарсить пришедшую XML
        SendLogMsg(MSG_CODE::CODE_ERROR, "Incorrect answer from server. Parser msg: " + XMLReader.errorString() + " Answer: " + Answer);
        return;
    }

    //если пришло пришла полная очередь талонов, то надо будет стразу отправляем еще один запрос
   // bool ManyData = (QueueCouponsServer.size() == HTTPServerInfo.MaxRecord);

    QSqlQuery Query(CouponsDB);
    CouponsDB.transaction();
   // qDebug() << "OK";
    //помечаем отправленные талоны
    for (const auto &Item : SendedTalonCode) {
        QString QueryText = "DELETE FROM \"Coupons\" "
                            "WHERE (\"Code\" = '" + Item.Code + "')";

        if (!Query.exec(QueryText)) {
            CouponsDB.rollback();
            QString LastError = "Cannot execute query. Error: " + Query.lastError().text() + " Query: " + QueryText;
            qDebug() << LastError;
            exit(-1);
        }

        QueryText = "INSERT INTO \"Coupons\" (\"Code\", \"Active\", \"CreateDate\", \"SENT\" ) "
                    "VALUES ( "
                    "'" + Item.Code + "', " +
                    QString::number(Item.Active) + ", "
                    "'" + Item.DateTime.toString("yyyy-MM-dd hh:mm:ss.zzz") + "', "
                    "1 )";
        if (!Query.exec(QueryText)) {
            CouponsDB.rollback();
            QString LastError = "Cannot execute query. Error: " + Query.lastError().text() + " Query: " + QueryText;
            qDebug() << LastError;
            exit(-1);
        }
    }
    //сохраняем пришедшие талоны
    for (const auto &Item : ListCouponsOnServer) {
        QString QueryText = "DELETE FROM \"Coupons\" "
                            "WHERE (\"Code\" = '" + Item.Code + "')";
        if (!Query.exec(QueryText)) {
            CouponsDB.rollback();
            QString LastError = "Cannot execute query. Error: " + Query.lastError().text() + " Query: " + QueryText;
            qDebug() << LastError;
            exit(-1);
        }
        QueryText = "INSERT INTO \"Coupons\" (\"Code\", \"Active\", \"UpdateDate\", \"SENT\" ) "
                    "VALUES ( "
                    "'" + Item.Code + "', " +
                    QString::number(Item.Active) + ", "
                    "'" + Item.DateTime.toString("yyyy-MM-dd hh:mm:ss.zzz") + "', "
                    "1 )";
        if (!Query.exec(QueryText)) {
            CouponsDB.rollback();
            QString LastError = "Cannot execute query. Error: " + Query.lastError().text() + " Query: " + QueryText;
            qDebug() << LastError;
            exit(-1);
        }
    }

    if (!CouponsDB.commit()) {
       CouponsDB.rollback();
       QString LastError = "Cannot commit transation. Error: " + DB.lastError().text();
       qDebug() << LastError;
       exit(-2);
    };

    HTTPServerInfo.LastUpdateLocal2Server = HTTPServerInfo.CurrentUpdateLocal2Server;//Обновляем время последнего обновления
    HTTPServerInfo.LastUpdateServer2LocalID = HTTPServerInfo.CurrentUpdateServer2LocalID;
    SaveDateTimeLastUpdate();

 //   qDebug() << "Parse answer XML time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";
    //получены не все талоны с сервера
    if (ListCouponsOnServer.size() == HTTPServerInfo.MaxRecord) {
        SendLogMsg(MSG_CODE::CODE_INFORMATION, "Not all coupons have been received");
        SendServerRequest();
        return;
    }

    //Отправлены не все талоны
    if (ManyDataLocal) {
        SendLogMsg(MSG_CODE::CODE_INFORMATION, "Not all coupons were sent");
        SendLocalTalons();
        return;
    }

    //Если дошли до сюда, то обменн данными окончен
    SendLogMsg(MSG_CODE::CODE_OK, "Coupons data update was successful."
                                  " LastUpdateLocal2Server:" + HTTPServerInfo.LastUpdateLocal2Server.toString("yyyy-MM-dd hh:mm:ss.zzz") +
                                  " LastUpdateServer2LocalID: " + QString::number(HTTPServerInfo.LastUpdateServer2LocalID));


    Sending = false;
}

void TCouponsUpdater::onStartGetData()
{
    if (Sending) {
        return;
    }

    TransferMode = !TransferMode;
    if (DebugMode)  {
        qDebug() << "Start get talons. Mode" << TransferMode << "Time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms" ;
    }
    if (TransferMode) {
        SendLocalTalons();  //получаем список талонов необходимых к отправке
    }
    else {
        SendServerRequest();
    }
}

void TCouponsUpdater::onHTTPError()
{
    //Sending = false;
    if (DebugMode) qDebug() << "Error getting anwser from HTTP Server. Retry. Time:" << TimeOfRun.msecsTo(QTime::currentTime()) << "ms";

    if (!XMLStr.isEmpty()) HTTPQuery->Run(XMLStr);
}

void TCouponsUpdater::onSendLogMsg(uint16_t Category, const QString &Msg)
{
    SendLogMsg(Category, Msg);
}
