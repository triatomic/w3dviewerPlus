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

// TheSuperHackers @feature W3DView material viewer 3D preview pane.
// Renders its own scene into an additional D3D8 swap chain, so the preview is
// independent of the main CGraphicView while sharing the single device.

#pragma once

#include "vector3.h"
#include "dx8wrapper.h"

class RenderObjClass;
class SimpleSceneClass;
class CameraClass;
class LightClass;

// Implements DX8_CleanupHook because the pane owns non-managed device resources
// (additional swap chain + depth surface). D3D8 Reset fails while they are
// alive, leaving the device corrupted; the hook releases them before a reset.
class CMaterialPreviewPane : public CWnd, public DX8_CleanupHook
{
public:
	CMaterialPreviewPane();
	virtual ~CMaterialPreviewPane();

	BOOL Create(CWnd *parent, const RECT &rect, UINT id);

	// Creates a render object by name from the shared asset manager and frames
	// the camera on it. Returns false if the asset does not exist.
	bool LoadModel(const char *name);

	// Removes the current render object and releases its ref (so the asset
	// manager can drop the prototype before a reload).
	void UnloadModel();

	// Renders one frame into the swap chain. Must be called from the main
	// thread, outside the main view's Begin_Render/End_Render bracket.
	void Render();

	// DX8_CleanupHook: release device resources before a reset; recreation is
	// lazy (Render recreates the swap chain when it is missing).
	virtual void ReleaseResources();
	virtual void ReAcquireResources();

protected:
	afx_msg int OnCreate(LPCREATESTRUCT create_struct);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT type, int cx, int cy);
	afx_msg void OnLButtonDown(UINT flags, CPoint point);
	afx_msg void OnLButtonUp(UINT flags, CPoint point);
	afx_msg void OnMouseMove(UINT flags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
	DECLARE_MESSAGE_MAP()

private:
	void Free_Swap_Chain();
	void Create_Swap_Chain();
	void Update_Camera();

	IDirect3DSwapChain8	*m_SwapChain;
	// Pane-sized non-MSAA depth buffer. The device's default depth buffer is
	// sized to the main view (and may be multisampled), so pairing it with our
	// swap chain's back buffer can fail and render nothing.
	IDirect3DSurface8	*m_DepthBuffer;
	SimpleSceneClass	*m_Scene;
	CameraClass			*m_Camera;
	LightClass			*m_Light;
	RenderObjClass		*m_RenderObj;

	// Orbit camera state
	Vector3				m_Center;
	float				m_Distance;
	float				m_Yaw;
	float				m_Pitch;
	bool				m_Dragging;
	CPoint				m_LastPoint;

	// Set by OnSize; the swap chain is recreated on the next Render() so a
	// drag-resize recreates at most once per rendered frame.
	bool				m_SizeDirty;

	// GetTickCount() of the previous Render(); 0 until the first frame. Used to
	// advance WW3D's global sync clock (WW3D::Sync) so animated UV mappers
	// (linear-offset / grid / rotate / sine / ...) keep moving while the main
	// viewport is paused and only this preview is rendering.
	DWORD				m_LastRenderTicks;
};
