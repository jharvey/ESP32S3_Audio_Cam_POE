// Minimal HTTP/1.1 server for one simulated unit's web interface. Parses the request and
// hands it to the firmware core's WebUi (via SimUnit::web); one request per connection.
#pragma once
#include <QHash>
#include <QObject>
#include <QTcpServer>

class SimUnit;
class QTcpSocket;

class SimWebServer : public QObject {
    Q_OBJECT
public:
    SimWebServer(SimUnit* unit, QObject* parent = nullptr);
    bool listen(int port);
    QString error() const { return server_.errorString(); }

private:
    void onConnection();
    void onData(QTcpSocket* s);

    SimUnit* unit_;
    QTcpServer server_;
    QHash<QTcpSocket*, QByteArray> buf_;
};
