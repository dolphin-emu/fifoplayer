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
	LayoutStream& AddComboBox(BitField<BFPos, BFSize>& bitfield, const std::vector<QString>& elements);

	LayoutStream& endl();

	void Clear()
	{
		cur_hlayout = NULL;
		ClearLayout(this);
		endl();
	}

public slots:
	void OnCommandChanged(u32 data);
	void ActiveItemChanged(const QModelIndex& index);

private:
	static void ClearLayout(QLayout *layout);

	QHBoxLayout* cur_hlayout;

/*
	u8 cmd;
	u8* data;*/
};

class LinkedSpinBox : public QSpinBox
{
	Q_OBJECT

public:
	LinkedSpinBox(BitFieldWrapper bitfield);

public slots:
	void OnValueChanged(int value);

signals:
	void ValueChanged(u32 val);

private:
	BitFieldWrapper &bitfield;
};

class LinkedCheckBox : public QCheckBox
{
	Q_OBJECT

public:
	LinkedCheckBox(const QString& str, BitFieldWrapper bitfield);

public slots:
	void OnStateChanged(int state);

signals:
	void HexChanged(u32 val);

private:
	BitFieldWrapper &bitfield;
};

class LinkedLineEdit : public QLineEdit
{
	Q_OBJECT

public:
	LinkedLineEdit(BitFieldWrapper bitfield);

public slots:
	void OnTextChanged(const QString& str);

signals:
	void HexChanged(u32 val);

private:
	BitFieldWrapper &bitfield;
};

class LinkedComboBox : public QComboBox
{
	Q_OBJECT

public:
	LinkedComboBox(BitFieldWrapper bitfield, const std::vector<QString>& elements);

public slots:
	void OnCurrentIndexChanged(int index);

signals:
	void HexChanged(u32 val);

private:
	BitFieldWrapper &bitfield;
};
