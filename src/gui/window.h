#pragma once

// The pieces every window of the interface uses: the message font at the window's DPI, the text and
// state of controls, and Page, a child window whose controls a FormLayout places and whose control
// notifications go to handlers registered with the controls.

#include "gui/dialogs.h"
#include "gui/form_layout.h"

#include <windows.h>

#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pdftweak::gui {

// The Segoe UI message font of the system at the window's DPI.
HFONT messageFont(HWND window);

// Pixels at 96 DPI scaled to the window's DPI.
int scaled(HWND window, int pixels);

std::wstring windowText(HWND hwnd);
void setWindowText(HWND hwnd, const std::wstring& text);
bool isChecked(HWND button);
void setChecked(HWND button, bool checked);
bool isEnabled(HWND hwnd);
void enable(HWND hwnd, bool enabled);

// Combo boxes with a drop-down list.
int selectedIndex(HWND combo);
void selectIndex(HWND combo, int index);
int itemCount(HWND combo);
std::wstring itemText(HWND combo, int index);
// Replaces the items and selects the first, as a new DefaultComboBoxModel does.
void setItems(HWND combo, const std::vector<std::wstring>& items);

// A child window of the given class with the message font.
HWND createChild(HWND parent, const wchar_t* className, const std::wstring& text, DWORD style,
                 DWORD exStyle = 0, int id = 0);

// Text of a tooltip shown over a control.
void setTooltip(HWND control, const std::wstring& text);

class Page {
public:
    Page();
    virtual ~Page();
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    // Creates the page as a hidden child window of parent, then its controls.
    HWND create(HWND parent);
    HWND handle() const { return hwnd_; }

    // The size the layout asks for, with the margin of the page.
    SIZE preferredSize() const;

    // Places the controls again, after a change of the window size or of preferred sizes.
    void relayout();

    // Simulates what a user's selection of an item does: selects it and runs the handler.
    void chooseItem(HWND combo, int index);

    // Runs the work after the messages already queued, as SwingUtilities.invokeLater.
    void later(std::function<void()> work);

protected:
    // Creates the controls and sets the layout.
    virtual void build() = 0;

    // Messages the page does not handle itself. The default passes them to DefWindowProc.
    virtual LRESULT message(UINT msg, WPARAM wParam, LPARAM lParam);

    void setLayout(Component root) { root_ = std::move(root); }
    // A layout that lives as long as the page.
    FormLayout& newLayout(std::wstring_view columns, std::wstring_view rows);

    HWND button(const std::wstring& text, std::function<void()> onClick);
    HWND checkBox(const std::wstring& text, std::function<void()> onClick = {});
    HWND label(const std::wstring& text);
    // A text field. With columns, its preferred width is that many widths of "m", as JTextField's.
    HWND edit(const std::wstring& text, int columns = 0, DWORD style = 0);
    // A combo box whose handler runs when the user picks an item, also the selected one again, as an
    // ActionListener of a JComboBox does.
    HWND comboBox(const std::vector<std::wstring>& items, std::function<void()> onAction = {});
    HWND separator();
    HWND groupBox(const std::wstring& text);

    // Sends the WM_COMMAND notifications of a control to a handler.
    void onCommand(HWND control, std::function<void(int code)> handler);
    // Sends the WM_NOTIFY notifications of a control to a handler.
    void onNotify(HWND control, std::function<LRESULT(NMHDR*)> handler);
    // Drops the handlers of a control that is destroyed.
    void forget(HWND control);

    // The margin around the layout, in pixels at 96 DPI, 4 unless set.
    void setMargin(int pixels) { margin_ = pixels; }

    // A page created as a child of this one, as a component of this page's layout.
    Component childPage(Page& child);

    // A component made of a group box around a nested layout, as a panel with a TitledBorder.
    Component titled(HWND groupBox, Component inner);

    // Two components side by side with a divider the user can drag. The left side starts at its
    // preferred width and each side takes half of any extra width, as JSplitPane with resize weight
    // 0.5.
    Component splitPane(Component left, Component right);

private:
    struct Split;
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);
    Split* splitAt(POINT point);

    HWND hwnd_ = nullptr;
    int margin_ = 4;
    Component root_;
    std::vector<std::unique_ptr<FormLayout>> layouts_;
    std::vector<std::unique_ptr<Split>> splits_;
    Split* dragging_ = nullptr;
    std::map<HWND, std::function<void(int)>> commands_;
    std::map<HWND, std::function<LRESULT(NMHDR*)>> notifies_;
    std::map<HWND, int> droppedIndex_;
};

// A top-level window that holds one page, sized to the page's preferred size, as a packed JFrame or
// JDialog. The keyboard works as in a dialog: Tab moves between controls, Enter sends IDOK, and
// Escape IDCANCEL, to onDialogCommand.
class Frame {
public:
    // With an owner, the frame is centred on it, otherwise on the screen.
    Frame(HWND owner, const std::wstring& title, Page& content, bool resizable);
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    HWND handle() const { return hwnd_; }

    // IDOK and IDCANCEL from the keyboard.
    std::function<void(int id)> onDialogCommand;
    // The close box. Without a handler, the frame closes.
    std::function<void()> onClose;
    // Keys the frame handles before the dialog keys, such as Ctrl+Tab. Returns whether it used the
    // message.
    std::function<bool(const MSG&)> onKey;
    // The cursor over a window of the frame. Returns whether it set one.
    std::function<bool(HWND over)> onSetCursor;

    // Shows the frame, then runs a message loop until it is destroyed, with the owner disabled.
    void runModal();

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    Page& content_;
};

// Gives a message to the frame it belongs to, for the keyboard of dialogs. Returns whether a frame
// used it, so that the message loop skips TranslateMessage and DispatchMessage.
bool translateFrameMessage(MSG& msg);

// The body of a window procedure, which lets no exception out: one cannot pass back through the
// system's call into the procedure. A failure that no handler caught shows the box for an unexpected
// error, as Java's uncaught-exception handler did, and the procedure answers as fallback does, so
// that the window goes on.
template <typename Body, typename Fallback>
LRESULT guardedProc(HWND hwnd, Body&& body, Fallback&& fallback) {
    try {
        return body();
    } catch (const std::exception& error) {
        showUnexpectedError(hwnd, error.what());
    } catch (...) {
        showUnexpectedError(hwnd, nullptr);
    }
    return fallback();
}

} // namespace pdftweak::gui
