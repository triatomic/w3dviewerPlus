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

// TheSuperHackers @feature W3DView material viewer window.
// Modeless frame with its own File menu: left pane is a native WW3D preview of
// a .w3d file, right pane is a read-only Qt panel replicating the 3ds Max W3D
// material editor. The panel is fed from the raw chunk data (W3dMaterialParser)
// while the preview uses the shared asset manager.

#pragma once

#include "W3dMaterialData.h"

class CMaterialPreviewPane;

class CMaterialViewerFrame : public CFrameWnd
{
public:
	CMaterialViewerFrame();
	virtual ~CMaterialViewerFrame();

	// Creates the window if needed and brings it to the foreground.
	static void ShowViewer();

	// Opens the viewer directly on an already-loaded asset (e.g. from the tree's
	// right-click menu). `objName` is the render-obj name to preview; `sourceFile`
	// is the .w3d filename from the asset source-file map (resolved through the
	// file factory) and may be empty. `meshName` optionally pre-selects a mesh
	// in the material panel.
	static void ShowViewerForAsset(const char *objName, const char *sourceFile, const char *meshName = nullptr);

	// Renders the preview pane of the open viewer (no-op when closed). Called
	// once per frame from CGraphicView::RepaintView, after the main render.
	static void RenderActivePreview();

	// True while a viewer with a Qt panel is open (drives idle-time Qt pumping).
	static bool HasQtPanel();

	// The viewer is an owned popup, so the main frame's theme broadcast (which
	// only walks descendants) misses it. Called explicitly on theme switches.
	static void NotifyThemeChanged();

protected:
	virtual void PostNcDestroy();

	afx_msg int OnCreate(LPCREATESTRUCT create_struct);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC *dc);
	afx_msg void OnFileOpen();
	afx_msg void OnFileClose();
	afx_msg void OnShowFullObject();
	afx_msg void OnUpdateShowFullObject(CCmdUI *cmd_ui);
	afx_msg void OnToggleAlpha();
	afx_msg void OnUpdateToggleAlpha(CCmdUI *cmd_ui);
	afx_msg void OnShaderAdditive();
	afx_msg void OnUpdateShaderAdditive(CCmdUI *cmd_ui);
	afx_msg void OnShaderAlphaTest();
	afx_msg void OnUpdateShaderAlphaTest(CCmdUI *cmd_ui);
	afx_msg void OnShaderAlphaBlend();
	afx_msg void OnUpdateShaderAlphaBlend(CCmdUI *cmd_ui);
	afx_msg void OnShaderAlphaBlendTest();
	afx_msg void OnUpdateShaderAlphaBlendTest(CCmdUI *cmd_ui);
	afx_msg void OnShaderDoubleSided();
	afx_msg void OnUpdateShaderDoubleSided(CCmdUI *cmd_ui);
	afx_msg LRESULT OnThemeChanged(WPARAM wparam, LPARAM lparam);
	DECLARE_MESSAGE_MAP()

private:
	void Layout(int cx, int cy);
	void ReassertLayout();
	void ApplyPanelTheme();
	void UpdatePreviewModel();
	void OnPanelMeshSelected(const char *meshName);
	static void PanelMeshSelectedThunk(const char *meshName);

	static CMaterialViewerFrame *_TheInstance;

	CMaterialPreviewPane	*m_Preview;
	CStatic					m_PlaceholderText;	// shown where the Qt panel would be
	HWND					m_PanelWnd;
	W3dMaterialViewer::MaterialDocument m_Document;

	// Preview shows the mesh selected in the panel by default; the View menu
	// toggle switches to the whole object (HLod).
	bool					m_ShowFullObject;
	std::string				m_TopLevelName;
	std::string				m_CurrentMeshName;
};
