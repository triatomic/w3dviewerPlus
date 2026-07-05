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

// TheSuperHackers @feature W3DView material viewer.
// Qt-free interface to the Qt material panel. The implementation lives in
// MaterialPanel.cpp, which is compiled into the separate core_w3dview_qtpanel
// static library (no MFC, no WW3D, no tool PCH). Everything here is guarded by
// W3DVIEW_HAS_QT so the tool builds without Qt.

#pragma once

#ifdef W3DVIEW_HAS_QT

#include <windows.h>
#include "W3dMaterialData.h"

namespace W3dMaterialViewer
{

struct PanelTheme
{
	bool		dark = false;
	COLORREF	background = 0;
	COLORREF	ctrlBackground = 0;
	COLORREF	text = 0;
	COLORREF	edge = 0;
};

// Creates the (single) Qt material panel as a WS_CHILD of `parent`, lazily
// initializing the QApplication. Returns null if Qt failed to initialize.
HWND CreatePanel(HWND parent);

// Destroys the panel widget. Must be called before the parent window dies.
void DestroyPanel();

HWND GetPanelHwnd();

// Resizes the panel through Qt so its logical geometry and layout stay in
// sync. Sizing the HWND natively (MoveWindow) leaves Qt believing the old
// size and it stops painting short of the real bottom edge.
void ResizePanel(int width, int height);

void SetPanelDocument(const MaterialDocument &document);

// Selects the given mesh ("Container.Mesh") in the panel's mesh combo, if present.
void SelectPanelMesh(const char *meshName);

//////////////////////////////////////////////////////////////////////////////
//	Edit mode
//////////////////////////////////////////////////////////////////////////////

// Invoked when the user toggles Edit on. The host runs the file-integrity gate
// (a load/write round trip of the file must reproduce it byte-identically);
// return false to refuse edit mode (the toggle snaps back to View).
void SetPanelEditGateCallback(bool (*callback)());

// Invoked when the user clicks Save. Receives the edited document; the host
// writes it back to disk and refreshes the preview. Return true on success
// (the panel clears its dirty flag), false to keep the edits dirty.
void SetPanelSaveCallback(bool (*callback)(const MaterialDocument &document));

// Invoked when the user clicks Revert (or confirms "Discard"): the host
// re-parses the file from disk and calls SetPanelDocument.
void SetPanelRevertCallback(void (*callback)());

// Invoked whenever the panel's dirty state changes, so the host can show an
// unsaved-changes marker in its title.
void SetPanelDirtyChangedCallback(void (*callback)(bool dirty));

// Invoked after every committed edit so the host can refresh the live 3D
// preview from the in-memory document (typically debounced). Independent of
// Save: the file is not written.
void SetPanelLivePreviewCallback(void (*callback)(const MaterialDocument &document));

// Decodes a preview thumbnail for a texture filename (needs the D3D device +
// file factory, which live on the host side). Fills `resolvedPath` and the
// preview pixels of `texture` (already carrying the name); leaves them empty
// when the file cannot be found/decoded. Lets the Textures tab refresh its
// thumbnail live when the filename is edited.
void SetPanelResolveTextureCallback(void (*callback)(TextureData &texture));

// True when edit mode has unsaved changes. Lets the host prompt before the
// document is replaced (File->Open) or the window closes.
bool PanelHasUnsavedChanges();

// Asks the panel to run its Save action (as if the Save button was clicked).
// Returns true if the document was clean or the save succeeded.
bool RequestPanelSave();

//////////////////////////////////////////////////////////////////////////////
//	Edit-mode menu bridge
//
// Let the host frame mirror the panel's Undo / Redo / Revert / Save actions on
// its menu bar. The Can* queries drive ON_UPDATE_COMMAND_UI enable state; the
// Request* calls perform the action (as if the matching panel button was
// clicked). All are no-ops / return false when not in edit mode.

bool PanelIsEditing();
bool PanelCanUndo();
bool PanelCanRedo();
bool PanelCanSaveOrRevert();	// true when editing and dirty

void RequestPanelUndo();
void RequestPanelRedo();
void RequestPanelRevert();

// Invoked (synchronously, on the UI thread) whenever the panel's mesh combo
// selection changes; receives the mesh name. Pass null to clear.
void SetPanelMeshSelectedCallback(void (*callback)(const char *meshName));

void ApplyPanelTheme(const PanelTheme &theme);

// Delivers Qt posted events/timers. Qt's native child windows receive input
// through the regular Win32 message pump; this handles the rest. Cheap no-op
// when the QApplication does not exist.
void PumpQtEvents();

// Deletes the QApplication. Call at app exit, after DestroyPanel.
void ShutdownQt();

} // namespace W3dMaterialViewer

#endif // W3DVIEW_HAS_QT
