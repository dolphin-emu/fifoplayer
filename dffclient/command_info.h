#include <QBoxLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <stdint.h>

#include <vector>

#include "../source/BitField.h"

typedef uint32_t u32;

class QString;
class QModelIndex;
class QByteArray;
class QStringList;

class LayoutStream : public QVBoxLayout
{
	Q_OBJECT

public:
	LayoutStream(QWidget* parent=NULL) : QVBoxLayout(parent), cur_hlayout(NULL)
	{
		endl();
	}

	LayoutStream& AddLabel(const QString& str);

	template<u32 bitfield_position, u32 bitfield_size>
	LayoutStream& AddSpinBox(BitField<bitfield_position, bitfield_size>& bitfield);

	template<u32 BFPos, u32 BFSize>
	LayoutStream& AddCheckBox(BitField<BFPos, BFSize>& bitfield, const QString& str="");

	template<u32 BFPos, u32 BFSize>
	LayoutStream& AddLineEdit(BitField<BFPos, BFSize>& bitfield);

	template<u32 BFPos, u32 BFSize>
	LayoutStream& AddComboBox(BitField<BFPos, BFSize>& bitfield, const QStringList& elements);

	LayoutStream& endl();

	void Clear()
	{
		cur_hlayout = NULL;
		ClearLayout(this);
		endl();
	}

public slots:
	void OnFifoDataChanged();
	void ActiveItemChanged(const QModelIndex& index);

private:
	static void ClearLayout(QLayout *layout);
	static void FifoDataChanged(u32 start, u32 size);

	QHBoxLayout* cur_hlayout;

	QByteArray cur_fifo_data; // Holds current frame's fifoData
	QModelIndex current_index;
	QAbstractItemModel* dff_model;
	u32 edit_offset;
	u32 edit_size;

/*
	u8 cmd;
	u8* data;*/
};

class LinkedSpinBox : public QSpinBox
{
	Q_OBJECT

public:
	LinkedSpinBox(const BitFieldWrapper& bitfield);

public slots:
	void OnValueChanged(int value);

signals:
	void ValueChanged(u32 val);

private:
	BitFieldWrapper bitfield;
};

class LinkedCheckBox : public QCheckBox
{
	Q_OBJECT

public:
	LinkedCheckBox(const QString& str, const BitFieldWrapper& bitfield);

public slots:
	void OnStateChanged(int state);

signals:
	void HexChanged(u32 val);

private:
	BitFieldWrapper bitfield;
};

class LinkedLineEdit : public QLineEdit
{
	Q_OBJECT

public:
	LinkedLineEdit(const BitFieldWrapper& bitfield);

public slots:
	void OnTextChanged(const QString& str);

signals:
	void HexChanged(u32 val);

private:
	BitFieldWrapper bitfield;
};

class LinkedComboBox : public QComboBox
{
	Q_OBJECT

public:
	LinkedComboBox(const BitFieldWrapper& bitfield, const QStringList& elements);

public slots:
	void OnCurrentIndexChanged(int index);

signals:
	void HexChanged(u32 val);

private:
	BitFieldWrapper bitfield;
};
