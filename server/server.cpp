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
#include <QTreeView>
#include "server.h"
#include <sys/socket.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "../source/protocol.h"
#include "../source/FifoDataFile.h"
#include "../source/FifoAnalyzer.h"

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

class TreeItem : public QObject
{
public:
	TreeItem(QObject* parent) : QObject(parent), type(-1), index(-1), parent(NULL) {}

	int type;
	int index;
	std::vector<TreeItem*> children;
	TreeItem* parent;
};

DffModel::DffModel(QObject* parent) : QAbstractItemModel(parent)
{
	root_item = new TreeItem(this);
}

int DffModel::columnCount(const QModelIndex& parent) const
{
	return 1;
}

QVariant DffModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return QVariant();

	if (role == Qt::DisplayRole)
	{
		TreeItem* item = (TreeItem*)index.internalPointer();
		if (item->type == IDX_FRAME)
			return QVariant(QString("Frame %1").arg(index.row()));
		else if (item->type == IDX_OBJECT)
			return QVariant(QString("Object %1").arg(index.row()));
		else
			return QVariant(QString("Command %1").arg(index.row()));
	}
	else
		return QVariant();
}

QModelIndex DffModel::index(int row, int column, const QModelIndex& parent) const
{
	if (!parent.isValid())
	{
		return createIndex(row, column, root_item->children[row]);
	}
	else if (!parent.parent().isValid())
	{
		TreeItem* frame = (TreeItem*)parent.internalPointer();
		return createIndex(row, column, frame->children[row]);
	}
	else
	{
		TreeItem* object = (TreeItem*)parent.internalPointer();
		return createIndex(row, column, object->children[row]);
	}
}

QModelIndex DffModel::parent(const QModelIndex& index) const
{
	if (!index.isValid())
		return QModelIndex();

	TreeItem* item = (TreeItem*)index.internalPointer();
	return createIndex(item->parent->index, 0, item->parent);
}

int DffModel::rowCount(const QModelIndex& parent) const
{
	TreeItem* item;
	if (!parent.isValid()) item = root_item;
	else item = (TreeItem*)parent.internalPointer();

	return item->children.size();
}

void DffModel::OnFifoDataChanged(FifoData& fifo_data)
{
	FifoDataAnalyzer analyzer;
	analyzer.AnalyzeFrames(fifo_data, analyzed_frames);

	beginResetModel();

	delete root_item;
	root_item = new TreeItem(this);

	for (auto frameit = analyzed_frames.begin(); frameit != analyzed_frames.end(); ++frameit)
	{
		auto frame = new TreeItem(root_item);
		frame->type = IDX_FRAME;
		frame->parent = root_item;
		frame->index = frameit - analyzed_frames.begin();
		root_item->children.push_back(frame);
		for (auto objectit = (*frameit).objects.begin(); objectit != (*frameit).objects.end(); ++objectit)
		{
			auto object = new TreeItem(frame);
			object->type = IDX_OBJECT;
			object->parent = frame;
			object->index = objectit - (*frameit).objects.begin();
			frame->children.push_back(object);
			for (auto commandit = (*objectit).cmd_starts.begin(); commandit != (*objectit).cmd_starts.end(); ++commandit)
			{
				auto command = new TreeItem(object);
				command->type = IDX_COMMAND;
				command->parent = object;
				command->index = commandit - (*objectit).cmd_starts.begin();
				object->children.push_back(command);
			}
		}
	}

	endResetModel();
}

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

	QTreeView* dff_view = new QTreeView;
	DffModel* dff_model = new DffModel(this);
	dff_view->setModel(dff_model);

	connect(this, SIGNAL(FifoDataChanged(FifoData&)), dff_model, SLOT(OnFifoDataChanged(FifoData&)));

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
	{
		main_layout->addWidget(dff_view);
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

	FifoData fifo_data;
	LoadDffData(filename.toLatin1().constData(), fifo_data);
	fclose(fifo_data.file);

	emit FifoDataChanged(fifo_data);
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
