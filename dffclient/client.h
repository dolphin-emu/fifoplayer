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
	enum ChangeSelectionFlag
	{
		CSF_DISABLE = 0,
		CSF_ENABLE = 1,

		CSF_ONLY_GEOMETRY = 0,
		CSF_GEOMETRY_AND_STATE = 2,
	};

	DffView(QWidget* parent);

public slots:
	void OnEnableSelection(int flags); // ChangeSelectionFlag

signals:
	void EnableEntry(const QModelIndex& index, bool enable);

private:
	// Enables/Disables the command and all of its children
	void EnableIndexRecursively(const QModelIndex& index, bool enable, bool geometry_only);
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
		UserRole_IsGeometryCommand,
		UserRole_FifoData,
		UserRole_FifoDataForCommand,
	};

public:
	DffModel(QObject* parent = NULL);

	QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const;
	QModelIndex parent(const QModelIndex& index) const;
	int rowCount(const QModelIndex& parent = QModelIndex()) const;
	int columnCount(const QModelIndex& parent = QModelIndex()) const;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

	bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole);
	Qt::ItemFlags flags(const QModelIndex& index) const;

public slots:
	void OnFifoDataChanged(FifoData& fifo_data);

	void SetEntryEnabled(const QModelIndex& index, bool enable);

	void Optimize();

private:
	std::vector<AnalyzedFrameInfo> analyzed_frames;
	TreeItem* root_item;
	FifoData fifo_data_; // TODO: Shouldn't be required :/
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
