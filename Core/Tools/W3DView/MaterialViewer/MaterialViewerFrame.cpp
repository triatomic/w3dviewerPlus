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

#include "../StdAfx.h"
#include "MaterialViewerFrame.h"

#include "MaterialPanel.h"
#include "MaterialPreviewPane.h"
#include "W3dChunkTree.h"
#include "W3dMaterialParser.h"
#include "W3dMaterialWriter.h"
#include "../GraphicView.h"
#include "../resource.h"
#include "../Utils.h"
#include "../W3DDarkMode.h"
#include "../W3DViewDoc.h"

#include "assetmgr.h"
#include "dx8wrapper.h"
#include "ffactory.h"
#include "shader.h"
#include <d3dx8tex.h>
#include <map>
#include <utility>

CMaterialViewerFrame *CMaterialViewerFrame::_TheInstance = nullptr;

namespace
{
	const int PREVIEW_ID = 1;
	const int PLACEHOLDER_ID = 2;

	// Ground-height slider (Qt QSlider): strip width left of the preview, and
	// tick count (position resolution) across the pane's z range.
	const int GROUND_SLIDER_WIDTH = 26;
	const int GROUND_SLIDER_STEPS = 1000;

	// Debounce timer for the live material preview: coalesce a burst of edits
	// (dragging a spin box, typing) into a single in-memory apply.
	const UINT_PTR LIVE_PREVIEW_TIMER = 0xA1;
	const UINT LIVE_PREVIEW_DELAY_MS = 200;

	// Preview pane takes this share of the client width; the panel gets the rest.
	const float PREVIEW_WIDTH_RATIO = 0.55F;

	// Floor for the file-tab strip across the top; the actual height comes from
	// the Qt tab bar's style/font (GetTabBarPreferredHeight).
	const int MIN_TAB_BAR_HEIGHT = 26;

	// Tab-bar interaction deferred out of the Qt signal stack (see the thunks).
	// wparam = tab index, lparam = 0 for select / 1 for close-button.
	const UINT WM_MATVIEWER_TAB_ACTION = WM_USER + 0x41;

	// Longest edge of the texture thumbnails shown in the panel's Textures tab.
	const int TEXTURE_PREVIEW_MAX_DIM = 96;

	// Decodes a small BGRA thumbnail for the texture using the engine's own
	// loader (_Create_DX8_Surface resolves through the file factory and falls
	// back from .tga to .dds), so the Qt panel never parses image formats.
	void Build_Texture_Preview(W3dMaterialViewer::TextureData &texture)
	{
		if (texture.name.empty()) {
			return;
		}

		IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
		if (device == nullptr) {
			return;
		}

		// Record where the texture was actually found (for the tooltip); mirror
		// the engine's .tga -> .dds fallback. Skip decoding entirely when the
		// file is missing so the panel can say so instead of showing the
		// missing-texture pattern.
		{
			file_auto_ptr file(_TheFileFactory, texture.name.c_str());
			if (file->Is_Available()) {
				texture.resolvedPath = file->File_Name();
			} else {
				std::string dds_name = texture.name;
				size_t ext = dds_name.rfind('.');
				if (ext != std::string::npos) {
					dds_name.replace(ext, std::string::npos, ".dds");
					file_auto_ptr dds_file(_TheFileFactory, dds_name.c_str());
					if (dds_file->Is_Available()) {
						texture.resolvedPath = dds_file->File_Name();
					}
				}
			}
			if (texture.resolvedPath.empty()) {
				return;
			}
		}

		IDirect3DSurface8 *source = DX8Wrapper::_Create_DX8_Surface(texture.name.c_str());
		if (source == nullptr) {
			return;
		}

		D3DSURFACE_DESC desc;
		if (SUCCEEDED(source->GetDesc(&desc)) && desc.Width > 0 && desc.Height > 0) {

			int width = (int)desc.Width;
			int height = (int)desc.Height;
			if (width >= height) {
				height = max(1, height * TEXTURE_PREVIEW_MAX_DIM / width);
				width = TEXTURE_PREVIEW_MAX_DIM;
			} else {
				width = max(1, width * TEXTURE_PREVIEW_MAX_DIM / height);
				height = TEXTURE_PREVIEW_MAX_DIM;
			}

			IDirect3DSurface8 *dest = nullptr;
			if (SUCCEEDED(device->CreateImageSurface(width, height, D3DFMT_A8R8G8B8, &dest))) {
				if (SUCCEEDED(::D3DXLoadSurfaceFromSurface(dest, nullptr, nullptr,
						source, nullptr, nullptr, D3DX_FILTER_TRIANGLE, 0))) {
					D3DLOCKED_RECT locked;
					if (SUCCEEDED(dest->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
						texture.previewPixels.resize((size_t)width * height * 4);
						for (int y = 0; y < height; y++) {
							memcpy(&texture.previewPixels[(size_t)y * width * 4],
								(const uint8_t *)locked.pBits + (size_t)y * locked.Pitch,
								(size_t)width * 4);
						}
						dest->UnlockRect();
						texture.previewWidth = width;
						texture.previewHeight = height;
					}
				}
				dest->Release();
			}
		}

		source->Release();
	}

	void Fill_Texture_Previews(W3dMaterialViewer::MaterialDocument &document)
	{
		// Meshes in one file usually share textures; decode each name only once.
		std::map<std::string, const W3dMaterialViewer::TextureData *> cache;

		for (size_t m = 0; m < document.meshes.size(); m++) {
			std::vector<W3dMaterialViewer::TextureData> &textures = document.meshes[m].textures;
			for (size_t t = 0; t < textures.size(); t++) {
				W3dMaterialViewer::TextureData &texture = textures[t];
				std::map<std::string, const W3dMaterialViewer::TextureData *>::const_iterator it = cache.find(texture.name);
				if (it != cache.end()) {
					texture.resolvedPath = it->second->resolvedPath;
					texture.previewWidth = it->second->previewWidth;
					texture.previewHeight = it->second->previewHeight;
					texture.previewPixels = it->second->previewPixels;
				} else {
					Build_Texture_Preview(texture);
					cache[texture.name] = &texture;
				}
			}
		}
	}
}

BEGIN_MESSAGE_MAP(CMaterialViewerFrame, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_ERASEBKGND()
	ON_COMMAND(IDM_MATVIEWER_OPEN, OnFileOpen)
	ON_COMMAND(IDM_MATVIEWER_CLOSE, OnFileClose)
	ON_COMMAND(IDM_MATVIEWER_SAVE, OnFileSave)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_SAVE, OnUpdateFileSave)
	ON_COMMAND(IDM_MATVIEWER_UNDO, OnEditUndo)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_UNDO, OnUpdateEditUndo)
	ON_COMMAND(IDM_MATVIEWER_REDO, OnEditRedo)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_REDO, OnUpdateEditRedo)
	ON_COMMAND(IDM_MATVIEWER_REVERT, OnEditRevert)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_REVERT, OnUpdateEditRevert)
	ON_COMMAND(IDM_MATVIEWER_SHOW_FULL, OnShowFullObject)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_SHOW_FULL, OnUpdateShowFullObject)
	ON_COMMAND(IDM_TOGGLE_ALPHA, OnToggleAlpha)
	ON_UPDATE_COMMAND_UI(IDM_TOGGLE_ALPHA, OnUpdateToggleAlpha)
	ON_COMMAND(IDM_SHADER_ADDITIVE, OnShaderAdditive)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ADDITIVE, OnUpdateShaderAdditive)
	ON_COMMAND(IDM_SHADER_ALPHA_TEST, OnShaderAlphaTest)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_TEST, OnUpdateShaderAlphaTest)
	ON_COMMAND(IDM_SHADER_ALPHA_BLEND, OnShaderAlphaBlend)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_BLEND, OnUpdateShaderAlphaBlend)
	ON_COMMAND(IDM_SHADER_ALPHA_BLEND_TEST, OnShaderAlphaBlendTest)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_ALPHA_BLEND_TEST, OnUpdateShaderAlphaBlendTest)
	ON_COMMAND(IDM_SHADER_DOUBLE_SIDED, OnShaderDoubleSided)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_DOUBLE_SIDED, OnUpdateShaderDoubleSided)
	ON_COMMAND(IDM_SHADER_BACKFACE_TINT, OnShaderBackfaceTint)
	ON_UPDATE_COMMAND_UI(IDM_SHADER_BACKFACE_TINT, OnUpdateShaderBackfaceTint)
	ON_COMMAND(IDM_SHADER_BACKFACE_TINT_COLOR, OnShaderBackfaceTintColor)
	ON_COMMAND(IDM_MATVIEWER_LIGHT_FREEROAM, OnLightFreeRoam)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_LIGHT_FREEROAM, OnUpdateLightFreeRoam)
	ON_COMMAND(IDM_MATVIEWER_LIGHT_PERFACE, OnLightPerFace)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_LIGHT_PERFACE, OnUpdateLightPerFace)
	ON_COMMAND(IDM_MATVIEWER_GROUND, OnShowGround)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_GROUND, OnUpdateShowGround)
	ON_COMMAND(IDM_MATVIEWER_GROUND_RESET, OnGroundReset)
	ON_UPDATE_COMMAND_UI(IDM_MATVIEWER_GROUND_RESET, OnUpdateGroundReset)
	ON_WM_DROPFILES()
	ON_MESSAGE(WM_MATVIEWER_TAB_ACTION, OnTabAction)
	ON_MESSAGE(WM_W3DVIEW_THEME_CHANGED, OnThemeChanged)
END_MESSAGE_MAP()

// TheSuperHackers @feature W3D Shaders menu (same global ShaderClass filters
// as the main frame's View > W3D Shaders — the toggles affect both viewports
// since the renderer is shared).
void CMaterialViewerFrame::OnToggleAlpha()
{
	bool alpha = !ShaderClass::Is_Alpha_Blending_Forced();
	ShaderClass::Force_Alpha_Blending(alpha);
	::AfxGetApp()->WriteProfileInt("Config", "ForceAlphaBlend", alpha ? 1 : 0);
}

void CMaterialViewerFrame::OnUpdateToggleAlpha(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Is_Alpha_Blending_Forced() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderAdditive()
{
	ShaderClass::Set_Filter_Additive(!ShaderClass::Get_Filter_Additive());
}

void CMaterialViewerFrame::OnUpdateShaderAdditive(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Get_Filter_Additive() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderAlphaTest()
{
	ShaderClass::Set_Filter_Alpha_Test(!ShaderClass::Get_Filter_Alpha_Test());
}

void CMaterialViewerFrame::OnUpdateShaderAlphaTest(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Get_Filter_Alpha_Test() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderAlphaBlend()
{
	ShaderClass::Set_Filter_Alpha_Blend(!ShaderClass::Get_Filter_Alpha_Blend());
}

void CMaterialViewerFrame::OnUpdateShaderAlphaBlend(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Get_Filter_Alpha_Blend() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderAlphaBlendTest()
{
	ShaderClass::Set_Filter_Alpha_Blend_Test(!ShaderClass::Get_Filter_Alpha_Blend_Test());
}

void CMaterialViewerFrame::OnUpdateShaderAlphaBlendTest(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Get_Filter_Alpha_Blend_Test() ? 1 : 0);
}

void CMaterialViewerFrame::OnShaderDoubleSided()
{
	ShaderClass::Force_Double_Sided(!ShaderClass::Is_Double_Sided_Forced());
}

void CMaterialViewerFrame::OnUpdateShaderDoubleSided(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(ShaderClass::Is_Double_Sided_Forced() ? 1 : 0);
}

// TheSuperHackers @feature Tria The Back Faces Tinted toggle is a viewer-wide setting
// applied by CGraphicView's second render pass; mirror the command here so the menu
// item is enabled (not greyed) and its check stays in sync while the Material Viewer
// is the active frame.
void CMaterialViewerFrame::OnShaderBackfaceTint()
{
	CGraphicView::Set_Show_Backface_Tint(!CGraphicView::Is_Show_Backface_Tint());
}

void CMaterialViewerFrame::OnUpdateShaderBackfaceTint(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(CGraphicView::Is_Show_Backface_Tint() ? 1 : 0);
}

// TheSuperHackers @feature Light menu: how Ctrl+left-drag moves the preview's
// scene light. Free Roam = the main viewport's trackball rotation; Per Face =
// place the light on the surface point under the cursor. Persisted in the
// profile; menu uses check marks (SetCheck) to match the surrounding items.
void CMaterialViewerFrame::OnLightFreeRoam()
{
	if (m_Preview != nullptr) {
		m_Preview->Set_Light_Placement_Mode(CMaterialPreviewPane::LIGHT_FREE_ROAM);
	}
	::AfxGetApp()->WriteProfileInt("Config", "MatViewerLightPerFace", 0);
}

void CMaterialViewerFrame::OnUpdateLightFreeRoam(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck((m_Preview != nullptr
		&& m_Preview->Get_Light_Placement_Mode() == CMaterialPreviewPane::LIGHT_FREE_ROAM) ? 1 : 0);
}

void CMaterialViewerFrame::OnLightPerFace()
{
	if (m_Preview != nullptr) {
		m_Preview->Set_Light_Placement_Mode(CMaterialPreviewPane::LIGHT_PER_FACE);
	}
	::AfxGetApp()->WriteProfileInt("Config", "MatViewerLightPerFace", 1);
}

void CMaterialViewerFrame::OnUpdateLightPerFace(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck((m_Preview != nullptr
		&& m_Preview->Get_Light_Placement_Mode() == CMaterialPreviewPane::LIGHT_PER_FACE) ? 1 : 0);
}

// TheSuperHackers @feature Tria Ground plane: a simple checkered ground with a
// blob shadow under the object, drawn by the preview pane below the model. The
// vertical slider on the preview's left edge moves the plane's height and
// appears together with the toggle.
void CMaterialViewerFrame::OnShowGround()
{
	if (m_Preview == nullptr) {
		return;
	}
	m_Preview->Set_Ground_Visible(!m_Preview->Is_Ground_Visible());
	SyncGroundSlider();
	ReassertLayout();
}

void CMaterialViewerFrame::OnUpdateShowGround(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck((m_Preview != nullptr && m_Preview->Is_Ground_Visible()) ? 1 : 0);
}

void CMaterialViewerFrame::OnGroundReset()
{
	if (m_Preview == nullptr) {
		return;
	}
	m_Preview->Reset_Ground_To_Object();
	SyncGroundSlider();
}

void CMaterialViewerFrame::OnUpdateGroundReset(CCmdUI *cmd_ui)
{
	cmd_ui->Enable((m_Preview != nullptr && m_Preview->Is_Ground_Visible()) ? 1 : 0);
}

// Pushes the pane's current ground height onto the Qt slider (clamped into the
// current object's range so thumb and plane agree). Called when the toggle
// turns on and after every model change (which re-derives the range).
// Pushes the pane's current ground height onto the Qt slider (clamped into the
// current object's range so thumb and plane agree). Called when the toggle
// turns on and after every model change (which re-derives the range).
void CMaterialViewerFrame::SyncGroundSlider()
{
#ifdef W3DVIEW_HAS_QT
	if (m_Preview == nullptr || m_GroundSliderWnd == nullptr) {
		return;
	}

	int pos = GROUND_SLIDER_STEPS / 2;
	float z_min = 0.0F, z_max = 0.0F;
	if (m_Preview->Get_Ground_Z_Range(z_min, z_max) && z_max > z_min) {
		float z = m_Preview->Get_Ground_Z();
		z = (z < z_min) ? z_min : (z > z_max) ? z_max : z;
		m_Preview->Set_Ground_Z(z);
		pos = (int)((z_max - z) / (z_max - z_min) * GROUND_SLIDER_STEPS + 0.5F);
	}
	W3dMaterialViewer::SetGroundSliderPos(pos);
#endif
}

// User moved the Qt height slider: map its position (top = high) onto the
// pane's range. Fires synchronously on the UI thread from the Qt bridge.
void CMaterialViewerFrame::OnGroundSliderChanged(int pos)
{
	if (m_Preview == nullptr) {
		return;
	}
	float z_min = 0.0F, z_max = 0.0F;
	if (m_Preview->Get_Ground_Z_Range(z_min, z_max)) {
		float frac = (float)pos / (float)GROUND_SLIDER_STEPS;
		m_Preview->Set_Ground_Z(z_max - frac * (z_max - z_min));
	}
}

void CMaterialViewerFrame::GroundSliderChangedThunk(int pos)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnGroundSliderChanged(pos);
	}
}

// TheSuperHackers @feature Tria Pick the back-face tint colour via the Qt colour
// picker (to match the rest of the Material Viewer UI). Converts between the viewer's
// 0..1 float Vector3 and Win32's 0..255 COLORREF.
void CMaterialViewerFrame::OnShaderBackfaceTintColor()
{
	COLORREF initial = CGraphicView::Tint_Color_To_ColorRef(CGraphicView::Get_Backface_Tint_Color());

	COLORREF chosen = initial;
	if (W3dMaterialViewer::PickColor(initial, chosen)) {
		CGraphicView::Set_Backface_Tint_Color(CGraphicView::ColorRef_To_Tint_Color(chosen));
	}
}

CMaterialViewerFrame::CMaterialViewerFrame()
	: m_Preview(nullptr),
	  m_PanelWnd(nullptr),
	  m_TabBarWnd(nullptr),
	  m_GroundSliderWnd(nullptr),
	  m_ActiveTab(-1),
	  m_LivePreviewPending(false)
{
}

MaterialViewerTab &
CMaterialViewerFrame::Active()
{
	static MaterialViewerTab s_scratch;
	if (m_ActiveTab < 0 || m_ActiveTab >= (int)m_Tabs.size()) {
		s_scratch = MaterialViewerTab();
		return s_scratch;
	}
	return m_Tabs[m_ActiveTab];
}

CMaterialViewerFrame::~CMaterialViewerFrame()
{
}

void
CMaterialViewerFrame::ShowViewer()
{
	if (_TheInstance == nullptr) {
		CMaterialViewerFrame *frame = new CMaterialViewerFrame;
		if (frame->Create(nullptr,
				"W3D Material Viewer",
				WS_OVERLAPPEDWINDOW,
				rectDefault,
				::AfxGetMainWnd(),
				MAKEINTRESOURCE(IDR_MATERIAL_VIEWER_MENU)) == FALSE) {
			delete frame;
			return;
		}
		_TheInstance = frame;
		frame->ShowWindow(SW_SHOW);
		frame->UpdateWindow();
	} else {
		if (_TheInstance->IsIconic()) {
			_TheInstance->ShowWindow(SW_RESTORE);
		}
		_TheInstance->SetForegroundWindow();
	}
}

void
CMaterialViewerFrame::ShowViewerForAsset(const char *objName, const char *sourceFile, const char *meshName)
{
	ShowViewer();
	if (_TheInstance == nullptr || objName == nullptr || objName[0] == '\0') {
		return;
	}

	CMaterialViewerFrame *frame = _TheInstance;

	MaterialViewerTab tab;
	bool parsed = false;
	if (sourceFile != nullptr && sourceFile[0] != '\0') {
		parsed = W3dMaterialViewer::ParseMaterialDocumentFromFactory(sourceFile, tab.document);
	}

	// If the file (or, for source-less assets, the same top-level object) is
	// already open in a tab, switch to it instead of opening a duplicate.
	int existing = -1;
	if (parsed) {
		tab.sourceFilePath = tab.document.resolvedDiskPath;
		existing = frame->FindTab(tab.sourceFilePath.c_str());
	} else {
		for (size_t i = 0; i < frame->m_Tabs.size(); i++) {
			if (frame->m_Tabs[i].sourceFilePath.empty()
					&& ::_stricmp(frame->m_Tabs[i].topLevelName.c_str(), objName) == 0) {
				existing = (int)i;
				break;
			}
		}
	}
	if (existing >= 0) {
		if (existing != frame->m_ActiveTab) {
			if (!frame->PromptSaveIfDirty()) {
				return;
			}
			frame->ActivateTab(existing);
		}
		if (meshName != nullptr && meshName[0] != '\0') {
			frame->Active().currentMeshName = meshName;
#ifdef W3DVIEW_HAS_QT
			W3dMaterialViewer::SelectPanelMesh(meshName);
#endif
			frame->UpdatePreviewModel();
		}
		return;
	}

	if (parsed) {
		Fill_Texture_Previews(tab.document);
	}

	// The full object is the parsed HLod when available, else the clicked asset.
	tab.topLevelName = (parsed && !tab.document.topLevelName.empty())
		? tab.document.topLevelName : objName;
	if (meshName != nullptr && meshName[0] != '\0') {
		tab.currentMeshName = meshName;
	} else if (parsed && !tab.document.meshes.empty()) {
		tab.currentMeshName = tab.document.meshes.front().meshName;
	} else {
		tab.currentMeshName = objName;
	}

	if (!frame->PromptSaveIfDirty()) {
		return;
	}
	frame->AppendTabAndActivate(tab);
}

void
CMaterialViewerFrame::RenderActivePreview()
{
	if (_TheInstance != nullptr) {
#ifdef W3DVIEW_HAS_QT
		W3dMaterialViewer::PumpQtEvents();
#endif
		if (_TheInstance->m_Preview != nullptr) {
			_TheInstance->m_Preview->Render();
		}
	}
}

bool
CMaterialViewerFrame::HasQtPanel()
{
	return (_TheInstance != nullptr) && (_TheInstance->m_PanelWnd != nullptr);
}

void
CMaterialViewerFrame::NotifyThemeChanged()
{
	if (_TheInstance != nullptr && ::IsWindow(_TheInstance->m_hWnd)) {
		_TheInstance->PostMessage(WM_W3DVIEW_THEME_CHANGED, W3DDarkMode::IsDark() ? 1 : 0, 0);
	}
}

// When keyboard focus is inside the Qt panel, hand keyboard messages straight
// to the focused window instead of letting the frame translate them. The frame
// owns an accelerator table (arrows / PgUp / PgDn / Ctrl+combos for the main
// window) and CFrameWnd also does dialog-style key routing; both would eat the
// editing keys the Qt text fields and spin boxes need (Delete, Home/End,
// arrows, Ctrl+A, Backspace, ...), so the user could only append text.
BOOL
CMaterialViewerFrame::PreTranslateMessage(MSG *msg)
{
	// The File/Edit menu shortcuts (Ctrl+O/S/Z, Ctrl+Shift+Z) are advertised in
	// the menu but the frame has no accelerator table, and the Qt hand-off below
	// would otherwise swallow them. Handle these viewer-level commands here so
	// they work regardless of whether focus is in the Qt panel or the preview.
	if (msg->message == WM_KEYDOWN && (::GetKeyState(VK_CONTROL) & 0x8000)) {
		const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;

		// Ctrl+Tab / Ctrl+Shift+Tab cycle through the file tabs (wrapping).
		// Goes through OnTabChanged so unsaved edits get the same
		// Save/Discard/Cancel prompt as a tab-strip click.
		if (msg->wParam == VK_TAB && m_Tabs.size() > 1) {
			const int count = (int)m_Tabs.size();
			OnTabChanged(shift ? (m_ActiveTab - 1 + count) % count
				: (m_ActiveTab + 1) % count);
			return TRUE;
		}

		UINT command = 0;
		switch (msg->wParam) {
			case 'S': command = IDM_MATVIEWER_SAVE; break;
			case 'O': command = IDM_MATVIEWER_OPEN; break;
			case 'Z': command = shift ? IDM_MATVIEWER_REDO : IDM_MATVIEWER_UNDO; break;
		}
		if (command != 0) {
			SendMessage(WM_COMMAND, MAKEWPARAM(command, 0), 0);
			return TRUE;
		}
	}

	if (msg->message >= WM_KEYFIRST && msg->message <= WM_KEYLAST) {
#ifdef W3DVIEW_HAS_QT
		HWND panel = m_PanelWnd;
		if (panel != nullptr && ::IsWindow(panel)) {
			HWND focus = ::GetFocus();
			if (focus != nullptr && (focus == panel || ::IsChild(panel, focus))) {
				// Dispatch to Qt's wndproc directly; skip the frame's
				// accelerator / IsDialogMessage handling entirely.
				::TranslateMessage(msg);
				::DispatchMessage(msg);
				return TRUE;
			}
		}
#endif
	}

	return CFrameWnd::PreTranslateMessage(msg);
}

int
CMaterialViewerFrame::OnCreate(LPCREATESTRUCT create_struct)
{
	if (CFrameWnd::OnCreate(create_struct) == -1) {
		return -1;
	}

	m_Preview = new CMaterialPreviewPane;
	m_Preview->Create(this, CRect(0, 0, 10, 10), PREVIEW_ID);

	m_Preview->Set_Light_Placement_Mode(
		::AfxGetApp()->GetProfileInt("Config", "MatViewerLightPerFace", 1) != 0
			? CMaterialPreviewPane::LIGHT_PER_FACE
			: CMaterialPreviewPane::LIGHT_FREE_ROAM);

#ifdef W3DVIEW_HAS_QT
	m_PanelWnd = W3dMaterialViewer::CreatePanel(m_hWnd);
	m_TabBarWnd = W3dMaterialViewer::CreateTabBar(m_hWnd);
	if (m_TabBarWnd != nullptr) {
		W3dMaterialViewer::SetTabBarChangedCallback(&CMaterialViewerFrame::TabChangedThunk);
		W3dMaterialViewer::SetTabBarCloseRequestedCallback(&CMaterialViewerFrame::TabCloseRequestedThunk);
		::ShowWindow(m_TabBarWnd, SW_HIDE);	// shown once the first tab opens
	}
	// Ground-height slider: a Qt QSlider so it follows the panel's dark theme
	// (the Win32 trackbar has no dark mode). Thin vertical strip left of the
	// preview, shown only while Ground > Show Ground Plane is on.
	m_GroundSliderWnd = W3dMaterialViewer::CreateGroundSlider(m_hWnd, GROUND_SLIDER_STEPS);
	if (m_GroundSliderWnd != nullptr) {
		W3dMaterialViewer::SetGroundSliderChangedCallback(&CMaterialViewerFrame::GroundSliderChangedThunk);
		W3dMaterialViewer::SetGroundSliderPos(GROUND_SLIDER_STEPS / 2);
		::ShowWindow(m_GroundSliderWnd, SW_HIDE);	// shown with the toggle
	}

	W3dMaterialViewer::SetPanelMeshSelectedCallback(&CMaterialViewerFrame::PanelMeshSelectedThunk);
	W3dMaterialViewer::SetPanelEditGateCallback(&CMaterialViewerFrame::EditGateThunk);
	W3dMaterialViewer::SetPanelSaveCallback(&CMaterialViewerFrame::SaveThunk);
	W3dMaterialViewer::SetPanelRevertCallback(&CMaterialViewerFrame::RevertThunk);
	W3dMaterialViewer::SetPanelDirtyChangedCallback(&CMaterialViewerFrame::DirtyChangedThunk);
	W3dMaterialViewer::SetPanelResolveTextureCallback(&CMaterialViewerFrame::ResolveTextureThunk);
	W3dMaterialViewer::SetPanelLivePreviewCallback(&CMaterialViewerFrame::LivePreviewThunk);
#endif

	if (m_PanelWnd == nullptr) {
		m_PlaceholderText.Create(
			"This build does not include the Qt material panel.",
			WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
			CRect(0, 0, 10, 10), this, PLACEHOLDER_ID);
	}

	// Pause the main viewport for the viewer's lifetime: both viewports share
	// the one D3D8 device, and running them in lockstep has proven fragile.
	// OnDestroy unpauses and refreshes (device reset) the main view.
	{
		CW3DViewDoc *doc = ::GetCurrentDocument();
		CGraphicView *view = (doc != nullptr) ? doc->GetGraphicView() : nullptr;
		if (view != nullptr) {
			view->Allow_Update(false);
		}
	}

	// Accept .w3d files dropped anywhere on the window (each opens as a tab).
	DragAcceptFiles(TRUE);

	W3DDarkMode::ApplyToWindow(m_hWnd);
	ApplyPanelTheme();

	// Same follow-up as InitInstance does for the main frame: non-client
	// pixels painted before the dark-mode subclasses were installed (the
	// menubar's bottom line) stay stale without a forced frame redraw.
	SetWindowPos(nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	RedrawWindow(nullptr, nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
	return 0;
}

void
CMaterialViewerFrame::OnClose()
{
	// Guard unsaved edits before the window (and its document) goes away.
	if (!PromptSaveIfDirty()) {
		return;
	}

	// Hand activation back to the owner before the window dies. Otherwise
	// Windows picks the next window in the Alt+Tab order, which can shove the
	// main frame to the background or even minimize it.
	CWnd *owner = GetOwner();
	if (owner != nullptr && ::IsWindow(owner->m_hWnd)) {
		owner->SetForegroundWindow();
	}
	CFrameWnd::OnClose();
}

void
CMaterialViewerFrame::OnDestroy()
{
	KillTimer(LIVE_PREVIEW_TIMER);	// no late live-preview fire after teardown

#ifdef W3DVIEW_HAS_QT
	if (m_TabBarWnd != nullptr) {
		W3dMaterialViewer::SetTabBarChangedCallback(nullptr);
		W3dMaterialViewer::SetTabBarCloseRequestedCallback(nullptr);
		W3dMaterialViewer::DestroyTabBar();
		m_TabBarWnd = nullptr;
	}
	if (m_GroundSliderWnd != nullptr) {
		W3dMaterialViewer::SetGroundSliderChangedCallback(nullptr);
		W3dMaterialViewer::DestroyGroundSlider();
		m_GroundSliderWnd = nullptr;
	}
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::DestroyPanel();
		m_PanelWnd = nullptr;
	}
#endif

	// Resume the main viewport (paused in OnCreate) and queue a full refresh
	// (F5 path: device reset) so it comes back to a pristine device state.
	{
		CW3DViewDoc *doc = ::GetCurrentDocument();
		CGraphicView *view = (doc != nullptr) ? doc->GetGraphicView() : nullptr;
		if (view != nullptr) {
			view->Allow_Update(true);
		}
		CWnd *main_wnd = ::AfxGetMainWnd();
		if (main_wnd != nullptr && ::IsWindow(main_wnd->m_hWnd)) {
			main_wnd->PostMessage(WM_COMMAND, IDM_REFRESH_VIEWPORT);
		}
	}

	CFrameWnd::OnDestroy();
}

void
CMaterialViewerFrame::PostNcDestroy()
{
	if (_TheInstance == this) {
		_TheInstance = nullptr;
	}
	m_Preview = nullptr;	// destroyed with the window; freed by MFC PostNcDestroy
	CFrameWnd::PostNcDestroy();
}

void
CMaterialViewerFrame::OnSize(UINT type, int cx, int cy)
{
	CFrameWnd::OnSize(type, cx, cy);
	if (cx > 0 && cy > 0) {
		Layout(cx, cy);
	}
}

// The frame's client area is normally fully covered by the panes, but fill
// it with the theme background so any sliver that isn't never shows white.
BOOL
CMaterialViewerFrame::OnEraseBkgnd(CDC *dc)
{
	if (W3DDarkMode::IsDark()) {
		CRect rect;
		GetClientRect(&rect);
		::FillRect(dc->m_hDC, &rect, W3DDarkMode::GetDlgBackgroundBrush());
		return TRUE;
	}

	return CFrameWnd::OnEraseBkgnd(dc);
}

void
CMaterialViewerFrame::ReassertLayout()
{
	CRect rect;
	GetClientRect(&rect);
	if (rect.Width() > 0 && rect.Height() > 0) {
		Layout(rect.Width(), rect.Height());
	}
}

void
CMaterialViewerFrame::Layout(int cx, int cy)
{
	// File-tab strip across the top, shown only while tabs are open.
	int tab_height = 0;
#ifdef W3DVIEW_HAS_QT
	if (m_TabBarWnd != nullptr && HasTabs()) {
		tab_height = W3dMaterialViewer::GetTabBarPreferredHeight();
		if (tab_height < MIN_TAB_BAR_HEIGHT) {
			tab_height = MIN_TAB_BAR_HEIGHT;
		}
		W3dMaterialViewer::ResizeTabBar(cx, tab_height);
		::MoveWindow(m_TabBarWnd, 0, 0, cx, tab_height, TRUE);
	}
#endif

	int body_height = cy - tab_height;
	int preview_width = (int)(cx * PREVIEW_WIDTH_RATIO);

	// Ground-height slider strip (Qt QSlider) on the preview's left, only while
	// the ground plane is shown; the preview shrinks by the strip's width.
	int slider_width = 0;
#ifdef W3DVIEW_HAS_QT
	if (m_GroundSliderWnd != nullptr) {
		bool visible = (m_Preview != nullptr && m_Preview->Is_Ground_Visible());
		slider_width = visible ? GROUND_SLIDER_WIDTH : 0;
		if (visible) {
			// Resize through Qt first so its logical geometry matches (same
			// dance as the panel/tab bar).
			W3dMaterialViewer::ResizeGroundSlider(slider_width, body_height);
			::MoveWindow(m_GroundSliderWnd, 0, tab_height, slider_width, body_height, TRUE);
			::ShowWindow(m_GroundSliderWnd, SW_SHOW);
		} else {
			::ShowWindow(m_GroundSliderWnd, SW_HIDE);
		}
	}
#endif

	if ((m_Preview != nullptr) && ::IsWindow(m_Preview->m_hWnd)) {
		m_Preview->MoveWindow(slider_width, tab_height, preview_width - slider_width, body_height);
	}

	if (m_PanelWnd != nullptr) {
#ifdef W3DVIEW_HAS_QT
		// Resize through Qt first so its logical geometry/layout match; the
		// MoveWindow then pins the child's position (Qt may have placed it at
		// a stale pre-reparent position).
		W3dMaterialViewer::ResizePanel(cx - preview_width, body_height);
#endif
		::MoveWindow(m_PanelWnd, preview_width, tab_height, cx - preview_width, body_height, TRUE);
	} else if (::IsWindow(m_PlaceholderText.m_hWnd)) {
		m_PlaceholderText.MoveWindow(preview_width, tab_height, cx - preview_width, body_height);
	}
}

void
CMaterialViewerFrame::OnFileOpen()
{
	CFileDialog dialog(TRUE, ".w3d", nullptr,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT,
		"Westwood 3D Files (*.w3d)|*.w3d|All Files (*.*)|*.*||", this);

	// Room for many selected paths; the default OPENFILENAME buffer holds one.
	std::vector<char> path_buffer(32768, '\0');
	dialog.m_ofn.lpstrFile = &path_buffer[0];
	dialog.m_ofn.nMaxFile = (DWORD)path_buffer.size();

	if (dialog.DoModal() != IDOK) {
		return;
	}

	POSITION pos = dialog.GetStartPosition();
	while (pos != nullptr) {
		OpenFileInTab(dialog.GetNextPathName(pos));
	}
}

void
CMaterialViewerFrame::OnFileClose()
{
	// Closes the active tab; the window itself goes when the last tab does.
	if (HasTabs()) {
		CloseTab(m_ActiveTab);
	} else {
		PostMessage(WM_CLOSE);
	}
}

// Dropping .w3d files anywhere on the viewer opens each one as a tab (a file
// that is already open just gets its tab activated). Other files are ignored.
void
CMaterialViewerFrame::OnDropFiles(HDROP drop)
{
	UINT count = ::DragQueryFile(drop, 0xFFFFFFFF, nullptr, 0);
	for (UINT i = 0; i < count; i++) {
		char path[MAX_PATH] = { 0 };
		if (::DragQueryFile(drop, i, path, sizeof(path)) == 0) {
			continue;
		}
		const char *ext = ::strrchr(path, '.');
		if (ext == nullptr || ::_stricmp(ext, ".w3d") != 0) {
			continue;
		}
		OpenFileInTab(path);
	}
	::DragFinish(drop);
}

////////////////////////////////////////////////////////////////////////////
//	Tabs
////////////////////////////////////////////////////////////////////////////

int
CMaterialViewerFrame::FindTab(const char *path) const
{
	if (path == nullptr || path[0] == '\0') {
		return -1;
	}
	for (size_t i = 0; i < m_Tabs.size(); i++) {
		if (::_stricmp(m_Tabs[i].sourceFilePath.c_str(), path) == 0) {
			return (int)i;
		}
	}
	return -1;
}

std::string
CMaterialViewerFrame::TabLabel(const MaterialViewerTab &tab)
{
	std::string label;
	if (!tab.sourceFilePath.empty()) {
		const char *slash = ::strrchr(tab.sourceFilePath.c_str(), '\\');
		const char *fwd = ::strrchr(tab.sourceFilePath.c_str(), '/');
		if (fwd > slash) {
			slash = fwd;
		}
		label = slash ? slash + 1 : tab.sourceFilePath.c_str();
	} else if (!tab.topLevelName.empty()) {
		label = tab.topLevelName;
	} else {
		label = "(untitled)";
	}
	if (tab.dirty) {
		label += " *";
	}
	return label;
}

bool
CMaterialViewerFrame::OpenFileInTab(const char *path, const char *meshName)
{
	if (path == nullptr || path[0] == '\0') {
		return false;
	}

	int existing = FindTab(path);
	if (existing >= 0) {
		if (existing != m_ActiveTab) {
			if (!PromptSaveIfDirty()) {
				return false;
			}
			ActivateTab(existing);
		}
		if (meshName != nullptr && meshName[0] != '\0') {
			Active().currentMeshName = meshName;
#ifdef W3DVIEW_HAS_QT
			W3dMaterialViewer::SelectPanelMesh(meshName);
#endif
			UpdatePreviewModel();
		}
		return true;
	}

	// The panel always reflects the actual bytes of the chosen file.
	MaterialViewerTab tab;
	if (W3dMaterialViewer::ParseMaterialDocument(path, tab.document) == false) {
		CString message;
		message.Format("%s\n\nThe file could not be read or contains no meshes.", path);
		::MessageBox(m_hWnd, message, "W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	if (!PromptSaveIfDirty()) {
		return false;
	}

	tab.sourceFilePath = path;
	Fill_Texture_Previews(tab.document);
	tab.topLevelName = tab.document.topLevelName;
	if (meshName != nullptr && meshName[0] != '\0') {
		tab.currentMeshName = meshName;
	} else {
		tab.currentMeshName = tab.document.meshes.empty() ? tab.document.topLevelName
			: tab.document.meshes.front().meshName;
	}

	AppendTabAndActivate(tab);
	return true;
}

void
CMaterialViewerFrame::AppendTabAndActivate(MaterialViewerTab &tab)
{
	m_Tabs.push_back(std::move(tab));
	ActivateTab((int)m_Tabs.size() - 1);
}

void
CMaterialViewerFrame::SnapshotActiveTab()
{
	if (m_ActiveTab < 0 || m_ActiveTab >= (int)m_Tabs.size()) {
		return;
	}
	MaterialViewerTab &tab = m_Tabs[m_ActiveTab];
	if (m_Preview != nullptr) {
		m_Preview->Get_Camera_State(tab.cameraTransform, tab.cameraCenter,
			tab.cameraRotation, tab.cameraDistance, tab.cameraMinZoomAdjust);
		tab.cameraValid = true;
	}
}

void
CMaterialViewerFrame::ActivateTab(int index)
{
	if (index < 0 || index >= (int)m_Tabs.size()) {
		return;
	}

	SnapshotActiveTab();
	m_ActiveTab = index;
	MaterialViewerTab &tab = m_Tabs[index];

#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::SetPanelDocument(tab.document);
		if (!tab.currentMeshName.empty()) {
			W3dMaterialViewer::SelectPanelMesh(tab.currentMeshName.c_str());
		}
	}
#endif

	// Different files can carry identically-named prototypes (Load_3D_Assets
	// silently skips duplicates), so reload this file's assets to guarantee
	// the preview shows ITS bytes. Ends with UpdatePreviewModel; a source-less
	// tab (asset straight from the tree) just re-targets the preview.
	if (!tab.sourceFilePath.empty()) {
		ReloadAssetsForPreview();
	} else {
		UpdatePreviewModel();
	}

	// Restore this tab's saved view. Must come after the model load above,
	// which re-frames the camera whenever the loaded name changes.
	if (tab.cameraValid && m_Preview != nullptr) {
		m_Preview->Set_Camera_State(tab.cameraTransform, tab.cameraCenter,
			tab.cameraRotation, tab.cameraDistance, tab.cameraMinZoomAdjust);
	}

	RebuildTabBar();
	UpdateTitle();
}

bool
CMaterialViewerFrame::CloseTab(int index)
{
	if (index < 0 || index >= (int)m_Tabs.size()) {
		return true;
	}

	// Only the active tab can hold unsaved edits (switching away prompts).
	if (index == m_ActiveTab && !PromptSaveIfDirty()) {
		return false;
	}

	m_Tabs.erase(m_Tabs.begin() + index);

	if (m_Tabs.empty()) {
		m_ActiveTab = -1;
		RebuildTabBar();
		PostMessage(WM_CLOSE);
		return true;
	}

	if (index < m_ActiveTab) {
		// Indices above the erased tab shift down; the active file is unchanged.
		m_ActiveTab--;
		RebuildTabBar();
	} else if (index == m_ActiveTab) {
		// The active tab went away: show its right-hand neighbour (or the new
		// last tab). Invalidate first so ActivateTab does not snapshot the
		// camera into whatever tab now occupies the dead tab's index.
		m_ActiveTab = -1;
		ActivateTab((index < (int)m_Tabs.size()) ? index : (int)m_Tabs.size() - 1);
	} else {
		RebuildTabBar();
	}
	return true;
}

void
CMaterialViewerFrame::RebuildTabBar()
{
#ifdef W3DVIEW_HAS_QT
	if (m_TabBarWnd == nullptr) {
		return;
	}

	std::vector<std::string> labels;
	std::vector<const char *> label_ptrs;
	std::vector<const char *> tip_ptrs;
	labels.reserve(m_Tabs.size());
	for (size_t i = 0; i < m_Tabs.size(); i++) {
		labels.push_back(TabLabel(m_Tabs[i]));
	}
	for (size_t i = 0; i < m_Tabs.size(); i++) {
		label_ptrs.push_back(labels[i].c_str());
		tip_ptrs.push_back(m_Tabs[i].sourceFilePath.c_str());
	}

	W3dMaterialViewer::SetTabBarTabs(
		label_ptrs.empty() ? nullptr : &label_ptrs[0],
		tip_ptrs.empty() ? nullptr : &tip_ptrs[0],
		(int)m_Tabs.size(), m_ActiveTab);

	::ShowWindow(m_TabBarWnd, HasTabs() ? SW_SHOW : SW_HIDE);
	ReassertLayout();
#endif
}

void
CMaterialViewerFrame::OnTabChanged(int index)
{
	if (index == m_ActiveTab || index < 0 || index >= (int)m_Tabs.size()) {
		return;
	}
	if (!PromptSaveIfDirty()) {
		// Cancelled: snap the strip's selection back to the active tab.
#ifdef W3DVIEW_HAS_QT
		W3dMaterialViewer::SetTabBarCurrent(m_ActiveTab);
#endif
		return;
	}
	ActivateTab(index);
}

void
CMaterialViewerFrame::OnTabCloseRequested(int index)
{
	CloseTab(index);
}

LRESULT
CMaterialViewerFrame::OnTabAction(WPARAM wparam, LPARAM lparam)
{
	if (lparam == 0) {
		OnTabChanged((int)wparam);
	} else {
		OnTabCloseRequested((int)wparam);
	}
	return 0;
}

void
CMaterialViewerFrame::TabChangedThunk(int index)
{
	if (_TheInstance != nullptr && ::IsWindow(_TheInstance->m_hWnd)) {
		_TheInstance->PostMessage(WM_MATVIEWER_TAB_ACTION, (WPARAM)index, 0);
	}
}

void
CMaterialViewerFrame::TabCloseRequestedThunk(int index)
{
	if (_TheInstance != nullptr && ::IsWindow(_TheInstance->m_hWnd)) {
		_TheInstance->PostMessage(WM_MATVIEWER_TAB_ACTION, (WPARAM)index, 1);
	}
}

////////////////////////////////////////////////////////////////////////////
//
//	Edit-mode menu commands (File > Save, Edit > Undo/Redo/Revert). These
//	mirror the panel's in-window buttons; each item is enabled only when the
//	corresponding panel action is available.
//
void
CMaterialViewerFrame::OnFileSave()
{
#ifdef W3DVIEW_HAS_QT
	W3dMaterialViewer::RequestPanelSave();
#endif
}

void
CMaterialViewerFrame::OnUpdateFileSave(CCmdUI *cmd_ui)
{
#ifdef W3DVIEW_HAS_QT
	cmd_ui->Enable(W3dMaterialViewer::PanelCanSaveOrRevert());
#else
	cmd_ui->Enable(FALSE);
#endif
}

void
CMaterialViewerFrame::OnEditUndo()
{
#ifdef W3DVIEW_HAS_QT
	W3dMaterialViewer::RequestPanelUndo();
#endif
}

void
CMaterialViewerFrame::OnUpdateEditUndo(CCmdUI *cmd_ui)
{
#ifdef W3DVIEW_HAS_QT
	cmd_ui->Enable(W3dMaterialViewer::PanelCanUndo());
#else
	cmd_ui->Enable(FALSE);
#endif
}

void
CMaterialViewerFrame::OnEditRedo()
{
#ifdef W3DVIEW_HAS_QT
	W3dMaterialViewer::RequestPanelRedo();
#endif
}

void
CMaterialViewerFrame::OnUpdateEditRedo(CCmdUI *cmd_ui)
{
#ifdef W3DVIEW_HAS_QT
	cmd_ui->Enable(W3dMaterialViewer::PanelCanRedo());
#else
	cmd_ui->Enable(FALSE);
#endif
}

void
CMaterialViewerFrame::OnEditRevert()
{
#ifdef W3DVIEW_HAS_QT
	W3dMaterialViewer::RequestPanelRevert();
#endif
}

void
CMaterialViewerFrame::OnUpdateEditRevert(CCmdUI *cmd_ui)
{
#ifdef W3DVIEW_HAS_QT
	cmd_ui->Enable(W3dMaterialViewer::PanelCanSaveOrRevert());
#else
	cmd_ui->Enable(FALSE);
#endif
}

////////////////////////////////////////////////////////////////////////////
//
//	Preview model selection: the pane shows the mesh selected in the material
//	panel unless the View > Show Full Object toggle is on.
//
void
CMaterialViewerFrame::UpdatePreviewModel()
{
	if (m_Preview == nullptr) {
		return;
	}

	MaterialViewerTab &tab = Active();

	const std::string &name = (tab.showFullObject && !tab.topLevelName.empty())
		? tab.topLevelName : tab.currentMeshName;

	if (!name.empty() && m_Preview->LoadModel(name.c_str()) == false) {
		// Fall back to the other candidate before giving up silently.
		const std::string &other = tab.showFullObject ? tab.currentMeshName : tab.topLevelName;
		if (!other.empty()) {
			m_Preview->LoadModel(other.c_str());
		}
	}

	// A freshly loaded render object comes from the pristine prototype; if the
	// user has unsaved edits, re-stamp them so switching meshes / toggling the
	// full-object view keeps showing the in-progress material changes. The
	// panel's latest edits live in livePreviewDoc (document only updates on
	// save), so prefer it while dirty.
	if (tab.dirty) {
		m_Preview->ApplyLiveMaterials(tab.livePreviewDoc);
	}

	// In the full-object view, outline the selected mesh so the user can see
	// which part of the object the panel is editing.
	m_Preview->Set_Highlight_Mesh((tab.showFullObject && !tab.currentMeshName.empty())
		? tab.currentMeshName.c_str() : nullptr);

	// The model (and so the ground plane's default height/range) may have changed.
	SyncGroundSlider();
}

void
CMaterialViewerFrame::OnShowFullObject()
{
	Active().showFullObject = !Active().showFullObject;
	UpdatePreviewModel();
}

void
CMaterialViewerFrame::OnUpdateShowFullObject(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(Active().showFullObject ? 1 : 0);
}

void
CMaterialViewerFrame::OnPanelMeshSelected(const char *meshName)
{
	if (meshName == nullptr || meshName[0] == '\0') {
		return;
	}
	Active().currentMeshName = meshName;
	if (!Active().showFullObject) {
		UpdatePreviewModel();
	} else if (m_Preview != nullptr) {
		// Full-object view: the displayed object is unchanged, only the
		// selection outline moves to the newly selected mesh.
		m_Preview->Set_Highlight_Mesh(meshName);
	}
}

void
CMaterialViewerFrame::PanelMeshSelectedThunk(const char *meshName)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnPanelMeshSelected(meshName);
	}
}

////////////////////////////////////////////////////////////////////////////
//	Edit mode
////////////////////////////////////////////////////////////////////////////

void
CMaterialViewerFrame::UpdateTitle()
{
	MaterialViewerTab &tab = Active();

	CString title;
	if (!HasTabs()) {
		title = "W3D Material Viewer";
	} else if (tab.sourceFilePath.empty()) {
		// Asset opened from the tree whose .w3d could not be located on disk.
		title.Format("W3D Material Viewer - %s (source file not found)", tab.topLevelName.c_str());
	} else {
		title.Format("W3D Material Viewer - %s", TabLabel(tab).c_str());
	}
	SetWindowText(title);

	// Keep the active tab's caption (and its dirty '*') in sync too.
#ifdef W3DVIEW_HAS_QT
	if (m_TabBarWnd != nullptr && m_ActiveTab >= 0 && m_ActiveTab < (int)m_Tabs.size()) {
		W3dMaterialViewer::SetTabBarLabel(m_ActiveTab, TabLabel(tab).c_str());
	}
#endif
}

// The panel runs this before entering edit mode: refuse to edit any file we
// cannot reproduce byte-for-byte through the generic chunk tree.
bool
CMaterialViewerFrame::RunEditGate()
{
	if (Active().sourceFilePath.empty()) {
		::MessageBox(m_hWnd,
			"This asset's source .w3d file could not be located on disk, so it cannot be edited.",
			"W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	std::string error;
	if (!W3dMaterialViewer::ChunkTreeRoundTripsIdentically(Active().sourceFilePath.c_str(), error)) {
		CString message;
		message.Format("This file cannot be edited safely:\n\n%s\n\n"
			"Editing is disabled to avoid corrupting it.", error.c_str());
		::MessageBox(m_hWnd, message, "W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	return true;
}

bool
CMaterialViewerFrame::SaveDocument(const W3dMaterialViewer::MaterialDocument &document)
{
	std::string error;
	if (!W3dMaterialViewer::SaveMaterialDocument(document, error)) {
		CString message;
		message.Format("The file could not be saved:\n\n%s", error.c_str());
		::MessageBox(m_hWnd, message, "W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	// Keep our working copy in sync with what is now on disk.
	Active().document = document;
	Active().dirty = false;

	ReloadAssetsForPreview();
	UpdateTitle();
	return true;
}

void
CMaterialViewerFrame::RevertDocument()
{
	MaterialViewerTab &tab = Active();
	if (tab.sourceFilePath.empty()) {
		return;
	}

	W3dMaterialViewer::MaterialDocument fresh;
	if (!W3dMaterialViewer::ParseMaterialDocument(tab.sourceFilePath.c_str(), fresh)) {
		return;
	}

	tab.document = fresh;
	Fill_Texture_Previews(tab.document);
	tab.dirty = false;

#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::SetPanelDocument(tab.document);
	}
#endif

	// The live preview still carries the reverted (edited) materials. Rebuild the
	// render object from the untouched prototype so it returns to the original
	// look; the tab is now clean so UpdatePreviewModel won't re-stamp any edits.
	UpdatePreviewModel();

	UpdateTitle();
}

// Force the asset manager to drop this file's prototypes and textures so the
// next load rebuilds them from the freshly saved bytes (Load_3D_Assets skips
// prototypes whose names already exist). The main viewport is paused while the
// viewer is open, so removing prototypes cannot disturb a live render.
void
CMaterialViewerFrame::ReloadAssetsForPreview()
{
	MaterialViewerTab &tab = Active();
	if (tab.sourceFilePath.empty()) {
		return;
	}

	// Release the preview's current render object first (it holds a ref that
	// would otherwise keep the old prototype's data alive).
	if (m_Preview != nullptr) {
		m_Preview->UnloadModel();
	}

	WW3DAssetManager *assets = WW3DAssetManager::Get_Instance();
	if (assets != nullptr) {
		for (const W3dMaterialViewer::MeshMaterialData &mesh : tab.document.meshes) {
			assets->Remove_Prototype(mesh.meshName.c_str());
		}
		if (!tab.document.topLevelName.empty()) {
			assets->Remove_Prototype(tab.document.topLevelName.c_str());
		}
		assets->Release_Unused_Textures();
	}

	CW3DViewDoc *doc = ::GetCurrentDocument();
	if (doc != nullptr) {
		doc->LoadAssetsFromFile(tab.sourceFilePath.c_str());
	}

	UpdatePreviewModel();
}

bool
CMaterialViewerFrame::PromptSaveIfDirty()
{
#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd == nullptr || !W3dMaterialViewer::PanelHasUnsavedChanges()) {
		return true;
	}

	int choice = ::MessageBox(m_hWnd,
		"Save the material changes before continuing?",
		"W3D Material Viewer", MB_ICONWARNING | MB_YESNOCANCEL);
	if (choice == IDCANCEL) {
		return false;
	}
	if (choice == IDYES) {
		return W3dMaterialViewer::RequestPanelSave();
	}
	// IDNO: discard by reloading the pristine file.
	RevertDocument();
	return true;
#else
	return true;
#endif
}

bool
CMaterialViewerFrame::EditGateThunk()
{
	return (_TheInstance != nullptr) && _TheInstance->RunEditGate();
}

bool
CMaterialViewerFrame::SaveThunk(const W3dMaterialViewer::MaterialDocument &document)
{
	return (_TheInstance != nullptr) && _TheInstance->SaveDocument(document);
}

void
CMaterialViewerFrame::RevertThunk()
{
	if (_TheInstance != nullptr) {
		_TheInstance->RevertDocument();
	}
}

void
CMaterialViewerFrame::OnPanelDirtyChanged(bool dirty)
{
	Active().dirty = dirty;
	UpdateTitle();
}

void
CMaterialViewerFrame::DirtyChangedThunk(bool dirty)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnPanelDirtyChanged(dirty);
	}
}

// Live-preview: stash the latest edited document and (re)arm the debounce
// timer. Coalescing avoids re-cloning materials on every spin-box tick.
void
CMaterialViewerFrame::OnPanelLivePreview(const W3dMaterialViewer::MaterialDocument &document)
{
	if (m_Preview == nullptr || !::IsWindow(m_hWnd)) {
		return;
	}
	Active().livePreviewDoc = document;
	m_LivePreviewPending = true;
	SetTimer(LIVE_PREVIEW_TIMER, LIVE_PREVIEW_DELAY_MS, nullptr);	// restart
}

void
CMaterialViewerFrame::LivePreviewThunk(const W3dMaterialViewer::MaterialDocument &document)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnPanelLivePreview(document);
	}
}

void
CMaterialViewerFrame::OnTimer(UINT_PTR event_id)
{
	if (event_id == LIVE_PREVIEW_TIMER) {
		KillTimer(LIVE_PREVIEW_TIMER);
		if (m_LivePreviewPending && m_Preview != nullptr) {
			m_LivePreviewPending = false;
			m_Preview->ApplyLiveMaterials(Active().livePreviewDoc);
		}
		return;
	}
	CFrameWnd::OnTimer(event_id);
}

// Re-decodes a single texture thumbnail when its filename is edited. Uses the
// same host-side decoder as the initial parse (D3D device + file factory).
void
CMaterialViewerFrame::ResolveTextureThunk(W3dMaterialViewer::TextureData &texture)
{
	Build_Texture_Preview(texture);
}

void
CMaterialViewerFrame::ApplyPanelTheme()
{
#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::PanelTheme theme;
		theme.dark = W3DDarkMode::IsDark();
		if (theme.dark) {
			theme.background = W3DDarkMode::GetDlgBackgroundColor();
			theme.ctrlBackground = W3DDarkMode::GetCtrlBackgroundColor();
			theme.text = W3DDarkMode::GetTextColor();
			theme.edge = W3DDarkMode::GetEdgeColor();
		}
		W3dMaterialViewer::ApplyPanelTheme(theme);
	}
#endif
}

LRESULT
CMaterialViewerFrame::OnThemeChanged(WPARAM wparam, LPARAM lparam)
{
	W3DDarkMode::ApplyToWindow(m_hWnd);
	ApplyPanelTheme();
	if (m_Preview != nullptr && ::IsWindow(m_Preview->m_hWnd)) {
		m_Preview->Invalidate();
	}
	return 0;
}
