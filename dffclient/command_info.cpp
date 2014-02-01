#include <QLabel>
#include <QSpinBox>
#include <QModelIndex>
#include <QItemSelectionModel>
#include "command_info.h"

#include "client.h" // just for DffModel::UserRoles...
#include <QDebug>

#include "../source/OpcodeDecoding.h"
#include "../source/BPMemory.h"

LinkedSpinBox::LinkedSpinBox(const BitFieldWrapper& bitfield) : QSpinBox(), bitfield(bitfield)
{
	setMinimum(0);
	setMaximum(bitfield.MaxVal());
	setValue(bitfield);

	connect(this, SIGNAL(valueChanged(int)), this, SLOT(OnValueChanged(int)));
}

void LinkedSpinBox::OnValueChanged(int value)
{
	bitfield = value;
	emit ValueChanged(bitfield.RawValue());
}

LinkedCheckBox::LinkedCheckBox(const QString& str, const BitFieldWrapper& bitfield) : QCheckBox(str), bitfield(bitfield)
{
//	assert(bitfield.NumBits() == 1);//TODO: Enable!

	setCheckState((bitfield != 0) ? Qt::Checked : Qt::Unchecked);

	connect(this, SIGNAL(stateChanged(int)), this, SLOT(OnStateChanged(int)));
}

void LinkedCheckBox::OnStateChanged(int state)
{
	bitfield = (state == Qt::Checked) ? 1 : 0;
	emit HexChanged(bitfield.RawValue());
}

LinkedLineEdit::LinkedLineEdit(const BitFieldWrapper& bitfield) : QLineEdit(), bitfield(bitfield)
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
		emit HexChanged(bitfield.RawValue());
	}
	else {
		qDebug() << "Failed to convert " << str << "to UInt!";
	}
}

LinkedComboBox::LinkedComboBox(const BitFieldWrapper& bitfield, const QStringList& elements) : QComboBox(), bitfield(bitfield)
{
	// assert(elements.size() == bitfield.MaxVal()); TODO!

	addItems(elements);

	setCurrentIndex(bitfield);

	connect(this, SIGNAL(currentIndexChanged(int)), this, SLOT(OnCurrentIndexChanged(int)));
}

void LinkedComboBox::OnCurrentIndexChanged(int index)
{
	bitfield = index;
	emit HexChanged(bitfield.RawValue());
}


template<u32 bitfield_position, u32 bitfield_size>
LayoutStream& LayoutStream::AddSpinBox(BitField<bitfield_position, bitfield_size>& bitfield)
{
	LinkedSpinBox* widget = new LinkedSpinBox(BitFieldWrapper(bitfield));

	connect(widget, SIGNAL(ValueChanged(u32)), this, SLOT(OnFifoDataChanged()));

	cur_hlayout->addWidget(widget);
	return *this;
}

template<u32 BFPos, u32 BFSize>
LayoutStream& LayoutStream::AddCheckBox(BitField<BFPos, BFSize>& bitfield, const QString& str)
{
	LinkedCheckBox* widget = new LinkedCheckBox(str, BitFieldWrapper(bitfield));

	connect(widget, SIGNAL(HexChanged(u32)), this, SLOT(OnFifoDataChanged()));

	cur_hlayout->addWidget(widget);
	return *this;
}

template<u32 BFPos, u32 BFSize>
LayoutStream& LayoutStream::AddLineEdit(BitField<BFPos, BFSize>& bitfield)
{
	LinkedLineEdit* widget = new LinkedLineEdit(BitFieldWrapper(bitfield));

	connect(widget, SIGNAL(HexChanged(u32)), this, SLOT(OnFifoDataChanged()));

	cur_hlayout->addWidget(widget);
	return *this;
}

template<u32 BFPos, u32 BFSize>
LayoutStream& LayoutStream::AddComboBox(BitField<BFPos, BFSize>& bitfield, const QStringList& elements)
{
	LinkedComboBox* widget = new LinkedComboBox(BitFieldWrapper(bitfield), elements);

	connect(widget, SIGNAL(HexChanged(u32)), this, SLOT(OnFifoDataChanged()));

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

	current_index = index;
	dff_model = (QAbstractItemModel*)sender; // TODO: not const-correct...

	u32 cmd_start = sender->data(index, DffModel::UserRole_CmdStart).toUInt();
	cur_fifo_data.clear();
	cur_fifo_data.append(sender->data(index, DffModel::UserRole_FifoData).toByteArray()); // TODO: Only retrieve this on dataChanged()...
	u8* fifo_data = (u8*)cur_fifo_data.data();

	if (fifo_data[cmd_start] == GX_LOAD_BP_REG)
	{
		u32& cmddata = (*(u32*)&fifo_data[cmd_start+1]);
		edit_offset = cmd_start+1;
		edit_size = sizeof(u32);

#define GET(type, name) type& name = *(type*)&cmddata

		if (fifo_data[cmd_start+1] == BPMEM_GENMODE) // 0x00
		{
			GET(GenMode, mode);

			AddLabel(tr("Gen mode")).endl();
			AddLabel(tr("numtexgens: ")).AddSpinBox(mode.numtexgens).endl();
			AddLabel(tr("numcolchans: ")).AddSpinBox(mode.numcolchans).endl();
			AddCheckBox(mode.ms_en, tr("Enable multisampling")).endl();
			AddLabel(tr("numtexstages: ")).AddSpinBox(mode.numtexstages).endl();
			AddLabel(tr("Culling: ")).AddComboBox(mode.cullmode, {tr("Disabled"), tr("Cull front facing"), tr("Cull back facing"), tr("Cull everything") }).endl();
			AddLabel(tr("numindstages: ")).AddSpinBox(mode.numindstages).endl();
			AddCheckBox(mode.zfreeze, tr("Enable zfreeze")).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_SCISSORTL) // 0x20
		{
			GET(X12Y12, coord); 
			AddLabel(tr("Scissor rectangle")).endl();
			AddLabel(tr("Left coordinate:")).AddSpinBox(coord.x).endl();
			AddLabel(tr("Top coordinate:")).AddSpinBox(coord.y).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_SCISSORBR) // 0x21
		{
			GET(X12Y12, coord); 
			AddLabel(tr("Scissor rectangle")).endl();
			AddLabel(tr("Right coordinate:")).AddSpinBox(coord.x).endl();
			AddLabel(tr("Bottom coordinate:")).AddSpinBox(coord.y).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_ZMODE) // 0x40
		{
			ZMode& zmode = *(ZMode*)&cmddata;

			AddLabel(tr("BPMEM_ZMODE")).endl();
			AddCheckBox(zmode.testenable, tr("Enable depth testing")).endl();
			AddLabel(tr("Depth test function: ")).AddComboBox(zmode.func, {tr("Never"), tr("Less"), tr("Equal"),
							tr("Less or equal"), tr("Greater"), tr("Not equal"),
							tr("Greater or equal"), tr("Always")}).endl();
			AddCheckBox(zmode.updateenable, tr("Enable depth writing")).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_BLENDMODE) // 0x41
		{
			BlendMode& mode = *(BlendMode*)&cmddata;

			QStringList dstfactors = {
				tr("Zero"), tr("One"), tr("SRCCOL"), tr("1-SRCCOL"),
				tr("SRCALP"), tr("1-SRCALP"), tr("DSTALP"), tr("1-DSTALP")
			};
			QStringList srcfactors = {
				tr("Zero"), tr("One"), tr("DSTCOL"), tr("1-DSTCOL"),
				tr("SRCALP"), tr("1-SRCALP"), tr("DSTALP"), tr("1-DSTALP")
			};
			QStringList logicmodes = {
				"0", "s & d", "s & ~d", "s",
				"~s & d", "d", "s ^ d", "s | d",
				"~(s | d)", "~(s ^ d)", "~d", "s | ~d",
				"~s", "~s | d", "~(s & d)", "1"
			};

			AddLabel(tr("BPMEM_BLENDMODE")).endl();
			AddCheckBox(mode.blendenable, tr("Enable blending")).endl();
			AddComboBox(mode.dstfactor, dstfactors).endl();
			AddComboBox(mode.srcfactor, srcfactors).endl();
			AddCheckBox(mode.subtract, tr("Subtract")).endl();
			AddCheckBox(mode.logicopenable, tr("Enable logical operations")).endl();
			AddLabel(tr("Logic operation: ")).AddComboBox(mode.logicmode, logicmodes).endl();
			AddCheckBox(mode.colorupdate, tr("Enable color writing")).endl();
			AddCheckBox(mode.alphaupdate, tr("Enable alpha writing")).endl();
			AddCheckBox(mode.dither, tr("Enable dithering")).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_ZCOMPARE) // 0x43
		{
			GET(PE_CONTROL, ctrl);

			AddLabel(tr("Pixel Engine control")).endl();
			AddLabel(tr("EFB pixel format: ")).AddComboBox(ctrl.pixel_format, {"RGB8 Z24", "RGBA6 Z24", "RGB565 Z16", "Z24", "Y8", "U8", "V8", "YUV420"}).endl();
			AddLabel(tr("EFB depth format: ")).AddComboBox(ctrl.zformat, {tr("Linear"), tr("Near"), tr("Mid"), tr("Far"), tr("Inverse near"), tr("Inverse mid"), tr("Inverse far")}).endl();
			AddCheckBox(ctrl.early_ztest, tr("Early depth test")).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_TRIGGER_EFB_COPY) // 0x52
		{
			UPE_Copy& copy = *(UPE_Copy*)&cmddata;

			AddLabel(tr("BPMEM_TRIGGER_EFB_COPY")).endl();
			AddLabel(tr("Clamping: ")).AddCheckBox(copy.clamp0, tr("Top")).AddCheckBox(copy.clamp1, tr("Bottom")).endl();
			AddLabel(tr("Convert from RGB to YUV: ")).AddCheckBox(copy.yuv).endl();
			AddLabel(tr("Target pixel format: ")).AddComboBox(copy.target_pixel_format,
											{"Z4/I4/R4", "Z8/I8/R8", "IA4/RA4", "Z16/IA8/RA8",
											"RGB565", "RGB5A3", "Z24X8/RGBA8", "A8",
											"Z8/I8/R8", "Z8M/G8", "Z8L/B8"", Z16Rev/RG8",
											"Z16L/GB8", "", "", "" }).endl();
			AddLabel(tr("Gamma correction: ")).AddComboBox(copy.gamma, {"1.0","1.7","2.2","Inv." }).endl();
			AddLabel(tr("Downscale: ")).AddCheckBox(copy.half_scale).endl();
			AddLabel(tr("Vertical scaling: ")).AddCheckBox(copy.scale_invert).endl();
			AddLabel(tr("Clear: ")).AddCheckBox(copy.clear).endl();
			AddLabel(tr("Copy to XFB: ")).AddCheckBox(copy.frame_to_field).endl();
			AddLabel(tr("Copy as intensity: ")).AddCheckBox(copy.intensity_fmt).endl();
			AddLabel(tr("Automatic color conversion: ")).AddCheckBox(copy.auto_conv).endl();
		}
		else if (fifo_data[cmd_start+1] >= BPMEM_TEV_COLOR_ENV &&
				fifo_data[cmd_start+1] <= BPMEM_TEV_ALPHA_ENV + 2*15) // 0xC0 - 0xDF
		{
			int stage = (fifo_data[cmd_start+1] - BPMEM_TEV_COLOR_ENV) / 2;

			QStringList bias_values = { tr("0"), tr("+0.5"), tr("-0.5"), tr("Use compare function") };
			QStringList shift_values = { "1.0", "2.0", "4.0", "0.5" };
			QStringList outputs = { "PREV", "REG0", "REG1", "REG2"};
			QStringList operations = { tr("Add"), tr("Subtract")};
			QStringList compare_functions = {"Greater", "Equal"};

			if ((fifo_data[cmd_start+1] & 1) == 0)
			{
				GET(TevStageCombiner::ColorCombiner, comb);

				QStringList inputs = {
					tr("PREV color"), tr("PREV alpha"), tr("REG0 color"), tr("REG0 alpha"),
					tr("REG1 color"), tr("REG1 alpha"), tr("REG2 color"), tr("REG2 alpha"),
					tr("Texture color"), tr("Texture alpha"), tr("Rasterized color"), tr("Rasterized alpha"),
					tr("1"), tr("0.5"), tr("Konst color"), tr("0")
				};

				AddLabel(tr("Color combiner for Tev stage %1").arg(stage)).endl();
				AddLabel(tr("Input A: ")).AddComboBox(comb.a, inputs).endl();
				AddLabel(tr("Input B: ")).AddComboBox(comb.b, inputs).endl();
				AddLabel(tr("Input C: ")).AddComboBox(comb.c, inputs).endl();
				AddLabel(tr("Input D: ")).AddComboBox(comb.d, inputs).endl();

				AddLabel(tr("Bias: ")).AddComboBox(comb.bias, bias_values).endl();
				AddLabel(tr("Operation: ")).AddComboBox(comb.op, operations).endl();
				AddCheckBox(comb.clamp, tr("Clamp")).endl();
				AddLabel(tr("Scale: ")).AddComboBox(comb.shift, shift_values).endl();

				AddLabel(tr("Destination: ")).AddComboBox(comb.dest, outputs).endl();

				// TODO: Changing these does not update the upper ones!!
				AddLabel(tr("Compare operation: ")).AddComboBox(comb.op, compare_functions).endl();
				AddLabel(tr("Compared components: ")).AddComboBox(comb.shift, {"R", "GR", "BGR", "RGB per-channel"}).endl();
			}
			else
			{
				GET(TevStageCombiner::AlphaCombiner, comb);

				QStringList inputs = {
					tr("PREV"), tr("REG0"), tr("REG1"), tr("REG2"),
					tr("Texture"), tr("Rasterizer"), tr("Konst"), tr("0")
				};

				AddLabel(tr("Alpha combiner for Tev stage %1").arg(stage)).endl();
				AddLabel(tr("Input A: ")).AddComboBox(comb.a, inputs).endl();
				AddLabel(tr("Input B: ")).AddComboBox(comb.b, inputs).endl();
				AddLabel(tr("Input C: ")).AddComboBox(comb.c, inputs).endl();
				AddLabel(tr("Input D: ")).AddComboBox(comb.d, inputs).endl();

				AddLabel(tr("Bias: ")).AddComboBox(comb.bias, bias_values).endl();
				AddLabel(tr("Operation: ")).AddComboBox(comb.op, operations).endl();
				AddCheckBox(comb.clamp, tr("Clamp")).endl();
				AddLabel(tr("Scale: ")).AddComboBox(comb.shift, shift_values).endl();

				AddLabel(tr("Destination: ")).AddComboBox(comb.dest, outputs).endl();

				// TODO: Changing these does not update the upper ones!!
				AddLabel(tr("Compare operation: ")).AddComboBox(comb.op, compare_functions).endl();
				AddLabel(tr("Compared components: ")).AddComboBox(comb.shift, {"R", "GR", "BGR", "A"}).endl();

				AddLabel(tr("Rasterized input swap: ")).AddSpinBox(comb.rswap).endl();
				AddLabel(tr("Texture input swap: ")).AddSpinBox(comb.tswap).endl();
			}
		}
		else if (fifo_data[cmd_start+1] == BPMEM_FOGRANGE) // 0xE8
		{
			GET(FogRangeParams::RangeBase, range);

			AddLabel(tr("Fog range adjustment")).endl();
			AddCheckBox(range.Enabled, tr("Enable")).endl();
			AddLabel(tr("Center: ")).AddSpinBox(range.Center).endl();
		}
		else if (fifo_data[cmd_start+1] >= BPMEM_FOGRANGE + 1 &&
				fifo_data[cmd_start+1] <= BPMEM_FOGRANGE + 5) // 0xE9 - 0xED
		{
			GET(FogRangeKElement, range);

			AddLabel(tr("Fog range adjustment factor group %1").arg(fifo_data[cmd_start+1]-BPMEM_FOGRANGE)).endl();
			AddLabel(tr("Factor HI: ")).AddSpinBox(range.HI).endl();
			AddLabel(tr("Factor LO: ")).AddSpinBox(range.LO).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_FOGPARAM0) // 0xEE
		{
			GET(FogParam0, fog);

			AddLabel(tr("Fog parameter A")).endl();
			AddLabel(tr("Mantissa: ")).AddSpinBox(fog.mantissa).endl();
			AddLabel(tr("Exponent: ")).AddSpinBox(fog.exponent).endl();
			AddLabel(tr("Sign: ")).AddComboBox(fog.sign, {tr("Positive"), tr("Negative")}).endl();

			// TODO: _additionally_ add an input field for directly entering the floating point value!
		}
		else if (fifo_data[cmd_start+1] == BPMEM_FOGBMAGNITUDE) // 0xEF
		{
			AddLabel(tr("Fog parameter B")).endl();
			// TODO!
		}
		else if (fifo_data[cmd_start+1] == BPMEM_FOGBEXPONENT) // 0xF0
		{
			AddLabel(tr("Fog parameter B")).endl();
			// TODO!
		}
		else if (fifo_data[cmd_start+1] == BPMEM_FOGPARAM3) // 0xF1
		{
			GET(FogParam3, fog);

			AddLabel(tr("Fog configuration and fog parameter C")).endl();
			AddLabel(tr("Mantissa: ")).AddSpinBox(fog.c_mant).endl();
			AddLabel(tr("Exponent: ")).AddSpinBox(fog.c_exp).endl();
			AddLabel(tr("Sign: ")).AddComboBox(fog.c_sign, {tr("Positive"), tr("Negative")}).endl();
			AddLabel(tr("Projection mode: ")).AddComboBox(fog.c_sign, {tr("Perspective"), tr("Orthographic")}).endl();
			AddLabel(tr("Fog mode: ")).AddComboBox(fog.c_sign, {tr("Disabled"), tr("Unknown"), tr("Linear"), tr("Unknown"),
											tr("Exponential"), tr("Exponential squared"), tr("Inverse exponential"),
											tr("Inverse exponential squared")}).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_FOGCOLOR) // 0xF2
		{
			GET(FogParams::FogColor, color);

			AddLabel(tr("Fog color")).endl();
			AddLabel(tr("Red:")).AddSpinBox(color.r).endl();
			AddLabel(tr("Green:")).AddSpinBox(color.g).endl();
			AddLabel(tr("Blue:")).AddSpinBox(color.b).endl();
		}
		else if (fifo_data[cmd_start+1] == BPMEM_ALPHACOMPARE) // 0xF3
		{
			GET(AlphaTest, test);

			QStringList comp_funcs = {
				tr("Never"), tr("Less"), tr("Equal"), tr("Less or equal"),
				tr("Greater"), tr("Not equal"), tr("Greater or equal"), tr("Always"),
			};

			QStringList alpha_ops = {
				tr("and"), tr("or"), tr("xor"), tr("equal"),
			};

			AddLabel(tr("Alpha test")).endl();
			AddLabel(tr("First reference value: ")).AddSpinBox(test.ref0).endl();
			AddLabel(tr("First comparison function: ")).AddComboBox(test.comp0, comp_funcs).endl();
			AddLabel(tr("Second reference value: ")).AddSpinBox(test.ref1).endl();
			AddLabel(tr("Second comparison function: ")).AddComboBox(test.comp1, comp_funcs).endl();
			AddLabel(tr("Combining function: ")).AddComboBox(test.logic, alpha_ops).endl();
		}
	}
}

void LayoutStream::OnFifoDataChanged()
{
//	printf("data changed: %08x\n", data);
	int cmd_start = dff_model->data(current_index, DffModel::UserRole_CmdStart).toInt();
	QByteArray data = cur_fifo_data.mid(cmd_start, edit_size+(edit_offset-cmd_start));
	dff_model->setData(current_index, QVariant(data), DffModel::UserRole_FifoDataForCommand);
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
