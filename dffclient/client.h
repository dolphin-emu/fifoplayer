#include <QWidget>
#include <QAbstractItemModel>
#include <QTreeView>

#include <vector>

class QLineEdit;
class QModelIndex;
class QItemSelection;

//class AnalyzedFrameInfo;
#include "../source/FifoAnalyzer.h"
class FifoData;
class TreeItem;


class DffView : public QTreeView
{
	Q_OBJECT

public:
	DffView(QWidget* parent);

public slots:
	void OnEnableSelection(int enable);

signals:
	void EnableEntry(const QModelIndex& index, bool enable);

private:
	// Enables/Disables the command and all of its children
	void EnableIndexRecursively(const QModelIndex& index, bool enable);
};

class DffModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	enum {
		IDX_FRAME,
		IDX_OBJECT,
		IDX_COMMAND,
	};

	enum {
		UserRole_IsEnabled = Qt::UserRole,
		UserRole_Type,
		UserRole_FrameIndex,
		UserRole_ObjectIndex,
		UserRole_CommandIndex,
		UserRole_CmdStart,
	};

public:
	DffModel(QObject* parent = NULL);

	QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const;
	QModelIndex parent(const QModelIndex& index) const;
	int rowCount(const QModelIndex& parent = QModelIndex()) const;
	int columnCount(const QModelIndex& parent = QModelIndex()) const;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

public slots:
	void OnFifoDataChanged(FifoData& fifo_data);

	void SetEntryEnabled(const QModelIndex& index, bool enable);

private:
	std::vector<AnalyzedFrameInfo> analyzed_frames;
	TreeItem* root_item;
};

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

class ServerWidget : public QWidget
{
	Q_OBJECT

public:
	ServerWidget();

public slots:
	void OnTryConnect();

	void OnSelectDff();
	void OnLoadDff();

	void OnSetProgress(int current, int max);

signals:
	void FifoDataChanged(FifoData& fifo_data);

	void ShowProgressBar();
	void HideProgressBar();
	void SetProgressBarMax(int max);
	void SetProgressBarValue(int value);

private:
	DffClient* client;
	QLineEdit* hostname;
	QLineEdit* dffpath;
};
