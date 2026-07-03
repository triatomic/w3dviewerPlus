/*
**	Command & Conquer Generals Zero Hour(tm)
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


#include "Resource.h"
#include <map>			// for std::pair
#include <string>		// for std::string
#include <vector>		// for std::vector
#include <cstdio>		// for FILE* (Save-to-file logging)

typedef std::pair<std::string, std::string>	PairString;
typedef std::vector<PairString>				VecPairString;
typedef std::vector<std::string>			VecString;

typedef std::vector<PairString>::iterator	VecPairStringIt;
typedef std::vector<std::string>::iterator	VecStringIt;

// Index from variable name -> position in mVariables, so AdjustVariable is O(1)
// instead of a linear scan of every variable on every call.
typedef std::map<std::string, size_t>		MapVarIndex;


class DebugWindowDialog : public CDialog
{
	public:
		enum {IDD = IDD_DebugWindow};
		DebugWindowDialog(UINT nIDTemplate = DebugWindowDialog::IDD, CWnd* pParentWnd = nullptr);

		bool CanProceed(void);
		bool RunAppFast(void);
		void AppendMessage(const std::string& messageToAppend);
		void AdjustVariable(const std::string& varName, const std::string& varValue);
		void SetFrameNumber(int frameNumber);
		HWND GetMainWndHWND(void);
		void ForcePause(void);
		void ForceContinue(void);
		int  GetLogInterval(void);		// frames between variable pushes the game should use (>=1)

	// This var shouldn't be here, but honsestly...
	protected:
		HWND					mMainWndHWND;
		int						mNumberOfStepsAllowed;		/// -1 means go forever, 0 means stop now, a positive number will be decremented to 0, 1 frame at a time
		std::string		mVariablesString;
		std::string		mMessagesString;
		CString				mVariablesDisplayString;	// For double buffering
		CString				mMessagesDisplayString;		// For double buffering
		std::string		mFrameNumber;
		bool					mStepping;
		bool					mRunFast;



		VecPairString	mVariables;
		MapVarIndex		mVariableIndex;				// name -> index into mVariables (O(1) lookup)
		std::vector<bool> mVarFileDirty;			// per-variable: changed since last written to the save file
		VecString		mMessages;
		size_t			mMessagesDropped;			// count of messages aged out of the ring buffer
		std::string		mMessageFilter;				// lowercased substring filter for the Messages display ("" = show all)
		CSize			mLayoutBaseClient;			// client size at OnInitDialog; OnSize repositions controls by the delta from this
		bool			mLayoutReady;				// true once the baseline + initial control rects are captured
		CRect			mRectVariables;				// original (template) rects of the resizable controls, in client coords
		CRect			mRectFilterEdit;
		CRect			mRectMessages;
		bool			mVarsDirty;					// pending variable display update, flushed once per frame
		bool			mMesgDirty;					// pending message display update, flushed once per frame
		UINT			mDisplayIntervalMs;			// display repaint period (the WM_TIMER flush rate)
		int				mLogIntervalFrames;			// frames between variable pushes the game should send (>=1)
		CToolTipCtrl	mToolTip;					// tooltips for the rate / control fields

		FILE*			mLogFile;					// open text log when Save is checked, else null
		bool			mSaveEnabled;				// mirror of the Save checkbox state
		int				mLastVarsSnapshotFrame;		// last frame a variable snapshot was written to file
		size_t			mLogBytesWritten;			// running byte count, for the size cap (avoids per-write stat)
		bool			mLogCapHit;					// true once the size cap was reached (notice written once)
		size_t			mMaxVarNameLen;				// longest variable name seen, for column alignment in the file
		int				mLogFramesWritten;			// count of frame blocks written (for the close footer)
		bool			mPrettyLog;					// Pretty checkbox: cosmetic formatting on/off (perf protections stay either way)
		bool			mLosslessLog;				// Lossless checkbox: write every variable change immediately (frame-prefixed) instead of interval snapshots
		int				mLogSession;				// per-process counter, suffixes the filename so same-second opens don't collide

		void _LayoutControls(void);						// reposition resizable controls for the current client size
	void _ApplyTransparency(int percentOpaque);		// set whole-window opacity via a layered window (100 = fully opaque)

	void _RebuildVarsString(void);
		void _RebuildMesgString(void);
		void _FlushDisplays(void);					// push any pending dirty displays to the controls
		void _UpdatePauseButton(void);
		void _ApplyDisplayInterval(void);			// read IDC_DisplayInterval, clamp, restart the timer
		void _ApplyLogInterval(void);				// read IDC_LogInterval, clamp into mLogIntervalFrames
		void _OpenLogFile(void);					// start a fresh timestamped log file
		void _CloseLogFile(void);					// flush + close the current log file
		void _WriteVarsSnapshotToFile(void);		// write a "--- frame N ---" + changed-vars block (interval mode)
		void _WriteVarLineToFile(const std::string& name, const std::string& value);	// write one frame-prefixed line (lossless mode)

	protected:
		afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
		virtual BOOL OnInitDialog();
		virtual BOOL PreTranslateMessage(MSG* pMsg);
		afx_msg void OnSize(UINT nType, int cx, int cy);
		afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
		afx_msg void OnTimer(UINT_PTR nIDEvent);
		afx_msg void OnPause();
		afx_msg void OnStep();
		afx_msg void OnRunFast();
		afx_msg void OnStepTen();
		afx_msg void OnClearWindows();
		afx_msg void OnDisplayIntervalChanged();
		afx_msg void OnLogIntervalChanged();
		afx_msg void OnSaveToggle();
		afx_msg void OnPrettyToggle();
		afx_msg void OnLosslessToggle();
		afx_msg void OnMessageFilterChanged();
		afx_msg void OnTransparencyChanged();
		afx_msg void OnDestroy();
		afx_msg void OnClose();
		DECLARE_MESSAGE_MAP()



};
