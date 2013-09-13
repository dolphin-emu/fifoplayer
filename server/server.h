#include <QWidget>
#include <QTcpSocket>
#include <QTcpServer>

class DummyClient : public QTcpSocket
{
	Q_OBJECT

public:
	DummyClient(QObject* parent = NULL);

public slots:
	void Connect(const QString & hostName);
	void OnConnected();
	void CheckIncomingData();
};

class DffServer : public QTcpServer
{
	Q_OBJECT

public:
	DffServer(QObject* parent = NULL);

public slots:
	void StartListen();
	void OnNewConnection();
};

class ServerWidget : public QWidget
{
	Q_OBJECT

public:
	ServerWidget();

public slots:
	void OnTryConnect();

private:
	DummyClient* client;
};
