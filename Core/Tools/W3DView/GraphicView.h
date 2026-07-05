/*
**	Command & Conquer Renegade(tm)
**	Copyright 2025 Electronic Arts Inc.
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

#pragma once

// GraphicView.h : header file
//

/////////////////////////////////////////////////////////////////
//
//  Constants
//
#define ROTATION_X      0x01
#define ROTATION_Y      0x02
#define ROTATION_Z      0x04
#define ROTATION_X_BACK 0x08
#define ROTATION_Y_BACK 0x10
#define ROTATION_Z_BACK 0x20


// Forward declarations
class CW3DViewDoc;
class ParticleEmitterClass;


/////////////////////////////////////////////////////////////////////////////
// CGraphicView view

#include "camera.h"

class CGraphicView : public CView
{
protected:
	CGraphicView();           // protected constructor used by dynamic creation
	DECLARE_DYNCREATE(CGraphicView)

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CGraphicView)
	public:
	virtual void OnInitialUpdate();
	protected:
	virtual void OnDraw(CDC* pDC);      // overridden to draw this view
	virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
	//}}AFX_VIRTUAL

// Implementation
protected:
	virtual ~CGraphicView();
#ifdef RTS_DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

	// Generated message map functions
protected:
	//{{AFX_MSG(CGraphicView)
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint point);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

    public:

        /////////////////////////////////////////////////
        //
        //  Public Data Types
        //
        typedef enum
        {
            AnimInvalid = -1,
            AnimPlaying = 0,
            AnimStopped = 1,
            AnimPaused = 2
        } ANIMATION_STATE;

        typedef enum
        {
            CameraFront = -1,
            CameraBack = 0,
            CameraTop = 1,
            CameraBottom = 2,
            CameraLeft = 3,
            CameraRight = 4
        } CAMERA_POS;

        typedef enum
        {
            NoRotation = 0,
            RotateX = ROTATION_X,
            RotateY = ROTATION_Y,
            RotateZ = ROTATION_Z,
				RotateXBack = ROTATION_X_BACK,
				RotateYBack = ROTATION_Y_BACK,
				RotateZBack = ROTATION_Z_BACK
        } OBJECT_ROTATION;

        typedef enum
        {
            FreeRotation = 0,
            OnlyRotateX = ROTATION_X,
            OnlyRotateY = ROTATION_Y,
            OnlyRotateZ = ROTATION_Z
        } CAMERA_ROTATION;


        /////////////////////////////////////////////////
        //
        //  Public Methods
        //

        BOOL			InitializeGraphicView (void);

        //
		  //	Initial display methods
		  //
		  void			Reset_Camera_To_Display_Sphere (SphereClass &sphere);
		  void			Reset_Camera_To_Display_Object (RenderObjClass &physObject);
		  void			Reset_Camera_To_Display_Emitter (ParticleEmitterClass &emitter);
		  void			Load_Default_Dat (void);

        void			UpdateDisplay (void);
        void			RepaintView (BOOL bUpdateAnimation = TRUE, DWORD ticks_to_use = 0);
        void			SetActiveUpdate (BOOL bActive)
								{ m_bActive = bActive;
								  if (!m_bActive) { ::SetProp (m_hWnd, "Inactive", (HANDLE)1); }
								  else { RemoveProp (m_hWnd, "Inactive"); m_dwLastFrameUpdate = ::GetTickCount (); }
								}

			void			Allow_Update (bool onoff);

        //
        // Animation methods
        //
        float					GetAnimationSpeed (void) const				{ return m_animationSpeed; }
        void					SetAnimationSpeed (float animationSpeed)	{ m_animationSpeed = animationSpeed; }
        ANIMATION_STATE		GetAnimationState (void) const				{ return m_animationState; }
        void					SetAnimationState (ANIMATION_STATE animationState);

        //
        // Camera Methods
        //
        void					SetAllowedCameraRotation (CAMERA_ROTATION cameraRotation);
        CAMERA_ROTATION		GetAllowedCameraRotation () const			{ return m_allowedCameraRotation; }
        void					SetCameraPos (CAMERA_POS cameraPos);
        class CameraClass *GetCamera (void) const							{ return m_pCamera; }

		  float					Get_Camera_Distance (void) const				{ return m_CameraDistance; }
		  void					Set_Camera_Distance (float dist);

		  void					Set_Camera_Bone_Pos_X (bool onoff)			{ m_CameraBonePosX = onoff; }
		  BOOL					Is_Camera_Bone_Pos_X (void) const			{ return m_CameraBonePosX; }

		  // Invert the vertical (pitch) direction of drag-to-orbit. Persisted to
		  // the MFC profile so the preference survives restarts.
		  void					Set_Invert_Camera_Y (bool onoff);
		  bool					Is_Invert_Camera_Y (void) const				{ return m_InvertCameraY; }

		  // TheSuperHackers @feature Tria When set, a second render pass draws the
		  // normally-culled back faces flat-tinted with s_BackfaceTintColor. Session-
		  // only; static so the menu handlers (in CMainFrame and the Material Viewer
		  // frame) can toggle/recolour it without a view pointer.
		  static void			Set_Show_Backface_Tint (bool onoff)			{ s_ShowBackfaceTint = onoff; }
		  static bool			Is_Show_Backface_Tint (void)				{ return s_ShowBackfaceTint; }
		  static void			Set_Backface_Tint_Color (const Vector3 &c)	{ s_BackfaceTintColor = c; }
		  static const Vector3 &Get_Backface_Tint_Color (void)				{ return s_BackfaceTintColor; }

		  // Renders the backface-tint second pass on `scene`/`camera` when the toggle
		  // is on (no-op otherwise). Shared by the main viewport and the Material
		  // Viewer preview so the pass logic lives in one place.
		  static void			Render_Backface_Tint_Pass (class SceneClass *scene, class CameraClass *camera);

		  // Colour conversions between the viewer's 0..1 float Vector3 and Win32's
		  // 0..255 COLORREF, used by the tint-colour menu handlers.
		  static COLORREF		Tint_Color_To_ColorRef (const Vector3 &c);
		  static Vector3		ColorRef_To_Tint_Color (COLORREF c);

        //
        // Object rotation methods
        //
        void					ResetObject (void);
        void					RotateObject (OBJECT_ROTATION rotation);
        OBJECT_ROTATION		GetObjectRotation (void) const				{ return m_objectRotation; }

        //
        // Light rotation methods
        //
        void					Rotate_Light (OBJECT_ROTATION rotation)	{ m_LightRotation = rotation; }
        OBJECT_ROTATION		Get_Light_Rotation (void) const				{ return m_LightRotation; }

			//
			//	Fullscreen mode
			//
			BOOL					Is_Fullscreen (void) const						{ return !(BOOL)m_iWindowed; }
			void					Set_Fullscreen (bool fullscreen)				{ int prev = m_iWindowed; m_iWindowed = fullscreen ? 0 : 1; if (!InitializeGraphicView ()) m_iWindowed = prev; }

			//
			//	Misc
			//
			RenderObjClass *	Get_Light_Mesh (void) const					{ return m_pLightMesh; }
			Vector3 &			Get_Object_Center (void)						{ return m_ObjectCenter; }

			//
			//	FOV methods
			//
			void					Set_FOV (double hfov, double vfov, bool force = false);
			void					Reset_FOV (void);

    protected:

        /////////////////////////////////////////////////
        //
        //  Protected methods
        //
		  void					Rotate_Object (void);
		  void					Rotate_Light (void);

    private:

        /////////////////////////////////////////////////
        //
        //  Private Methods
        //
        void                    Clip_Cursor_To_View (void);
        bool                    Wrap_Cursor_In_View (CPoint &point);
        void                    Orbit_Camera (int deltaX, int deltaY, CW3DViewDoc *doc);

        /////////////////////////////////////////////////
        //
        //  Private Member Data
        //
        BOOL					m_bInitialized;
        BOOL					m_bActive;
        UINT					m_TimerID;
        UINT					m_TimerPeriod;
        CameraClass	*		m_pCamera;
		  RenderObjClass *	m_pLightMesh;
		  bool					m_bLightMeshInScene;
		  Vector3				m_ObjectCenter;
		  SphereClass			m_ViewedSphere;

        BOOL					m_bMouseDown;
        BOOL					m_bRMouseDown;
        BOOL					m_bMMouseDown;
        POINT					m_lastPoint;
        POINT					m_mButtonDownPoint;
        bool					m_AltOnMDown;
		  int						m_iWindowed;
		  int						m_UpdateCounter;
		  float					m_CameraDistance;
		  DWORD					m_ParticleCountUpdate;
		  BOOL					m_CameraBonePosX;

        // Animation data
        DWORD					m_dwLastFrameUpdate;
        float					m_animationSpeed;
        ANIMATION_STATE		m_animationState;
        OBJECT_ROTATION		m_objectRotation;
		  OBJECT_ROTATION		m_LightRotation;
        CAMERA_ROTATION		m_allowedCameraRotation;
        bool					m_InvertCameraY;

        static bool				s_ShowBackfaceTint;
        static Vector3			s_BackfaceTintColor;
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Developer Studio will insert additional declarations immediately before the previous line.
