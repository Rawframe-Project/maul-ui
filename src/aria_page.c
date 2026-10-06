// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ARIA adapter's calls into the page (record mui-0008), through
// Emscripten's EM_JS. Each adapter's elements are kept in Module.muiAria
// under a handle: the host element, the adapter's container in it, the
// enabling button, and the elements by slot. The container is invisible
// as Flutter makes its semantics (filter: opacity(0%), transparent
// text): visibility or display would hide it from screen readers too.

#include "aria.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>

// The enabling button was pressed.
EMSCRIPTEN_KEEPALIVE void muiAriaEnableFromPage(muiAriaAdapter* adapter)
{
    muiAriaAdapter_Enable(adapter);
}

// clang-format off
EM_JS(int, OpenPage, (const char* host, int deferred, const char* label, void* adapter), {
    const element = typeof document === "undefined" ? null
                                                    : document.querySelector(UTF8ToString(host));
    if (!element) {
        return -1;
    }
    const pages = Module.muiAria || (Module.muiAria = []);
    const root = document.createElement("div");
    root.style.cssText = "position:absolute;left:0;top:0;width:0;height:0;overflow:visible;" +
                         "margin:0;padding:0;border:0;filter:opacity(0%);" +
                         "color:rgba(0,0,0,0);pointer-events:none";
    element.appendChild(root);
    const page = {root, elements: [], button: null};
    if (deferred) {
        // Visually hidden, read and pressed by screen readers.
        const button = document.createElement("button");
        button.textContent = UTF8ToString(label);
        button.style.cssText = "position:absolute;left:0;top:0;width:1px;height:1px;" +
                               "overflow:hidden;clip-path:inset(50%);white-space:nowrap;" +
                               "margin:0;padding:0;border:0";
        button.addEventListener("click", () => _muiAriaEnableFromPage(adapter));
        element.appendChild(button);
        page.button = button;
    }
    let handle = pages.indexOf(null);
    if (handle < 0) {
        handle = pages.length;
    }
    pages[handle] = page;
    return handle;
});

EM_JS(void, ClosePage, (int handle), {
    const page = Module.muiAria[handle];
    page.root.remove();
    if (page.button) {
        page.button.remove();
    }
    Module.muiAria[handle] = null;
});

EM_JS(void, DropButton, (int handle), {
    const page = Module.muiAria[handle];
    if (page.button) {
        page.button.remove();
        page.button = null;
    }
});

EM_JS(void, Make, (int handle, uint32_t slot, int range, const char* id), {
    const element = document.createElement(range ? "input" : "div");
    if (range) {
        element.type = "range";
    }
    element.id = UTF8ToString(id);
    element.style.cssText = "position:absolute;margin:0;padding:0;border:0;" +
                            "box-sizing:border-box;overflow:visible";
    Module.muiAria[handle].elements[slot] = element;
});

EM_JS(void, Remove, (int handle, uint32_t slot), {
    const elements = Module.muiAria[handle].elements;
    elements[slot].remove();
    elements[slot] = undefined;
});

// Puts an element at an index among its parent's: where it is, or before
// the one there. Placed in rising index after the leaving ones are
// gone, each parent's elements end in order.
EM_JS(void, Place, (int handle, uint32_t slot, int parent, uint32_t index), {
    const page = Module.muiAria[handle];
    const element = page.elements[slot];
    const into = parent < 0 ? page.root : page.elements[parent];
    const there = into.children[index] || null;
    if (there !== element) {
        into.insertBefore(element, there);
    }
});

EM_JS(void, Box, (int handle, uint32_t slot, double x, double y, double width, double height), {
    const style = Module.muiAria[handle].elements[slot].style;
    style.left = x + "px";
    style.top = y + "px";
    style.width = width + "px";
    style.height = height + "px";
});

// An attribute, or none for a NULL value. A range's value is its
// property, as the attribute only gives its first value.
EM_JS(void, Attribute, (int handle, uint32_t slot, const char* name, const char* value), {
    const element = Module.muiAria[handle].elements[slot];
    const key = UTF8ToString(name);
    if (key === "value" && element.tagName === "INPUT") {
        element.value = value ? UTF8ToString(value) : "";
    } else if (value) {
        element.setAttribute(key, UTF8ToString(value));
    } else {
        element.removeAttribute(key);
    }
});

EM_JS(void, Text, (int handle, uint32_t slot, const char* text), {
    Module.muiAria[handle].elements[slot].textContent = text ? UTF8ToString(text) : "";
});
// clang-format on

int muiAriaPageOpen(const char* host, bool deferred, const char* label, muiAriaAdapter* adapter)
{
    return OpenPage(host, deferred ? 1 : 0, label, adapter);
}

void muiAriaPageClose(int page)
{
    ClosePage(page);
}

void muiAriaPageDropButton(int page)
{
    DropButton(page);
}

void muiAriaPageMake(int page, uint32_t slot, bool range, const char* id)
{
    Make(page, slot, range ? 1 : 0, id);
}

void muiAriaPageRemove(int page, uint32_t slot)
{
    Remove(page, slot);
}

void muiAriaPagePlace(int page, uint32_t slot, uint32_t parent, uint32_t index)
{
    Place(page, slot, parent == ARIA_NO_SLOT ? -1 : (int)parent, index);
}

void muiAriaPageBox(int page, uint32_t slot, float x, float y, float width, float height)
{
    Box(page, slot, (double)x, (double)y, (double)width, (double)height);
}

void muiAriaPageAttribute(int page, uint32_t slot, const char* name, const char* value)
{
    Attribute(page, slot, name, value);
}

void muiAriaPageText(int page, uint32_t slot, const char* text)
{
    Text(page, slot, text);
}
