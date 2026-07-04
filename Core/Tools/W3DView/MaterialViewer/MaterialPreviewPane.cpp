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
#include "dx8renderer.h"
#include "dx8wrapper.h"
#include "light.h"
#include "mesh.h"
#include "meshmdl.h"
#include "rendobj.h"
#include "scene.h"
#include "shader.h"
#include "vertmaterial.h"
#include "w3d_file.h"
#include "ww3d.h"
#include "../W3DDarkMode.h"
#include "../W3DViewDoc.h"

#include "W3dMaterialData.h"

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
	  m_SizeDirty(false),
	  m_LastRenderTicks(0)
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

namespace
{

// Recursively give every MeshClass under a render object a private copy of its
// MeshModelClass (which is shared with the cached prototype by default), so the
// live preview can mutate materials without affecting the shared asset.
void Make_Preview_Meshes_Unique(RenderObjClass *obj)
{
	if (obj == nullptr) {
		return;
	}
	if (obj->Class_ID() == RenderObjClass::CLASSID_MESH) {
		((MeshClass *)obj)->Make_Unique(true);	// force clone of the mesh model
	}
	int count = obj->Get_Num_Sub_Objects();
	for (int i = 0; i < count; i++) {
		RenderObjClass *child = obj->Get_Sub_Object(i);
		if (child != nullptr) {
			Make_Preview_Meshes_Unique(child);
			child->Release_Ref();	// Get_Sub_Object adds a ref
		}
	}
}

} // anonymous namespace

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

	// Mesh instances share their MeshModelClass with the cached asset-manager
	// prototype by default. The live material preview mutates the model, so give
	// this preview's meshes private copies first — otherwise edits would leak
	// into the shared prototype (and the main viewport) and could not be undone
	// by reloading. See ApplyLiveMaterials.
	Make_Preview_Meshes_Unique(render_obj);

	// Only re-frame the camera when a *different* object is loaded. Reloading
	// the same model (e.g. the post-save asset refresh after a material edit)
	// keeps the user's current orbit and zoom so the view doesn't jump.
	bool same_model = (m_LoadedName == name);
	SphereClass sphere = render_obj->Get_Bounding_Sphere();
	m_Center = sphere.Center;
	if (!same_model) {
		m_Distance = sphere.Radius * 3.0F;
		m_Distance = (m_Distance < 1.0F) ? 1.0F : m_Distance;
		m_Yaw = 0;
		m_Pitch = 0.35F;
	}
	m_LoadedName = name;
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
	// Deliberately keep m_LoadedName: a reload of the same model (unload then
	// load during the post-save refresh) must preserve the camera. A genuine
	// mesh switch loads a different name and re-frames.
}

//////////////////////////////////////////////////////////////////////////////
//	Live material apply
//////////////////////////////////////////////////////////////////////////////

namespace
{

// Mesh names in the document are "Container.MeshName"; a render object's mesh
// reports either that full name or just the trailing "MeshName". Match on the
// part after the last '.', case-insensitively, so both forms line up.
const char *Bare_Mesh_Name(const char *name)
{
	if (name == nullptr) {
		return "";
	}
	const char *dot = strrchr(name, '.');
	return (dot != nullptr) ? dot + 1 : name;
}

bool Mesh_Names_Match(const char *a, const char *b)
{
	return _stricmp(Bare_Mesh_Name(a), Bare_Mesh_Name(b)) == 0;
}

// Stamp one mesh's edited vertex materials + shaders onto the live model. The
// model was already made private (Make_Preview_Meshes_Unique) so mutating it is
// safe; the vertex material is still cloned so Replace_VertexMaterial has a
// distinct new pointer to swap in and update the DX8 render category.
void Apply_Mesh_Materials(MeshClass *mesh, const W3dMaterialViewer::MeshMaterialData &data)
{
	MeshModelClass *model = mesh->Peek_Model();
	if (model == nullptr) {
		return;
	}

	bool shader_changed = false;

	int passes = model->Get_Pass_Count();
	for (int pass = 0; pass < passes && pass < (int)data.passes.size(); pass++) {
		const W3dMaterialViewer::PassData &pass_data = data.passes[pass];

		// --- Vertex material: colors / opacity / shininess ---
		// Replace_VertexMaterial swaps the material everywhere it is used (single
		// slot or per-vertex array) AND updates the live DX8 render category, so
		// the change is visible without re-registering. We clone the current one,
		// edit the clone, and swap it in (never touching the shared original).
		int vmi = pass_data.vertexMaterialIndex;
		if (vmi >= 0 && vmi < (int)data.vertexMaterials.size()) {
			const W3dMaterialViewer::VertexMaterialData &vm = data.vertexMaterials[vmi];
			VertexMaterialClass *live = model->Peek_Single_Material(pass);
			if (live != nullptr) {
				VertexMaterialClass *edit = live->Clone();
				edit->Set_Ambient(vm.ambient[0] / 255.0F, vm.ambient[1] / 255.0F, vm.ambient[2] / 255.0F);
				edit->Set_Diffuse(vm.diffuse[0] / 255.0F, vm.diffuse[1] / 255.0F, vm.diffuse[2] / 255.0F);
				edit->Set_Specular(vm.specular[0] / 255.0F, vm.specular[1] / 255.0F, vm.specular[2] / 255.0F);
				edit->Set_Emissive(vm.emissive[0] / 255.0F, vm.emissive[1] / 255.0F, vm.emissive[2] / 255.0F);
				edit->Set_Opacity(vm.opacity);
				edit->Set_Shininess(vm.shininess);

				// --- Stage 0/1 UV mapping type + mapper args ---
				// Rebuild the mappers exactly as the loader does: clear both
				// stages, then let the engine's own factory recreate them from
				// the edited mapping-type bits (in attributes) and args strings.
				// A change to plain UV leaves the cleared (null) mapper.
				edit->Set_Mapper(nullptr, 0);
				edit->Set_Mapper(nullptr, 1);
				W3dVertexMaterialStruct vmat_struct;
				::memset(&vmat_struct, 0, sizeof(vmat_struct));
				vmat_struct.Attributes = vm.attributes;
				// Parse_Mapping_Args takes non-const char*; copy the args strings.
				std::vector<char> args0(vm.mapperArgs0.begin(), vm.mapperArgs0.end());
				args0.push_back('\0');
				std::vector<char> args1(vm.mapperArgs1.begin(), vm.mapperArgs1.end());
				args1.push_back('\0');
				edit->Parse_Mapping_Args(vmat_struct,
					vm.mapperArgs0.empty() ? nullptr : args0.data(),
					vm.mapperArgs1.empty() ? nullptr : args1.data());

				model->Replace_VertexMaterial(live, edit);
				edit->Release_Ref();	// Replace_VertexMaterial took its own ref
			}
		}

		// --- Shader: blend / depth / gradients / alpha test ---
		int si = pass_data.shaderIndex;
		if (si >= 0 && si < (int)data.shaders.size()) {
			const W3dMaterialViewer::ShaderData &sh = data.shaders[si];
			// Start from the live shader so bits we don't manage are preserved,
			// then overwrite the managed fields (their W3D chunk values map 1:1
			// to the ShaderClass enums).
			ShaderClass shader = model->Get_Single_Shader(pass);
			shader.Set_Src_Blend_Func((ShaderClass::SrcBlendFuncType)sh.srcBlend);
			shader.Set_Dst_Blend_Func((ShaderClass::DstBlendFuncType)sh.destBlend);
			shader.Set_Depth_Compare((ShaderClass::DepthCompareType)sh.depthCompare);
			shader.Set_Depth_Mask((ShaderClass::DepthMaskType)sh.depthMask);
			shader.Set_Alpha_Test((ShaderClass::AlphaTestType)sh.alphaTest);
			shader.Set_Primary_Gradient((ShaderClass::PriGradientType)sh.priGradient);
			shader.Set_Secondary_Gradient((ShaderClass::SecGradientType)sh.secGradient);
			// Detail color/alpha funcs are not live-previewed (the runtime shader
			// exposes only the post-detail fields); they still save to the file.
			model->Set_Single_Shader(shader, pass);
			shader_changed = true;
		}
	}

	// A shader change re-buckets which DX8 render category the mesh belongs to
	// (categories are keyed by texture+shader), so rebuild its registration.
	// Material changes above already updated their category in place.
	if (shader_changed) {
		TheDX8MeshRenderer.Unregister_Mesh_Type(model);
		TheDX8MeshRenderer.Register_Mesh_Type(model);
	}
}

// Recursively visit every MeshClass under a render object, matching it to a
// document mesh by name and stamping the edits.
void Apply_To_Render_Obj(RenderObjClass *obj, const W3dMaterialViewer::MaterialDocument &document)
{
	if (obj == nullptr) {
		return;
	}

	if (obj->Class_ID() == RenderObjClass::CLASSID_MESH) {
		const char *obj_name = obj->Get_Name();
		for (const W3dMaterialViewer::MeshMaterialData &mesh_data : document.meshes) {
			if (Mesh_Names_Match(obj_name, mesh_data.meshName.c_str())) {
				Apply_Mesh_Materials((MeshClass *)obj, mesh_data);
				break;
			}
		}
	}

	int count = obj->Get_Num_Sub_Objects();
	for (int i = 0; i < count; i++) {
		RenderObjClass *child = obj->Get_Sub_Object(i);
		if (child != nullptr) {
			Apply_To_Render_Obj(child, document);
			child->Release_Ref();	// Get_Sub_Object adds a ref
		}
	}
}

} // anonymous namespace

void
CMaterialPreviewPane::ApplyLiveMaterials(const W3dMaterialViewer::MaterialDocument &document)
{
	if (m_RenderObj == nullptr) {
		return;
	}
	Apply_To_Render_Obj(m_RenderObj, document);
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

	// Advance WW3D's global sync clock by the real elapsed time so animated UV
	// mappers (linear-offset scrolling, grid, rotate, ...) keep moving. Their
	// texture matrices are driven by WW3D::Get_Sync_Time(), which only advances
	// via WW3D::Sync(). While the viewer is open the main viewport is paused and
	// no longer calls Sync, so without this the scrolling UVs freeze.
	{
		DWORD now = ::GetTickCount();
		DWORD elapsed = (m_LastRenderTicks != 0) ? (now - m_LastRenderTicks) : 0;
		m_LastRenderTicks = now;
		// Clamp the first/stalled frame so a long pause doesn't jump the UVs.
		if (elapsed > 250) {
			elapsed = 250;
		}
		if (elapsed > 0) {
			WW3D::Update_Logic_Frame_Time((float)elapsed);
			WW3D::Sync(true);
		}
	}

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
