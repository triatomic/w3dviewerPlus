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

CMaterialViewerFrame *CMaterialViewerFrame::_TheInstance = nullptr;

namespace
{
	const int PREVIEW_ID = 1;
	const int PLACEHOLDER_ID = 2;

	// Preview pane takes this share of the client width; the panel gets the rest.
	const float PREVIEW_WIDTH_RATIO = 0.55F;

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
	ON_WM_ERASEBKGND()
	ON_COMMAND(IDM_MATVIEWER_OPEN, OnFileOpen)
	ON_COMMAND(IDM_MATVIEWER_CLOSE, OnFileClose)
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

CMaterialViewerFrame::CMaterialViewerFrame()
	: m_Preview(nullptr),
	  m_PanelWnd(nullptr),
	  m_ShowFullObject(false),
	  m_Dirty(false)
{
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

	bool parsed = false;
	if (sourceFile != nullptr && sourceFile[0] != '\0') {
		parsed = W3dMaterialViewer::ParseMaterialDocumentFromFactory(sourceFile, frame->m_Document);
	}
	if (parsed) {
		Fill_Texture_Previews(frame->m_Document);
		frame->m_SourceFilePath = frame->m_Document.resolvedDiskPath;
	} else {
		frame->m_SourceFilePath.clear();
	}
	frame->m_Dirty = false;

	// The full object is the parsed HLod when available, else the clicked asset.
	frame->m_TopLevelName = (parsed && !frame->m_Document.topLevelName.empty())
		? frame->m_Document.topLevelName : objName;
	if (meshName != nullptr && meshName[0] != '\0') {
		frame->m_CurrentMeshName = meshName;
	} else if (parsed && !frame->m_Document.meshes.empty()) {
		frame->m_CurrentMeshName = frame->m_Document.meshes.front().meshName;
	} else {
		frame->m_CurrentMeshName = objName;
	}

#ifdef W3DVIEW_HAS_QT
	if (frame->m_PanelWnd != nullptr) {
		W3dMaterialViewer::SetPanelDocument(frame->m_Document);
		if (parsed && meshName != nullptr && meshName[0] != '\0') {
			W3dMaterialViewer::SelectPanelMesh(meshName);
		}
		frame->ReassertLayout();
	}
#endif

	frame->UpdatePreviewModel();

	if (parsed) {
		frame->UpdateTitle();
	} else {
		CString title;
		title.Format("W3D Material Viewer - %s (source file not found)", objName);
		frame->SetWindowText(title);
	}
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

#ifdef W3DVIEW_HAS_QT
	m_PanelWnd = W3dMaterialViewer::CreatePanel(m_hWnd);
	W3dMaterialViewer::SetPanelMeshSelectedCallback(&CMaterialViewerFrame::PanelMeshSelectedThunk);
	W3dMaterialViewer::SetPanelEditGateCallback(&CMaterialViewerFrame::EditGateThunk);
	W3dMaterialViewer::SetPanelSaveCallback(&CMaterialViewerFrame::SaveThunk);
	W3dMaterialViewer::SetPanelRevertCallback(&CMaterialViewerFrame::RevertThunk);
	W3dMaterialViewer::SetPanelDirtyChangedCallback(&CMaterialViewerFrame::DirtyChangedThunk);
	W3dMaterialViewer::SetPanelResolveTextureCallback(&CMaterialViewerFrame::ResolveTextureThunk);
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
#ifdef W3DVIEW_HAS_QT
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
	int preview_width = (int)(cx * PREVIEW_WIDTH_RATIO);

	if ((m_Preview != nullptr) && ::IsWindow(m_Preview->m_hWnd)) {
		m_Preview->MoveWindow(0, 0, preview_width, cy);
	}

	if (m_PanelWnd != nullptr) {
#ifdef W3DVIEW_HAS_QT
		// Resize through Qt first so its logical geometry/layout match; the
		// MoveWindow then pins the child's position (Qt may have placed it at
		// a stale pre-reparent position).
		W3dMaterialViewer::ResizePanel(cx - preview_width, cy);
#endif
		::MoveWindow(m_PanelWnd, preview_width, 0, cx - preview_width, cy, TRUE);
	} else if (::IsWindow(m_PlaceholderText.m_hWnd)) {
		m_PlaceholderText.MoveWindow(preview_width, 0, cx - preview_width, cy);
	}
}

void
CMaterialViewerFrame::OnFileOpen()
{
	// Opening a new file discards the current one; guard unsaved edits first.
	if (!PromptSaveIfDirty()) {
		return;
	}

	CFileDialog dialog(TRUE, ".w3d", nullptr,
		OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
		"Westwood 3D Files (*.w3d)|*.w3d|All Files (*.*)|*.*||", this);

	if (dialog.DoModal() != IDOK) {
		return;
	}

	CString path = dialog.GetPathName();

	// The panel always reflects the actual bytes of the chosen file.
	if (W3dMaterialViewer::ParseMaterialDocument(path, m_Document) == false) {
		::MessageBox(m_hWnd, "The file could not be read or contains no meshes.",
			"W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return;
	}

	m_SourceFilePath = (LPCTSTR)path;

	// Load the assets through the document so texture-path handling and the
	// asset tree stay consistent with a normal File->Open.
	CW3DViewDoc *doc = ::GetCurrentDocument();
	if (doc != nullptr) {
		doc->LoadAssetsFromFile(path);
	}

	Fill_Texture_Previews(m_Document);

	m_TopLevelName = m_Document.topLevelName;
	m_CurrentMeshName = m_Document.meshes.empty() ? m_Document.topLevelName
		: m_Document.meshes.front().meshName;

#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::SetPanelDocument(m_Document);
		ReassertLayout();
	}
#endif

	UpdatePreviewModel();

	m_Dirty = false;
	UpdateTitle();
}

void
CMaterialViewerFrame::OnFileClose()
{
	PostMessage(WM_CLOSE);
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

	const std::string &name = (m_ShowFullObject && !m_TopLevelName.empty())
		? m_TopLevelName : m_CurrentMeshName;

	if (!name.empty() && m_Preview->LoadModel(name.c_str()) == false) {
		// Fall back to the other candidate before giving up silently.
		const std::string &other = m_ShowFullObject ? m_CurrentMeshName : m_TopLevelName;
		if (!other.empty()) {
			m_Preview->LoadModel(other.c_str());
		}
	}
}

void
CMaterialViewerFrame::OnShowFullObject()
{
	m_ShowFullObject = !m_ShowFullObject;
	UpdatePreviewModel();
}

void
CMaterialViewerFrame::OnUpdateShowFullObject(CCmdUI *cmd_ui)
{
	cmd_ui->SetCheck(m_ShowFullObject ? 1 : 0);
}

void
CMaterialViewerFrame::OnPanelMeshSelected(const char *meshName)
{
	if (meshName == nullptr || meshName[0] == '\0') {
		return;
	}
	m_CurrentMeshName = meshName;
	if (!m_ShowFullObject) {
		UpdatePreviewModel();
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
	CString title;
	if (m_SourceFilePath.empty()) {
		title = "W3D Material Viewer";
	} else {
		const char *slash = ::strrchr(m_SourceFilePath.c_str(), '\\');
		const char *fwd = ::strrchr(m_SourceFilePath.c_str(), '/');
		if (fwd > slash) {
			slash = fwd;
		}
		const char *name = slash ? slash + 1 : m_SourceFilePath.c_str();
		title.Format("W3D Material Viewer - %s%s", name, m_Dirty ? " *" : "");
	}
	SetWindowText(title);
}

// The panel runs this before entering edit mode: refuse to edit any file we
// cannot reproduce byte-for-byte through the generic chunk tree.
bool
CMaterialViewerFrame::RunEditGate()
{
	if (m_SourceFilePath.empty()) {
		::MessageBox(m_hWnd,
			"This asset's source .w3d file could not be located on disk, so it cannot be edited.",
			"W3D Material Viewer", MB_ICONEXCLAMATION | MB_OK);
		return false;
	}

	std::string error;
	if (!W3dMaterialViewer::ChunkTreeRoundTripsIdentically(m_SourceFilePath.c_str(), error)) {
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
	m_Document = document;
	m_Dirty = false;

	ReloadAssetsForPreview();
	UpdateTitle();
	return true;
}

void
CMaterialViewerFrame::RevertDocument()
{
	if (m_SourceFilePath.empty()) {
		return;
	}

	W3dMaterialViewer::MaterialDocument fresh;
	if (!W3dMaterialViewer::ParseMaterialDocument(m_SourceFilePath.c_str(), fresh)) {
		return;
	}

	m_Document = fresh;
	Fill_Texture_Previews(m_Document);
	m_Dirty = false;

#ifdef W3DVIEW_HAS_QT
	if (m_PanelWnd != nullptr) {
		W3dMaterialViewer::SetPanelDocument(m_Document);
	}
#endif

	UpdateTitle();
}

// Force the asset manager to drop this file's prototypes and textures so the
// next load rebuilds them from the freshly saved bytes (Load_3D_Assets skips
// prototypes whose names already exist). The main viewport is paused while the
// viewer is open, so removing prototypes cannot disturb a live render.
void
CMaterialViewerFrame::ReloadAssetsForPreview()
{
	if (m_SourceFilePath.empty()) {
		return;
	}

	// Release the preview's current render object first (it holds a ref that
	// would otherwise keep the old prototype's data alive).
	if (m_Preview != nullptr) {
		m_Preview->UnloadModel();
	}

	WW3DAssetManager *assets = WW3DAssetManager::Get_Instance();
	if (assets != nullptr) {
		for (const W3dMaterialViewer::MeshMaterialData &mesh : m_Document.meshes) {
			assets->Remove_Prototype(mesh.meshName.c_str());
		}
		if (!m_Document.topLevelName.empty()) {
			assets->Remove_Prototype(m_Document.topLevelName.c_str());
		}
		assets->Release_Unused_Textures();
	}

	CW3DViewDoc *doc = ::GetCurrentDocument();
	if (doc != nullptr) {
		doc->LoadAssetsFromFile(m_SourceFilePath.c_str());
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
	m_Dirty = dirty;
	UpdateTitle();
}

void
CMaterialViewerFrame::DirtyChangedThunk(bool dirty)
{
	if (_TheInstance != nullptr) {
		_TheInstance->OnPanelDirtyChanged(dirty);
	}
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
