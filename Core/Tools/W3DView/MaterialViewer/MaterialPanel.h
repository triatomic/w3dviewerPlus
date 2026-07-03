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
