#include <QApplication>
#include <QMainWindow>
#include <QTcpSocket>
#include <QTcpServer>
#include <QPushButton>
#include <QBoxLayout>
#include <QTimer>
#include "server.h"
#include <sys/socket.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>

#define DFF_CONN_PORT 15342

#define CMD_HANDSHAKE 0x00  // First command ever sent; used for exchanging version and handshake tokens
#define CMD_STREAM_DFF 0x01  // Start uploading a new fifo log
#define CMD_RUN_DFF 0x02  // Run most recently uploaded fifo log
#define CMD_SET_CONTEXT_FRAME 0x03  // Specify what frame the following communication is referring to
#define CMD_SET_CONTEXT_COMMAND 0x04  // Specify what command the following communication is referring to
#define CMD_ENABLE_COMMAND 0x05 // Enable current command
#define CMD_DISABLE_COMMAND 0x06 // Enable current command

#define RET_FAIL 0
#define RET_SUCCESS 1
#define RET_WOULDBLOCK 2

const int version = 0;
const uint32_t handshake = 0x6fe62ac7;

int ReadHandshake(int socket)
{
	char data[5];
	recv(socket, data, sizeof(data), 0);
	uint32_t received_handshake = ntohl(*(uint32_t*)&data[1]);

	if (data[0] != CMD_HANDSHAKE || received_handshake != handshake)
		return RET_FAIL;

	return RET_SUCCESS;
}


// /dev/net/ip/top
DummyClient::DummyClient(QObject* parent) : QTcpSocket(parent)
{
	connect(this, SIGNAL(connected()), this, SLOT(OnConnected()));
}

void DummyClient::Connect(const QString & hostName)
{
	connectToHost("127.0.0.1", DFF_CONN_PORT); // TODO: Configurable IP address
	if (!waitForConnected(1000))
	{
		qDebug() << "Client couldn't connect";
	}
}

void DummyClient::OnConnected()
{
	qDebug() << "Client connected successfully";

	// NOTE: We should periodically check for incoming data instead of spinlocking here...
	// However, this seems buggy. If I enable the code, nothing works anymore.
	// Once this runs in the fifo player it won't matter anyway, so I'll just leave it commented out
/*	QTimer* timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SLOT(CheckIncomingData()));
	timer->setSingleShot(false);
	timer->start(1000);*/
	CheckIncomingData();
}

void DummyClient::CheckIncomingData()
{
	int socket = socketDescriptor();

	fd_set readset;
	FD_ZERO(&readset);
	FD_SET(socket, &readset);
	int maxfd = socket;

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	char data[12];
	int ret = select(maxfd+1, &readset, NULL, NULL, &timeout); // TODO: Is this compatible with winsocks?
	if (ret <= 0)
	{
		CheckIncomingData();
		return;
	}

	if (FD_ISSET(socket, &readset))
	{
		char cmd;
		ssize_t numread = recv(socket, &cmd, 1, MSG_PEEK);
		switch (cmd)
		{
			case CMD_HANDSHAKE:
				if (RET_SUCCESS == ReadHandshake(socket))
					qDebug() << tr("Successfully exchanged handshake token!");
				else
					qDebug() << tr("Failed to exchange handshake token!");
				break;

			default:
				qDebug() << tr("Received unknown command: ") << cmd;
		}
	}
	else
	{
		CheckIncomingData();
		return;
	}
}

void WriteHandshake(int socket)
{
	char data[5];
	data[0] = CMD_HANDSHAKE;
	*(uint32_t*)&data[1] = htonl(handshake);
	send(socket, data, sizeof(data), 0);
}

DffServer::DffServer(QObject* parent) : QTcpServer(parent)
{
	connect(this, SIGNAL(newConnection()), this, SLOT(OnNewConnection()));
}

void DffServer::StartListen()
{
	listen(QHostAddress::Any, DFF_CONN_PORT);
}

void DffServer::OnNewConnection()
{
	qDebug() << "Server got a new connection";
	QTcpSocket* socket = nextPendingConnection();

	WriteHandshake(socket->socketDescriptor());
}

ServerWidget::ServerWidget() : QWidget()
{
	DffServer* server = new DffServer(this);
	client = new DummyClient(this);

	QPushButton* start_listen = new QPushButton(tr("Server"));
	QPushButton* try_connect = new QPushButton(tr("Client"));

	connect(start_listen, SIGNAL(clicked()), server, SLOT(StartListen()));
	connect(try_connect, SIGNAL(clicked()), this, SLOT(OnTryConnect()));

	QHBoxLayout* layout = new QHBoxLayout;
	layout->addWidget(start_listen);
	layout->addWidget(try_connect);
	setLayout(layout);
}

void ServerWidget::OnTryConnect()
{
	client->Connect("127.0.0.1");
}

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QMainWindow win;
	win.setCentralWidget(new ServerWidget);
	win.show();

	return app.exec();
}
