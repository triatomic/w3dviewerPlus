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
#include "MaterialPreviewPane.h"

#include "assetmgr.h"
#include "camera.h"
#include "dx8wrapper.h"
#include "light.h"
#include "rendobj.h"
#include "scene.h"
#include "ww3d.h"
#include "../W3DDarkMode.h"
#include "../W3DViewDoc.h"

BEGIN_MESSAGE_MAP(CMaterialPreviewPane, CWnd)
	ON_WM_CREATE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

CMaterialPreviewPane::CMaterialPreviewPane()
	: m_SwapChain(nullptr),
	  m_DepthBuffer(nullptr),
	  m_Scene(nullptr),
	  m_Camera(nullptr),
	  m_Light(nullptr),
	  m_RenderObj(nullptr),
	  m_Center(0, 0, 0),
	  m_Distance(10.0F),
	  m_Yaw(0),
	  m_Pitch(0.35F),
	  m_Dragging(false),
	  m_SizeDirty(false)
{
}

CMaterialPreviewPane::~CMaterialPreviewPane()
{
}

BOOL
CMaterialPreviewPane::Create(CWnd *parent, const RECT &rect, UINT id)
{
	return CWnd::Create(nullptr, "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, rect, parent, id);
}

int
CMaterialPreviewPane::OnCreate(LPCREATESTRUCT create_struct)
{
	if (CWnd::OnCreate(create_struct) == -1) {
		return -1;
	}

	m_Scene = new SimpleSceneClass;
	m_Scene->Set_Ambient_Light(Vector3(0.5F, 0.5F, 0.5F));

	m_Camera = new CameraClass;

	// Mirror the main viewer's default scene light (see CW3DViewDoc::InitScene).
	m_Light = new LightClass;
	m_Light->Set_Position(Vector3(0, 5000, 3000));
	m_Light->Set_Intensity(1.0F);
	m_Light->Set_Force_Visible(true);
	m_Light->Set_Flag(LightClass::NEAR_ATTENUATION, false);
	m_Light->Set_Far_Attenuation_Range(1000000, 1000000);
	m_Light->Set_Ambient(Vector3(0, 0, 0));
	m_Light->Set_Diffuse(Vector3(1, 1, 1));
	m_Light->Set_Specular(Vector3(1, 1, 1));
	m_Scene->Add_Render_Object(m_Light);

	Create_Swap_Chain();
	Update_Camera();

	// W3DView does not otherwise use the cleanup hook (only WorldBuilder does),
	// so the single slot is free for the preview pane.
	DX8Wrapper::SetCleanupHook(this);
	return 0;
}

void
CMaterialPreviewPane::ReleaseResources()
{
	Free_Swap_Chain();
}

void
CMaterialPreviewPane::ReAcquireResources()
{
	// Lazy: Render() recreates the swap chain when it is missing.
}

void
CMaterialPreviewPane::OnDestroy()
{
	DX8Wrapper::SetCleanupHook(nullptr);

	if (m_RenderObj != nullptr) {
		m_Scene->Remove_Render_Object(m_RenderObj);
		m_RenderObj->Release_Ref();
		m_RenderObj = nullptr;
	}

	if (m_Light != nullptr) {
		m_Scene->Remove_Render_Object(m_Light);
		m_Light->Release_Ref();
		m_Light = nullptr;
	}

	if (m_Camera != nullptr) {
		m_Camera->Release_Ref();
		m_Camera = nullptr;
	}

	if (m_Scene != nullptr) {
		m_Scene->Release_Ref();
		m_Scene = nullptr;
	}

	Free_Swap_Chain();
	CWnd::OnDestroy();
}

void
CMaterialPreviewPane::Free_Swap_Chain()
{
	if (m_DepthBuffer != nullptr) {
		m_DepthBuffer->Release();
		m_DepthBuffer = nullptr;
	}

	if (m_SwapChain != nullptr) {
		m_SwapChain->Release();
		m_SwapChain = nullptr;
	}
}

void
CMaterialPreviewPane::Create_Swap_Chain()
{
	Free_Swap_Chain();

	CRect rect;
	GetClientRect(&rect);
	if (rect.Width() < 1 || rect.Height() < 1) {
		return;
	}

	m_SwapChain = DX8Wrapper::Create_Additional_Swap_Chain(m_hWnd);

	// The device's default depth buffer is sized to the main view and may be
	// multisampled; either mismatch makes SetRenderTarget fail silently. Pair
	// the swap chain with a pane-sized non-MSAA depth buffer instead.
	if (m_SwapChain != nullptr) {
		IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
		if (device != nullptr) {
			D3DFORMAT zformat = D3DFMT_D24S8;
			IDirect3DSurface8 *default_z = nullptr;
			if (SUCCEEDED(device->GetDepthStencilSurface(&default_z)) && default_z != nullptr) {
				D3DSURFACE_DESC desc;
				if (SUCCEEDED(default_z->GetDesc(&desc))) {
					zformat = desc.Format;
				}
				default_z->Release();
			}
			device->CreateDepthStencilSurface(rect.Width(), rect.Height(),
				zformat, D3DMULTISAMPLE_NONE, &m_DepthBuffer);
		}
	}

	// Never keep a swap chain without its matching depth buffer: pairing the
	// pane's back buffer with the device's default (main-view-sized) depth
	// makes SetRenderTarget fail silently, and the preview would then clear
	// and draw straight into the MAIN view's back buffer.
	if (m_SwapChain != nullptr && m_DepthBuffer == nullptr) {
		Free_Swap_Chain();
	}

	if (m_Camera != nullptr) {
		m_Camera->Set_Aspect_Ratio((float)rect.Width() / (float)rect.Height());
	}
}

bool
CMaterialPreviewPane::LoadModel(const char *name)
{
	if (m_RenderObj != nullptr) {
		m_Scene->Remove_Render_Object(m_RenderObj);
		m_RenderObj->Release_Ref();
		m_RenderObj = nullptr;
	}

	RenderObjClass *render_obj = WW3DAssetManager::Get_Instance()->Create_Render_Obj(name);
	if (render_obj == nullptr) {
		return false;
	}

	render_obj->Set_Transform(Matrix3D(1));
	m_Scene->Add_Render_Object(render_obj);
	m_RenderObj = render_obj;

	// Frame the camera on the object's bounding sphere.
	SphereClass sphere = render_obj->Get_Bounding_Sphere();
	m_Center = sphere.Center;
	m_Distance = sphere.Radius * 3.0F;
	m_Distance = (m_Distance < 1.0F) ? 1.0F : m_Distance;
	m_Yaw = 0;
	m_Pitch = 0.35F;
	Update_Camera();
	return true;
}

void
CMaterialPreviewPane::UnloadModel()
{
	if (m_RenderObj != nullptr) {
		if (m_Scene != nullptr) {
			m_Scene->Remove_Render_Object(m_RenderObj);
		}
		m_RenderObj->Release_Ref();
		m_RenderObj = nullptr;
	}
}

void
CMaterialPreviewPane::Update_Camera()
{
	if (m_Camera == nullptr) {
		return;
	}

	// Clamp the pitch so Look_At's up vector stays valid.
	if (m_Pitch > 1.55F) m_Pitch = 1.55F;
	if (m_Pitch < -1.55F) m_Pitch = -1.55F;

	Vector3 offset;
	offset.X = m_Distance * cos(m_Pitch) * cos(m_Yaw);
	offset.Y = m_Distance * cos(m_Pitch) * sin(m_Yaw);
	offset.Z = m_Distance * sin(m_Pitch);

	Matrix3D transform(1);
	transform.Look_At(m_Center + offset, m_Center, 0);
	m_Camera->Set_Transform(transform);

	float max_dist = m_Distance * 60.0F;
	float min_dist = max(0.2F, m_Distance / 380.0F);
	m_Camera->Set_Clip_Planes(min_dist, max_dist);
}

void
CMaterialPreviewPane::Render()
{
	if (!IsWindowVisible()) {
		return;
	}

	// Never touch the render target while the device is lost or pending reset;
	// the main view's Begin_Render handles the reset and our cleanup hook
	// releases the swap chain for it.
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	if (device == nullptr || device->TestCooperativeLevel() != D3D_OK) {
		Free_Swap_Chain();
		return;
	}

	// Recreate lazily, at most once per rendered frame. Recreating directly
	// in OnSize churns swap chains and depth surfaces many times per frame
	// during a drag-resize of the viewer window, which pressures the shared
	// device's video memory and destabilizes the main view.
	if (m_SwapChain == nullptr || m_SizeDirty) {
		Create_Swap_Chain();
		m_SizeDirty = false;
		if (m_SwapChain == nullptr) {
			return;
		}
	}

	// Create_Swap_Chain guarantees the pane-sized depth buffer exists whenever
	// the swap chain does; never render against the device default depth.
	if (m_DepthBuffer == nullptr) {
		Free_Swap_Chain();
		return;
	}

	// Match the main viewport's per-theme default background.
	float bg = W3DDarkMode::IsDark() ? W3DVIEW_BG_DARK_F : W3DVIEW_BG_LIGHT_F;

	IDirect3DSurface8 *back_buffer = nullptr;
	m_SwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &back_buffer);
	if (back_buffer == nullptr) {
		Free_Swap_Chain();
		return;
	}
	DX8Wrapper::Set_Render_Target(back_buffer, m_DepthBuffer);
	back_buffer->Release();

	WW3D::Begin_Render(true, true, Vector3(bg, bg, bg));
	WW3D::Render(m_Scene, m_Camera, FALSE, FALSE);
	WW3D::End_Render(false);

	HRESULT hr = m_SwapChain->Present(nullptr, nullptr, nullptr, nullptr);
	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)nullptr);

	if (FAILED(hr)) {
		// Device was reset (e.g. main window resize); recreate next frame.
		Free_Swap_Chain();
	}
}

void
CMaterialPreviewPane::OnSize(UINT type, int cx, int cy)
{
	CWnd::OnSize(type, cx, cy);
	if (cx > 0 && cy > 0) {
		// Deferred to the next Render(): WM_SIZE arrives many times per frame
		// during a drag-resize and each recreation hits the shared device.
		m_SizeDirty = true;
	}
}

void
CMaterialPreviewPane::OnLButtonDown(UINT flags, CPoint point)
{
	m_Dragging = true;
	m_LastPoint = point;
	SetCapture();
	CWnd::OnLButtonDown(flags, point);
}

void
CMaterialPreviewPane::OnLButtonUp(UINT flags, CPoint point)
{
	m_Dragging = false;
	ReleaseCapture();
	CWnd::OnLButtonUp(flags, point);
}

void
CMaterialPreviewPane::OnMouseMove(UINT flags, CPoint point)
{
	if (m_Dragging) {
		m_Yaw -= (point.x - m_LastPoint.x) * 0.01F;
		m_Pitch += (point.y - m_LastPoint.y) * 0.01F;
		m_LastPoint = point;
		Update_Camera();
	}
	CWnd::OnMouseMove(flags, point);
}

BOOL
CMaterialPreviewPane::OnMouseWheel(UINT flags, short delta, CPoint point)
{
	float scale = (delta > 0) ? (1.0F / 1.15F) : 1.15F;
	m_Distance *= scale;
	m_Distance = (m_Distance < 0.5F) ? 0.5F : m_Distance;
	Update_Camera();
	return TRUE;
}
