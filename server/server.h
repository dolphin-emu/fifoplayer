#include <QWidget>
#include <QTcpSocket>
#include <QTcpServer>

class DffClient : public QTcpSocket
{
	Q_OBJECT

public:
	DffClient(QObject* parent = NULL);

public slots:
	void Connect(const QString & hostName);
	void OnConnected();
};

class DummyServer : public QTcpServer
{
	Q_OBJECT

public:
	DummyServer(QObject* parent = NULL);

public slots:
	void StartListen();
	void OnNewConnection();
	void CheckIncomingData(int socket);
};

class ServerWidget : public QWidget
{
	Q_OBJECT

public:
	ServerWidget();

public slots:
	void OnTryConnect();

private:
	DffClient* client;
};
