/*
 * Rufus: The Reliable USB Formatting Utility
 * WinUI 3 main window, hosted in the existing native dialog.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The native controls remain the engine's state store during migration. Never
 * copy device identifiers or formatting policy into this view: selections and
 * commands go through the same handlers as the original dialog.
 */
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#undef GetCurrentTime
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include "winui.h"
#include "resource.h"
extern "C" {
#include "localization.h"
extern char* szStatusMessage;
extern BOOL right_to_left_mode;
}

namespace {
using namespace winrt;
using namespace winrt::Microsoft::UI;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Hosting;
using namespace winrt::Microsoft::UI::Xaml::Markup;
using namespace winrt::Microsoft::UI::Dispatching;

constexpr UINT_PTR syncTimer = 0x57494E;
constexpr UINT_PTR subclassId = 0x57494E;
constexpr UINT commandMessage = WM_APP + 0x574;
constexpr UINT initialFocusMessage = WM_APP + 0x575;

struct XamlApp : ApplicationT<XamlApp, IXamlMetadataProvider> {
	XamlTypeInfo::XamlControlsXamlMetaDataProvider metadata;

	IXamlType GetXamlType(Windows::UI::Xaml::Interop::TypeName const& type) { return metadata.GetXamlType(type); }
	IXamlType GetXamlType(hstring const& name) { return metadata.GetXamlType(name); }
	com_array<XmlnsDefinition> GetXmlnsDefinitions() { return metadata.GetXmlnsDefinitions(); }
};

HMODULE bootstrapModule = nullptr;
decltype(&MddBootstrapShutdown) bootstrapShutdown = nullptr;
bool bootstrapReady = false;
using PreTranslate = BOOL (WINAPI*)(const MSG*);
PreTranslate contentPreTranslate = nullptr;
Application application{ nullptr };
DispatcherQueueController dispatcher{ nullptr };
WindowsXamlManager xamlManager{ nullptr };
int progressState = PBST_NORMAL;
bool progressMarquee = false;
std::wstring initializationError;

hstring WindowText(HWND window)
{
	int length = GetWindowTextLengthW(window);
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	text.resize(GetWindowTextW(window, text.data(), length + 1));
	return hstring(text);
}

hstring LabelText(hstring const& text)
{
	std::wstring result;
	for (hstring::size_type i = 0; i < text.size(); ++i) {
		if (text[i] == L'&') {
			if (i + 1 < text.size() && text[i + 1] == L'&') ++i;
			else continue;
		}
		result += text[i];
	}
	return hstring(result);
}

hstring Localized(int id) { return LabelText(to_hstring(lmprintf(id))); }

void SetButtonText(Button const& button, hstring const& text)
{
	if (unbox_value_or<hstring>(button.Content(), {}) != text) button.Content(box_value(text));
}

struct Binding {
	int id;
	HWND native;
	Control view{ nullptr };
	FrameworkElement container{ nullptr };
	int toolbarCommand = 0;
	std::vector<std::wstring> items;
	std::vector<LPARAM> itemValues;
	int selected = -1;
	int label = 0;
	TextBlock caption{ nullptr };
};

struct MainView {
	HWND dialog = nullptr;
	DesktopWindowXamlSource island{ nullptr };
	Grid root{ nullptr };
	TextBlock info{ nullptr }, status{ nullptr }, elapsed{ nullptr };
	ProgressBar progress{ nullptr };
	std::vector<Binding> bindings;
	std::vector<HWND> nativeChildren;
	bool syncing = false;
	bool redirectingFocus = false;
	bool initialFocusSet = false;

	void Build();
	void Sync();
	void Resize();
	void Focus(int id = IDC_DEVICE);
	bool Enabled(Binding const& binding) const;
	bool ItemsCurrent(Binding const& binding) const;
	bool SelectionsCurrent() const;
	Binding* Find(int id);
	void AddCombo(Panel const& parent, int id, int label, hstring const& fallback = {});
	void AddEdit(Panel const& parent, int id, int label);
	void AddCheck(Panel const& parent, int id);
	Button AddButton(Panel const& parent, int id, int toolbar = 0, hstring const& text = {});
	void Bind(Panel const& parent, int id, Control const& control, int label = 0,
		int toolbar = 0, hstring const& fallback = {});
};
std::unique_ptr<MainView> view;

// Queue commands so destroying/recreating the host cannot invalidate a XAML
// event handler on its own stack. Recheck native state when the command runs.
void QueueCommand(int id, int notification = BN_CLICKED)
{
	if (view && !view->syncing)
		PostMessageW(view->dialog, commandMessage, MAKEWPARAM(id, notification), 0);
}

void SetName(DependencyObject const& control, hstring const& name, int id)
{
	Automation::AutomationProperties::SetName(control, name);
	Automation::AutomationProperties::SetAutomationId(control, L"rufus_" + to_hstring(id));
}

StackPanel Section(Panel const& parent, hstring const& title)
{
	StackPanel section;
	section.Spacing(12);
	TextBlock heading;
	heading.Text(title);
	heading.FontSize(20);
	heading.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
	section.Children().Append(heading);
	parent.Children().Append(section);
	return section;
}

Binding* MainView::Find(int id)
{
	auto it = std::find_if(bindings.begin(), bindings.end(), [id](Binding const& b) { return b.id == id; });
	return it == bindings.end() ? nullptr : &*it;
}

bool MainView::Enabled(Binding const& b) const
{
	return IsWindowEnabled(dialog) && IsWindowEnabled(b.native) &&
		(!b.toolbarCommand || SendMessageW(b.native, TB_ISBUTTONENABLED, b.toolbarCommand, 0));
}

bool MainView::ItemsCurrent(Binding const& b) const
{
	if (SendMessageW(b.native, CB_GETCOUNT, 0, 0) != static_cast<LRESULT>(b.items.size())) return false;
	for (size_t i = 0; i < b.items.size(); ++i) {
		if (SendMessageW(b.native, CB_GETITEMDATA, i, 0) != b.itemValues[i]) return false;
		int length = static_cast<int>(SendMessageW(b.native, CB_GETLBTEXTLEN, i, 0));
		if (length < 0 || static_cast<size_t>(length) != b.items[i].size()) return false;
		std::wstring text(static_cast<size_t>(length) + 1, L'\0');
		SendMessageW(b.native, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(text.data()));
		text.resize(length);
		if (text != b.items[i]) return false;
	}
	return true;
}

bool MainView::SelectionsCurrent() const
{
	for (auto const& binding : bindings)
		if (binding.view.try_as<ComboBox>() && (!ItemsCurrent(binding) ||
			binding.selected != SendMessageW(binding.native, CB_GETCURSEL, 0, 0))) return false;
	return true;
}

void MainView::Bind(Panel const& parent, int id, Control const& control, int label,
	int toolbar, hstring const& fallback)
{
	HWND native = GetDlgItem(dialog, toolbar ? toolbar : id);
	hstring name = label ? LabelText(WindowText(GetDlgItem(dialog, label))) : fallback;
	if (name.empty()) name = LabelText(WindowText(native));
	SetName(control, name, id);
	control.HorizontalAlignment(HorizontalAlignment::Stretch);
	FrameworkElement container = control;
	TextBlock caption{ nullptr };
	if (label || !fallback.empty()) {
		StackPanel field;
		field.Spacing(5);
		caption = TextBlock();
		caption.Text(name);
		caption.TextWrapping(TextWrapping::Wrap);
		field.Children().Append(caption);
		field.Children().Append(control);
		container = field;
	}
	Binding binding{ id, native, control, container, toolbar ? id : 0 };
	binding.label = label;
	binding.caption = caption;
	bindings.push_back(std::move(binding));
	parent.Children().Append(container);
}

void MainView::AddCombo(Panel const& parent, int id, int label, hstring const& fallback)
{
	ComboBox combo;
	Bind(parent, id, combo, label, 0, fallback);
	combo.SelectionChanged([id](auto const& sender, auto const&) {
		if (!view || view->syncing) return;
		auto* binding = view->Find(id);
		if (!binding || !view->Enabled(*binding)) return;
		// Hotplug or image scanning can replace a list between the last refresh
		// and this input. Do not apply an old index to the replacement list.
		if (!view->ItemsCurrent(*binding)) { view->Sync(); return; }
		// Native combo entries retain the engine's item data (physical drive,
		// filesystem, partition type...). Only the selected index crosses here.
		int index = sender.template as<ComboBox>().SelectedIndex();
		if (index < 0 || index >= SendMessageW(binding->native, CB_GETCOUNT, 0, 0)) return;
		SendMessageW(binding->native, CB_SETCURSEL, index, 0);
		SendMessageW(view->dialog, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(binding->native));
		if (view) view->Sync();
	});
}

void MainView::AddEdit(Panel const& parent, int id, int label)
{
	TextBox edit;
	Bind(parent, id, edit, label);
	edit.TextChanged([id](auto const& sender, auto const&) {
		if (!view || view->syncing) return;
		auto* binding = view->Find(id);
		if (!binding || !view->Enabled(*binding)) return;
		hstring text = sender.template as<TextBox>().Text();
		if (id == IDC_PERSISTENCE_SIZE && std::wstring_view(text).find_first_not_of(L"0123456789") != std::wstring_view::npos) {
			view->Sync();
			return;
		}
		// SetWindowText sends EN_CHANGE. Sending it a second time would corrupt
		// the engine's app_changed_label/app_changed_size tracking.
		SetWindowTextW(binding->native, text.c_str());
	});
	edit.LostFocus([id](auto const&, auto const&) { QueueCommand(id, EN_KILLFOCUS); });
}

void MainView::AddCheck(Panel const& parent, int id)
{
	CheckBox checkbox;
	checkbox.Content(box_value(LabelText(WindowText(GetDlgItem(dialog, id)))));
	Bind(parent, id, checkbox);
	checkbox.Click([id](auto const&, auto const&) { QueueCommand(id); });
}

Button MainView::AddButton(Panel const& parent, int id, int toolbar, hstring const& text)
{
	Button button;
	button.Content(box_value(text));
	Bind(parent, id, button, 0, toolbar);
	if (!text.empty()) SetName(button, text, id);
	button.Click([id](auto const&, auto const&) { QueueCommand(id); });
	return button;
}

void MainView::Build()
{
	root = Grid();
	root.Background(Application::Current().Resources().Lookup(box_value(L"ApplicationPageBackgroundThemeBrush")).as<Media::Brush>());
	root.FlowDirection(right_to_left_mode ? FlowDirection::RightToLeft : FlowDirection::LeftToRight);
	// Let controls handle Escape first (for example, closing a combo popup).
	// An otherwise unhandled Escape retains the native dialog's Cancel action.
	root.KeyDown([](auto const&, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args) {
		if (args.Key() != Windows::System::VirtualKey::Escape || args.Handled() || !view) return;
		auto* cancel = view->Find(IDCANCEL);
		if (cancel && view->Enabled(*cancel)) {
			QueueCommand(IDCANCEL);
			args.Handled(true);
		}
	});
	RowDefinition bodyRow, footerRow;
	bodyRow.Height(GridLength{ 1, GridUnitType::Star });
	footerRow.Height(GridLength{ 1, GridUnitType::Auto });
	root.RowDefinitions().Append(bodyRow);
	root.RowDefinitions().Append(footerRow);

	ScrollViewer scroll;
	scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
	scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
	StackPanel body;
	body.Spacing(24);
	body.Margin(Thickness{ 24, 20, 24, 20 });
	scroll.Content(body);
	root.Children().Append(scroll);

	auto drive = Section(body, LabelText(WindowText(GetDlgItem(dialog, IDS_DRIVE_PROPERTIES_TXT))));
	AddCombo(drive, IDC_DEVICE, IDS_DEVICE_TXT);
	AddButton(drive, IDC_SAVE, IDC_SAVE_TOOLBAR, Localized(MSG_304));
	AddCombo(drive, IDC_BOOT_SELECTION, IDS_BOOT_SELECTION_TXT);
	StackPanel imageButtons;
	imageButtons.Orientation(Orientation::Horizontal);
	imageButtons.Spacing(8);
	drive.Children().Append(imageButtons);
	AddButton(imageButtons, IDC_SELECT);
	Button modes;
	modes.Content(box_value(L"\uE70D"));
	modes.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
	SetName(modes, LabelText(WindowText(GetDlgItem(dialog, IDS_BOOT_SELECTION_TXT))), IDM_SELECT);
	// The existing split-button menu supplies localized Select/Download labels
	// and applies the engine's availability rules.
	modes.Click([](auto const&, auto const&) { QueueCommand(IDM_SELECT, BCN_DROPDOWN); });
	bindings.push_back({ IDM_SELECT, GetDlgItem(dialog, IDC_SELECT), modes, modes, 0, {} });
	imageButtons.Children().Append(modes);
	AddButton(drive, IDC_HASH, IDC_HASH_TOOLBAR, Localized(MSG_314));
	AddCombo(drive, IDC_IMAGE_OPTION, IDS_IMAGE_OPTION_TXT);
	AddEdit(drive, IDC_PERSISTENCE_SIZE, IDS_IMAGE_OPTION_TXT);
	AddCombo(drive, IDC_PERSISTENCE_UNITS, 0, LabelText(WindowText(GetDlgItem(dialog, IDS_IMAGE_OPTION_TXT))));
	Slider persistence;
	persistence.StepFrequency(1);
	Bind(drive, IDC_PERSISTENCE_SLIDER, persistence);
	SetName(persistence, LabelText(WindowText(GetDlgItem(dialog, IDS_IMAGE_OPTION_TXT))), IDC_PERSISTENCE_SLIDER);
	persistence.ValueChanged([](auto const& sender, auto const&) {
		if (!view || view->syncing) return;
		auto* binding = view->Find(IDC_PERSISTENCE_SLIDER);
		if (!view->Enabled(*binding)) return;
		int position = static_cast<int>(sender.template as<Slider>().Value());
		SendMessageW(binding->native, TBM_SETPOS, TRUE, position);
		SendMessageW(view->dialog, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, position), reinterpret_cast<LPARAM>(binding->native));
		if (view) view->Sync();
	});
	AddCombo(drive, IDC_PARTITION_TYPE, IDS_PARTITION_TYPE_TXT);
	AddCombo(drive, IDC_TARGET_SYSTEM, IDS_TARGET_SYSTEM_TXT);
	AddButton(drive, IDC_ADVANCED_DRIVE_PROPERTIES, IDC_ADVANCED_DEVICE_TOOLBAR);
	AddCheck(drive, IDC_LIST_USB_HDD);
	AddCheck(drive, IDC_OLD_BIOS_FIXES);
	AddCheck(drive, IDC_UEFI_MEDIA_VALIDATION);

	auto format = Section(body, LabelText(WindowText(GetDlgItem(dialog, IDS_FORMAT_OPTIONS_TXT))));
	AddEdit(format, IDC_LABEL, IDS_LABEL_TXT);
	AddCombo(format, IDC_FILE_SYSTEM, IDS_FILE_SYSTEM_TXT);
	AddCombo(format, IDC_CLUSTER_SIZE, IDS_CLUSTER_SIZE_TXT);
	AddButton(format, IDC_ADVANCED_FORMAT_OPTIONS, IDC_ADVANCED_FORMAT_TOOLBAR);
	AddCheck(format, IDC_QUICK_FORMAT);
	AddCheck(format, IDC_EXTENDED_LABEL);
	AddCheck(format, IDC_BAD_BLOCKS);
	AddCombo(format, IDC_NB_PASSES, 0, Localized(MSG_316));

	StackPanel footer;
	footer.Spacing(10);
	footer.Margin(Thickness{ 24, 12, 24, 18 });
	Grid::SetRow(footer, 1);
	root.Children().Append(footer);
	info = TextBlock();
	info.TextWrapping(TextWrapping::Wrap);
	Automation::AutomationProperties::SetLiveSetting(info, Automation::Peers::AutomationLiveSetting::Polite);
	footer.Children().Append(info);
	progress = ProgressBar();
	SetName(progress, LabelText(WindowText(GetDlgItem(dialog, IDS_STATUS_TXT))), IDC_PROGRESS);
	footer.Children().Append(progress);
	status = TextBlock();
	status.TextWrapping(TextWrapping::Wrap);
	footer.Children().Append(status);
	elapsed = TextBlock();
	elapsed.HorizontalAlignment(HorizontalAlignment::Right);
	footer.Children().Append(elapsed);
	StackPanel actions;
	actions.Orientation(Orientation::Horizontal);
	actions.HorizontalAlignment(HorizontalAlignment::Right);
	actions.Spacing(12);
	footer.Children().Append(actions);
	auto start = AddButton(actions, IDC_START);
	start.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
	AddButton(actions, IDCANCEL);

	StackPanel tools;
	tools.Orientation(Orientation::Horizontal);
	tools.Spacing(8);
	footer.Children().Append(tools);
	const int ids[] = { IDC_LANG, IDC_ABOUT, IDC_SETTINGS, IDC_LOG };
	const int labels[] = { MSG_138, MSG_302, MSG_301, MSG_303 };
	const wchar_t* glyphs[] = { L"\uE774", L"\uE946", L"\uE713", L"\uE9D9" };
	for (int i = 0; i < 4; ++i) {
		auto button = AddButton(tools, ids[i], IDC_MULTI_TOOLBAR, Localized(labels[i]));
		FontIcon icon;
		icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
		icon.Glyph(glyphs[i]);
		button.Content(icon);
		ToolTipService::SetToolTip(button, box_value(Localized(labels[i])));
	}

	island = DesktopWindowXamlSource();
	island.Initialize(GetWindowIdFromWindow(dialog));
	island.Content(root);
	island.TakeFocusRequested([](auto const&, DesktopWindowXamlSourceTakeFocusRequestedEventArgs const& args) {
		if (view) {
			auto reason = args.Request().Reason() == XamlSourceFocusNavigationReason::Last ?
				XamlSourceFocusNavigationReason::Last : XamlSourceFocusNavigationReason::First;
			view->island.NavigateFocus(XamlSourceFocusNavigationRequest(reason));
		}
	});
	Sync();
}

void MainView::Sync()
{
	if (syncing) return;
	syncing = true;
	struct Reset { bool& value; ~Reset() { value = false; } } reset{ syncing };
	for (auto& binding : bindings) {
		HWND native = binding.native;
		if (binding.label && binding.caption) {
			auto name = LabelText(WindowText(GetDlgItem(dialog, binding.label)));
			binding.caption.Text(name);
			SetName(binding.view, name, binding.id);
		}
		bool visible = (GetWindowLongPtrW(native, GWL_STYLE) & WS_VISIBLE) != 0;
		if (binding.toolbarCommand && SendMessageW(native, TB_COMMANDTOINDEX, binding.toolbarCommand, 0) < 0)
			visible = false;
		if (binding.id == IDM_SELECT)
			visible = (GetWindowLongPtrW(native, GWL_STYLE) & BS_TYPEMASK) == BS_SPLITBUTTON;
		binding.container.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
		binding.view.IsEnabled(Enabled(binding));
		if (auto combo = binding.view.try_as<ComboBox>()) {
			std::vector<std::wstring> items;
			std::vector<LPARAM> itemValues;
			int count = static_cast<int>(SendMessageW(native, CB_GETCOUNT, 0, 0));
			for (int i = 0; i < count; ++i) {
				int length = static_cast<int>(SendMessageW(native, CB_GETLBTEXTLEN, i, 0));
				if (length < 0) continue;
				std::wstring text(static_cast<size_t>(length) + 1, L'\0');
				SendMessageW(native, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(text.data()));
				text.resize(length);
				items.push_back(std::move(text));
				itemValues.push_back(SendMessageW(native, CB_GETITEMDATA, i, 0));
			}
			if (items != binding.items) {
				combo.Items().Clear();
				for (auto const& item : items) combo.Items().Append(box_value(hstring(item)));
				binding.items = std::move(items);
			}
			binding.itemValues = std::move(itemValues);
			int selected = static_cast<int>(SendMessageW(native, CB_GETCURSEL, 0, 0));
			binding.selected = selected;
			if (combo.SelectedIndex() != selected) combo.SelectedIndex(selected);
		} else if (auto edit = binding.view.try_as<TextBox>()) {
			auto text = WindowText(native);
			if (edit.Text() != text) edit.Text(text);
			edit.MaxLength(static_cast<int>(SendMessageW(native, EM_GETLIMITTEXT, 0, 0)));
		} else if (auto checkbox = binding.view.try_as<CheckBox>()) {
			checkbox.IsChecked(SendMessageW(native, BM_GETCHECK, 0, 0) == BST_CHECKED);
		} else if (auto slider = binding.view.try_as<Slider>()) {
			slider.Maximum(static_cast<double>(SendMessageW(native, TBM_GETRANGEMAX, 0, 0)));
			slider.Minimum(static_cast<double>(SendMessageW(native, TBM_GETRANGEMIN, 0, 0)));
			slider.Value(static_cast<double>(SendMessageW(native, TBM_GETPOS, 0, 0)));
		} else if (auto button = binding.view.try_as<Button>()) {
			if (binding.id == IDC_START || binding.id == IDCANCEL || binding.id == IDC_SELECT)
				SetButtonText(button, LabelText(WindowText(native)));
			else if (binding.id == IDC_ADVANCED_DRIVE_PROPERTIES || binding.id == IDC_ADVANCED_FORMAT_OPTIONS) {
				LRESULT length = SendMessageW(native, TB_GETBUTTONTEXTW, binding.id, 0);
				if (length >= 0) {
					std::wstring text(static_cast<size_t>(length) + 1, L'\0');
					SendMessageW(native, TB_GETBUTTONTEXTW, binding.id, reinterpret_cast<LPARAM>(text.data()));
					SetButtonText(button, LabelText(hstring(text.c_str())));
				}
			}
		}
	}
	HWND nativeProgress = GetDlgItem(dialog, IDC_PROGRESS);
	PBRANGE range{};
	SendMessageW(nativeProgress, PBM_GETRANGE, FALSE, reinterpret_cast<LPARAM>(&range));
	progress.Maximum(std::max(range.iLow + 1, range.iHigh));
	progress.Minimum(range.iLow);
	progress.Value(static_cast<double>(SendMessageW(nativeProgress, PBM_GETPOS, 0, 0)));
	progress.IsIndeterminate(progressMarquee);
	progress.ShowError(progressState == PBST_ERROR);
	progress.ShowPaused(progressState == PBST_PAUSED);
	info.Text(WindowText(nativeProgress));
	status.Text(szStatusMessage ? to_hstring(szStatusMessage) : hstring{});
	HWND nativeStatus = GetDlgItem(dialog, IDC_STATUS);
	LRESULT length = SendMessageW(nativeStatus, SB_GETTEXTLENGTHW, 1, 0);
	if (HIWORD(length) & SBT_OWNERDRAW) {
		const char* timer = reinterpret_cast<const char*>(SendMessageA(nativeStatus, SB_GETTEXTA, 1, 0));
		if (timer) elapsed.Text(to_hstring(timer));
	}
}

void MainView::Resize()
{
	if (!island) return;
	RECT client{};
	GetClientRect(dialog, &client);
	island.SiteBridge().MoveAndResize(Windows::Graphics::RectInt32{ 0, 0, client.right, client.bottom });
}

void MainView::Focus(int id)
{
	if (redirectingFocus || !island) return;
	redirectingFocus = true;
	struct Reset { bool& value; ~Reset() { value = false; } } reset{ redirectingFocus };
	auto* binding = Find(id);
	if (!binding || !binding->view.Focus(FocusState::Programmatic))
		island.NavigateFocus(XamlSourceFocusNavigationRequest(XamlSourceFocusNavigationReason::First));
}

LRESULT CALLBACK NativeFocusProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
	if (message == WM_SETFOCUS && view) {
		try {
			view->Focus(GetDlgCtrlID(window));
		} catch (hresult_error const& error) {
			OutputDebugStringW(error.message().c_str());
		}
		return 0;
	}
	if (message == WM_NCDESTROY) RemoveWindowSubclass(window, NativeFocusProc, subclassId);
	return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK HostProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
	try {
		if (view && view->dialog == window) {
			if (message == WM_SHOWWINDOW && wParam && !view->initialFocusSet)
				PostMessageW(window, initialFocusMessage, 0, 0);
			if (message == initialFocusMessage) {
				if (!view->initialFocusSet && IsWindowVisible(window)) {
					view->Focus(IsWindowEnabled(GetDlgItem(window, IDC_START)) ? IDC_START : IDC_SELECT);
					view->initialFocusSet = true;
				}
				return 0;
			}
			if (message == WM_TIMER && wParam == syncTimer) { view->Sync(); return 0; }
			if (message == WM_SIZE) view->Resize();
			if (message == WM_GETMINMAXINFO) {
				auto* size = reinterpret_cast<MINMAXINFO*>(lParam);
				size->ptMinTrackSize = POINT{ MulDiv(480, GetDpiForWindow(window), 96), MulDiv(480, GetDpiForWindow(window), 96) };
				return 0;
			}
			if (message == WM_SETFOCUS) { view->Focus(); return 0; }
			if (message == commandMessage) {
				int id = LOWORD(wParam);
				auto* binding = view->Find(id);
				if (!binding || !view->Enabled(*binding)) return 0;
				if ((id == IDC_START || id == IDC_SAVE || id == IDC_HASH) && !view->SelectionsCurrent()) {
					view->Sync();
					return 0;
				}
				HWND native = binding->native;
				if (id == IDM_SELECT) {
					NMBCDROPDOWN notification{};
					notification.hdr.hwndFrom = native;
					notification.hdr.idFrom = IDC_SELECT;
					notification.hdr.code = BCN_DROPDOWN;
					GetClientRect(native, &notification.rcButton);
					SendMessageW(window, WM_NOTIFY, IDC_SELECT, reinterpret_cast<LPARAM>(&notification));
				} else {
					if (binding->view.try_as<CheckBox>())
						SendMessageW(native, BM_SETCHECK, SendMessageW(native, BM_GETCHECK, 0, 0) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
					SendMessageW(window, WM_COMMAND, wParam, reinterpret_cast<LPARAM>(native));
				}
				if (view) view->Sync();
				return 0;
			}
		}
	} catch (hresult_error const& error) {
		OutputDebugStringW(error.message().c_str());
	}
	if (message == WM_NCDESTROY) RemoveWindowSubclass(window, HostProc, subclassId);
	return DefSubclassProc(window, message, wParam, lParam);
}

void InitializeRuntime()
{
	if (application) return;
	std::wstring modulePath(32768, L'\0');
	DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
	check_bool(length && length < modulePath.size());
	modulePath.resize(length);
	modulePath.resize(modulePath.find_last_of(L"\\/") + 1);
	modulePath += L"Microsoft.WindowsAppRuntime.Bootstrap.dll";
	bootstrapModule = LoadLibraryExW(modulePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	check_bool(bootstrapModule != nullptr);
	auto initialize = reinterpret_cast<decltype(&MddBootstrapInitialize2)>(GetProcAddress(bootstrapModule, "MddBootstrapInitialize2"));
	bootstrapShutdown = reinterpret_cast<decltype(&MddBootstrapShutdown)>(GetProcAddress(bootstrapModule, "MddBootstrapShutdown"));
	check_bool(initialize && bootstrapShutdown);
	PACKAGE_VERSION minimum{};
	minimum.Version = WINDOWSAPPSDK_RUNTIME_VERSION_UINT64;
	check_hresult(initialize(WINDOWSAPPSDK_RELEASE_MAJORMINOR, WINDOWSAPPSDK_RELEASE_VERSION_TAG_W, minimum, MddBootstrapInitializeOptions_None));
	bootstrapReady = true;
	dispatcher = DispatcherQueueController::CreateOnCurrentThread();
	application = make<XamlApp>();
	xamlManager = WindowsXamlManager::InitializeForCurrentThread();
	application.Resources().MergedDictionaries().Append(XamlControlsResources());
	auto windowing = GetModuleHandleW(L"Microsoft.UI.Windowing.Core.dll");
	contentPreTranslate = reinterpret_cast<PreTranslate>(GetProcAddress(windowing, "ContentPreTranslateMessage"));
	check_bool(contentPreTranslate != nullptr);
}
} // namespace

extern "C" BOOL WinUIInitialize(HWND dialog)
{
	try {
		initializationError.clear();
		InitializeRuntime();
		view = std::make_unique<MainView>();
		view->dialog = dialog;
		// Capture only the engine controls, before WinUI creates its own HWNDs.
		for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
			view->nativeChildren.push_back(child);
		view->Build();
		check_bool(SetWindowSubclass(dialog, HostProc, subclassId, 0));
		for (HWND child : view->nativeChildren) {
			// An empty region conceals the compatibility controls without changing
			// WS_VISIBLE, which the engine uses for advanced/conditional options.
			HRGN region = CreateRectRgn(0, 0, 0, 0);
			if (!SetWindowRgn(child, region, FALSE)) DeleteObject(region);
			SetWindowSubclass(child, NativeFocusProc, subclassId, 0);
		}
		LONG_PTR style = GetWindowLongPtrW(dialog, GWL_STYLE);
		SetWindowLongPtrW(dialog, GWL_STYLE, (style & ~DS_MODALFRAME) | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN);
		RECT work{};
		MONITORINFO monitor{ sizeof(MONITORINFO) };
		GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor);
		work = monitor.rcWork;
		int width = std::min(MulDiv(640, GetDpiForWindow(dialog), 96), static_cast<int>(work.right - work.left));
		int height = std::min(MulDiv(880, GetDpiForWindow(dialog), 96), static_cast<int>(work.bottom - work.top));
		SetWindowPos(dialog, nullptr, work.left + (work.right - work.left - width) / 2,
			work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER | SWP_FRAMECHANGED);
		view->Resize();
		check_bool(SetTimer(dialog, syncTimer, 100, nullptr) != 0);
		PostMessageW(dialog, initialFocusMessage, 0, 0);
		return TRUE;
	} catch (...) {
		hresult_error error(to_hresult());
		initializationError = L"Rufus could not initialize WinUI 3. Install the Windows App Runtime 2.5 "
			L"for this architecture and keep the bootstrap DLL beside rufus.exe.\n\n";
		initializationError += error.message().c_str();
		WinUIDestroy();
		return FALSE;
	}
}

extern "C" LPCWSTR WinUIErrorMessage(void) { return initializationError.c_str(); }

extern "C" void WinUIDestroy(void)
{
	if (!view) return;
	auto previous = std::move(view);
	KillTimer(previous->dialog, syncTimer);
	RemoveWindowSubclass(previous->dialog, HostProc, subclassId);
	for (HWND child : previous->nativeChildren) {
		if (IsWindow(child)) {
			RemoveWindowSubclass(child, NativeFocusProc, subclassId);
			SetWindowRgn(child, nullptr, FALSE);
		}
	}
	try {
		if (previous->island) previous->island.Close();
	} catch (hresult_error const& error) {
		OutputDebugStringW(error.message().c_str());
	}
}

extern "C" void WinUIShutdown(void)
{
	WinUIDestroy();
	if (xamlManager) { xamlManager.Close(); xamlManager = nullptr; }
	application = nullptr;
	if (dispatcher) {
		dispatcher.ShutdownQueue();
		dispatcher = nullptr;
	}
	contentPreTranslate = nullptr;
	clear_factory_cache();
	if (bootstrapReady) bootstrapShutdown();
	bootstrapReady = false;
	bootstrapShutdown = nullptr;
	if (bootstrapModule) FreeLibrary(bootstrapModule);
	bootstrapModule = nullptr;
}

extern "C" BOOL WinUIIsActive(void) { return view != nullptr; }
extern "C" void WinUISetProgressState(int state) { progressState = state; }
extern "C" void WinUISetProgressMarquee(BOOL enabled) { progressMarquee = enabled != FALSE; }

extern "C" BOOL WinUIPreTranslateMessage(MSG* message)
{
	return contentPreTranslate && contentPreTranslate(message);
}
