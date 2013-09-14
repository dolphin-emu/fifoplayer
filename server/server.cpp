#include <QApplication>
#include <QMainWindow>
#include <QTcpSocket>
#include <QTcpServer>
#include <QPushButton>
#include <QBoxLayout>
#include <QTimer>
#include <QLineEdit>
#include <QStyle>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include "server.h"
#include <sys/socket.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../source/protocol.h"

void WriteHandshake(int socket)
{
	char data[5];
	data[0] = CMD_HANDSHAKE;
	*(uint32_t*)&data[1] = htonl(handshake);
	send(socket, data, sizeof(data), 0);
}

DffClient::DffClient(QObject* parent) : QObject(parent)
{
	connect(this, SIGNAL(connected()), this, SLOT(OnConnected()));
}

void DffClient::Connect(const QString& hostName)
{
	int count;
	struct sockaddr_in serv_name;
	int status;

	socket = ::socket(AF_INET, SOCK_STREAM, 0);
	if (socket == -1)
	{
		qDebug() << "Error at creating socket!";
	}

	memset(&serv_name, 0, sizeof(serv_name));
	serv_name.sin_family = AF_INET;
//	inet_aton(hostName.toLatin1().constData(), &serv_name.sin_addr);
	inet_aton("192.168.178.22", &serv_name.sin_addr);
	serv_name.sin_port = htons(DFF_CONN_PORT);

	status = ::connect(socket, (struct sockaddr*)&serv_name, sizeof(serv_name));
	if (status == -1)
	{
		perror("connect");
	}
	else
	{
		emit connected();
	}
}

void DffClient::OnConnected()
{
	qDebug() << "Client connected successfully";

	WriteHandshake(socket);
}

// Kept for reference
#if 0
void DummyServer::CheckIncomingData()
{
while (true) {
begin:

	fd_set readset;
	FD_ZERO(&readset);
	FD_SET(server_socket, &readset);
	if (client_socket != -1)
		FD_SET(client_socket, &readset);
	int maxfd = std::max(client_socket, server_socket);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	char data[12];
	int ret = select(maxfd+1, &readset, NULL, NULL, &timeout); // TODO: Is this compatible with winsocks?
	if (ret <= 0)
	{
//		CheckIncomingData(socket);
//		return;
		goto begin;
	}

	if (FD_ISSET(server_socket, &readset))
	{
		int new_socket = accept(server_socket, NULL, NULL);
		if (new_socket < 0)
		{
			qDebug() << "accept failed";
		}
		else client_socket = new_socket;
	}
	if (FD_ISSET(client_socket, &readset))
	{
		char cmd;
		ssize_t numread = recv(client_socket, &cmd, 1, MSG_PEEK);
		switch (cmd)
		{
			case CMD_HANDSHAKE:
/*				if (RET_SUCCESS == ReadHandshake(client_socket))
					qDebug() << tr("Successfully exchanged handshake token!");
				else
					qDebug() << tr("Failed to exchange handshake token!");
*/
				// TODO: should probably write a handshake in return, but ... I'm lazy
				break;

			case CMD_STREAM_DFF:
//				ReadStreamedDff(client_socket);
				break;

			default:;
//				qDebug() << tr("Received unknown command: ") << cmd;
		}
	}
}
}
#endif

ServerWidget::ServerWidget() : QWidget()
{
	client = new DffClient(this);

	hostname = new QLineEdit("127.0.0.1");
	QPushButton* try_connect = new QPushButton(tr("Connect"));

	connect(try_connect, SIGNAL(clicked()), this, SLOT(OnTryConnect()));

	// TODO: Change the lineedit text to be a default text?
	dffpath = new QLineEdit;
	dffpath->setReadOnly(true);
	QPushButton* openDffFile = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), "");
	QPushButton* loadDffFile = new QPushButton(tr("Load"));

	connect(openDffFile, SIGNAL(clicked()), this, SLOT(OnSelectDff()));
	connect(loadDffFile, SIGNAL(clicked()), this, SLOT(OnLoadDff()));

	QVBoxLayout* main_layout = new QVBoxLayout;
	{
		QHBoxLayout* layout = new QHBoxLayout;
		layout->addWidget(hostname);
		layout->addWidget(try_connect);
		main_layout->addLayout(layout);
	}
	{
		QHBoxLayout* layout = new QHBoxLayout;
		layout->addWidget(new QLabel(tr("Dff file:")));
		layout->addWidget(dffpath);
		layout->addWidget(openDffFile);
		layout->addWidget(loadDffFile);
		main_layout->addLayout(layout);
	}
	setLayout(main_layout);
}

void WriteStreamDff(int socket, QString filename)
{
	char cmd = CMD_STREAM_DFF;
	send(socket, &cmd, 1, 0);

	QFile file(filename);
	int32_t size = file.size();
	int32_t n_size = htonl(size);
	send(socket, &n_size, 4, 0);
	qDebug() << "About to send " << size << " bytes of dff data!" << n_size;

	file.open(QIODevice::ReadOnly);
	QDataStream stream(&file);

	for (; size > 0; size -= dff_stream_chunk_size)
	{
		char data[dff_stream_chunk_size];
		stream.readRawData(data, std::min(size,dff_stream_chunk_size));
		int ret = send(socket, data, std::min(size,dff_stream_chunk_size), 0);
		if (ret == -1)
		{
			perror("send");
		}
		else if (ret != std::min(size,dff_stream_chunk_size))
		{
			qDebug() << "Only printed " << ret << " bytes of data...";
		}
		else
		{
			qDebug() << size << " bytes left to be sent!";
		}
	}

	file.close();
}

void ServerWidget::OnSelectDff()
{
	QString filename = QFileDialog::getOpenFileName(this, tr("Select a dff file ..."), QString(), tr("Dolphin Fifo Files (*.dff)"));
	if (!QFile::exists(filename))
		return;

	dffpath->setText(filename);

	// TODO: Analyze dff and display contents
}

void ServerWidget::OnLoadDff()
{
	WriteStreamDff(client->socket, dffpath->text());
}

void ServerWidget::OnTryConnect()
{
	client->Connect(hostname->text());
}

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QMainWindow win;
	win.setCentralWidget(new ServerWidget);
	win.show();

	return app.exec();
}
