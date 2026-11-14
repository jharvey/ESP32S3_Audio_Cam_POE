#include "SimWebServer.h"

#include <QTcpSocket>

#include "SimUnit.h"

namespace {
constexpr int kMaxRequestBytes = 16384;
}

SimWebServer::SimWebServer(SimUnit* unit, QObject* parent) : QObject(parent), unit_(unit) {
    connect(&server_, &QTcpServer::newConnection, this, &SimWebServer::onConnection);
}

bool SimWebServer::listen(int port) { return server_.listen(QHostAddress::Any, quint16(port)); }

void SimWebServer::onConnection() {
    while (QTcpSocket* s = server_.nextPendingConnection()) {
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onData(s); });
        connect(s, &QTcpSocket::disconnected, this, [this, s] {
            buf_.remove(s);
            s->deleteLater();
        });
    }
}

void SimWebServer::onData(QTcpSocket* s) {
    QByteArray& b = buf_[s];
    b += s->readAll();
    if (b.size() > kMaxRequestBytes) {
        s->abort();
        return;
    }
    int end = b.indexOf("\r\n\r\n");
    if (end < 0) return;

    QList<QByteArray> lines = b.left(end).split('\n');
    QList<QByteArray> first = lines.value(0).trimmed().split(' ');
    ic::HttpRequest req;
    req.method = first.value(0).toStdString();
    req.path = first.value(1).toStdString();
    int contentLength = 0;
    for (int i = 1; i < lines.size(); i++) {
        QByteArray l = lines[i].trimmed();
        int colon = l.indexOf(':');
        if (colon < 0) continue;
        QByteArray name = l.left(colon).trimmed().toLower(), value = l.mid(colon + 1).trimmed();
        if (name == "authorization") req.authorization = value.toStdString();
        else if (name == "x-intercom") req.xIntercom = value.toStdString();
        else if (name == "content-length") contentLength = value.toInt();
    }
    if (b.size() < end + 4 + contentLength) return;  // body still arriving
    req.body = b.mid(end + 4, contentLength).toStdString();

    ic::HttpResponse r = unit_->web(req);
    QByteArray body = QByteArray::fromStdString(r.body);
    QByteArray head = "HTTP/1.1 " + QByteArray::number(r.status) + " " + r.reason() + "\r\n";
    head += "Content-Type: " + QByteArray::fromStdString(r.contentType) + "\r\n";
    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\nX-Frame-Options: DENY\r\nConnection: close\r\n";
    if (r.askLogin) head += "WWW-Authenticate: Basic realm=\"" + QByteArray::fromStdString(r.realm) + "\"\r\n";
    s->write(head + "\r\n" + body);
    s->disconnectFromHost();
    buf_.remove(s);
}
