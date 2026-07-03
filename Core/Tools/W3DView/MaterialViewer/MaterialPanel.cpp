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

namespace W3dMaterialViewer
{

namespace
{

// Notifies the host (MFC frame) when the mesh combo selection changes.
void (*g_MeshSelectedCallback)(const char *) = nullptr;

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
//	Read-only control factory helpers
//////////////////////////////////////////////////////////////////////////////

// Blocks interaction while keeping the normal (non-grayed) look.
void Make_Read_Only(QWidget *widget)
{
	widget->setFocusPolicy(Qt::NoFocus);
	widget->setAttribute(Qt::WA_TransparentForMouseEvents);
}

template<int N>
QComboBox *Make_Combo(const char *const (&table)[N], int index)
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
	Make_Read_Only(combo);
	return combo;
}

QCheckBox *Make_Check(const char *label, bool checked)
{
	QCheckBox *check = new QCheckBox(QString::fromLatin1(label));
	check->setChecked(checked);
	Make_Read_Only(check);
	return check;
}

QDoubleSpinBox *Make_Float_Spin(double value, double min_value, double max_value, int decimals = 3)
{
	QDoubleSpinBox *spin = new QDoubleSpinBox;
	spin->setRange(min_value, max_value);
	spin->setDecimals(decimals);
	spin->setValue(value);
	Make_Read_Only(spin);
	return spin;
}

QSpinBox *Make_Int_Spin(int value, int min_value, int max_value)
{
	QSpinBox *spin = new QSpinBox;
	spin->setRange(min_value, max_value);
	spin->setValue(value);
	Make_Read_Only(spin);
	return spin;
}

// Small color swatch in the Max style: a flat filled rectangle.
// Clicking the swatch opens a (non-native, theme-following) color dialog on
// the value so it can be inspected and copied as hex/RGB/HSV. The viewer is
// read-only: the dialog has no OK/Cancel and nothing is written back.
class ColorSwatchWidget : public QFrame
{
public:
	explicit ColorSwatchWidget(const QColor &color) : m_Color(color) {}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (event->button() == Qt::LeftButton) {
			QColorDialog dialog(m_Color, this);
			dialog.setOptions(QColorDialog::DontUseNativeDialog | QColorDialog::NoButtons);
			dialog.setWindowTitle(QStringLiteral("Color (read-only)"));
			Apply_Dark_Title_Bar((HWND)dialog.winId());
			dialog.exec();
		}
		QFrame::mousePressEvent(event);
	}

private:
	QColor m_Color;
};

QFrame *Make_Color_Swatch(const uint8_t rgb[3])
{
	QFrame *swatch = new ColorSwatchWidget(QColor(rgb[0], rgb[1], rgb[2]));
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

QWidget *Make_Color_Row(const uint8_t rgb[3])
{
	QWidget *row = new QWidget;
	QHBoxLayout *layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	QLabel *value = new QLabel(QStringLiteral("%1, %2, %3").arg(rgb[0]).arg(rgb[1]).arg(rgb[2]));
	value->setTextInteractionFlags(Qt::TextSelectableByMouse);
	value->setCursor(Qt::IBeamCursor);
	layout->addWidget(Make_Color_Swatch(rgb));
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

QWidget *Build_Stage_Mapping_Group(const char *title, const VertexMaterialData &material, int stage)
{
	QGroupBox *group = new QGroupBox(QString::fromLatin1(title));
	QFormLayout *form = new QFormLayout(group);

	int mapping;
	const std::string *args;
	if (stage == 0) {
		mapping = (int)((material.attributes & VERTMAT_STAGE0_MAPPING_MASK) >> VERTMAT_STAGE0_MAPPING_SHIFT);
		args = &material.mapperArgs0;
	} else {
		mapping = (int)((material.attributes & VERTMAT_STAGE1_MAPPING_MASK) >> VERTMAT_STAGE1_MAPPING_SHIFT);
		args = &material.mapperArgs1;
	}

	form->addRow(QStringLiteral("Type:"), Make_Combo(MAPPING_TYPES, mapping));

	QPlainTextEdit *args_edit = new QPlainTextEdit(QString::fromStdString(*args));
	args_edit->setReadOnly(true);
	args_edit->setFixedHeight(56);
	QWidget *args_row = new QWidget;
	QHBoxLayout *args_layout = new QHBoxLayout(args_row);
	args_layout->setContentsMargins(0, 0, 0, 0);
	args_layout->setSpacing(2);
	args_layout->addWidget(args_edit, 1);
	args_layout->addWidget(Make_Copy_Button(QString::fromStdString(*args), "Copy mapper args"),
		0, Qt::AlignTop);
	form->addRow(QStringLiteral("Args:"), args_row);

	form->addRow(QStringLiteral("UV Channel:"), Make_Int_Spin(Parse_UV_Channel(*args), 1, 99));
	return group;
}

QWidget *Build_Vertex_Material_Tab(const MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);

	static const VertexMaterialData DEFAULT_MATERIAL;
	const VertexMaterialData &material =
		(pass.vertexMaterialIndex >= 0 && pass.vertexMaterialIndex < (int)mesh.vertexMaterials.size())
			? mesh.vertexMaterials[pass.vertexMaterialIndex]
			: DEFAULT_MATERIAL;

	if (!material.name.empty()) {
		QLabel *name = new QLabel(QString::fromStdString(material.name));
		// Bold via QFont, not a stylesheet — a stylesheet detaches the label
		// from the palette and its text goes black in dark mode.
		QFont font = name->font();
		font.setBold(true);
		name->setFont(font);
		layout->addWidget(name);
	}

	QFormLayout *form = new QFormLayout;
	form->addRow(QStringLiteral("Ambient:"), Make_Color_Row(material.ambient));
	form->addRow(QStringLiteral("Diffuse:"), Make_Color_Row(material.diffuse));
	form->addRow(QStringLiteral("Specular:"), Make_Color_Row(material.specular));
	form->addRow(QStringLiteral("Emissive:"), Make_Color_Row(material.emissive));
	layout->addLayout(form);

	layout->addWidget(Make_Check("Specular To Diffuse",
		(material.attributes & VERTMAT_COPY_SPECULAR_TO_DIFFUSE) != 0));

	QFormLayout *value_form = new QFormLayout;
	value_form->addRow(QStringLiteral("Opacity:"), Make_Float_Spin(material.opacity, 0.0, 1.0));
	value_form->addRow(QStringLiteral("Translucency:"), Make_Float_Spin(material.translucency, 0.0, 1.0));
	value_form->addRow(QStringLiteral("Shininess:"), Make_Float_Spin(material.shininess, 0.0, 1.0));
	layout->addLayout(value_form);

	layout->addWidget(Build_Stage_Mapping_Group("Stage 0 Mapping", material, 0));
	layout->addWidget(Build_Stage_Mapping_Group("Stage 1 Mapping", material, 1));
	layout->addStretch(1);
	return tab;
}

QWidget *Build_Shader_Tab(const MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);

	static const ShaderData DEFAULT_SHADER;
	const ShaderData &shader =
		(pass.shaderIndex >= 0 && pass.shaderIndex < (int)mesh.shaders.size())
			? mesh.shaders[pass.shaderIndex]
			: DEFAULT_SHADER;

	QGroupBox *blend_group = new QGroupBox(QStringLiteral("Blend"));
	QFormLayout *blend_form = new QFormLayout(blend_group);
	int blend_mode = Derive_Blend_Mode(shader.srcBlend, shader.destBlend, shader.alphaTest);
	blend_form->addRow(QStringLiteral("Blend Mode:"), Make_Combo(BLEND_MODES, blend_mode));
	blend_form->addRow(QStringLiteral("Custom Src:"), Make_Combo(SRC_BLEND, shader.srcBlend));
	blend_form->addRow(QStringLiteral("Custom Dest:"), Make_Combo(DEST_BLEND, shader.destBlend));
	blend_form->addRow(QString(), Make_Check("Write Z Buffer", shader.depthMask != 0));
	blend_form->addRow(QString(), Make_Check("Alpha Test", shader.alphaTest != 0));
	layout->addWidget(blend_group);

	QGroupBox *advanced_group = new QGroupBox(QStringLiteral("Advanced"));
	QFormLayout *advanced_form = new QFormLayout(advanced_group);
	advanced_form->addRow(QStringLiteral("Pri Gradient:"), Make_Combo(PRI_GRADIENT, shader.priGradient));
	advanced_form->addRow(QStringLiteral("Sec Gradient:"), Make_Combo(SEC_GRADIENT, shader.secGradient));
	advanced_form->addRow(QStringLiteral("Depth Cmp:"), Make_Combo(DEPTH_COMPARE, shader.depthCompare));
	advanced_form->addRow(QStringLiteral("Detail Colour:"), Make_Combo(DETAIL_COLOR, shader.detailColorFunc));
	advanced_form->addRow(QStringLiteral("Detail Alpha:"), Make_Combo(DETAIL_ALPHA, shader.detailAlphaFunc));
	layout->addWidget(advanced_group);
	layout->addStretch(1);
	return tab;
}

QWidget *Build_Texture_Stage_Group(const MeshMaterialData &mesh, const PassData &pass, int stage)
{
	QGroupBox *group = new QGroupBox(QStringLiteral("Stage %1 Texture").arg(stage));
	QVBoxLayout *layout = new QVBoxLayout(group);

	int texture_index = pass.stageTextureIndex[stage];
	bool present = (texture_index >= 0 && texture_index < (int)mesh.textures.size());

	layout->addWidget(Make_Check("Enabled", present));

	static const TextureData DEFAULT_TEXTURE;
	const TextureData &texture = present ? mesh.textures[texture_index] : DEFAULT_TEXTURE;

	QLabel *name = new QLabel(present ? QString::fromStdString(texture.name) : QStringLiteral("None"));
	{
		QFont font = name->font();
		font.setBold(true);
		name->setFont(font);
	}
	name->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QHBoxLayout *name_row = new QHBoxLayout;
	name_row->addWidget(name);
	if (present) {
		name_row->addWidget(Make_Copy_Button(QString::fromStdString(texture.name),
			"Copy texture filename"));
	}
	name_row->addStretch(1);
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
			name->setToolTip(QString::fromStdString(texture.resolvedPath));
		}
		layout->addWidget(preview);
	} else if (present) {
		QLabel *missing = new QLabel(QStringLiteral("(texture file not found)"));
		layout->addWidget(missing);
	}

	QHBoxLayout *flags_row_1 = new QHBoxLayout;
	flags_row_1->addWidget(Make_Check("Clamp U", (texture.attributes & TEX_CLAMP_U) != 0));
	flags_row_1->addWidget(Make_Check("Clamp V", (texture.attributes & TEX_CLAMP_V) != 0));
	flags_row_1->addWidget(Make_Check("No LOD", (texture.attributes & TEX_NO_LOD) != 0));
	flags_row_1->addStretch(1);
	layout->addLayout(flags_row_1);

	QHBoxLayout *flags_row_2 = new QHBoxLayout;
	flags_row_2->addWidget(Make_Check("Publish", (texture.attributes & TEX_PUBLISH) != 0));
	flags_row_2->addWidget(Make_Check("Resize", (texture.attributes & TEX_RESIZE_OBSOLETE) != 0));
	flags_row_2->addWidget(Make_Check("Alpha Bitmap", (texture.attributes & TEX_ALPHA_BITMAP) != 0));
	flags_row_2->addStretch(1);
	layout->addLayout(flags_row_2);

	QFormLayout *form = new QFormLayout;
	form->addRow(QStringLiteral("Frames:"), Make_Int_Spin((int)texture.frameCount, 0, 999));
	form->addRow(QStringLiteral("FPS:"), Make_Float_Spin(texture.frameRate, 0.0, 60.0, 1));
	form->addRow(QStringLiteral("Anim Mode:"), Make_Combo(ANIM_MODES, texture.animType));
	form->addRow(QStringLiteral("Pass Hint:"),
		Make_Combo(PASS_HINTS, (texture.attributes & TEX_HINT_MASK) >> TEX_HINT_SHIFT));
	layout->addLayout(form);

	group->setEnabled(true);
	if (!present) {
		// Match the Max plugin: gray the whole group when the stage is unused.
		group->setEnabled(false);
	}
	return group;
}

QWidget *Build_Textures_Tab(const MeshMaterialData &mesh, const PassData &pass)
{
	QWidget *tab = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(tab);
	layout->addWidget(Build_Texture_Stage_Group(mesh, pass, 0));
	layout->addWidget(Build_Texture_Stage_Group(mesh, pass, 1));
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
		  m_Scroll(nullptr),
		  m_CurrentIndex(-1)
	{
		QVBoxLayout *layout = new QVBoxLayout(this);
		layout->setContentsMargins(4, 4, 4, 4);

		QHBoxLayout *mesh_row = new QHBoxLayout;
		mesh_row->addWidget(new QLabel(QStringLiteral("Mesh:")));
		m_MeshButton = new QPushButton(QStringLiteral("(no file loaded)"));
		m_MeshButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_MeshButton->setToolTip(QStringLiteral("Choose a mesh (filterable list)"));
		mesh_row->addWidget(m_MeshButton, 1);
		layout->addLayout(mesh_row);

		m_Scroll = new QScrollArea;
		m_Scroll->setWidgetResizable(true);
		m_Scroll->setFrameShape(QFrame::NoFrame);
		layout->addWidget(m_Scroll, 1);

		// Live controls connect through lambdas; no Q_OBJECT needed.
		QObject::connect(m_MeshButton, &QPushButton::clicked,
			[this]() { Pick_Mesh(); });

		Show_Empty();
	}

	void Set_Document(const MaterialDocument &document)
	{
		m_Document = document;

		if (m_Document.meshes.empty()) {
			m_CurrentIndex = -1;
			m_MeshButton->setText(QStringLiteral("(no meshes)"));
			Show_Empty();
		} else {
			Show_Mesh(0);
		}
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

		const MeshMaterialData &mesh = m_Document.meshes[index];
		m_CurrentIndex = index;
		m_MeshButton->setText(QString::fromStdString(mesh.meshName));

		if (g_MeshSelectedCallback != nullptr) {
			g_MeshSelectedCallback(mesh.meshName.c_str());
		}

		QWidget *content = new QWidget;
		QVBoxLayout *layout = new QVBoxLayout(content);
		layout->setContentsMargins(4, 4, 4, 4);

		// --- Material Surface Type rollup ---
		QGroupBox *surface_group = new QGroupBox(QStringLiteral("Material Surface Type"));
		QFormLayout *surface_form = new QFormLayout(surface_group);
		surface_form->addRow(QStringLiteral("Surface Type:"), Make_Combo(SURFACE_TYPES, (int)mesh.surfaceType));
		surface_form->addRow(QString(), Make_Check("Static Sorting Enabled", mesh.sortLevel != 0));
		surface_form->addRow(QStringLiteral("Sort Level:"), Make_Int_Spin(mesh.sortLevel, 0, 32));
		if (mesh.prelit) {
			surface_form->addRow(QString(), new QLabel(QStringLiteral("(Prelit material)")));
		}
		layout->addWidget(surface_group);

		// --- Material Pass Count rollup, with a live pass selector ---
		QGroupBox *pass_group = new QGroupBox(QStringLiteral("Material Pass Count"));
		QFormLayout *pass_form = new QFormLayout(pass_group);
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
				pass_tabs->addTab(Build_Pass_Content(mesh, (int)i),
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
	static QWidget *Build_Pass_Content(const MeshMaterialData &mesh, int pass_index)
	{
		const PassData &pass = mesh.passes[pass_index];
		QTabWidget *tabs = new QTabWidget;
		tabs->addTab(Build_Vertex_Material_Tab(mesh, pass), QStringLiteral("Vertex Material"));
		tabs->addTab(Build_Shader_Tab(mesh, pass), QStringLiteral("Shader"));
		tabs->addTab(Build_Textures_Tab(mesh, pass), QStringLiteral("Textures"));
		return tabs;
	}

	QPushButton		*m_MeshButton;
	QScrollArea		*m_Scroll;
	MaterialDocument m_Document;
	int				m_CurrentIndex;
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
