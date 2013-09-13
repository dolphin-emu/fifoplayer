#include <QWidget>
#include <QTcpSocket>
#include <QTcpServer>

class QLineEdit;

class DffClient : public QObject
{
	Q_OBJECT

public:
	DffClient(QObject* parent = NULL);

	int socket;
public slots:
	void Connect(const QString & hostName);
	void OnConnected();

signals:
	void connected();

private:
};

class DummyServer : public QObject
{
	Q_OBJECT

public:
	DummyServer(QObject* parent = NULL);

public slots:
	void StartListen();
	void CheckIncomingData();

private:
	int server_socket;
	int client_socket;
};

class ServerWidget : public QWidget
{
	Q_OBJECT

public:
	ServerWidget();

public slots:
	void OnTryConnect();

	void OnSelectDff();
	void OnLoadDff();

private:
	DffClient* client;
	QLineEdit* hostname;
	QLineEdit* dffpath;
};
