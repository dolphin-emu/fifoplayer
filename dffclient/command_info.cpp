#include <QLabel>
#include <QSpinBox>
#include <QModelIndex>
#include <QItemSelectionModel>
#include "command_info.h"

#include "client.h" // just for DffModel::UserRoles...
#include <QDebug>

#include "../source/OpcodeDecoding.h"
#include "../source/BPMemory.h"

LinkedSpinBox::LinkedSpinBox(BitFieldWrapper bitfield) : QSpinBox(), bitfield(bitfield)
{
	setMinimum(0);
	setMaximum(bitfield.MaxVal());
	setValue(bitfield);

	connect(this, SIGNAL(valueChanged(int)), this, SLOT(OnValueChanged(int)));
}

void LinkedSpinBox::OnValueChanged(int value)
{
	bitfield = value;
	emit ValueChanged(bitfield.storage);
}

LinkedCheckBox::LinkedCheckBox(const QString& str, BitFieldWrapper bitfield) : QCheckBox(str), bitfield(bitfield)
{
	// TODO: Assert that bitfield.bits == 1, I guess....

	setCheckState((bitfield != 0) ? Qt::Checked : Qt::Unchecked);

	connect(this, SIGNAL(stateChanged(int)), this, SLOT(OnStateChanged(int)));
}

void LinkedCheckBox::OnStateChanged(int state)
{
	bitfield = (state == Qt::Checked) ? 1 : 0;
	emit HexChanged(bitfield.storage);
}

LinkedLineEdit::LinkedLineEdit(BitFieldWrapper bitfield) : QLineEdit(), bitfield(bitfield)
{
	setText(QString("%1").arg(bitfield, (bitfield.NumBits()+7)/8, 16, QLatin1Char('0')));

	connect(this, SIGNAL(textChanged(const QString&)), this, SLOT(OnTextChanged(const QString&)));
}

void LinkedLineEdit::OnTextChanged(const QString& str)
{
	bool ok = false;
	uint ret = str.toUInt(&ok, 16);
	if (ok == ret) {
		bitfield = ret;
		emit HexChanged(bitfield.storage);
	}
	else {
		qDebug() << "Failed to convert " << str << "to UInt!";
	}
}

LinkedComboBox::LinkedComboBox(BitFieldWrapper bitfield, const std::vector< QString >& elements) : QComboBox(), bitfield(bitfield)
{
	for (auto item : elements)
		addItem(item);

	connect(this, SIGNAL(currentIndexChanged(int)), this, SLOT(OnCurrentIndexChanged(int)));
}

void LinkedComboBox::OnCurrentIndexChanged(int index)
{
	bitfield = index;
	emit HexChanged(bitfield.storage);
}


template<u32 bitfield_position, u32 bitfield_size>
LayoutStream& LayoutStream::AddSpinBox(BitField<bitfield_position, bitfield_size>& bitfield)
{
	LinkedSpinBox* widget = new LinkedSpinBox(BitFieldWrapper(bitfield));

	connect(widget, SIGNAL(ValueChanged(u32)), this, SLOT(OnCommandChanged(u32)));

	cur_hlayout->addWidget(widget);
	return *this;
}

template<u32 BFPos, u32 BFSize>
LayoutStream& LayoutStream::AddCheckBox(BitField<BFPos, BFSize>& bitfield, const QString& str)
{
	LinkedCheckBox* widget = new LinkedCheckBox(str, BitFieldWrapper(bitfield));

	connect(widget, SIGNAL(HexChanged(u32)), this, SLOT(OnCommandChanged(u32)));

	cur_hlayout->addWidget(widget);
	return *this;
}

template<u32 BFPos, u32 BFSize>
LayoutStream& LayoutStream::AddLineEdit(BitField<BFPos, BFSize>& bitfield)
{
	LinkedLineEdit* widget = new LinkedLineEdit(BitFieldWrapper(bitfield));

	connect(widget, SIGNAL(HexChanged(u32)), this, SLOT(OnCommandChanged(u32)));

	cur_hlayout->addWidget(widget);
	return *this;
}

template<u32 BFPos, u32 BFSize>
LayoutStream& LayoutStream::AddComboBox(BitField<BFPos, BFSize>& bitfield, const std::vector<QString>& elements)
{
	LinkedComboBox* widget = new LinkedComboBox(BitFieldWrapper(bitfield), elements);

	connect(widget, SIGNAL(HexChanged(u32)), this, SLOT(OnCommandChanged(u32)));

	cur_hlayout->addWidget(widget);
	return *this;
}

LayoutStream& LayoutStream::AddLabel(const QString& str)
{
	cur_hlayout->addWidget(new QLabel(str));
	return *this;
}

LayoutStream& LayoutStream::endl()
{
	cur_hlayout = new QHBoxLayout;
	addLayout(cur_hlayout);
	return *this;
}

void LayoutStream::ActiveItemChanged(const QModelIndex& index)
{
	Clear();

	const QAbstractItemModel* sender = ((QItemSelectionModel*)QObject::sender())->model();
	if (sender->data(index, DffModel::UserRole_Type) != DffModel::IDX_COMMAND)
		return;

	u32 cmd_start = sender->data(index, DffModel::UserRole_CmdStart).toUInt();
	const u8* fifo_data = (const u8*)sender->data(index, DffModel::UserRole_FifoData).toByteArray().data();

	if (fifo_data[cmd_start] == GX_LOAD_BP_REG)
	{
		u32 cmddata = be32toh(*(u32*)&fifo_data[cmd_start+1]) & 0xFFFFFF; // TODO: NOT GOOD!!!!!! We actually want a reference directly to the fifo_data!!
		if (fifo_data[cmd_start+1] == BPMEM_TRIGGER_EFB_COPY)
		{
			UPE_Copy& copy = *(UPE_Copy*)&cmddata;

			AddLabel(tr("BPMEM_TRIGGER_EFB_COPY")).endl();
			AddLabel(tr("Clamping: ")).AddCheckBox(copy._clamp0, tr("Top")).AddCheckBox(copy._clamp1, tr("Bottom")).endl();
			AddLabel(tr("Convert from RGB to YUV: ")).AddCheckBox(copy._yuv).endl();
			AddLabel(tr("Target pixel format: ")).AddSpinBox(copy._target_pixel_format).endl(); // TODO
			AddLabel(tr("Gamma correction: ")).AddComboBox(copy._gamma, {"1.0","1.7","2.2","Inv." }).endl();
			AddLabel(tr("Vertical scaling: ")).AddCheckBox(copy._half_scale).endl();
			AddLabel(tr("Downscale: ")).AddCheckBox(copy._half_scale).endl();
			AddLabel(tr("Vertical scaling: ")).AddCheckBox(copy._scale_invert).endl();
			AddLabel(tr("Clear: ")).AddCheckBox(copy._clear).endl();
			AddLabel(tr("Copy to XFB: ")).AddCheckBox(copy._frame_to_field).endl();
			AddLabel(tr("Copy as intensity: ")).AddCheckBox(copy._intensity_fmt).endl();
			AddLabel(tr("Automatic color conversion: ")).AddCheckBox(copy._auto_conv).endl();
		}
	}
}

void LayoutStream::OnCommandChanged(u32 data)
{
//	printf("Command changed: %02x: %02x %02x %02x %02x %02x", cmd, data[0], data[1], data[2], data[3], data[4]);
	printf("data changed: %08x\n", data);
}

void LayoutStream::ClearLayout(QLayout *layout)
{
	QLayoutItem *item;
	while((item = layout->takeAt(0))) {
		// Making sure not to delete things multiple times
		QLayout* layout = item->layout();
		QWidget* widget = item->widget();
		if (layout) {
			ClearLayout(layout);
			delete layout;
		}
		if (widget && (QLayoutItem*)widget != (QLayoutItem*)layout)
			delete widget;

		if (item != (QLayoutItem*)widget && item != (QLayoutItem*)layout)
			delete item;
	}
}
