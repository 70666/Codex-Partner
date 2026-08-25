#include "accessibility.h"
#include "native_ui.h"

#include <OleAcc.h>
#include <OleAuto.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <new>
#include <optional>

namespace codex_partner::accessibility {
namespace {

DWORD MsaaRole(Role role) noexcept {
    switch (role) {
    case Role::StaticText: return ROLE_SYSTEM_STATICTEXT;
    case Role::PushButton: return ROLE_SYSTEM_PUSHBUTTON;
    case Role::PageTab: return ROLE_SYSTEM_PAGETAB;
    case Role::CheckButton: return ROLE_SYSTEM_CHECKBUTTON;
    case Role::ComboBox: return ROLE_SYSTEM_COMBOBOX;
    case Role::Link: return ROLE_SYSTEM_LINK;
    }
    return ROLE_SYSTEM_CLIENT;
}

DWORD MsaaState(unsigned state) noexcept {
    DWORD result = 0;
    if (state & StateFocusable) result |= STATE_SYSTEM_FOCUSABLE;
    if (state & StateFocused) result |= STATE_SYSTEM_FOCUSED;
    if (state & StatePressed) result |= STATE_SYSTEM_PRESSED;
    if (state & StateChecked) result |= STATE_SYSTEM_CHECKED;
    if (state & StateSelected) result |= STATE_SYSTEM_SELECTED;
    if (state & StateUnavailable) result |= STATE_SYSTEM_UNAVAILABLE;
    if (state & StateReadOnly) result |= STATE_SYSTEM_READONLY;
    return result;
}

HRESULT CopyBstr(const std::wstring& value, BSTR* output) noexcept {
    if (!output) return E_INVALIDARG;
    *output = nullptr;
    if (value.empty()) return S_FALSE;
    *output = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
    return *output ? S_OK : E_OUTOFMEMORY;
}

bool ChildId(const VARIANT& child, long& id) noexcept {
    if (child.vt != VT_I4) return false;
    id = child.lVal;
    return true;
}

class AccessibleProvider final : public IAccessible {
public:
    explicit AccessibleProvider(HWND window) noexcept : window_(window) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IAccessible) {
            *object = static_cast<IAccessible*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return references_.fetch_add(1, std::memory_order_relaxed) + 1; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
        if (!count) return E_POINTER;
        *count = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override {
        return DISP_E_MEMBERNOTFOUND;
    }

    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) override {
        if (!parent) return E_POINTER;
        *parent = nullptr;
        IAccessible* accessible = nullptr;
        const HRESULT result = AccessibleObjectFromWindow(window_, OBJID_WINDOW, IID_IAccessible,
            reinterpret_cast<void**>(&accessible));
        if (FAILED(result) || !accessible) return S_FALSE;
        *parent = accessible;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) override {
        if (!count) return E_POINTER;
        const auto elements = Elements();
        *count = static_cast<long>(std::min<std::size_t>(elements.size(), static_cast<std::size_t>(std::numeric_limits<long>::max())));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT child, IDispatch** dispatch) override {
        if (!dispatch) return E_POINTER;
        *dispatch = nullptr;
        long id = 0;
        if (!ChildId(child, id) || id == CHILDID_SELF || !Find(id)) return E_INVALIDARG;
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR* name) override {
        long id = 0;
        if (!ChildId(child, id)) return E_INVALIDARG;
        if (id == CHILDID_SELF) {
            wchar_t title[256]{};
            GetWindowTextW(window_, title, static_cast<int>(std::size(title)));
            return CopyBstr(title[0] ? std::wstring(title) : L"Codex Partner", name);
        }
        const auto element = Find(id);
        return element ? CopyBstr(element->name, name) : E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR* value) override {
        long id = 0;
        if (!ChildId(child, id)) return E_INVALIDARG;
        if (id == CHILDID_SELF) return S_FALSE;
        const auto element = Find(id);
        return element ? CopyBstr(element->value, value) : E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child, BSTR* description) override {
        long id = 0;
        if (!ChildId(child, id)) return E_INVALIDARG;
        if (id == CHILDID_SELF) return CopyBstr(L"Native Codex usage and settings window", description);
        const auto element = Find(id);
        return element ? CopyBstr(element->description, description) : E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT* role) override {
        if (!role) return E_POINTER;
        VariantInit(role);
        long id = 0;
        if (!ChildId(child, id)) return E_INVALIDARG;
        role->vt = VT_I4;
        if (id == CHILDID_SELF) {
            role->lVal = ROLE_SYSTEM_CLIENT;
            return S_OK;
        }
        const auto element = Find(id);
        if (!element) return E_INVALIDARG;
        role->lVal = static_cast<LONG>(MsaaRole(element->role));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT* state) override {
        if (!state) return E_POINTER;
        VariantInit(state);
        long id = 0;
        if (!ChildId(child, id)) return E_INVALIDARG;
        state->vt = VT_I4;
        if (id == CHILDID_SELF) {
            state->lVal = IsWindowEnabled(window_) ? STATE_SYSTEM_FOCUSABLE : STATE_SYSTEM_UNAVAILABLE;
            return S_OK;
        }
        const auto element = Find(id);
        if (!element) return E_INVALIDARG;
        DWORD msaa_state = MsaaState(element->state);
        if (element->role == Role::ComboBox) msaa_state |= STATE_SYSTEM_HASPOPUP;
        state->lVal = static_cast<LONG>(msaa_state);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT, BSTR* help) override {
        if (!help) return E_POINTER;
        *help = nullptr;
        return S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* help_file, VARIANT, long* topic) override {
        if (help_file) *help_file = nullptr;
        if (topic) *topic = 0;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override {
        long id = 0;
        if (!ChildId(child, id)) return E_INVALIDARG;
        if (id == CHILDID_SELF) return S_FALSE;
        const auto element = Find(id);
        return element ? CopyBstr(element->keyboard_shortcut, shortcut) : E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* focus) override {
        if (!focus) return E_POINTER;
        VariantInit(focus);
        const auto elements = Elements();
        const auto found = std::find_if(elements.begin(), elements.end(), [](const Element& element) {
            return (element.state & StateFocused) != 0;
        });
        if (found == elements.end()) return S_FALSE;
        focus->vt = VT_I4;
        focus->lVal = found->child_id;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* selection) override {
        if (!selection) return E_POINTER;
        VariantInit(selection);
        const auto elements = Elements();
        const auto found = std::find_if(elements.begin(), elements.end(), [](const Element& element) {
            return (element.state & StateSelected) != 0;
        });
        if (found == elements.end()) return S_FALSE;
        selection->vt = VT_I4;
        selection->lVal = found->child_id;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR* action) override {
        long id = 0;
        if (!ChildId(child, id)) return E_INVALIDARG;
        if (id == CHILDID_SELF) return S_FALSE;
        const auto element = Find(id);
        return element ? CopyBstr(element->default_action, action) : E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT child) override {
        long id = 0;
        if (!ChildId(child, id) || id == CHILDID_SELF) return E_INVALIDARG;
        const auto element = Find(id);
        if (!element || element->command == 0) return E_INVALIDARG;
        if (flags & SELFLAG_TAKEFOCUS) PostMessageW(window_, kFocusElementMessage, static_cast<WPARAM>(id), 0);
        if ((flags & SELFLAG_TAKESELECTION) && element->role == Role::PageTab) {
            PostMessageW(window_, kActivateElementMessage, static_cast<WPARAM>(id), 0);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE accLocation(long* left, long* top, long* width, long* height, VARIANT child) override {
        if (!left || !top || !width || !height) return E_POINTER;
        long id = 0;
        if (!ChildId(child, id)) return E_INVALIDARG;
        if (id == CHILDID_SELF) {
            RECT rect{};
            if (!GetWindowRect(window_, &rect)) return E_FAIL;
            *left = rect.left;
            *top = rect.top;
            *width = rect.right - rect.left;
            *height = rect.bottom - rect.top;
            return S_OK;
        }
        const auto element = Find(id);
        if (!element) return E_INVALIDARG;
        POINT origin{};
        if (!ClientToScreen(window_, &origin)) return E_FAIL;
        const UINT dpi = GetDpiForWindow(window_);
        *left = origin.x + MulDiv(static_cast<int>(static_cast<float>(element->bounds.x) * ui::kContentScale), static_cast<int>(dpi), 96);
        *top = origin.y + MulDiv(static_cast<int>(static_cast<float>(element->bounds.y) * ui::kContentScale), static_cast<int>(dpi), 96);
        *width = MulDiv(static_cast<int>(static_cast<float>(element->bounds.width) * ui::kContentScale), static_cast<int>(dpi), 96);
        *height = MulDiv(static_cast<int>(static_cast<float>(element->bounds.height) * ui::kContentScale), static_cast<int>(dpi), 96);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE accNavigate(long direction, VARIANT start, VARIANT* destination) override {
        if (!destination) return E_POINTER;
        VariantInit(destination);
        long id = 0;
        if (!ChildId(start, id)) return E_INVALIDARG;
        const auto elements = Elements();
        if (elements.empty()) return S_FALSE;
        if (id == CHILDID_SELF && (direction == NAVDIR_FIRSTCHILD || direction == NAVDIR_LASTCHILD)) {
            destination->vt = VT_I4;
            destination->lVal = direction == NAVDIR_FIRSTCHILD ? elements.front().child_id : elements.back().child_id;
            return S_OK;
        }
        const auto found = std::find_if(elements.begin(), elements.end(), [id](const Element& element) { return element.child_id == id; });
        if (found == elements.end()) return E_INVALIDARG;
        if (direction == NAVDIR_NEXT && std::next(found) != elements.end()) {
            destination->vt = VT_I4;
            destination->lVal = std::next(found)->child_id;
            return S_OK;
        }
        if (direction == NAVDIR_PREVIOUS && found != elements.begin()) {
            destination->vt = VT_I4;
            destination->lVal = std::prev(found)->child_id;
            return S_OK;
        }
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE accHitTest(long screen_x, long screen_y, VARIANT* child) override {
        if (!child) return E_POINTER;
        VariantInit(child);
        POINT point{screen_x, screen_y};
        if (!ScreenToClient(window_, &point)) return E_FAIL;
        const UINT dpi = GetDpiForWindow(window_);
        point.x = static_cast<LONG>(static_cast<float>(MulDiv(point.x, 96, static_cast<int>(dpi))) / ui::kContentScale);
        point.y = static_cast<LONG>(static_cast<float>(MulDiv(point.y, 96, static_cast<int>(dpi))) / ui::kContentScale);
        const auto elements = Elements();
        const auto found = std::find_if(elements.begin(), elements.end(), [&](const Element& element) {
            return point.x >= element.bounds.x && point.x < element.bounds.x + element.bounds.width &&
                point.y >= element.bounds.y && point.y < element.bounds.y + element.bounds.height;
        });
        child->vt = VT_I4;
        child->lVal = found == elements.end() ? CHILDID_SELF : found->child_id;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        long id = 0;
        if (!ChildId(child, id) || id == CHILDID_SELF) return E_INVALIDARG;
        const auto element = Find(id);
        if (!element || element->command == 0 || element->default_action.empty()) return E_INVALIDARG;
        return PostMessageW(window_, kActivateElementMessage, static_cast<WPARAM>(id), 0) ? S_OK : E_FAIL;
    }

    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
    ~AccessibleProvider() = default;

    std::vector<Element> Elements() const {
        std::vector<Element> elements;
        (void)QueryElements(window_, elements);
        return elements;
    }

    std::optional<Element> Find(long id) const {
        auto elements = Elements();
        const auto found = std::find_if(elements.begin(), elements.end(), [id](const Element& element) { return element.child_id == id; });
        return found == elements.end() ? std::nullopt : std::optional<Element>(*found);
    }

    std::atomic<ULONG> references_{1};
    HWND window_ = nullptr;
};

}  // namespace

bool QueryElements(HWND window, std::vector<Element>& elements) noexcept {
    if (!IsWindow(window)) return false;
    return SendMessageW(window, kQueryElementsMessage, 0, reinterpret_cast<LPARAM>(&elements)) != 0;
}

LRESULT HandleGetObject(HWND window, WPARAM wparam, LPARAM lparam) {
    if (static_cast<LONG>(lparam) != OBJID_CLIENT) return 0;
    auto* provider = new (std::nothrow) AccessibleProvider(window);
    if (!provider) return 0;
    const LRESULT result = LresultFromObject(IID_IAccessible, wparam, provider);
    provider->Release();
    return result;
}

}  // namespace codex_partner::accessibility
