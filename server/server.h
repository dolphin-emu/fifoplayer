#include <QWidget>
#include <QAbstractItemModel>

#include <vector>

class QLineEdit;
class QModelIndex;
class QItemSelection;

//class AnalyzedFrameInfo;
#include "../source/FifoAnalyzer.h"
class FifoData;
class TreeItem;
class DffModel : public QAbstractItemModel
{
	Q_OBJECT

	enum {
		IDX_FRAME,
		IDX_OBJECT,
		IDX_COMMAND,
	};

public:
	DffModel(QObject* parent = NULL);

	QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const;
	QModelIndex parent(const QModelIndex& index) const;
	int rowCount(const QModelIndex& parent = QModelIndex()) const;
	int columnCount(const QModelIndex& parent = QModelIndex()) const;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;

	void SetSelectionEnabled(bool enable);

public slots:
	void OnFifoDataChanged(FifoData& fifo_data);

	void OnEnableSelected();
	void OnDisableSelected();

	void OnSelectionChanged(const QItemSelection& selected);

private:
	std::vector<AnalyzedFrameInfo> analyzed_frames;
	TreeItem* root_item;
	QModelIndexList selection;
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

signals:
	void FifoDataChanged(FifoData& fifo_data);

private:
	DffClient* client;
	QLineEdit* hostname;
	QLineEdit* dffpath;
};
