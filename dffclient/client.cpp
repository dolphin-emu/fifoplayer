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
#include <QProgressBar>
#include <QTreeView>
#include <QSignalMapper>
#include "client.h"
#include <sys/socket.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <functional>
#include "../source/protocol.h"
#include "../source/BPMemory.h"
#include "../source/FifoDataFile.h"
#include "../source/FifoAnalyzer.h"
#include "command_info.h"

int* client_socket = NULL; // TODO: Remove this

LayoutStream* command_description = NULL; // TODO

class NetworkQueue
{
public:
	NetworkQueue(int socket) : socket(socket), cur_pos_(0) {}

	void PushCommand(u8* data, u32 size)
	{
		// Flush existing data if new command doesn't fit
		if (cur_pos_ + size > size_)
			Flush();

		// TODO: Assert that size < size_!
		PushData(data, size);
	}
	void Flush()
	{
		send(socket, data_, cur_pos_, 0);
		cur_pos_ = 0;
	}

private:
	// Blindly push data without checking for overflows etc
	void PushData(u8* data, u32 size)
	{
		memcpy(data_ + cur_pos_, data, size);
		cur_pos_ += size;
	}

	static const int size_ = 65536;

	int socket;
	u8 data_[size_];
	int cur_pos_;
};

NetworkQueue* netqueue = NULL;

void WriteHandshake(int socket)
{
	u8 data[5];
	data[0] = CMD_HANDSHAKE;
	*(uint32_t*)&data[1] = htonl(handshake);
	netqueue->PushCommand(data, sizeof(data));
}

void WriteSetCommandEnabled(int socket, u32 frame, u32 object, u32 offset, int enable)
{
	u8 cmd = (enable) ? CMD_ENABLE_COMMAND : CMD_DISABLE_COMMAND;
	u32 frame_n = htonl(frame);
	u32 object_n = htonl(object);
	u32 offset_n = htonl(offset);

	u8 data[13];
	data[0] = cmd;
	*(u32*)&data[1] = frame_n;
	*(u32*)&data[5] = object_n;
	*(u32*)&data[9] = offset_n;
	netqueue->PushCommand(data, sizeof(data));
}

void WritePatchCommand(int socket, u32 frame, u32 offset, u32 size, u8* data)
{
	if (*client_socket == -1)
		return;

	u8 cmd = CMD_PATCH_COMMAND;
	u32 frame_n = htonl(frame);
	u32 offset_n = htonl(offset);
	u32 size_n = htonl(size);

	// TODO: Ugly
	u8 cmd_data[13];
	cmd_data[0] = cmd;
	*(u32*)&cmd_data[1] = frame_n;
	*(u32*)&cmd_data[5] = offset_n;
	*(u32*)&cmd_data[9] = size_n;
	netqueue->PushCommand(cmd_data, sizeof(cmd_data));
	netqueue->PushCommand(data, size);
	netqueue->Flush();
}

DffClient::DffClient(QObject* parent) : QObject(parent), socket(-1)
{
	client_socket = &socket;
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
	serv_name.sin_port = htons(DFF_CONN_PORT);
	inet_aton(hostName.toLatin1().constData(), &serv_name.sin_addr);

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

	if (netqueue)
	{
		netqueue->Flush();
		delete netqueue;
	}
	netqueue = new NetworkQueue(socket);
	WriteHandshake(socket);
	netqueue->Flush();
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
	TreeItem(QObject* parent) : QObject(parent), type(-1), index(-1), enabled(true), parent(NULL) {}

	int type;
	int index;
	bool enabled;
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

bool DffModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (role == UserRole_FifoDataForCommand)
	{
		if (data(index, UserRole_Type).toInt() != IDX_COMMAND)
			return false;

		QByteArray databuffer = value.toByteArray();
		int frame_idx = data(index, UserRole_FrameIndex).toInt();
		int cmd_start = data(index, UserRole_CmdStart).toInt();
		memcpy(&fifo_data_.frames[frame_idx].fifoData[cmd_start], databuffer.data(), databuffer.size());;

		emit dataChanged(index, index);

		WritePatchCommand(*client_socket, frame_idx, cmd_start, databuffer.size(), (u8*)databuffer.data());

		return true;
	}
	if (role == Qt::EditRole)
	{
		// TODO: Check that the input data is valid
		TreeItem* item = (TreeItem*)index.internalPointer();

		qDebug() << "Patching to data: " << value.toString();
		const AnalyzedFrameInfo* analyzed_frame = NULL;
		const AnalyzedObject* analyzed_object = NULL;

		analyzed_frame = &analyzed_frames[item->parent->parent->index];
		analyzed_object = &analyzed_frame->objects[item->parent->index];

		for (int byte = 0; byte < value.toString().size() / 2; ++byte)
		{
			bool ok;
			u8 data = value.toString().mid(byte*2, 2).toInt(&ok, 16);
			qDebug() << "byte: " << data;
			WritePatchCommand(*client_socket, item->parent->parent->index, byte + analyzed_object->cmd_starts[item->index], 1, &data);
		}

		emit dataChanged(index, index);

		return true;
	}
	return false;
}

Qt::ItemFlags DffModel::flags(const QModelIndex& index) const
{
	TreeItem* item = (TreeItem*)index.internalPointer();
	if (item->type == IDX_COMMAND)
		return Qt::ItemIsEditable | QAbstractItemModel::flags(index);

	return QAbstractItemModel::flags(index);
}

QVariant DffModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return QVariant();

	TreeItem* item = (TreeItem*)index.internalPointer();
	const AnalyzedFrameInfo* analyzed_frame = NULL;
	const AnalyzedObject* analyzed_object = NULL;

	if (item->type == IDX_FRAME)
	{
		analyzed_frame = &analyzed_frames[item->index];
	}
	else if (item->type == IDX_OBJECT)
	{
		analyzed_frame = &analyzed_frames[item->parent->index];
		analyzed_object = &analyzed_frame->objects[item->index];
	}
	else if (item->type == IDX_COMMAND)
	{
		analyzed_frame = &analyzed_frames[item->parent->parent->index];
		analyzed_object = &analyzed_frame->objects[item->parent->index];
	}

	if (role == Qt::DisplayRole || role == Qt::EditRole)
	{
		if (item->type == IDX_FRAME && role == Qt::DisplayRole)
		{
			return QVariant(QString("Frame %1: %2 objects").arg(index.row()).arg(analyzed_frame->objects.size()));
		}
		else if (item->type == IDX_OBJECT && role == Qt::DisplayRole)
		{
			return QVariant(QString("Object %1: %2 commands").arg(index.row()).arg(analyzed_object->cmd_starts.size()));
//			return QVariant(QString("Object %1: %2 commands (%3 geometry, %4 state changes)").arg(index.row()).arg(analyzed_object->cmd_starts.size()).arg(0).arg(0));
		}
		else if (item->type == IDX_COMMAND)
		{
			u32 object_idx = item->parent->index;
			u32 frame_idx = item->parent->parent->index;
			QString ret;
			if (role == Qt::DisplayRole)
				ret = tr("Command %1@%2: ").arg(index.row()).arg(analyzed_object->cmd_starts[item->index], 8, 16, QLatin1Char('0'));

			const u8* data = &fifo_data_.frames[frame_idx].fifoData[analyzed_object->cmd_starts[item->index]];

			if (data[0] == GX_LOAD_BP_REG)
			{
//				if (role == Qt::DisplayRole)
				{
					char reg_name[32] = { 0 };
					GetBPRegInfo(data+1, reg_name, sizeof(reg_name), NULL, 0);
					ret += QString::fromLatin1(reg_name) + " ";
				}

				u32 cmd_offset_limit = analyzed_object->last_cmd_byte+1;
				if (item->index+1 < analyzed_object->cmd_starts.size() && cmd_offset_limit > analyzed_object->cmd_starts[item->index+1])
					cmd_offset_limit = analyzed_object->cmd_starts[item->index+1];
				for (u32 i = analyzed_object->cmd_starts[item->index]; i < cmd_offset_limit; ++i)
					ret += QString("%1").arg(fifo_data_.frames[frame_idx].fifoData[i], 2, 16, QLatin1Char('0'));

				return QVariant(ret);
			}
			else
			{
				u32 cmd_offset_limit = analyzed_object->last_cmd_byte+1;
				if (item->index+1 < analyzed_object->cmd_starts.size() && cmd_offset_limit > analyzed_object->cmd_starts[item->index+1])
					cmd_offset_limit = analyzed_object->cmd_starts[item->index+1];
				for (u32 i = analyzed_object->cmd_starts[item->index]; i < cmd_offset_limit; ++i)
					ret += QString("%1").arg(fifo_data_.frames[frame_idx].fifoData[i], 2, 16, QLatin1Char('0'));

				return QVariant(ret);
			}
		}
		else return QVariant();
	}
	else if (role == Qt::BackgroundRole)
	{
		if (!item->enabled)
			return QVariant(QBrush(Qt::gray));
		else
			return QVariant();
	}
	else if (role == UserRole_IsEnabled)
	{
		return QVariant(item->enabled);
	}
	else if (role == UserRole_Type)
	{
		return QVariant(item->type);
	}
	else if (role == UserRole_CommandIndex)
	{
		if (item->type == IDX_COMMAND)
			return QVariant(item->index);
		else
			return QVariant(-1);
	}
	else if (role == UserRole_ObjectIndex)
	{
		if (item->type == IDX_OBJECT)
			return QVariant(item->index);
		else if (item->type == IDX_COMMAND)
			return QVariant(item->parent->index);
		else
			return QVariant(-1);
	}
	else if (role == UserRole_FrameIndex)
	{
		if (item->type == IDX_FRAME)
			return QVariant(item->index);
		else if (item->type == IDX_OBJECT)
			return QVariant(item->parent->index);
		else if (item->type == IDX_COMMAND)
			return QVariant(item->parent->parent->index);
		else
			return QVariant(-1);
	}
	else if (role == UserRole_CmdStart)
	{
		if (item->type != IDX_COMMAND)
			return QVariant(-1);

		u32 object_idx = item->parent->index;
		u32 frame_idx = item->parent->parent->index;
		return QVariant(analyzed_frames[frame_idx].objects[object_idx].cmd_starts[item->index]);
	}
	else if (role == UserRole_IsGeometryCommand)
	{
		if (item->type != IDX_COMMAND)
			return QVariant(false);

		u32 object_idx = item->parent->index;
		u32 frame_idx = item->parent->parent->index;
		return QVariant((fifo_data_.frames[frame_idx].fifoData[analyzed_frames[frame_idx].objects[object_idx].cmd_starts[item->index]] & 0x80) != 0);
	}
	else if (role == UserRole_FifoData)
	{
		u32 frame_idx = data(index, UserRole_FrameIndex).toInt();
		return QVariant(QByteArray((const char*)&fifo_data_.frames[frame_idx].fifoData[0], fifo_data_.frames[frame_idx].fifoData.size()));
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

	fifo_data_ = fifo_data;

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
				command->enabled = (*objectit).cmd_enabled[commandit - (*objectit).cmd_starts.begin()];
				object->children.push_back(command);
			}
		}
	}

	endResetModel();
}


void DffModel::SetEntryEnabled(const QModelIndex& index, bool enable)
{
	TreeItem* item = (TreeItem*)index.internalPointer();
	item->enabled = enable;
	emit dataChanged(index, index);
	qDebug() << "Changed item";
}

void DffModel::Optimize()
{
	fifo_data_.frames = FifoDataAnalyzer::OptimizeFifoData(fifo_data_);
	OnFifoDataChanged(fifo_data_);
}


DffView::DffView(QWidget* parent) : QTreeView(parent)
{
	setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void DffView::OnEnableSelection(int flags)
{
	QModelIndexList indexes = selectionModel()->selectedIndexes();

	for (QModelIndexList::iterator it = indexes.begin(); it != indexes.end(); ++it)
		EnableIndexRecursively(*it, (flags & CSF_ENABLE) != 0, (flags & CSF_GEOMETRY_AND_STATE) == 0);

	if (netqueue)
		netqueue->Flush();
}

void DffView::EnableIndexRecursively(const QModelIndex& index, bool enable, bool geometry_only)
{
	if (index.data(DffModel::UserRole_Type).toInt() != DffModel::IDX_COMMAND)
	{
		for (int i = 0; i < model()->rowCount(index); ++i)
			EnableIndexRecursively(index.child(i, 0), enable, geometry_only);

		return;
	}

	// Notify server on state change
	if (enable != index.data(DffModel::UserRole_IsEnabled).toBool() &&
		!(geometry_only && !index.data(DffModel::UserRole_IsGeometryCommand).toBool()))
	{
		u32 object_idx = index.data(DffModel::UserRole_ObjectIndex).toInt();
		u32 frame_idx = index.data(DffModel::UserRole_FrameIndex).toInt();
		WriteSetCommandEnabled(*client_socket, frame_idx, object_idx, index.data(DffModel::UserRole_CmdStart).toInt(), static_cast<int>(enable));

		// TODO: Should affect parent items when appropriate
		emit EnableEntry(index, enable);
	}
}

ServerWidget::ServerWidget() : QWidget()
{
	// dumb testing code for the bitfield class
/*	UPE_Copy copy;
	copy.Hex = 0x12345678;
#define PRINT_FIELD(name) qDebug() << copy.name << " " << copy._##name
	copy._target_pixel_format = 6;
	PRINT_FIELD(auto_conv);
	PRINT_FIELD(intensity_fmt);
	PRINT_FIELD(copy_to_xfb);
	PRINT_FIELD(frame_to_field);
	PRINT_FIELD(clear);
	PRINT_FIELD(scale_invert);
	PRINT_FIELD(half_scale);
	PRINT_FIELD(gamma);
	PRINT_FIELD(target_pixel_format);
	PRINT_FIELD(yuv);
	PRINT_FIELD(clamp1);
	PRINT_FIELD(clamp0);
	exit(0);*/

	client = new DffClient(this);

	hostname = new QLineEdit("127.0.0.1");
	QPushButton* try_connect = new QPushButton(tr("Connect"));

	connect(try_connect, SIGNAL(clicked()), this, SLOT(OnTryConnect()));

	// TODO: Change the lineedit text to be a default text?
	dffpath = new QLineEdit("");
	dffpath->setReadOnly(true);
	QPushButton* openDffFile = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon), "");
	QPushButton* loadDffFile = new QPushButton(tr("Load"));

	QProgressBar* progress_bar = new QProgressBar;
	connect(this, SIGNAL(ShowProgressBar()), progress_bar, SLOT(show()));
	connect(this, SIGNAL(HideProgressBar()), progress_bar, SLOT(hide()));
	connect(this, SIGNAL(SetProgressBarMax(int)), progress_bar, SLOT(setMaximum(int)));
	connect(this, SIGNAL(SetProgressBarValue(int)), progress_bar, SLOT(setValue(int)));
	progress_bar->hide();

	connect(openDffFile, SIGNAL(clicked()), this, SLOT(OnSelectDff()));
	connect(loadDffFile, SIGNAL(clicked()), this, SLOT(OnLoadDff()));

	DffView* dff_view = new DffView(this);
	DffModel* dff_model = new DffModel(this);
	dff_view->setModel(dff_model);

	connect(this, SIGNAL(FifoDataChanged(FifoData&)), dff_model, SLOT(OnFifoDataChanged(FifoData&)));

	command_description = new LayoutStream;

	connect(dff_view->selectionModel(), SIGNAL(currentChanged(const QModelIndex&,const QModelIndex&)),
			command_description, SLOT(ActiveItemChanged(const QModelIndex&)));

	QPushButton* expand_all_button = new QPushButton(tr("Expand All"));
	QPushButton* collapse_all_button = new QPushButton(tr("Collapse All"));

	connect(expand_all_button, SIGNAL(clicked()), dff_view, SLOT(expandAll()));
	connect(collapse_all_button, SIGNAL(clicked()), dff_view, SLOT(collapseAll()));

	// TODO: Add a "selection" frame around this
	QPushButton* enable_command_button = new QPushButton(tr("Enable All"));
	QPushButton* disable_command_button = new QPushButton(tr("Disable All"));
	QPushButton* enable_geometry_button = new QPushButton(tr("Enable Geometry"));
	QPushButton* disable_geometry_button = new QPushButton(tr("Disable Geometry"));

	QSignalMapper* dff_view_mapper = new QSignalMapper(this);
	connect(enable_command_button, SIGNAL(clicked()), dff_view_mapper, SLOT(map()));
	connect(disable_command_button, SIGNAL(clicked()), dff_view_mapper, SLOT(map()));
	connect(enable_geometry_button, SIGNAL(clicked()), dff_view_mapper, SLOT(map()));
	connect(disable_geometry_button, SIGNAL(clicked()), dff_view_mapper, SLOT(map()));
	dff_view_mapper->setMapping(enable_command_button, DffView::CSF_ENABLE|DffView::CSF_GEOMETRY_AND_STATE);
	dff_view_mapper->setMapping(disable_command_button, DffView::CSF_DISABLE|DffView::CSF_GEOMETRY_AND_STATE);
	dff_view_mapper->setMapping(enable_geometry_button, DffView::CSF_ENABLE|DffView::CSF_ONLY_GEOMETRY);
	dff_view_mapper->setMapping(disable_geometry_button, DffView::CSF_DISABLE|DffView::CSF_ONLY_GEOMETRY);
	connect(dff_view_mapper, SIGNAL(mapped(int)), dff_view, SLOT(OnEnableSelection(int)));
	connect(dff_view, SIGNAL(EnableEntry(const QModelIndex&,bool)), dff_model, SLOT(SetEntryEnabled(const QModelIndex&,bool)));

	QPushButton* optimize_fifostream_button = new QPushButton(tr("Optimize FIFO Stream"));
	connect(optimize_fifostream_button, SIGNAL(clicked()), dff_model, SLOT(Optimize()));

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
		main_layout->addWidget(progress_bar);
	}
	{
		QHBoxLayout* layout = new QHBoxLayout;
		layout->addWidget(expand_all_button);
		layout->addWidget(collapse_all_button);
		main_layout->addLayout(layout);
	}
	{
		main_layout->addWidget(dff_view);
	}
	{
		main_layout->addLayout(command_description);
	}
	{
		QHBoxLayout* layout = new QHBoxLayout;
		layout->addWidget(enable_command_button);
		layout->addWidget(disable_command_button);
		main_layout->addLayout(layout);
	}
	{
		QHBoxLayout* layout = new QHBoxLayout;
		layout->addWidget(enable_geometry_button);
		layout->addWidget(disable_geometry_button);
		main_layout->addLayout(layout);
	}
	{
		main_layout->addWidget(optimize_fifostream_button);
	}
	setLayout(main_layout);
}

// progress_callback takes a) the current progress as an arbitrary integer b) the progress value that corresponds to "completed task"
void WriteStreamDff(int socket, QString filename, std::function<void(int,int)> progress_callback)
{
	u8 cmd = CMD_STREAM_DFF;
	netqueue->PushCommand(&cmd, sizeof(cmd));

	QFile file(filename);
	int32_t size = file.size();
	int32_t n_size = htonl(size);
	netqueue->PushCommand((u8*)&n_size, sizeof(n_size));
	qDebug() << "About to send " << size << " bytes of dff data!" << n_size;

	file.open(QIODevice::ReadOnly);
	QDataStream stream(&file);

	for (int32_t remaining = size; remaining > 0; remaining -= dff_stream_chunk_size)
	{
		progress_callback(size-remaining, size);
		u8 data[dff_stream_chunk_size];
		stream.readRawData((char*)data, std::min(remaining,dff_stream_chunk_size));
		netqueue->PushCommand(data, std::min(remaining,dff_stream_chunk_size));

		qDebug() << remaining << " bytes left to be sent!";
	}
	netqueue->Flush();

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
	using namespace std::placeholders;

	emit ShowProgressBar();
	WriteStreamDff(client->socket, dffpath->text(), std::bind(&ServerWidget::OnSetProgress, this, _1, _2));
	emit HideProgressBar();
}

void ServerWidget::OnTryConnect()
{
	client->Connect(hostname->text());
}

void ServerWidget::OnSetProgress(int current, int max)
{
	emit SetProgressBarMax(max);
	emit SetProgressBarValue(current);
}

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QMainWindow win;
	win.setCentralWidget(new ServerWidget);
	win.show();

	return app.exec();
}
