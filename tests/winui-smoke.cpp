/* WinUI bridge integration checks. Uses mock HWND controls; no disk engine is linked. */
#define RUFUS_WINUI
#include "../src/winui.cpp"
#include <cstdio>
#include <stdexcept>

extern "C" {
char* szStatusMessage = const_cast<char*>("Mock device ready");
BOOL right_to_left_mode = FALSE;
char* lmprintf(uint32_t, ...) { return const_cast<char*>("Mock localized label"); }
}

namespace {
int commandCounts[1200]{};
int scrollCount = 0;

void Require(bool result, const char* message)
{
	if (!result) throw std::runtime_error(message);
}

LRESULT CALLBACK MockWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_COMMAND) {
		int id = LOWORD(wParam);
		if (id < ARRAYSIZE(commandCounts)) ++commandCounts[id];
		return 0;
	}
	if (message == WM_HSCROLL) { ++scrollCount; return 0; }
	if (message == WM_DESTROY) WinUIDestroy();
	return DefWindowProcW(window, message, wParam, lParam);
}

void Pump()
{
	MSG message;
	for (int count = 0; count < 1000 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
		if (message.message == WM_QUIT) continue;
		if (!WinUIPreTranslateMessage(&message)) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
}

HWND Child(HWND parent, int id, LPCWSTR type, DWORD style, LPCWSTR text = L"Mock field")
{
	HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
		0, 0, 200, 24, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
	Require(child != nullptr, "create mock child");
	return child;
}

void Toolbar(HWND parent, int id, std::initializer_list<int> commands)
{
	HWND toolbar = Child(parent, id, TOOLBARCLASSNAMEW, 0, nullptr);
	SendMessageW(toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
	for (int command : commands) {
		TBBUTTON button{};
		button.idCommand = command;
		button.iBitmap = I_IMAGENONE;
		button.fsState = TBSTATE_ENABLED;
		button.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
		button.iString = reinterpret_cast<INT_PTR>(L"Mock toolbar action");
		Require(SendMessageW(toolbar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&button)) != FALSE, "add mock toolbar action");
	}
}

HWND MockDialog()
{
	HWND window = CreateWindowExW(0, L"RufusWinUISmoke", L"Rufus WinUI mock", WS_OVERLAPPEDWINDOW,
		0, 0, 640, 880, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	Require(window != nullptr, "create mock host");
	const int labels[] = { IDS_DRIVE_PROPERTIES_TXT, IDS_DEVICE_TXT, IDS_BOOT_SELECTION_TXT,
		IDS_IMAGE_OPTION_TXT, IDS_PARTITION_TYPE_TXT, IDS_TARGET_SYSTEM_TXT, IDS_FORMAT_OPTIONS_TXT,
		IDS_LABEL_TXT, IDS_FILE_SYSTEM_TXT, IDS_CLUSTER_SIZE_TXT, IDS_STATUS_TXT };
	for (int id : labels) Child(window, id, L"STATIC", 0);
	const int combos[] = { IDC_DEVICE, IDC_BOOT_SELECTION, IDC_IMAGE_OPTION, IDC_PERSISTENCE_UNITS,
		IDC_PARTITION_TYPE, IDC_TARGET_SYSTEM, IDC_FILE_SYSTEM, IDC_CLUSTER_SIZE, IDC_NB_PASSES };
	for (int id : combos) {
		HWND combo = Child(window, id, WC_COMBOBOXW, CBS_DROPDOWNLIST);
		SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"First"));
		SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Second"));
		SendMessageW(combo, CB_SETITEMDATA, 0, 101);
		SendMessageW(combo, CB_SETITEMDATA, 1, 202);
		SendMessageW(combo, CB_SETCURSEL, 0, 0);
	}
	Child(window, IDC_LABEL, WC_EDITW, ES_AUTOHSCROLL, L"TEST");
	HWND persistence = Child(window, IDC_PERSISTENCE_SIZE, WC_EDITW, ES_NUMBER, L"1");
	SendMessageW(persistence, EM_LIMITTEXT, 7, 0);
	const int checks[] = { IDC_LIST_USB_HDD, IDC_OLD_BIOS_FIXES, IDC_UEFI_MEDIA_VALIDATION,
		IDC_QUICK_FORMAT, IDC_EXTENDED_LABEL, IDC_BAD_BLOCKS };
	for (int id : checks) Child(window, id, WC_BUTTONW, BS_AUTOCHECKBOX);
	Child(window, IDC_SELECT, WC_BUTTONW, BS_SPLITBUTTON, L"SELECT");
	Child(window, IDC_START, WC_BUTTONW, BS_DEFPUSHBUTTON, L"START");
	Child(window, IDCANCEL, WC_BUTTONW, BS_PUSHBUTTON, L"CLOSE");
	HWND slider = Child(window, IDC_PERSISTENCE_SLIDER, TRACKBAR_CLASSW, 0);
	SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
	HWND progress = Child(window, IDC_PROGRESS, PROGRESS_CLASSW, 0, L"Ready");
	SendMessageW(progress, PBM_SETRANGE32, 0, 1000);
	HWND status = Child(window, IDC_STATUS, STATUSCLASSNAMEW, 0);
	int parts[] = { 400, -1 };
	SendMessageW(status, SB_SETPARTS, ARRAYSIZE(parts), reinterpret_cast<LPARAM>(parts));
	SendMessageA(status, SB_SETTEXTA, 1 | SBT_OWNERDRAW, reinterpret_cast<LPARAM>("00:00:42"));
	Toolbar(window, IDC_SAVE_TOOLBAR, { IDC_SAVE });
	Toolbar(window, IDC_HASH_TOOLBAR, { IDC_HASH });
	Toolbar(window, IDC_ADVANCED_DEVICE_TOOLBAR, { IDC_ADVANCED_DRIVE_PROPERTIES });
	Toolbar(window, IDC_ADVANCED_FORMAT_TOOLBAR, { IDC_ADVANCED_FORMAT_OPTIONS });
	Toolbar(window, IDC_MULTI_TOOLBAR, { IDC_LANG, IDC_ABOUT, IDC_SETTINGS, IDC_LOG });
	return window;
}

void VerifyBridge(HWND window)
{
	Require(WinUIInitialize(window) != FALSE, "initialize WinUI island");
	Require(WinUIIsActive(), "island active");
	Pump();
	Require(view->Find(IDC_DEVICE)->view.as<ComboBox>().Items().Size() == 2, "initial device entries");
	Require(view->elapsed.Text() == L"00:00:42", "owner-draw timer text");
	Require(view->status.Text() == L"Mock device ready", "status text");

	view->Find(IDC_DEVICE)->view.as<ComboBox>().SelectedIndex(1);
	Require(SendDlgItemMessageW(window, IDC_DEVICE, CB_GETCURSEL, 0, 0) == 1, "combo selection reaches native state");
	Require(commandCounts[IDC_DEVICE] == 1, "combo notifies engine exactly once");
	view->Find(IDC_LABEL)->view.as<TextBox>().Text(L"NEW LABEL");
	Require(WindowText(GetDlgItem(window, IDC_LABEL)) == L"NEW LABEL", "text edit reaches native state");
	Require(commandCounts[IDC_LABEL] == 1, "text edit sends a single EN_CHANGE");

	QueueCommand(IDC_QUICK_FORMAT);
	Pump();
	Require(SendDlgItemMessageW(window, IDC_QUICK_FORMAT, BM_GETCHECK, 0, 0) == BST_CHECKED, "checkbox reaches native state");
	Require(commandCounts[IDC_QUICK_FORMAT] == 1, "checkbox command count");
	view->Find(IDC_PERSISTENCE_SLIDER)->view.as<Slider>().Value(25);
	Require(SendDlgItemMessageW(window, IDC_PERSISTENCE_SLIDER, TBM_GETPOS, 0, 0) == 25, "slider reaches native state");
	Require(scrollCount == 1, "slider notifies engine once");

	SendDlgItemMessageW(window, IDC_PROGRESS, PBM_SETPOS, 750, 0);
	WinUISetProgressState(PBST_ERROR);
	WinUISetProgressMarquee(TRUE);
	view->Sync();
	Require(view->progress.Value() == 750 && view->progress.ShowError() && view->progress.IsIndeterminate(), "progress state and marquee");
	ShowWindow(GetDlgItem(window, IDC_IMAGE_OPTION), SW_HIDE);
	view->Sync();
	Require(view->Find(IDC_IMAGE_OPTION)->container.Visibility() == Visibility::Collapsed, "conditional control visibility");
	EnableWindow(GetDlgItem(window, IDC_START), FALSE);
	QueueCommand(IDC_START);
	Pump();
	Require(commandCounts[IDC_START] == 0, "disabled native Start rejects stale view click");
	EnableWindow(GetDlgItem(window, IDC_START), TRUE);
	SendDlgItemMessageW(window, IDC_MULTI_TOOLBAR, TB_ENABLEBUTTON, IDC_ABOUT, FALSE);
	QueueCommand(IDC_ABOUT);
	Pump();
	Require(commandCounts[IDC_ABOUT] == 0, "disabled toolbar action rejected");

	view->Sync();
	SendDlgItemMessageW(window, IDC_DEVICE, CB_SETITEMDATA, 0, 303);
	view->Find(IDC_DEVICE)->view.as<ComboBox>().SelectedIndex(0);
	Require(SendDlgItemMessageW(window, IDC_DEVICE, CB_GETCURSEL, 0, 0) == 1, "replaced device identity rejects stale selection");
	Require(commandCounts[IDC_DEVICE] == 1, "stale device list never reaches engine");
	SendDlgItemMessageW(window, IDC_DEVICE, CB_SETCURSEL, 0, 0);
	QueueCommand(IDC_START);
	Pump();
	Require(commandCounts[IDC_START] == 0, "stale selected device rejects Start");
	QueueCommand(IDC_START);
	Pump();
	Require(commandCounts[IDC_START] == 1, "refreshed Start reaches engine");
	QueueCommand(IDCANCEL);
	Pump();
	Require(commandCounts[IDCANCEL] == 1, "Cancel reaches engine");
}
} // namespace

int main()
{
	SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
	InitCommonControlsEx(&controls);
	WNDCLASSW windowClass{};
	windowClass.lpfnWndProc = MockWindowProc;
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.lpszClassName = L"RufusWinUISmoke";
	RegisterClassW(&windowClass);
	HWND window = nullptr;
	int result = 0;
	try {
		window = MockDialog();
		VerifyBridge(window);
		DestroyWindow(window);
		window = nullptr;
		Require(!WinUIIsActive(), "destroy clears island");
		window = MockDialog();
		Require(WinUIInitialize(window) != FALSE, "language relaunch recreates island on existing runtime");
		Pump();
		DestroyWindow(window);
		window = nullptr;
		Require(!WinUIIsActive(), "second destroy clears island");
		WinUIShutdown();
		std::puts("PASS: WinUI initialization, state bridge, stale-device protection, relaunch and shutdown.");
	} catch (std::exception const& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		std::fwprintf(stderr, L"%ls\n", WinUIErrorMessage());
		result = 1;
	} catch (winrt::hresult_error const& error) {
		std::fwprintf(stderr, L"FAIL: 0x%08X %ls\n", static_cast<unsigned>(error.code().value), error.message().c_str());
		result = 1;
	}
	if (window) DestroyWindow(window);
	WinUIShutdown();
	CoUninitialize();
	return result;
}
