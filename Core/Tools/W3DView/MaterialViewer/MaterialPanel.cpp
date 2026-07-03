/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// TheSuperHackers @feature W3DView material viewer Qt panel.
// Read-only replica of the 3ds Max W3D material editor layout (max2w3d plugin):
// Surface Type / Pass Count rollups, then one rollup per pass with Vertex
// Material / Shader / Textures tabs. Fed from raw chunk data (W3dMaterialData.h).
//
// This translation unit is compiled into the core_w3dview_qtpanel static
// library: no MFC, no WW3D, no tool PCH. Everything stays moc-free (no
// Q_OBJECT); the one interactive control connects through a lambda.

#include "MaterialPanel.h"

#include <QtGui/QClipboard>
#include <QtGui/QImage>
#include <QtGui/QMouseEvent>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <dwmapi.h>
#pragma comment(lib, "dwmapi")

#include <functional>

namespace W3dMaterialViewer
{

namespace
{

// Notifies the host (MFC frame) when the mesh combo selection changes.
void (*g_MeshSelectedCallback)(const char *) = nullptr;

// Edit-mode host callbacks (see MaterialPanel.h).
bool (*g_EditGateCallback)() = nullptr;
bool (*g_SaveCallback)(const MaterialDocument &) = nullptr;
void (*g_RevertCallback)() = nullptr;
void (*g_DirtyChangedCallback)(bool) = nullptr;

// Tracks the active theme so popups opened from the panel (the swatch color
// dialog) can request a dark titlebar via DWM.
bool g_DarkTheme = false;

void Apply_Dark_Title_Bar(HWND hwnd)
{
	if (!g_DarkTheme || hwnd == nullptr) {
		return;
	}
	BOOL dark = TRUE;
	// 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (19 on pre-20H1 builds).
	if (FAILED(::DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
		::DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
	}
}

//////////////////////////////////////////////////////////////////////////////
//	Option label tables
//////////////////////////////////////////////////////////////////////////////

// SURFACE_TYPE_STRINGS order from w3d_file.h — the authority for this engine.
const char *const SURFACE_TYPES[] = {
	"Light Metal", "Heavy Metal", "Water", "Sand", "Dirt", "Mud", "Grass",
	"Wood", "Concrete", "Flesh", "Rock", "Snow", "Ice", "Default", "Glass",
	"Cloth", "Tiberium Field", "Foliage Permeable", "Glass Permeable",
	"Ice Permeable", "Cloth Permeable", "Electrical", "Flammable", "Steam",
	"Electrical Permeable", "Flammable Permeable", "Steam Permeable",
	"Water Permeable", "Tiberium Water", "Tiberium Water Permeable",
	"Underwater Dirt", "Underwater Tiberium Dirt",
};

// W3DVERTMAT_STAGEn_MAPPING_* order; labels match the Max plugin dropdown.
const char *const MAPPING_TYPES[] = {
	"UV", "Environment", "Classic Environment", "Screen", "Linear Offset",
	"Silhouette", "Scale", "Grid", "Rotate", "Sine", "Step", "ZigZag",
	"WS Classic Env", "WS Environment", "Grid Classic Env", "Grid Environment",
	"Random", "Edge", "Bump Env", "Grid WS Classic Env", "Grid WS Env",
};

const char *const DEPTH_COMPARE[] = {
	"Pass Never", "Pass Less", "Pass Equal", "Pass LEqual",
	"Pass Greater", "Pass NEqual", "Pass GEqual", "Pass Always",
};

const char *const SRC_BLEND[] = {
	"Zero", "One", "Src Alpha", "One Minus Src Alpha",
};

const char *const DEST_BLEND[] = {
	"Zero", "One", "Src Colour", "One Minus Src Colour",
	"Src Alpha", "One Minus Src Alpha", "Src Colour Pre-Fog",
};

const char *const PRI_GRADIENT[] = {
	"Disable", "Modulate", "Add", "Bump-Environment", "Bump-Env Luminance", "Modulate 2X",
};

const char *const SEC_GRADIENT[] = { "Disabled", "Enabled" };

const char *const DETAIL_COLOR[] = {
	"Disable", "Detail", "Scale", "InvScale", "Add", "Sub", "SubR", "Blend",
	"DetailBlend", "AddSigned", "AddSigned2X", "Scale2X", "ModAlphaAddClr",
};

const char *const DETAIL_ALPHA[] = { "Disable", "Detail", "Scale", "InvScale" };

const char *const BLEND_MODES[] = {
	"Opaque", "Add", "Multiply", "Multiply and Add", "Screen",
	"Alpha Blend", "Alpha Test", "Alpha Test and Blend", "Custom",
};

const char *const ANIM_MODES[] = { "Loop", "Ping-Pong", "Once", "Manual" };

const char *const PASS_HINTS[] = {
	"Base Texture", "Emissive Light Map", "Environment Map", "Shinyness Map",
};

// W3DTEXTURE_* attribute bits (w3d_file.h; duplicated here to stay WW3D-free).
const uint16_t TEX_PUBLISH = 0x0001;
const uint16_t TEX_RESIZE_OBSOLETE = 0x0002;
const uint16_t TEX_NO_LOD = 0x0004;
const uint16_t TEX_CLAMP_U = 0x0008;
const uint16_t TEX_CLAMP_V = 0x0010;
const uint16_t TEX_ALPHA_BITMAP = 0x0020;
const uint16_t TEX_HINT_MASK = 0xff00;
const int TEX_HINT_SHIFT = 8;

// W3DVERTMAT_* bits.
const uint32_t VERTMAT_COPY_SPECULAR_TO_DIFFUSE = 0x00000004;
const uint32_t VERTMAT_STAGE0_MAPPING_MASK = 0x00FF0000;
const int VERTMAT_STAGE0_MAPPING_SHIFT = 16;
const uint32_t VERTMAT_STAGE1_MAPPING_MASK = 0x0000FF00;
const int VERTMAT_STAGE1_MAPPING_SHIFT = 8;

template<int N>
QString Label_For(const char *const (&table)[N], int index)
{
	if (index >= 0 && index < N) {
		return QString::fromLatin1(table[index]);
	}
	return QStringLiteral("Unknown (%1)").arg(index);
}

// Derives the Max "Blend Mode" preset from (src, dest, alphaTest); index into
// BLEND_MODES, 8 = Custom. Reverse of the max2w3d preset table.
int Derive_Blend_Mode(int src, int dest, int alpha_test)
{
	struct { int src, dest, alphaTest; } const presets[] = {
		{ 1, 0, 0 },	// Opaque:              One / Zero
		{ 1, 1, 0 },	// Add:                 One / One
		{ 0, 2, 0 },	// Multiply:            Zero / Src Colour
		{ 1, 2, 0 },	// Multiply and Add:    One / Src Colour
		{ 1, 3, 0 },	// Screen:              One / One Minus Src Colour
		{ 2, 5, 0 },	// Alpha Blend:         Src Alpha / One Minus Src Alpha
		{ 1, 0, 1 },	// Alpha Test:          One / Zero + test
		{ 2, 5, 1 },	// Alpha Test and Blend
	};
	for (int i = 0; i < 8; i++) {
		if (presets[i].src == src && presets[i].dest == dest && presets[i].alphaTest == (alpha_test ? 1 : 0)) {
			return i;
		}
	}
	return 8;	// Custom
}

//////////////////////////////////////////////////////////////////////////////
//	Control factory helpers
//////////////////////////////////////////////////////////////////////////////
//
// Each factory takes an optional change callback. When it is null the control
// is built read-only (View mode, unchanged look). When it is set the control
// is interactive (Edit mode) and reports edits through the callback, which
// writes the new value into the in-memory document and marks it dirty.

// Blocks interaction while keeping the normal (non-grayed) look.
void Make_Read_Only(QWidget *widget)
{
	widget->setFocusPolicy(Qt::NoFocus);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
}

// Passed by value into the change lambdas; carried through every builder.
using ChangeFn = std::function<void()>;

// `on_change(index)` is called with the selected combo index in Edit mode.
template<int N>
QComboBox *Make_Combo(const char *const (&table)[N], int index,
	std::function<void(int)> on_change = nullptr)
{
	QComboBox *combo = new QComboBox;
	for (int i = 0; i < N; i++) {
		combo->addItem(QString::fromLatin1(table[i]));
	}
	if (index >= 0 && index < N) {
		combo->setCurrentIndex(index);
	} else {
		combo->addItem(QStringLiteral("Unknown (%1)").arg(index));
		combo->setCurrentIndex(N);
	}
	if (on_change) {
		QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
			[on_change](int value) { on_change(value); });
	} else {
		Make_Read_Only(combo);
	}
	return combo;
}

QCheckBox *Make_Check(const char *label, bool checked,
	std::function<void(bool)> on_change = nullptr)
{
	QCheckBox *check = new QCheckBox(QString::fromLatin1(label));
	check->setChecked(checked);
	if (on_change) {
		QObject::connect(check, &QCheckBox::toggled,
			[on_change](bool value) { on_change(value); });
	} else {
		Make_Read_Only(check);
	}
	return check;
}

QDoubleSpinBox *Make_Float_Spin(double value, double min_value, double max_value,
	int decimals = 3, std::function<void(double)> on_change = nullptr)
{
	QDoubleSpinBox *spin = new QDoubleSpinBox;
	spin->setRange(min_value, max_value);
	spin->setDecimals(decimals);
	spin->setValue(value);
	if (on_change) {
		QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			[on_change](double v) { on_change(v); });
	} else {
		Make_Read_Only(spin);
	}
	return spin;
}

QSpinBox *Make_Int_Spin(int value, int min_value, int max_value,
	std::function<void(int)> on_change = nullptr)
{
	QSpinBox *spin = new QSpinBox;
	spin->setRange(min_value, max_value);
	spin->setValue(value);
	if (on_change) {
		QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
			[on_change](int v) { on_change(v); });
	} else {
		Make_Read_Only(spin);
	}
	return spin;
}

// Small color swatch in the Max style: a flat filled rectangle. Clicking it
// opens a (non-native, theme-following) color dialog.
// View mode: the dialog has no OK/Cancel and nothing is written back (inspect
// / copy only). Edit mode: OK/Cancel; on accept the new color is stored, the
// swatch and its sibling label repaint, and on_change reports the RGB.
class ColorSwatchWidget : public QFrame
{
public:
	ColorSwatchWidget(const QColor &color, std::function<void(const QColor &)> on_change)
		: m_Color(color), m_OnChange(std::move(on_change)) {}

	void Set_Sibling_Label(QLabel *label) { m_Label = label; }

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton) {
			bool editable = (bool)m_OnChange;
			QColorDialog dialog(m_Color, this);
			QColorDialog::ColorDialogOptions options = QColorDialog::DontUseNativeDialog;
			if (!editable) {
				options |= QColorDialog::NoButtons;
			}
			dialog.setOptions(options);
			dialog.setWindowTitle(editable ? QStringLiteral("Color")
				: QStringLiteral("Color (read-only)"));
			Apply_Dark_Title_Bar((HWND)dialog.winId());

			if (dialog.exec() == QDialog::Accepted && editable) {
				m_Color = dialog.currentColor();
				QPalette palette = this->palette();
				palette.setColor(QPalette::Window, m_Color);
				setPalette(palette);
				if (m_Label != nullptr) {
					m_Label->setText(QStringLiteral("%1, %2, %3")
						.arg(m_Color.red()).arg(m_Color.green()).arg(m_Color.blue()));
				}
				m_OnChange(m_Color);
			}
		}
		QFrame::mousePressEvent(event);
	}

private:
	QColor m_Color;
	std::function<void(const QColor &)> m_OnChange;
	QLabel *m_Label = nullptr;
};

// on_change (Edit mode) receives the new RGB as a QColor.
ColorSwatchWidget *Make_Color_Swatch(const uint8_t rgb[3],
	std::function<void(const QColor &)> on_change = nullptr)
{
	ColorSwatchWidget *swatch = new ColorSwatchWidget(QColor(rgb[0], rgb[1], rgb[2]), std::move(on_change));
	swatch->setFrameShape(QFrame::StyledPanel);
	swatch->setFixedSize(48, 18);
	swatch->setAutoFillBackground(true);
	QPalette palette = swatch->palette();
	palette.setColor(QPalette::Window, QColor(rgb[0], rgb[1], rgb[2]));
	swatch->setPalette(palette);
	swatch->setCursor(Qt::PointingHandCursor);
	QString tip = QStringLiteral("RGB (%1, %2, %3) — click to inspect/copy")
		.arg(rgb[0]).arg(rgb[1]).arg(rgb[2]);
	swatch->setToolTip(tip);
	return swatch;
}

// `store` (Edit mode) writes the picked RGB back into the document's uint8[3].
QWidget *Make_Color_Row(const uint8_t rgb[3], std::function<void(const uint8_t[3])> store = nullptr)
{
	QWidget *row = new QWidget;
	QHBoxLayout *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	QLabel *value = new QLabel(QStringLiteral("%1, %2, %3").arg(rgb[0]).arg(rgb[1]).arg(rgb[2]));
	value->setTextInteractionFlags(Qt::TextSelectableByMouse);
	value->setCursor(Qt::IBeamCursor);

	std::function<void(const QColor &)> on_change;
	if (store) {
		on_change = [store](const QColor &color) {
			uint8_t out[3] = { (uint8_t)color.red(), (uint8_t)color.green(), (uint8_t)color.blue() };
			store(out);
		};
	}
	ColorSwatchWidget *swatch = Make_Color_Swatch(rgb, std::move(on_change));
	swatch->Set_Sibling_Label(value);

	layout->addWidget(swatch);
	layout->addWidget(value);
	layout->addStretch(1);
	return row;
}

// Very small flat button that puts `text` on the clipboard when clicked.
QToolButton *Make_Copy_Button(const QString &text, const char *tooltip)
{
	QToolButton *copy = new QToolButton;
	copy->setText(QString(QChar(0x29C9)));	// "two joined squares" copy glyph
	copy->setFixedSize(18, 18);
	copy->setAutoRaise(true);
	copy->setToolTip(QString::fromLatin1(tooltip));
	QObject::connect(copy, &QToolButton::clicked, [text]() {
		QApplication::clipboard()->setText(text);
	});
	return copy;
}

// Parses "UVSourceChannel=<n>" out of a mapper args string; 1 when absent.
int Parse_UV_Channel(const std::string &args)
{
	const char KEY[] = "UVSourceChannel";
	size_t pos = args.find(KEY);
	if (pos == std::string::npos) {
		return 1;
	}
	pos = args.find('=', pos);
	if (pos == std::string::npos) {
		return 1;
	}
	return atoi(args.c_str() + pos + 1);
}

//////////////////////////////////////////////////////////////////////////////
//	Tab builders (one mesh, one pass)
//////////////////////////////////////////////////////////////////////////////
//
// Every builder takes an EditCtx. In View mode (edit == false) it holds no
// mutable pointers and all controls are built read-only, exactly as before.
// In Edit mode it carries a pointer to the mesh being shown (owned by the
// panel's document copy) plus a markDirty callback; controls mutate the
// document structs in place and mark it dirty. Values-only editing never
// resizes the document vectors, so the raw pointers stay valid for the
// lifetime of the built page.

struct EditCtx
{
	bool				edit = false;
	MeshMaterialData	*mesh = nullptr;	// mutable when edit; else null
	ChangeFn			markDirty;			// called on any value change
};

QWidget *Build_Stage_Mapping_Group(const EditCtx &ctx, VertexMaterialData &material, int stage)
{
	QGroupBox *group = new QGroupBox(stage == 0
		? QStringLiteral("Stage 0 Mapping") : QStringLiteral("Stage 1 Mapping"));
	QFormLayout *form = new QFormLayout(group);

	uint32_t mask = (stage == 0) ? VERTMAT_STAGE0_MAPPING_MASK : VERTMAT_STAGE1_MAPPING_MASK;
	int shift = (stage == 0) ? VERTMAT_STAGE0_MAPPING_SHIFT : VERTMAT_STAGE1_MAPPING_SHIFT;
	std::string *args = (stage == 0) ? &material.mapperArgs0 : &material.mapperArgs1;
	int mapping = (int)((material.attributes & mask) >> shift);

	VertexMaterialData *material_ptr = &material;
	ChangeFn dirty = ctx.markDirty;

	std::function<void(int)> mapping_change;
	if (ctx.edit) {
		mapping_change = [material_ptr, mask, shift, dirty](int value) {
			material_ptr->attributes = (material_ptr->attributes & ~mask)
				| (((uint32_t)value << shift) & mask);
			if (dirty) dirty();
		};
	}
	form->addRow(QStringLiteral("Type:"), Make_Combo(MAPPING_TYPES, mapping, mapping_change));

	QPlainTextEdit *args_edit = new QPlainTextEdit(QString::fromStdString(*args));
	args_edit->setReadOnly(!ctx.edit);
	args_edit->setFixedHeight(56);
	if (ctx.edit) {
		std::string *args_ptr = args;
		QObject::connect(args_edit, &QPlainTextEdit::textChanged, [args_edit, args_ptr, dirty]() {
			*args_ptr = args_edit->toPlainText().toStdString();
			if (dirty) dirty();
		});
	}
	QWidget *args_row = new QWidget;
	QHBoxLayout *args_layout = new QHBoxLayout(args_row);
	args_layout->setContentsMargins(0, 0, 0, 0);
	args_layout->setSpacing(2);
	args_layout->addWidget(args_edit, 1);
	args_layout->addWidget(Make_Copy_Button(QString::fromStdString(*args), "Copy mapper args"),
		0, Qt::AlignTop);
	form->addRow(QStringLiteral("Args:"), args_row);

	// UV Channel stays derived (read-only) even in edit mode; it lives inside
	// the args text, which the user edits directly.
	form->addRow(QStringLiteral("UV Channel:"), Make_Int_Spin(Parse_UV_Channel(*args), 1, 99));
	return group;
}

QWidget *Build_Vertex_Material_Tab(const EditCtx &ctx, MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);

	static VertexMaterialData DEFAULT_MATERIAL;
	bool has_material = (pass.vertexMaterialIndex >= 0
		&& pass.vertexMaterialIndex < (int)mesh.vertexMaterials.size());
	VertexMaterialData &material =
		has_material ? mesh.vertexMaterials[pass.vertexMaterialIndex] : DEFAULT_MATERIAL;

	// Only edit real materials (never the shared default placeholder).
	EditCtx mat_ctx = ctx;
	mat_ctx.edit = ctx.edit && has_material;
	ChangeFn dirty = mat_ctx.edit ? ctx.markDirty : ChangeFn();

	if (!material.name.empty()) {
		QLabel *name = new QLabel(QString::fromStdString(material.name));
		// Bold via QFont, not a stylesheet — a stylesheet detaches the label
		// from the palette and its text goes black in dark mode.
		QFont font = name->font();
		font.setBold(true);
		name->setFont(font);
		layout->addWidget(name);
	}

	VertexMaterialData *material_ptr = &material;
	QFormLayout *form = new QFormLayout;
	form->addRow(QStringLiteral("Ambient:"), Make_Color_Row(material.ambient,
		mat_ctx.edit ? [material_ptr, dirty](const uint8_t c[3]) {
			memcpy(material_ptr->ambient, c, 3); if (dirty) dirty();
		} : std::function<void(const uint8_t[3])>()));
	form->addRow(QStringLiteral("Diffuse:"), Make_Color_Row(material.diffuse,
		mat_ctx.edit ? [material_ptr, dirty](const uint8_t c[3]) {
			memcpy(material_ptr->diffuse, c, 3); if (dirty) dirty();
		} : std::function<void(const uint8_t[3])>()));
	form->addRow(QStringLiteral("Specular:"), Make_Color_Row(material.specular,
		mat_ctx.edit ? [material_ptr, dirty](const uint8_t c[3]) {
			memcpy(material_ptr->specular, c, 3); if (dirty) dirty();
		} : std::function<void(const uint8_t[3])>()));
	form->addRow(QStringLiteral("Emissive:"), Make_Color_Row(material.emissive,
		mat_ctx.edit ? [material_ptr, dirty](const uint8_t c[3]) {
			memcpy(material_ptr->emissive, c, 3); if (dirty) dirty();
		} : std::function<void(const uint8_t[3])>()));
	layout->addLayout(form);

	layout->addWidget(Make_Check("Specular To Diffuse",
		(material.attributes & VERTMAT_COPY_SPECULAR_TO_DIFFUSE) != 0,
		mat_ctx.edit ? [material_ptr, dirty](bool on) {
			if (on) material_ptr->attributes |= VERTMAT_COPY_SPECULAR_TO_DIFFUSE;
			else    material_ptr->attributes &= ~VERTMAT_COPY_SPECULAR_TO_DIFFUSE;
			if (dirty) dirty();
		} : std::function<void(bool)>()));

	QFormLayout *value_form = new QFormLayout;
	value_form->addRow(QStringLiteral("Opacity:"), Make_Float_Spin(material.opacity, 0.0, 1.0, 3,
		mat_ctx.edit ? [material_ptr, dirty](double v) {
			material_ptr->opacity = (float)v; if (dirty) dirty();
		} : std::function<void(double)>()));
	value_form->addRow(QStringLiteral("Translucency:"), Make_Float_Spin(material.translucency, 0.0, 1.0, 3,
		mat_ctx.edit ? [material_ptr, dirty](double v) {
			material_ptr->translucency = (float)v; if (dirty) dirty();
		} : std::function<void(double)>()));
	value_form->addRow(QStringLiteral("Shininess:"), Make_Float_Spin(material.shininess, 0.0, 1.0, 3,
		mat_ctx.edit ? [material_ptr, dirty](double v) {
			material_ptr->shininess = (float)v; if (dirty) dirty();
		} : std::function<void(double)>()));
	layout->addLayout(value_form);

	layout->addWidget(Build_Stage_Mapping_Group(mat_ctx, material, 0));
	layout->addWidget(Build_Stage_Mapping_Group(mat_ctx, material, 1));
	layout->addStretch(1);
	return tab;
}

QWidget *Build_Shader_Tab(const EditCtx &ctx, MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);

	static ShaderData DEFAULT_SHADER;
	bool has_shader = (pass.shaderIndex >= 0 && pass.shaderIndex < (int)mesh.shaders.size());
	ShaderData &shader = has_shader ? mesh.shaders[pass.shaderIndex] : DEFAULT_SHADER;

	bool edit = ctx.edit && has_shader;
	ShaderData *shader_ptr = &shader;
	ChangeFn dirty = ctx.markDirty;

	QGroupBox *blend_group = new QGroupBox(QStringLiteral("Blend"));
	QFormLayout *blend_form = new QFormLayout(blend_group);

	// The preset combo and the three raw controls update each other. The
	// controls are created before they can all be cross-referenced, so the
	// pointers live in a heap holder captured by value in every lambda (a
	// reference to the function's stack locals would dangle — the lambdas run
	// after this function returns).
	struct BlendRefs { QComboBox *preset = nullptr, *src = nullptr, *dest = nullptr; QCheckBox *alpha = nullptr; };
	auto refs = std::make_shared<BlendRefs>();

	int blend_mode = Derive_Blend_Mode(shader.srcBlend, shader.destBlend, shader.alphaTest);

	// Preset -> (src, dest, alphaTest); mirrors Derive_Blend_Mode's table.
	auto apply_preset = [](int preset, int &src, int &dest, int &at) {
		static const struct { int src, dest, at; } presets[] = {
			{ 1, 0, 0 }, { 1, 1, 0 }, { 0, 2, 0 }, { 1, 2, 0 },
			{ 1, 3, 0 }, { 2, 5, 0 }, { 1, 0, 1 }, { 2, 5, 1 },
		};
		if (preset >= 0 && preset < 8) {
			src = presets[preset].src;
			dest = presets[preset].dest;
			at = presets[preset].at;
		}
	};

	refs->preset = Make_Combo(BLEND_MODES, blend_mode,
		edit ? std::function<void(int)>([shader_ptr, dirty, apply_preset, refs](int preset) {
			if (preset >= 8) return;	// Custom: no forced values
			int src = shader_ptr->srcBlend, dest = shader_ptr->destBlend, at = shader_ptr->alphaTest;
			apply_preset(preset, src, dest, at);
			shader_ptr->srcBlend = (uint8_t)src;
			shader_ptr->destBlend = (uint8_t)dest;
			shader_ptr->alphaTest = (uint8_t)at;
			if (refs->src)   { QSignalBlocker b(refs->src);   refs->src->setCurrentIndex(src); }
			if (refs->dest)  { QSignalBlocker b(refs->dest);  refs->dest->setCurrentIndex(dest); }
			if (refs->alpha) { QSignalBlocker b(refs->alpha); refs->alpha->setChecked(at != 0); }
			if (dirty) dirty();
		}) : std::function<void(int)>());

	auto rederive_preset = [refs, shader_ptr]() {
		if (!refs->preset) return;
		int mode = Derive_Blend_Mode(shader_ptr->srcBlend, shader_ptr->destBlend, shader_ptr->alphaTest);
		QSignalBlocker b(refs->preset);
		refs->preset->setCurrentIndex(mode);
	};

	refs->src = Make_Combo(SRC_BLEND, shader.srcBlend,
		edit ? std::function<void(int)>([shader_ptr, dirty, rederive_preset](int v) {
			shader_ptr->srcBlend = (uint8_t)v; rederive_preset(); if (dirty) dirty();
		}) : std::function<void(int)>());
	refs->dest = Make_Combo(DEST_BLEND, shader.destBlend,
		edit ? std::function<void(int)>([shader_ptr, dirty, rederive_preset](int v) {
			shader_ptr->destBlend = (uint8_t)v; rederive_preset(); if (dirty) dirty();
		}) : std::function<void(int)>());
	refs->alpha = Make_Check("Alpha Test", shader.alphaTest != 0,
		edit ? std::function<void(bool)>([shader_ptr, dirty, rederive_preset](bool on) {
			shader_ptr->alphaTest = on ? 1 : 0; rederive_preset(); if (dirty) dirty();
		}) : std::function<void(bool)>());

	blend_form->addRow(QStringLiteral("Blend Mode:"), refs->preset);
	blend_form->addRow(QStringLiteral("Custom Src:"), refs->src);
	blend_form->addRow(QStringLiteral("Custom Dest:"), refs->dest);
	blend_form->addRow(QString(), Make_Check("Write Z Buffer", shader.depthMask != 0,
		edit ? std::function<void(bool)>([shader_ptr, dirty](bool on) {
			shader_ptr->depthMask = on ? 1 : 0; if (dirty) dirty();
		}) : std::function<void(bool)>()));
	blend_form->addRow(QString(), refs->alpha);
	layout->addWidget(blend_group);

	QGroupBox *advanced_group = new QGroupBox(QStringLiteral("Advanced"));
	QFormLayout *advanced_form = new QFormLayout(advanced_group);
	advanced_form->addRow(QStringLiteral("Pri Gradient:"), Make_Combo(PRI_GRADIENT, shader.priGradient,
		edit ? std::function<void(int)>([shader_ptr, dirty](int v) {
			shader_ptr->priGradient = (uint8_t)v; if (dirty) dirty();
		}) : std::function<void(int)>()));
	advanced_form->addRow(QStringLiteral("Sec Gradient:"), Make_Combo(SEC_GRADIENT, shader.secGradient,
		edit ? std::function<void(int)>([shader_ptr, dirty](int v) {
			shader_ptr->secGradient = (uint8_t)v; if (dirty) dirty();
		}) : std::function<void(int)>()));
	advanced_form->addRow(QStringLiteral("Depth Cmp:"), Make_Combo(DEPTH_COMPARE, shader.depthCompare,
		edit ? std::function<void(int)>([shader_ptr, dirty](int v) {
			shader_ptr->depthCompare = (uint8_t)v; if (dirty) dirty();
		}) : std::function<void(int)>()));
	advanced_form->addRow(QStringLiteral("Detail Colour:"), Make_Combo(DETAIL_COLOR, shader.detailColorFunc,
		edit ? std::function<void(int)>([shader_ptr, dirty](int v) {
			shader_ptr->detailColorFunc = (uint8_t)v; if (dirty) dirty();
		}) : std::function<void(int)>()));
	advanced_form->addRow(QStringLiteral("Detail Alpha:"), Make_Combo(DETAIL_ALPHA, shader.detailAlphaFunc,
		edit ? std::function<void(int)>([shader_ptr, dirty](int v) {
			shader_ptr->detailAlphaFunc = (uint8_t)v; if (dirty) dirty();
		}) : std::function<void(int)>()));
	layout->addWidget(advanced_group);
	layout->addStretch(1);
	return tab;
}

QWidget *Build_Texture_Stage_Group(const EditCtx &ctx, MeshMaterialData &mesh, const PassData &pass, int stage)
{
	QGroupBox *group = new QGroupBox(QStringLiteral("Stage %1 Texture").arg(stage));
	QVBoxLayout *layout = new QVBoxLayout(group);

	int texture_index = pass.stageTextureIndex[stage];
	bool present = (texture_index >= 0 && texture_index < (int)mesh.textures.size());

	// Enabling/disabling a stage is structural (v1 leaves it read-only).
	layout->addWidget(Make_Check("Enabled", present));

	static TextureData DEFAULT_TEXTURE;
	TextureData &texture = present ? mesh.textures[texture_index] : DEFAULT_TEXTURE;

	bool edit = ctx.edit && present;
	TextureData *texture_ptr = &texture;
	ChangeFn dirty = ctx.markDirty;

	// Texture info bits require the optional TEXTURE_INFO chunk; setting any of
	// them marks the texture as having info so the writer materializes it.
	auto touch_info = [texture_ptr]() { texture_ptr->hasInfo = true; };

	QHBoxLayout *name_row = new QHBoxLayout;
	if (edit) {
		QLineEdit *name_edit = new QLineEdit(QString::fromStdString(texture.name));
		QObject::connect(name_edit, &QLineEdit::textChanged, [texture_ptr, dirty](const QString &t) {
			texture_ptr->name = t.toStdString();
			if (dirty) dirty();
		});
		name_row->addWidget(name_edit, 1);
	} else {
		QLabel *name = new QLabel(present ? QString::fromStdString(texture.name) : QStringLiteral("None"));
		QFont font = name->font();
		font.setBold(true);
		name->setFont(font);
		name->setTextInteractionFlags(Qt::TextSelectableByMouse);
		if (!texture.resolvedPath.empty()) {
			name->setToolTip(QString::fromStdString(texture.resolvedPath));
		}
		name_row->addWidget(name);
		if (present) {
			name_row->addWidget(Make_Copy_Button(QString::fromStdString(texture.name),
				"Copy texture filename"));
		}
		name_row->addStretch(1);
	}
	layout->addLayout(name_row);

	// Thumbnail decoded on the W3DView side (BGRA == little-endian ARGB32).
	if (texture.previewWidth > 0 && texture.previewHeight > 0 &&
			texture.previewPixels.size() >= (size_t)texture.previewWidth * texture.previewHeight * 4) {
		QImage image(texture.previewPixels.data(),
			texture.previewWidth, texture.previewHeight,
			texture.previewWidth * 4, QImage::Format_ARGB32);
		QLabel *preview = new QLabel;
		preview->setPixmap(QPixmap::fromImage(image.copy()));
		preview->setFixedSize(texture.previewWidth + 2, texture.previewHeight + 2);
		preview->setFrameShape(QFrame::StyledPanel);
		preview->setAlignment(Qt::AlignCenter);
		if (!texture.resolvedPath.empty()) {
			preview->setToolTip(QString::fromStdString(texture.resolvedPath));
		}
		layout->addWidget(preview);
	} else if (present) {
		QLabel *missing = new QLabel(QStringLiteral("(texture file not found)"));
		layout->addWidget(missing);
	}

	// Toggles a single bit of texture.attributes; marks info present + dirty.
	auto make_flag = [&](const char *label, uint16_t bit) -> QCheckBox * {
		return Make_Check(label, (texture.attributes & bit) != 0,
			edit ? std::function<void(bool)>([texture_ptr, bit, dirty, touch_info](bool on) {
				if (on) texture_ptr->attributes |= bit;
				else    texture_ptr->attributes &= ~bit;
				touch_info();
				if (dirty) dirty();
			}) : std::function<void(bool)>());
	};

	QHBoxLayout *flags_row_1 = new QHBoxLayout;
	flags_row_1->addWidget(make_flag("Clamp U", TEX_CLAMP_U));
	flags_row_1->addWidget(make_flag("Clamp V", TEX_CLAMP_V));
	flags_row_1->addWidget(make_flag("No LOD", TEX_NO_LOD));
	flags_row_1->addStretch(1);
	layout->addLayout(flags_row_1);

	QHBoxLayout *flags_row_2 = new QHBoxLayout;
	flags_row_2->addWidget(make_flag("Publish", TEX_PUBLISH));
	flags_row_2->addWidget(make_flag("Resize", TEX_RESIZE_OBSOLETE));
	flags_row_2->addWidget(make_flag("Alpha Bitmap", TEX_ALPHA_BITMAP));
	flags_row_2->addStretch(1);
	layout->addLayout(flags_row_2);

	QFormLayout *form = new QFormLayout;
	form->addRow(QStringLiteral("Frames:"), Make_Int_Spin((int)texture.frameCount, 0, 999,
		edit ? std::function<void(int)>([texture_ptr, dirty, touch_info](int v) {
			texture_ptr->frameCount = (uint32_t)v; touch_info(); if (dirty) dirty();
		}) : std::function<void(int)>()));
	form->addRow(QStringLiteral("FPS:"), Make_Float_Spin(texture.frameRate, 0.0, 60.0, 1,
		edit ? std::function<void(double)>([texture_ptr, dirty, touch_info](double v) {
			texture_ptr->frameRate = (float)v; touch_info(); if (dirty) dirty();
		}) : std::function<void(double)>()));
	form->addRow(QStringLiteral("Anim Mode:"), Make_Combo(ANIM_MODES, texture.animType,
		edit ? std::function<void(int)>([texture_ptr, dirty, touch_info](int v) {
			texture_ptr->animType = (uint16_t)v; touch_info(); if (dirty) dirty();
		}) : std::function<void(int)>()));
	form->addRow(QStringLiteral("Pass Hint:"),
		Make_Combo(PASS_HINTS, (texture.attributes & TEX_HINT_MASK) >> TEX_HINT_SHIFT,
			edit ? std::function<void(int)>([texture_ptr, dirty, touch_info](int v) {
				texture_ptr->attributes = (texture_ptr->attributes & ~TEX_HINT_MASK)
					| (uint16_t)(((uint32_t)v << TEX_HINT_SHIFT) & TEX_HINT_MASK);
				touch_info(); if (dirty) dirty();
			}) : std::function<void(int)>()));
	layout->addLayout(form);

	if (!present) {
		// Match the Max plugin: gray the whole group when the stage is unused.
		group->setEnabled(false);
	}
	return group;
}

QWidget *Build_Textures_Tab(const EditCtx &ctx, MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);
	layout->addWidget(Build_Texture_Stage_Group(ctx, mesh, pass, 0));
	layout->addWidget(Build_Texture_Stage_Group(ctx, mesh, pass, 1));
	layout->addStretch(1);
	return tab;
}

//////////////////////////////////////////////////////////////////////////////
//	MaterialPanelWidget
//////////////////////////////////////////////////////////////////////////////

class MaterialPanelWidget : public QWidget
{
public:
	MaterialPanelWidget()
		: m_MeshButton(nullptr),
		  m_EditButton(nullptr),
		  m_SaveButton(nullptr),
		  m_RevertButton(nullptr),
		  m_Scroll(nullptr),
		  m_CurrentIndex(-1),
		  m_EditMode(false),
		  m_Dirty(false)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);

		QHBoxLayout *mesh_row = new QHBoxLayout;
		mesh_row->addWidget(new QLabel(QStringLiteral("Mesh:")));
		m_MeshButton = new QPushButton(QStringLiteral("(no file loaded)"));
		m_MeshButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_MeshButton->setToolTip(QStringLiteral("Choose a mesh (filterable list)"));
		mesh_row->addWidget(m_MeshButton, 1);

		// Dual-state View/Edit gate; Save/Revert appear only while editing.
		m_EditButton = new QPushButton(QStringLiteral("Edit"));
		m_EditButton->setCheckable(true);
		m_EditButton->setToolTip(QStringLiteral("Toggle read-only View / editable Edit mode"));
		mesh_row->addWidget(m_EditButton);

		m_SaveButton = new QPushButton(QStringLiteral("Save"));
		m_SaveButton->setToolTip(QStringLiteral("Write the edited material back to the .w3d file"));
		m_SaveButton->setVisible(false);
		m_SaveButton->setEnabled(false);
		mesh_row->addWidget(m_SaveButton);

		m_RevertButton = new QPushButton(QStringLiteral("Revert"));
		m_RevertButton->setToolTip(QStringLiteral("Discard edits and reload the file from disk"));
		m_RevertButton->setVisible(false);
		m_RevertButton->setEnabled(false);
		mesh_row->addWidget(m_RevertButton);

		layout->addLayout(mesh_row);

		m_Scroll = new QScrollArea;
		m_Scroll->setWidgetResizable(true);
		m_Scroll->setFrameShape(QFrame::NoFrame);
		layout->addWidget(m_Scroll, 1);

		// Live controls connect through lambdas; no Q_OBJECT needed.
		QObject::connect(m_MeshButton, &QPushButton::clicked,
			[this]() { Pick_Mesh(); });
		QObject::connect(m_EditButton, &QPushButton::toggled,
			[this](bool on) { Toggle_Edit_Mode(on); });
		QObject::connect(m_SaveButton, &QPushButton::clicked,
			[this]() { Do_Save(); });
		QObject::connect(m_RevertButton, &QPushButton::clicked,
			[this]() { Do_Revert(); });

		Show_Empty();
	}

	void Set_Document(const MaterialDocument &document)
	{
		m_Document = document;
		Set_Dirty(false);

		if (m_Document.meshes.empty()) {
			m_CurrentIndex = -1;
			m_MeshButton->setText(QStringLiteral("(no meshes)"));
			Show_Empty();
		} else {
			Show_Mesh(m_CurrentIndex >= 0 && m_CurrentIndex < (int)m_Document.meshes.size()
				? m_CurrentIndex : 0);
		}
	}

	bool Has_Unsaved_Changes() const { return m_EditMode && m_Dirty; }

	bool Request_Save()
	{
		if (!m_EditMode || !m_Dirty) {
			return true;
		}
		return Do_Save();
	}

	void Select_Mesh(const char *mesh_name)
	{
		QString wanted = QString::fromLatin1(mesh_name);
		for (size_t i = 0; i < m_Document.meshes.size(); i++) {
			if (QString::fromStdString(m_Document.meshes[i].meshName)
					.compare(wanted, Qt::CaseInsensitive) == 0) {
				Show_Mesh((int)i);
				return;
			}
		}
	}

	void Apply_Theme(const PanelTheme &theme)
	{
		g_DarkTheme = theme.dark;

		QPalette palette;
		if (theme.dark) {
			QColor window(GetRValue(theme.background), GetGValue(theme.background), GetBValue(theme.background));
			QColor base(GetRValue(theme.ctrlBackground), GetGValue(theme.ctrlBackground), GetBValue(theme.ctrlBackground));
			QColor text(GetRValue(theme.text), GetGValue(theme.text), GetBValue(theme.text));
			QColor edge(GetRValue(theme.edge), GetGValue(theme.edge), GetBValue(theme.edge));

			palette.setColor(QPalette::Window, window);
			palette.setColor(QPalette::WindowText, text);
			palette.setColor(QPalette::Base, base);
			palette.setColor(QPalette::AlternateBase, window);
			palette.setColor(QPalette::Text, text);
			palette.setColor(QPalette::Button, window);
			palette.setColor(QPalette::ButtonText, text);
			palette.setColor(QPalette::ToolTipBase, base);
			palette.setColor(QPalette::ToolTipText, text);
			palette.setColor(QPalette::Mid, edge);
			palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
			palette.setColor(QPalette::HighlightedText, Qt::white);
		} else {
			palette = QApplication::style()->standardPalette();
		}

		// Read-only controls stay enabled-looking: mirror active colors into the
		// disabled group (used only by the unused-texture-stage groups).
		palette.setColor(QPalette::Disabled, QPalette::WindowText,
			palette.color(QPalette::Active, QPalette::WindowText).darker(theme.dark ? 100 : 130));

		setPalette(palette);
		// New children created by later Set_Document calls inherit this palette.

		// Top-level popups (the swatch QColorDialog) do NOT inherit from the
		// panel — they take the application palette, so set it too or they
		// come up light in dark mode.
		QApplication::setPalette(palette);
	}

private:
	void Show_Empty()
	{
		QLabel *empty = new QLabel(QStringLiteral("Open a .w3d file to inspect its materials."));
		empty->setAlignment(Qt::AlignCenter);
		m_Scroll->setWidget(empty);
	}

	// Modal mesh picker: full mesh list with a filter box. Double-click or
	// Enter accepts; the filter keeps a visible row selected so Enter always
	// picks the top match.
	void Pick_Mesh()
	{
		if (m_Document.meshes.empty()) {
			return;
		}

		QDialog dialog(this);
		dialog.setWindowTitle(QStringLiteral("Select Mesh"));
		dialog.resize(420, 480);

		QVBoxLayout *layout = new QVBoxLayout(&dialog);

		QLineEdit *filter = new QLineEdit;
		filter->setPlaceholderText(QStringLiteral("Filter meshes..."));
		filter->setClearButtonEnabled(true);
		layout->addWidget(filter);

		QListWidget *list = new QListWidget;
		for (size_t i = 0; i < m_Document.meshes.size(); i++) {
			QListWidgetItem *item =
				new QListWidgetItem(QString::fromStdString(m_Document.meshes[i].meshName));
			item->setData(Qt::UserRole, (int)i);
			list->addItem(item);
		}
		if (m_CurrentIndex >= 0 && m_CurrentIndex < list->count()) {
			list->setCurrentRow(m_CurrentIndex);
		}
		layout->addWidget(list, 1);

		QDialogButtonBox *buttons =
			new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		layout->addWidget(buttons);

		QObject::connect(filter, &QLineEdit::textChanged, [list](const QString &text) {
			int first_visible = -1;
			for (int i = 0; i < list->count(); i++) {
				QListWidgetItem *item = list->item(i);
				bool match = item->text().contains(text, Qt::CaseInsensitive);
				item->setHidden(!match);
				if (match && first_visible < 0) {
					first_visible = i;
				}
			}
			QListWidgetItem *current = list->currentItem();
			if ((current == nullptr || current->isHidden()) && first_visible >= 0) {
				list->setCurrentRow(first_visible);
			}
		});
		QObject::connect(list, &QListWidget::itemDoubleClicked,
			[&dialog](QListWidgetItem *) { dialog.accept(); });
		QObject::connect(filter, &QLineEdit::returnPressed, [&dialog]() { dialog.accept(); });
		QObject::connect(buttons, &QDialogButtonBox::accepted, [&dialog]() { dialog.accept(); });
		QObject::connect(buttons, &QDialogButtonBox::rejected, [&dialog]() { dialog.reject(); });

		filter->setFocus();
		Apply_Dark_Title_Bar((HWND)dialog.winId());

		if (dialog.exec() == QDialog::Accepted) {
			QListWidgetItem *item = list->currentItem();
			if (item != nullptr && !item->isHidden()) {
				Show_Mesh(item->data(Qt::UserRole).toInt());
			}
		}
	}

	void Show_Mesh(int index)
	{
		if (index < 0 || index >= (int)m_Document.meshes.size()) {
			Show_Empty();
			return;
		}

		MeshMaterialData &mesh = m_Document.meshes[index];
		m_CurrentIndex = index;
		m_MeshButton->setText(QString::fromStdString(mesh.meshName));

		if (g_MeshSelectedCallback != nullptr) {
			g_MeshSelectedCallback(mesh.meshName.c_str());
		}

		// Prelit meshes keep several material variants; editing one would desync
		// them, so they stay read-only even in edit mode (v1).
		EditCtx ctx;
		ctx.edit = m_EditMode && !mesh.prelit;
		ctx.mesh = &mesh;
		ctx.markDirty = [this]() { Set_Dirty(true); };

		MeshMaterialData *mesh_ptr = &mesh;
		ChangeFn dirty = ctx.markDirty;

		QWidget *content = new QWidget;
		QVBoxLayout *layout = new QVBoxLayout(content);
		layout->setContentsMargins(4, 4, 4, 4);

		// --- Material Surface Type rollup ---
		QGroupBox *surface_group = new QGroupBox(QStringLiteral("Material Surface Type"));
		QFormLayout *surface_form = new QFormLayout(surface_group);
		surface_form->addRow(QStringLiteral("Surface Type:"), Make_Combo(SURFACE_TYPES, (int)mesh.surfaceType,
			ctx.edit ? std::function<void(int)>([mesh_ptr, dirty](int v) {
				mesh_ptr->surfaceType = (uint32_t)v; if (dirty) dirty();
			}) : std::function<void(int)>()));
		surface_form->addRow(QString(), Make_Check("Static Sorting Enabled", mesh.sortLevel != 0,
			ctx.edit ? std::function<void(bool)>([mesh_ptr, dirty](bool on) {
				// Toggle drives sort level: on keeps/repairs a level, off clears it.
				if (on && mesh_ptr->sortLevel == 0) mesh_ptr->sortLevel = 1;
				else if (!on) mesh_ptr->sortLevel = 0;
				if (dirty) dirty();
			}) : std::function<void(bool)>()));
		surface_form->addRow(QStringLiteral("Sort Level:"), Make_Int_Spin(mesh.sortLevel, 0, 32,
			ctx.edit ? std::function<void(int)>([mesh_ptr, dirty](int v) {
				mesh_ptr->sortLevel = v; if (dirty) dirty();
			}) : std::function<void(int)>()));
		if (mesh.prelit) {
			surface_form->addRow(QString(), new QLabel(m_EditMode
				? QStringLiteral("(Prelit material — read-only)")
				: QStringLiteral("(Prelit material)")));
		}
		layout->addWidget(surface_group);

		// --- Material Pass Count rollup, with a live pass selector ---
		QGroupBox *pass_group = new QGroupBox(QStringLiteral("Material Pass Count"));
		QFormLayout *pass_form = new QFormLayout(pass_group);
		// Pass count is structural — read-only in v1 even while editing.
		pass_form->addRow(QStringLiteral("Current Pass Count:"),
			Make_Int_Spin((int)mesh.passes.size(), 0, 4));
		layout->addWidget(pass_group);

		if (mesh.passes.empty()) {
			QLabel *none = new QLabel(QStringLiteral("This mesh has no material passes."));
			none->setAlignment(Qt::AlignCenter);
			layout->addWidget(none);
		} else {
			// All passes are built up front as tabs; the Show Pass combo and
			// the tab bar stay in sync both ways (setCurrentIndex no-ops on
			// the same index, so the two connects cannot loop).
			QComboBox *pass_combo = new QComboBox;
			QTabWidget *pass_tabs = new QTabWidget;
			for (size_t i = 0; i < mesh.passes.size(); i++) {
				pass_combo->addItem(QStringLiteral("Pass %1").arg(i + 1));
				pass_tabs->addTab(Build_Pass_Content(ctx, mesh, (int)i),
					QStringLiteral("Pass %1").arg(i + 1));
			}
			pass_form->addRow(QStringLiteral("Show Pass:"), pass_combo);
			layout->addWidget(pass_tabs);

			QObject::connect(pass_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
				[pass_tabs](int pass_index) { pass_tabs->setCurrentIndex(pass_index); });
			QObject::connect(pass_tabs, &QTabWidget::currentChanged,
				[pass_combo](int pass_index) { pass_combo->setCurrentIndex(pass_index); });
		}

		layout->addStretch(1);
		m_Scroll->setWidget(content);
	}

	// One "Pass N" tab: the Vertex Material / Shader / Textures tab widget.
	static QWidget *Build_Pass_Content(const EditCtx &ctx, MeshMaterialData &mesh, int pass_index)
	{
		const PassData &pass = mesh.passes[pass_index];
		QTabWidget *tabs = new QTabWidget;
		tabs->addTab(Build_Vertex_Material_Tab(ctx, mesh, pass), QStringLiteral("Vertex Material"));
		tabs->addTab(Build_Shader_Tab(ctx, mesh, pass), QStringLiteral("Shader"));
		tabs->addTab(Build_Textures_Tab(ctx, mesh, pass), QStringLiteral("Textures"));
		return tabs;
	}

	//////////////////////////////////////////////////////////////////////////
	//	Edit-mode actions
	//////////////////////////////////////////////////////////////////////////

	void Toggle_Edit_Mode(bool on)
	{
		if (on == m_EditMode) {
			return;
		}

		if (on) {
			// Run the host's file-integrity gate before entering edit mode.
			if (g_EditGateCallback != nullptr && !g_EditGateCallback()) {
				QSignalBlocker block(m_EditButton);
				m_EditButton->setChecked(false);
				return;
			}
			m_EditMode = true;
		} else {
			// Leaving edit mode with unsaved edits: offer to keep editing.
			if (m_Dirty) {
				QMessageBox box(this);
				box.setWindowTitle(QStringLiteral("W3D Material Viewer"));
				box.setText(QStringLiteral("Discard unsaved material changes?"));
				box.setStandardButtons(QMessageBox::Discard | QMessageBox::Cancel);
				box.setIcon(QMessageBox::Warning);
				Apply_Dark_Title_Bar((HWND)box.winId());
				if (box.exec() != QMessageBox::Discard) {
					QSignalBlocker block(m_EditButton);
					m_EditButton->setChecked(true);
					return;
				}
				// Discard: reload the pristine document from disk.
				if (g_RevertCallback != nullptr) {
					g_RevertCallback();
				}
			}
			m_EditMode = false;
		}

		Update_Edit_Buttons();
		if (m_CurrentIndex >= 0) {
			Show_Mesh(m_CurrentIndex);
		}
	}

	bool Do_Save()
	{
		if (g_SaveCallback == nullptr) {
			return false;
		}
		if (g_SaveCallback(m_Document)) {
			Set_Dirty(false);
			return true;
		}
		return false;
	}

	void Do_Revert()
	{
		if (g_RevertCallback != nullptr) {
			g_RevertCallback();	// re-parses + calls Set_Document (clears dirty)
		}
	}

	void Set_Dirty(bool dirty)
	{
		if (dirty == m_Dirty) {
			// Still refresh button state on first document load.
			Update_Edit_Buttons();
			return;
		}
		m_Dirty = dirty;
		Update_Edit_Buttons();
		if (g_DirtyChangedCallback != nullptr) {
			g_DirtyChangedCallback(m_EditMode && m_Dirty);
		}
	}

	void Update_Edit_Buttons()
	{
		m_SaveButton->setVisible(m_EditMode);
		m_RevertButton->setVisible(m_EditMode);
		m_SaveButton->setEnabled(m_EditMode && m_Dirty);
		m_RevertButton->setEnabled(m_EditMode && m_Dirty);
	}

	QPushButton		*m_MeshButton;
	QPushButton		*m_EditButton;
	QPushButton		*m_SaveButton;
	QPushButton		*m_RevertButton;
	QScrollArea		*m_Scroll;
	MaterialDocument m_Document;
	int				m_CurrentIndex;
	bool			m_EditMode;
	bool			m_Dirty;
};

//////////////////////////////////////////////////////////////////////////////
//	Module state
//////////////////////////////////////////////////////////////////////////////

QApplication *g_Application = nullptr;
MaterialPanelWidget *g_Panel = nullptr;
bool g_ApplicationFailed = false;

bool Ensure_Application()
{
	if (g_Application != nullptr) {
		return true;
	}
	if (g_ApplicationFailed) {
		return false;
	}

	// argc/argv must outlive the QApplication.
	static int argc = 1;
	static char arg0[] = "W3DView";
	static char *argv[] = { arg0, nullptr };

	g_Application = new QApplication(argc, argv);
	if (g_Application == nullptr) {
		g_ApplicationFailed = true;
		return false;
	}

	// Fusion respects custom palettes (the native style ignores them), which the
	// dark theme needs.
	QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	return true;
}

} // namespace

//////////////////////////////////////////////////////////////////////////////
//	Public interface (MaterialPanel.h)
//////////////////////////////////////////////////////////////////////////////

HWND CreatePanel(HWND parent)
{
	if (!Ensure_Application()) {
		return nullptr;
	}

	if (g_Panel == nullptr) {
		g_Panel = new MaterialPanelWidget;
	}

	// Qt must consider the window frameless BEFORE the native window is
	// created: otherwise it keeps top-level frame margins (caption + borders)
	// in its geometry math after the re-parent, leaving its logical size a
	// title-bar short of the real client size — the bottom rows then never
	// get painted.
	g_Panel->setWindowFlags(g_Panel->windowFlags() | Qt::FramelessWindowHint);

	// Re-parent the native Qt window into the MFC frame as a WS_CHILD.
	HWND hwnd = (HWND)g_Panel->winId();
	::SetParent(hwnd, parent);
	LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
	style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME);
	style |= WS_CHILD;
	::SetWindowLong(hwnd, GWL_STYLE, style);

	// The MFC host sizes this window natively (MoveWindow), which does not set
	// Qt's WA_Resized flag — without it, Qt's layout engine "auto-sizes" the
	// window to its preferred size whenever the content is rebuilt, leaving a
	// gap of unstyled frame background.
	g_Panel->setAttribute(Qt::WA_Resized, true);

	g_Panel->show();
	return hwnd;
}

void DestroyPanel()
{
	if (g_Panel != nullptr) {
		delete g_Panel;
		g_Panel = nullptr;
	}
}

HWND GetPanelHwnd()
{
	return (g_Panel != nullptr) ? (HWND)g_Panel->winId() : nullptr;
}

void ResizePanel(int width, int height)
{
	if (g_Panel != nullptr) {
		g_Panel->resize(width, height);
	}
}

void SetPanelDocument(const MaterialDocument &document)
{
	if (g_Panel != nullptr) {
		g_Panel->Set_Document(document);
	}
}

void SelectPanelMesh(const char *meshName)
{
	if (g_Panel != nullptr) {
		g_Panel->Select_Mesh(meshName);
	}
}

void SetPanelMeshSelectedCallback(void (*callback)(const char *meshName))
{
	g_MeshSelectedCallback = callback;
}

void SetPanelEditGateCallback(bool (*callback)())
{
	g_EditGateCallback = callback;
}

void SetPanelSaveCallback(bool (*callback)(const MaterialDocument &document))
{
	g_SaveCallback = callback;
}

void SetPanelRevertCallback(void (*callback)())
{
	g_RevertCallback = callback;
}

void SetPanelDirtyChangedCallback(void (*callback)(bool dirty))
{
	g_DirtyChangedCallback = callback;
}

bool PanelHasUnsavedChanges()
{
	return g_Panel != nullptr && g_Panel->Has_Unsaved_Changes();
}

bool RequestPanelSave()
{
	return g_Panel == nullptr || g_Panel->Request_Save();
}

void ApplyPanelTheme(const PanelTheme &theme)
{
	if (g_Panel != nullptr) {
		g_Panel->Apply_Theme(theme);
	}
}

void PumpQtEvents()
{
	if (g_Application != nullptr) {
		// Window messages (input, paint, Qt's timer window) reach the Qt HWNDs
		// through the regular Win32 pump; only Qt-posted events (deleteLater,
		// layout) need explicit delivery. Deliberately NOT processEvents():
		// that would drain the thread's whole message queue and re-enter MFC
		// window procedures mid render tick.
		QCoreApplication::sendPostedEvents();
	}
}

void ShutdownQt()
{
	DestroyPanel();
	if (g_Application != nullptr) {
		delete g_Application;
		g_Application = nullptr;
	}
}

} // namespace W3dMaterialViewer
