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

#include "StdAfx.h"
#include "DebugWindowDialog.h"
#include <cctype>		// for tolower (case-insensitive message filter)

// Cap on retained messages. The script engine appends a message for every script
// that runs, every frame; without a cap mMessages grows without bound and rebuilding
// the display string turns into O(N^2) work over a session. When the cap is exceeded
// the oldest messages are dropped (a count is kept so the user knows it happened).
static const size_t MAX_RETAINED_MESSAGES = 2000;

// Periodic flush so pending variable/message updates still appear while the app is
// paused (when SetFrameNumber, the normal per-frame flush point, isn't being called).
static const UINT_PTR DISPLAY_FLUSH_TIMER = 1;
static const UINT     DISPLAY_FLUSH_MS    = 100;

// How often (in game frames) the save file considers writing a variable snapshot.
// This is deliberately independent of the on-screen "Log every N" so the display can
// stay live (interval 1) while the file is written far less often. Combined with the
// change-only writes below, idle frames cost essentially nothing. Could be promoted to
// a UI field later if per-session tuning is wanted.
static const int FILE_SNAPSHOT_INTERVAL_FRAMES = 5;

// Hard ceiling on the save file. Once reached we stop writing (a single notice line is
// emitted). Prevents runaway disk use if Save is left on for a very long session.
static const size_t MAX_LOG_FILE_BYTES = 1024ULL * 1024 * 1024;	// 1 GB

DebugWindowDialog::DebugWindowDialog(UINT nIDTemplate, CWnd* pParentWnd) :
	CDialog(nIDTemplate, pParentWnd)
{
	mStepping = false;
	mRunFast = false;
	mNumberOfStepsAllowed = -1;
	mMessagesDropped = 0;
	mVarsDirty = false;
	mMesgDirty = false;
	mDisplayIntervalMs = DISPLAY_FLUSH_MS;
	mLogIntervalFrames = 1;
	mLogFile = nullptr;
	mSaveEnabled = false;
	mLastVarsSnapshotFrame = -1;
	mLogBytesWritten = 0;
	mLogCapHit = false;
	mMaxVarNameLen = 0;
	mLogFramesWritten = 0;
	mPrettyLog = false;	// default: plain log; the Pretty checkbox turns on cosmetic formatting
	mLosslessLog = false;	// default: interval snapshots; Lossless writes every change immediately
	mLogSession = 0;
	mLayoutBaseClient = CSize(0, 0);
	mLayoutReady = false;
	mMainWndHWND = ::FindWindow(nullptr, "Command & Conquer: Generals");
}

int DebugWindowDialog::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	return CDialog::OnCreate(lpCreateStruct);
}

BOOL DebugWindowDialog::OnInitDialog()
{
	BOOL result = CDialog::OnInitDialog();

	// Seed the rate fields with the current defaults. The dialog's child controls
	// only exist once the template has been realized (WM_INITDIALOG), not at
	// WM_CREATE, so this must run here rather than in OnCreate.
	CWnd* pDisp = GetDlgItem(IDC_DisplayInterval);
	if (pDisp) {
		char buff[16];
		sprintf(buff, "%u", mDisplayIntervalMs);
		pDisp->SetWindowText(buff);
	}
	CWnd* pLog = GetDlgItem(IDC_LogInterval);
	if (pLog) {
		char buff[16];
		sprintf(buff, "%d", mLogIntervalFrames);
		pLog->SetWindowText(buff);
	}

	// Transparency presets (whole-window opacity). The item's text is the label shown;
	// the matching percentage is parsed back out in OnTransparencyChanged, so the list
	// and the parse stay trivially in sync. Default selection is the first (Opaque).
	CComboBox* pTrans = (CComboBox*)GetDlgItem(IDC_Transparency);
	if (pTrans) {
		static const int kOpacityPercents[] = { 100, 90, 75, 50, 25 };
		for (int i = 0; i < sizeof(kOpacityPercents) / sizeof(kOpacityPercents[0]); ++i) {
			char buff[16];
			sprintf(buff, "%d%%", kOpacityPercents[i]);
			int idx = pTrans->AddString(buff);
			pTrans->SetItemData(idx, (DWORD_PTR)kOpacityPercents[i]);
		}
		pTrans->SetCurSel(0);
	}

	// Tooltips. This dialog is modeless and created from the DLL, so MFC's
	// PreTranslateMessage relay can't be relied on to feed the tooltip mouse events.
	// Registering each tool with TTF_SUBCLASS makes the tooltip control subclass the
	// target control and intercept its own mouse messages, so tips show with no relay.
	if (mToolTip.Create(this, TTS_ALWAYSTIP)) {
		struct { UINT id; const char* text; } tips[] = {
			{ IDC_DisplayInterval, "Milliseconds between debug-window redraws.\r\nRAISE this if the game stutters - fewer repaints means less overhead.\r\nLOWER it for a more live display at the cost of performance.\r\n(Works with any game build.)" },
			{ IDC_LogInterval,     "Push variable values to this window only every Nth frame.\r\nRAISE this to reduce game slowdown (the per-frame variable update is the most expensive part).\r\n1 = every frame (most accurate, slowest).\r\nNOTE: requires a rebuilt game .exe - has no effect on older executables." },
			{ IDC_Pause,           "Pause / resume the game." },
			{ IDC_Step,            "Advance the game by a single frame." },
			{ IDC_StepTen,         "Advance the game by ten frames." },
			{ IDC_RUN_FAST,        "Run the game at ~10x by skipping most frame draws." },
			{ IDC_ClearWindows,    "Clear the variables and messages lists." },
			{ IDC_SaveLog,         "Also write the message + variable log to\r\na timestamped text file in the game folder." },
			{ IDC_PrettyLog,       "Format the saved file with aligned columns, indents,\r\ntimestamps and a header/footer. Unchecked = plain text.\r\n(Cosmetic only - size/throttle protections always apply.)" },
			{ IDC_LosslessLog,     "Write EVERY variable change immediately, frame-prefixed\r\n(e.g. '[1315] Counter = 2'), instead of coalesced interval\r\nsnapshots. Higher fidelity but a larger, more frequent log." },
			{ IDC_MessageFilter,   "Show only messages containing this text (case-insensitive).\r\nClear the box to show all messages again.\r\nFiltering affects the display only - the saved log keeps everything." },
			{ IDC_Transparency,    "Make this whole window see-through so you can watch the game behind it.\r\n100% = fully opaque (normal); lower = more transparent." },
		};
		for (int i = 0; i < sizeof(tips) / sizeof(tips[0]); ++i) {
			CWnd* pCtrl = GetDlgItem(tips[i].id);
			if (!pCtrl) {
				continue;
			}
			TOOLINFO ti;
			memset(&ti, 0, sizeof(ti));
			ti.cbSize   = sizeof(ti);
			ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
			ti.hwnd     = m_hWnd;
			ti.uId      = (UINT_PTR)pCtrl->m_hWnd;
			ti.lpszText = (LPSTR)tips[i].text;
			mToolTip.SendMessage(TTM_ADDTOOL, 0, (LPARAM)&ti);
		}
		// A max width must be set for the tooltip to honor embedded \r\n line breaks
		// (and it makes over-long lines wrap instead of running off screen).
		mToolTip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 300);
		mToolTip.Activate(TRUE);
	}

	// Record the client size the .rc template was authored at. OnSize repositions the
	// resizable controls by the delta between the live client size and this baseline,
	// so the layout math stays in sync with whatever the template says (no hardcoded
	// coordinates duplicated here).
	CRect client;
	GetClientRect(&client);
	mLayoutBaseClient = client.Size();

	// Capture the original (template) rects of the controls that resize with the dialog,
	// in client coordinates. _LayoutControls computes new rects as original + delta, so
	// these are read exactly once and the math never drifts across repeated OnSize calls.
	struct { UINT id; CRect* out; } dyn[] = {
		{ IDC_Variables,      &mRectVariables  },
		{ IDC_MessageFilter,  &mRectFilterEdit },
		{ IDC_Messages,       &mRectMessages   },
	};
	for (int i = 0; i < sizeof(dyn) / sizeof(dyn[0]); ++i) {
		CWnd* pCtrl = GetDlgItem(dyn[i].id);
		if (pCtrl) {
			pCtrl->GetWindowRect(dyn[i].out);
			ScreenToClient(dyn[i].out);
		}
	}

	mLayoutReady = true;

	SetTimer(DISPLAY_FLUSH_TIMER, mDisplayIntervalMs, nullptr);
	return result;
}

BOOL DebugWindowDialog::PreTranslateMessage(MSG* pMsg)
{
	if (mToolTip.GetSafeHwnd()) {
		mToolTip.RelayEvent(pMsg);
	}
	return CDialog::PreTranslateMessage(pMsg);
}

void DebugWindowDialog::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == DISPLAY_FLUSH_TIMER) {
		_FlushDisplays();
	}
	CDialog::OnTimer(nIDEvent);
}

HWND DebugWindowDialog::GetMainWndHWND(void)
{
	return mMainWndHWND;
}

void DebugWindowDialog::ForcePause(void)
{
	mNumberOfStepsAllowed = 0;
	_UpdatePauseButton();
}

void DebugWindowDialog::ForceContinue(void)
{
	mNumberOfStepsAllowed = -1;
	_UpdatePauseButton();
}

void DebugWindowDialog::OnPause()
{
	if (mNumberOfStepsAllowed < 0) {
		mNumberOfStepsAllowed = 0;
	} else {
		mNumberOfStepsAllowed = -1;
	}
	_UpdatePauseButton();
}

void DebugWindowDialog::OnStep()
{
	mStepping = true;
	mNumberOfStepsAllowed = 1;
	_UpdatePauseButton();
}

void DebugWindowDialog::OnRunFast()
{
	CButton *pButton = (CButton*)GetDlgItem(IDC_RUN_FAST);
	mRunFast = pButton->GetCheck() == 1;
}

void DebugWindowDialog::OnStepTen()
{
	mStepping = true;
	mNumberOfStepsAllowed = 10;
	_UpdatePauseButton();
}

void DebugWindowDialog::OnClearWindows()
{
	mMessages.clear();
	mMessagesString = "";
	mMessagesDropped = 0;
	mVariables.clear();
	mVariableIndex.clear();
	mVarFileDirty.clear();
	mMaxVarNameLen = 0;
	_RebuildMesgString();
	_RebuildVarsString();
	mVarsDirty = false;
	mMesgDirty = false;
}

bool DebugWindowDialog::CanProceed(void)
{
	if (mNumberOfStepsAllowed < 0) {
		return true;
	} else if (mNumberOfStepsAllowed == 0) {
		if (mStepping) {
			mStepping = false;
			_UpdatePauseButton();
		}
		return false;
	}

	--mNumberOfStepsAllowed;
	return true;
}

bool DebugWindowDialog::RunAppFast(void)
{
	return mRunFast;
}

void DebugWindowDialog::AppendMessage(const std::string& messageToAppend)
{
	mMessages.push_back(messageToAppend);

	// Maintain mMessagesString incrementally rather than rebuilding it from the whole
	// mMessages vector on every call (that was O(N) per call -> O(N^2) per session).
	// The actual SetWindowText is coalesced to the per-frame flush via mMesgDirty.
	mMessagesString += messageToAppend;
	mMessagesString += "\r\n";

	// Tee to the save file if it's open. This is the "from tick onward" behavior:
	// only messages that arrive while Save is checked get written. The file handle
	// stays open for the session, so this is just a buffered fputs (cheap, no stall).
	if (mLogFile && mLogBytesWritten < MAX_LOG_FILE_BYTES) {
		fputs(messageToAppend.c_str(), mLogFile);
		fputc('\n', mLogFile);
		mLogBytesWritten += messageToAppend.size() + 1;
	}

	// Cap retained history so a long session can't grow the buffer without bound. The
	// trim costs O(dropped) but only happens once we are over the cap, so it is rare.
	if (mMessages.size() > MAX_RETAINED_MESSAGES) {
		mMessages.erase(mMessages.begin());
		++mMessagesDropped;
		// The incrementally-built string still has the dropped line; rebuild from the
		// (now-trimmed) vector so the display stays in sync. Still coalesced to flush.
		mMessagesString = "";
		for (VecStringIt it = mMessages.begin(); it != mMessages.end(); it++) {
			mMessagesString += (*it);
			mMessagesString += "\r\n";
		}
	}

	mMesgDirty = true;
}

void DebugWindowDialog::AdjustVariable(const std::string& varName, const std::string& varValue)
{
	// O(1) lookup via the name->index map instead of a linear scan. The ordered
	// mVariables vector is preserved as the source of truth for display order and
	// for anything that reads variable values back.
	MapVarIndex::iterator found = mVariableIndex.find(varName);
	if (found != mVariableIndex.end()) {
		PairString& existing = mVariables[found->second];
		if (existing.second != varValue) {
			existing.second = varValue;
			mVarsDirty = true;	// coalesced: actual repaint happens once per frame
			mVarFileDirty[found->second] = true;	// only changed vars get written to file
			// Lossless mode writes the change here, the moment it happens, so no
			// intermediate value is lost to interval coalescing.
			if (mLosslessLog) {
				_WriteVarLineToFile(varName, varValue);
			}
		}
		return;
	}

	PairString newPair;
	newPair.first = varName;
	newPair.second = varValue;
	mVariableIndex[varName] = mVariables.size();
	mVariables.push_back(newPair);
	mVarFileDirty.push_back(true);	// a new variable is written on its first appearance
	if (varName.size() > mMaxVarNameLen) {
		mMaxVarNameLen = varName.size();	// widest name -> column width for file alignment
	}
	if (mLosslessLog) {
		_WriteVarLineToFile(varName, varValue);
	}

	mVarsDirty = true;
}

void DebugWindowDialog::SetFrameNumber(int frameNumber)
{
	// Format into a fixed buffer and assign, so mFrameNumber's length matches the
	// actual digits. (The old "frameNumber/10 + 2" sizing over-allocated and left the
	// std::string holding a trailing NUL + garbage past the number - harmless for
	// SetWindowText, which stops at the NUL, but it leaked into the file log.)
	char frameBuff[16];
	sprintf(frameBuff, "%d", frameNumber);
	mFrameNumber = frameBuff;

	CWnd *pWnd = GetDlgItem(IDC_FrameNumber);
	if (pWnd) {
		pWnd->SetWindowText(mFrameNumber.c_str());
	}

	// SetFrameNumber is called once per frame, so this is the natural point to push
	// any variable/message changes accumulated during the frame in a single repaint.
	_FlushDisplays();

	if (mLogFile) {
		// A frame number lower than the last one we logged means the game's frame
		// counter reset - i.e. the match/mission restarted while Save was on. Roll to
		// a fresh timestamped file so each game session is self-contained instead of
		// blending two games (and so logging doesn't stall waiting for the counter to
		// climb back past the old value). _OpenLogFile closes the previous file first
		// and re-baselines all variables.
		if (mLastVarsSnapshotFrame >= 0 && frameNumber < mLastVarsSnapshotFrame) {
			_OpenLogFile();
		}

		if (mLosslessLog) {
			// Lossless mode writes each change immediately in AdjustVariable, so there
			// is no interval block here. Still track the frame so restart detection
			// (the frame-went-backwards check above) keeps working.
			mLastVarsSnapshotFrame = frameNumber;
		} else if (mLastVarsSnapshotFrame < 0 || (frameNumber - mLastVarsSnapshotFrame) >= FILE_SNAPSHOT_INTERVAL_FRAMES) {
			// Interval snapshot: only the variables that actually changed since the last
			// write, on the file's own cadence (independent of the on-screen "Log every N").
			mLastVarsSnapshotFrame = frameNumber;
			_WriteVarsSnapshotToFile();
		}
	}
}

void DebugWindowDialog::_FlushDisplays(void)
{
	if (mVarsDirty) {
		_RebuildVarsString();
		mVarsDirty = false;
	}
	if (mMesgDirty) {
		_RebuildMesgString();
		mMesgDirty = false;
	}
}

int DebugWindowDialog::GetLogInterval(void)
{
	return mLogIntervalFrames;
}

void DebugWindowDialog::_ApplyDisplayInterval(void)
{
	CWnd* pWnd = GetDlgItem(IDC_DisplayInterval);
	if (!pWnd) {
		return;
	}

	CString text;
	pWnd->GetWindowText(text);
	int ms = atoi(text);

	// Floor at ~60Hz so a stray 0 can't spin the timer; the field is ES_NUMBER so
	// it can never go negative.
	if (ms < 16) {
		ms = 16;
	}

	if ((UINT)ms != mDisplayIntervalMs) {
		mDisplayIntervalMs = (UINT)ms;
		KillTimer(DISPLAY_FLUSH_TIMER);
		SetTimer(DISPLAY_FLUSH_TIMER, mDisplayIntervalMs, nullptr);
	}
}

void DebugWindowDialog::_ApplyLogInterval(void)
{
	CWnd* pWnd = GetDlgItem(IDC_LogInterval);
	if (!pWnd) {
		return;
	}

	CString text;
	pWnd->GetWindowText(text);
	int frames = atoi(text);

	// 1 = push variables every frame (original behavior); never below 1.
	if (frames < 1) {
		frames = 1;
	}
	mLogIntervalFrames = frames;
}

void DebugWindowDialog::OnDisplayIntervalChanged(void)
{
	_ApplyDisplayInterval();
}

void DebugWindowDialog::OnLogIntervalChanged(void)
{
	_ApplyLogInterval();
}

void DebugWindowDialog::OnSaveToggle(void)
{
	CButton* pButton = (CButton*)GetDlgItem(IDC_SaveLog);
	if (!pButton) {
		return;
	}
	bool nowChecked = (pButton->GetCheck() == BST_CHECKED);
	if (nowChecked == mSaveEnabled) {
		return;
	}
	mSaveEnabled = nowChecked;

	if (mSaveEnabled) {
		_OpenLogFile();
	} else {
		_CloseLogFile();
	}
}

void DebugWindowDialog::OnPrettyToggle(void)
{
	CButton* pButton = (CButton*)GetDlgItem(IDC_PrettyLog);
	if (!pButton) {
		return;
	}
	// Cosmetic only: switches the header/line styling for subsequent writes. The
	// change-only writes, file snapshot interval, and 1 GB cap are unaffected.
	mPrettyLog = (pButton->GetCheck() == BST_CHECKED);
}

void DebugWindowDialog::OnLosslessToggle(void)
{
	CButton* pButton = (CButton*)GetDlgItem(IDC_LosslessLog);
	if (!pButton) {
		return;
	}
	// When on, every variable change is written immediately (frame-prefixed) instead of
	// coalesced interval snapshots - higher fidelity, larger/more-frequent output.
	mLosslessLog = (pButton->GetCheck() == BST_CHECKED);
}

void DebugWindowDialog::OnMessageFilterChanged(void)
{
	CWnd* pWnd = GetDlgItem(IDC_MessageFilter);
	if (!pWnd) {
		return;
	}

	CString text;
	pWnd->GetWindowText(text);

	// Store the filter lowercased so the match in _RebuildMesgString is case-insensitive.
	std::string filter = (LPCSTR)text;
	for (size_t i = 0; i < filter.size(); ++i) {
		filter[i] = (char)tolower((unsigned char)filter[i]);
	}

	if (filter == mMessageFilter) {
		return;
	}
	mMessageFilter = filter;

	// Re-filter and repaint now (don't wait for the next frame) so typing feels live.
	_RebuildMesgString();
	mMesgDirty = false;
}

void DebugWindowDialog::OnTransparencyChanged(void)
{
	CComboBox* pTrans = (CComboBox*)GetDlgItem(IDC_Transparency);
	if (!pTrans) {
		return;
	}
	int sel = pTrans->GetCurSel();
	if (sel == CB_ERR) {
		return;
	}
	// The percentage was stored as item data when the list was populated.
	_ApplyTransparency((int)pTrans->GetItemData(sel));
}

void DebugWindowDialog::_ApplyTransparency(int percentOpaque)
{
	if (percentOpaque < 1) {
		percentOpaque = 1;		// never fully invisible - the user couldn't find the window again
	} else if (percentOpaque > 100) {
		percentOpaque = 100;
	}

	LONG exStyle = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
	if (percentOpaque >= 100) {
		// Fully opaque: drop the layered style entirely so there's zero compositing cost.
		if (exStyle & WS_EX_LAYERED) {
			::SetWindowLong(m_hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
			::RedrawWindow(m_hWnd, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
		}
		return;
	}

	if (!(exStyle & WS_EX_LAYERED)) {
		::SetWindowLong(m_hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
	}
	BYTE alpha = (BYTE)((percentOpaque * 255) / 100);
	::SetLayeredWindowAttributes(m_hWnd, 0, alpha, LWA_ALPHA);
}

void DebugWindowDialog::_OpenLogFile(void)
{
	_CloseLogFile();	// never leak a previous handle

	// Name the file with a local datetime stamp: DebugLog_YYYY-MM-DD_HH-MM-SS.txt.
	// It is written to the process working directory (where the game runs); change
	// the path here if a different location is wanted. A per-process session counter is
	// appended so two opens within the same wall-clock second (e.g. a quick restart)
	// never collide and clobber the previous session's file.
	SYSTEMTIME st;
	GetLocalTime(&st);
	char name[80];
	if (mLogSession == 0) {
		sprintf(name, "DebugLog_%04d-%02d-%02d_%02d-%02d-%02d.txt",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	} else {
		sprintf(name, "DebugLog_%04d-%02d-%02d_%02d-%02d-%02d_%d.txt",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, mLogSession);
	}
	++mLogSession;

	mLogFile = fopen(name, "w");
	mLastVarsSnapshotFrame = -1;	// force a snapshot on the next frame
	mLogBytesWritten = 0;
	mLogCapHit = false;
	mLogFramesWritten = 0;

	// Mark every current variable dirty so the first snapshot writes a full baseline;
	// after that only changed variables are written.
	for (size_t i = 0; i < mVarFileDirty.size(); ++i) {
		mVarFileDirty[i] = true;
	}

	if (mLogFile) {
		// Header style depends on the Pretty checkbox. Write directly to the file
		// (no fixed intermediate buffer) so the length can never overrun a stack array.
		int n;
		if (mPrettyLog) {
			n = fprintf(mLogFile,
				"+----------------------------------------------------------+\n"
				"|  DebugWindow script log                                  |\n"
				"|  started %04d-%02d-%02d %02d:%02d:%02d                            |\n"
				"|  messages logged as they fire; variables only when they  |\n"
				"|  change (a baseline is written first).                   |\n"
				"+----------------------------------------------------------+\n\n",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		} else {
			n = fprintf(mLogFile, "=== DebugWindow log started %04d-%02d-%02d %02d:%02d:%02d ===\n",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}
		if (n > 0) {
			mLogBytesWritten += n;
		}
	}
}

void DebugWindowDialog::_CloseLogFile(void)
{
	if (mLogFile) {
		if (mPrettyLog) {	// plain mode has no footer
			SYSTEMTIME st;
			GetLocalTime(&st);
			fprintf(mLogFile,
				"\n=== log closed %04d-%02d-%02d %02d:%02d:%02d - %d frame block(s), %lu byte(s) ===\n",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
				mLogFramesWritten, (unsigned long)mLogBytesWritten);
		}
		fflush(mLogFile);
		fclose(mLogFile);
		mLogFile = nullptr;
	}
}

void DebugWindowDialog::_WriteVarLineToFile(const std::string& name, const std::string& value)
{
	// Lossless mode: one line per change, prefixed with the current frame so the order
	// and timing are preserved without per-frame grouping. Respects the size cap. Pretty
	// just pads the name to the alignment column.
	if (!mLogFile || mLogBytesWritten >= MAX_LOG_FILE_BYTES) {
		if (mLogFile && !mLogCapHit) {
			mLogCapHit = true;
			fputs("=== log size cap (1 GB) reached, stopping ===\n", mLogFile);
			fflush(mLogFile);
		}
		return;
	}

	std::string line = "[";
	line += mFrameNumber;
	line += "] ";
	line += name;
	if (mPrettyLog && mMaxVarNameLen > name.size()) {
		line.append(mMaxVarNameLen - name.size(), ' ');
	}
	line += " = ";
	line += value;
	line += "\n";

	fwrite(line.data(), 1, line.size(), mLogFile);
	mLogBytesWritten += line.size();
}

void DebugWindowDialog::_WriteVarsSnapshotToFile(void)
{
	if (!mLogFile) {
		return;
	}

	if (mLogBytesWritten >= MAX_LOG_FILE_BYTES) {
		if (!mLogCapHit) {
			mLogCapHit = true;
			fputs("=== log size cap (1 GB) reached, stopping ===\n", mLogFile);
			fflush(mLogFile);
		}
		return;
	}

	// Build the whole block first, then do a single fwrite, so a frame with many
	// changed variables is one buffered write instead of many fprintf calls. Only
	// variables flagged dirty (changed since last write) are emitted; if none changed
	// this frame, nothing is written at all - not even the header. Lines are indented
	// and the names padded to a column so values line up; the pad width is the widest
	// name seen so far (tracked in AdjustVariable, no per-frame scan).
	// Pretty mode indents each line and pads the name to a column so values line up;
	// plain mode writes bare "name = value". Either way only changed (dirty) variables
	// are emitted - the change-only / cap / interval protections are independent of the
	// Pretty toggle.
	std::string block;
	for (size_t i = 0; i < mVariables.size(); ++i) {
		if (!mVarFileDirty[i]) {
			continue;
		}
		mVarFileDirty[i] = false;
		const std::string& name = mVariables[i].first;
		if (mPrettyLog) {
			block += "    ";		// indent under the frame header
			block += name;
			if (mMaxVarNameLen > name.size()) {
				block.append(mMaxVarNameLen - name.size(), ' ');	// pad name to the column
			}
		} else {
			block += name;
		}
		block += " = ";
		block += mVariables[i].second;
		block += "\n";
	}

	if (block.empty()) {
		return;
	}

	// Frame header. Pretty mode adds a wall-clock timestamp; plain mode is the original
	// "--- frame N ---". GetLocalTime only runs on snapshot frames (the file cadence),
	// not every game frame, so it is cheap.
	std::string header = "--- frame ";
	header += mFrameNumber;
	if (mPrettyLog) {
		SYSTEMTIME st;
		GetLocalTime(&st);
		char timeBuff[16];
		sprintf(timeBuff, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
		header += " @ ";
		header += timeBuff;
	}
	header += " ---\n";

	fwrite(header.data(), 1, header.size(), mLogFile);
	fwrite(block.data(), 1, block.size(), mLogFile);
	mLogBytesWritten += header.size() + block.size();
	++mLogFramesWritten;
}

void DebugWindowDialog::_RebuildVarsString(void)
{
	int cursorPosBeg, cursorPosEnd;
	((CEdit*)GetDlgItem(IDC_Variables))->GetSel(cursorPosBeg, cursorPosEnd);
	mVariablesString = "";

	for (VecPairStringIt it = mVariables.begin(); it != mVariables.end(); it++) {
		mVariablesString += it->first;
		mVariablesString += " = ";
		mVariablesString += it->second;
		mVariablesString += "\r\n";
	}



	// Push the new string
	mVariablesDisplayString = mVariablesString.c_str();
	GetDlgItem(IDC_Variables)->SetWindowText(mVariablesDisplayString);
	((CEdit*)GetDlgItem(IDC_Variables))->GetSel(cursorPosBeg, cursorPosEnd);
}

void DebugWindowDialog::_RebuildMesgString(void)
{
	// mMessagesString is maintained incrementally by AppendMessage / OnClearWindows and
	// stays the full unfiltered buffer (source of truth, also what's teed to the file).
	// When a filter is active we build the displayed text from mMessages instead, keeping
	// only the lines that contain the filter substring (case-insensitive). SetWindowText
	// is used (not EM_REPLACESEL) because the Messages control is ES_READONLY, on which
	// EM_REPLACESEL is silently ignored.
	if (mMessageFilter.empty()) {
		mMessagesDisplayString = mMessagesString.c_str();
	} else {
		std::string filtered;
		for (VecStringIt it = mMessages.begin(); it != mMessages.end(); it++) {
			// Case-insensitive substring match against the (lowercased) filter.
			std::string lower = *it;
			for (size_t i = 0; i < lower.size(); ++i) {
				lower[i] = (char)tolower((unsigned char)lower[i]);
			}
			if (lower.find(mMessageFilter) != std::string::npos) {
				filtered += (*it);
				filtered += "\r\n";
			}
		}
		mMessagesDisplayString = filtered.c_str();
	}
	GetDlgItem(IDC_Messages)->SetWindowText(mMessagesDisplayString);
	int end = mMessagesDisplayString.GetLength();
	((CEdit*)GetDlgItem(IDC_Messages))->SetSel(end, end, false);
}

void DebugWindowDialog::_UpdatePauseButton(void)
{
	// huh huh huhuh he said pButt
	CButton* pButt = (CButton*) GetDlgItem(IDC_Pause);

	if (!pButt) {
		return;
	}

	// The state should of the button should reflect !mNumberOfStepsAllowed
	pButt->SetCheck((mNumberOfStepsAllowed ? FALSE : TRUE));
}


void DebugWindowDialog::OnDestroy()
{
	_CloseLogFile();	// don't leave the save file open if the window is destroyed while checked
	CDialog::OnDestroy();
}

void DebugWindowDialog::OnClose()
{
	ShowWindow(SW_MINIMIZE);
}

void DebugWindowDialog::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) {
		if (mMainWndHWND) {
			::SetFocus(mMainWndHWND);
		}
		return;
	}
	_LayoutControls();
}

void DebugWindowDialog::_LayoutControls(void)
{
	// Reposition the resizable controls for the current client size. All the top-row
	// controls (Pause, Step, Step 10, Run Fast, Clear, the rate fields, the Save/Pretty/
	// Lossless checkboxes, labels) keep their template positions - they do not move with
	// the window. Only the Variables/Filter boxes stretch their width, and the Messages
	// box absorbs both width and height. Each new rect is its captured template rect plus
	// the delta - no cumulative drift.
	if (!mLayoutReady) {
		return;
	}

	CRect client;
	GetClientRect(&client);
	int dx = client.Width()  - mLayoutBaseClient.cx;
	int dy = client.Height() - mLayoutBaseClient.cy;

	HDWP hdwp = ::BeginDeferWindowPos(3);

	// Variables: top-anchored, stretches width only.
	CWnd* pVars = GetDlgItem(IDC_Variables);
	if (pVars) {
		hdwp = ::DeferWindowPos(hdwp, pVars->m_hWnd, nullptr,
			0, 0, mRectVariables.Width() + dx, mRectVariables.Height(),
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	// Filter edit: fixed y, stretches width only.
	CWnd* pFilter = GetDlgItem(IDC_MessageFilter);
	if (pFilter) {
		hdwp = ::DeferWindowPos(hdwp, pFilter->m_hWnd, nullptr,
			0, 0, mRectFilterEdit.Width() + dx, mRectFilterEdit.Height(),
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	// Messages: absorbs all the growth - stretches both width and height.
	CWnd* pMsg = GetDlgItem(IDC_Messages);
	if (pMsg) {
		hdwp = ::DeferWindowPos(hdwp, pMsg->m_hWnd, nullptr,
			0, 0, mRectMessages.Width() + dx, mRectMessages.Height() + dy,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	if (hdwp) {
		::EndDeferWindowPos(hdwp);
	}
}

void DebugWindowDialog::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	// Don't let the window shrink below the authored template size, or the controls
	// would overlap / clip. The template client size plus the non-client border gives
	// the minimum outer size. mLayoutBaseClient is 0 until OnInitDialog runs, so fall
	// back to the default until then.
	if (mLayoutReady && mLayoutBaseClient.cx > 0) {
		CRect wnd, client;
		GetWindowRect(&wnd);
		GetClientRect(&client);
		int ncW = wnd.Width()  - client.Width();	// non-client (border) width
		int ncH = wnd.Height() - client.Height();
		lpMMI->ptMinTrackSize.x = mLayoutBaseClient.cx + ncW;
		lpMMI->ptMinTrackSize.y = mLayoutBaseClient.cy + ncH;
	}
	CDialog::OnGetMinMaxInfo(lpMMI);
}



BEGIN_MESSAGE_MAP(DebugWindowDialog, CDialog)
	ON_WM_CREATE( )
	ON_BN_CLICKED(IDC_Pause, OnPause)
	ON_BN_CLICKED(IDC_Step, OnStep)
	ON_BN_CLICKED(IDC_RUN_FAST, OnRunFast)
	ON_BN_CLICKED(IDC_StepTen, OnStepTen)
	ON_BN_CLICKED(IDC_ClearWindows, OnClearWindows)
	ON_EN_KILLFOCUS(IDC_DisplayInterval, OnDisplayIntervalChanged)
	ON_EN_KILLFOCUS(IDC_LogInterval, OnLogIntervalChanged)
	ON_BN_CLICKED(IDC_SaveLog, OnSaveToggle)
	ON_BN_CLICKED(IDC_PrettyLog, OnPrettyToggle)
	ON_BN_CLICKED(IDC_LosslessLog, OnLosslessToggle)
	ON_EN_CHANGE(IDC_MessageFilter, OnMessageFilterChanged)
	ON_CBN_SELCHANGE(IDC_Transparency, OnTransparencyChanged)
	ON_WM_DESTROY()
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_TIMER()
END_MESSAGE_MAP()
