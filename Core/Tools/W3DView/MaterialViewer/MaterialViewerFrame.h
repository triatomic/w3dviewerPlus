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

#include <string>
#include <vector>

#include "vector3.h"
#include "quat.h"
#include "matrix3d.h"
#include "W3dMaterialData.h"

class CMaterialPreviewPane;

// TheSuperHackers @feature Several .w3d files open at once, one tab per file.
// The preview pane and the Qt panel are shared between tabs; this struct holds
// everything that is per-file. Only the ACTIVE tab can be dirty: switching away
// from unsaved edits prompts Save/Discard/Cancel first (the panel's edit
// session — undo stack included — cannot survive a document swap).
struct MaterialViewerTab
{
	std::string							sourceFilePath;		// disk path (title / reload); may be empty
	W3dMaterialViewer::MaterialDocument	document;			// parsed material chunks
	W3dMaterialViewer::MaterialDocument	livePreviewDoc;		// latest in-flight edits (active tab only)
	bool								dirty = false;

	std::string							topLevelName;		// HLod / top render obj
	std::string							currentMeshName;	// mesh shown in the preview
	bool								showFullObject = false;

	// Saved orbit camera so switching back to this tab restores its exact view.
	bool								cameraValid = false;
	Matrix3D							cameraTransform = Matrix3D(1);
	Vector3								cameraCenter = Vector3(0, 0, 0);
	Quaternion							cameraRotation = Quaternion(true);
	float								cameraDistance = 0.0f;
	float								cameraMinZoomAdjust = 0.0f;
};

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

	// Keyboard input to the Qt panel (text fields, spin boxes) must bypass the
	// frame's accelerator table and MFC dialog-key handling, or editing keys
	// (Delete, Home/End, arrows, Ctrl+A, ...) get swallowed before Qt sees them.
	virtual BOOL PreTranslateMessage(MSG *msg);

	afx_msg int OnCreate(LPCREATESTRUCT create_struct);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC *dc);
	afx_msg void OnTimer(UINT_PTR event_id);
	afx_msg void OnFileOpen();
	afx_msg void OnFileClose();
	afx_msg void OnFileSave();
	afx_msg void OnUpdateFileSave(CCmdUI *cmd_ui);
	afx_msg void OnEditUndo();
	afx_msg void OnUpdateEditUndo(CCmdUI *cmd_ui);
	afx_msg void OnEditRedo();
	afx_msg void OnUpdateEditRedo(CCmdUI *cmd_ui);
	afx_msg void OnEditRevert();
	afx_msg void OnUpdateEditRevert(CCmdUI *cmd_ui);
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
	afx_msg void OnShaderBackfaceTint();
	afx_msg void OnUpdateShaderBackfaceTint(CCmdUI *cmd_ui);
	afx_msg void OnShaderBackfaceTintColor();
	afx_msg void OnDropFiles(HDROP drop);
	afx_msg LRESULT OnThemeChanged(WPARAM wparam, LPARAM lparam);
	DECLARE_MESSAGE_MAP()

	// Fired by the Qt tab bar for user interaction (never for programmatic
	// updates). The thunks PostMessage the request to the frame and return, so
	// the heavy work (dirty prompt, asset reload, tab-bar rebuild) never runs
	// inside QTabBar's own mouse-event stack.
	afx_msg LRESULT OnTabAction(WPARAM wparam, LPARAM lparam);
	void OnTabChanged(int index);
	void OnTabCloseRequested(int index);
	static void TabChangedThunk(int index);
	static void TabCloseRequestedThunk(int index);

private:
	void Layout(int cx, int cy);
	void ReassertLayout();
	void ApplyPanelTheme();
	void UpdatePreviewModel();
	void OnPanelMeshSelected(const char *meshName);
	static void PanelMeshSelectedThunk(const char *meshName);

	// Edit-mode host hooks handed to the Qt panel (see MaterialPanel.h).
	bool RunEditGate();
	bool SaveDocument(const W3dMaterialViewer::MaterialDocument &document);
	void RevertDocument();
	void OnPanelDirtyChanged(bool dirty);
	static bool EditGateThunk();
	static bool SaveThunk(const W3dMaterialViewer::MaterialDocument &document);
	static void RevertThunk();
	static void DirtyChangedThunk(bool dirty);
	static void ResolveTextureThunk(W3dMaterialViewer::TextureData &texture);

	// Live-preview: the panel calls this after every committed edit. We copy the
	// document and (re)start a short debounce timer; when it fires the edited
	// materials are stamped onto the live preview render object in memory (no
	// file write). See MaterialPreviewPane::ApplyLiveMaterials.
	void OnPanelLivePreview(const W3dMaterialViewer::MaterialDocument &document);
	static void LivePreviewThunk(const W3dMaterialViewer::MaterialDocument &document);

	// Reloads this file's assets so the preview reflects freshly saved bytes.
	void ReloadAssetsForPreview();

	// Save / Discard / Cancel prompt used before the document is replaced;
	// returns false only when the user cancels.
	bool PromptSaveIfDirty();

	void UpdateTitle();

	//----- Tabs ----------------------------------------------------------------

	// Active-tab accessor. Returns a shared scratch tab when none is open so the
	// panel callbacks (which can fire before the first file loads or after the
	// last one closes) never index out of range — the scratch state is simply
	// thrown away. Guard real work with HasTabs().
	MaterialViewerTab &Active();
	bool HasTabs() const { return !m_Tabs.empty(); }

	// Opens `path` as a new tab, or switches to its tab if already open. Prompts
	// for unsaved edits before leaving the current tab; returns false if the
	// user cancelled or the file could not be parsed. `meshName` optionally
	// pre-selects a mesh in the panel.
	bool OpenFileInTab(const char *path, const char *meshName = nullptr);

	// Tab-opening back end shared with ShowViewerForAsset (which may have no
	// on-disk file). Appends `tab` and activates it.
	void AppendTabAndActivate(MaterialViewerTab &tab);

	// Makes `index` active: snapshots the outgoing tab's camera, reloads the
	// incoming file's assets (files can contain identically-named prototypes,
	// so the shared asset manager must hold THIS file's versions), feeds the
	// panel and restores mesh selection + camera. Does NOT prompt for unsaved
	// edits — callers do.
	void ActivateTab(int index);

	// Captures the preview camera into the active tab before a switch.
	void SnapshotActiveTab();

	// Closes tab `index` (prompting for unsaved edits when it is the active
	// one); closes the whole window when the last tab goes. Returns false if
	// the user cancelled.
	bool CloseTab(int index);

	// Mirrors m_Tabs into the Qt tab bar and relayouts (the strip shows only
	// when at least one tab is open).
	void RebuildTabBar();

	// Index of the tab already showing `path` (case-insensitive), or -1.
	int FindTab(const char *path) const;

	// Tab caption: file name (or top-level asset name) plus a dirty '*'.
	static std::string TabLabel(const MaterialViewerTab &tab);

	static CMaterialViewerFrame *_TheInstance;

	CMaterialPreviewPane	*m_Preview;
	CStatic					m_PlaceholderText;	// shown where the Qt panel would be
	HWND					m_PanelWnd;
	HWND					m_TabBarWnd;		// Qt QTabBar hosted in the frame

	std::vector<MaterialViewerTab>	m_Tabs;
	int						m_ActiveTab;		// index into m_Tabs, -1 when empty

	// True while a refresh timer is pending for the debounced live preview
	// (the pending document itself lives in the active tab).
	bool					m_LivePreviewPending;
};
