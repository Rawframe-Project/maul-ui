# Scrolling, lists and popups

## Contents

- [Scrolling](#scrolling)
- [Long lists](#long-lists)
- [Popups](#popups)

## Scrolling

A node whose layout scrolls on an axis (`scrollAxes`) is a scroll container: its children may reach past its box, and an offset moves them. `muiNode_SetScroll` sets the offset, and `muiNode_ScrollIntoView` brings a node into its containers' view. Unhandled wheel turns, keys and gamepad directions scroll the container under the pointer or holding the focus, eased as the scroll rule says (`muiSetScrollRule`): a wheel step, a latch time, the fling's deceleration and whether to overscroll. `muiNode_GetScrollThumb` gives a scrollbar's thumb along its track. Painting moves the children through a transform, so scrolling redraws nothing else.

Touch and pens pan a scroll container by dragging it and fling it when let go, with iOS's deceleration. Overscroll is off by default, as on desktop browsers; turned on, a pan past the end stretches by iOS's rubber band and springs back.

## Long lists

A list of many items keeps only those near its viewport as nodes. The library keeps every item's extent, fixed or estimated until measured, and after each layout works out the window of items that should exist; the program makes them:

```c
// A list of 10,000 rows 24 units high that scrolls, of which only the
// rows near its viewport exist as nodes.
static muiResult MakeList(muiContext* context, muiNodeId list)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.scrollAxes = mui_scrollVertical;
    muiVirtualList items = muiDefaultVirtualList();
    items.count = 10000;
    items.extent = 24.0f;
    items.fixed = true;
    muiResult result =
        muiNode_SetLayoutValues(context, list, &layout, MUI_PROPERTY_BIT(mui_propertyScrollAxes));
    return result == mui_success ? muiNode_SetVirtualList(context, list, &items) : result;
}
```

```c
// Makes the rows the list's window asks for: a node bound to each index
// from first up to end. The old rows go and the window's are made anew;
// a program reuses them instead, binding a row that left to an index
// that came and resetting what it showed.
static muiResult Realize(muiContext* context, muiNodeId list)
{
    uint32_t first = 0;
    uint32_t end = 0;
    muiResult result = muiNode_GetVirtualWindow(context, list, &first, &end);
    for (muiNodeId row = muiNode_GetFirstChild(context, list);
         result == mui_success && row.index1 != 0; row = muiNode_GetFirstChild(context, list))
    {
        result = muiDestroyNode(context, row);
    }
    muiNodeDef def = muiDefaultNodeDef();
    for (uint32_t index = first; result == mui_success && index < end; index++)
    {
        muiNodeId row = {0, 0};
        result = muiCreateNode(context, &def, &row);
        if (result == mui_success)
        {
            result = muiNode_InsertChild(context, list, row, (muiNodeId){0, 0});
        }
        if (result == mui_success)
        {
            result = muiNode_SetItem(context, row, index);
        }
    }
    return result;
}
```

```c
// A frame with a list: layout says when the list's window changed, the
// rows are made, and layout places them.
static muiResult ListFrame(muiContext* context, muiNodeId root, muiNodeId list,
                           const muiLayoutInput* input)
{
    muiResult result = muiComputeLayout(context, root, input);
    bool changed = false;
    muiNotification notification;
    while (result == mui_success && muiNextNotification(context, &notification) == mui_success)
    {
        changed = changed || (notification.kind == mui_notificationWindowChanged &&
                              notification.node.index1 == list.index1);
    }
    if (result == mui_success && changed)
    {
        result = Realize(context, list);
        result = result == mui_success ? muiComputeLayout(context, root, input) : result;
    }
    return result;
}
```

A list 300 units high holds a few dozen rows of its 10,000: those in its viewport and the overscan on each side. Scrolled to row 5,000, it holds the rows around that one. Items inserted, removed and moved (`muiNode_InsertVirtualItems`, `muiNode_RemoveVirtualItems`, `muiNode_MoveVirtualItem`) keep the rows bound to the items they show, and the scroll position steady: an item added above the viewport does not push what the user is reading.

Items of unknown size start at the list's estimate and take their measured extent once they exist, and the scrollbar settles as more are measured. A list whose items have one fixed extent never measures them.

## Popups

A popup is a node placed beside an anchor after each layout: below, above or to a side, flipped when it does not fit, and kept inside the root. Its layer says how it paints; an overlay paints above everything and outside its ancestors' clips:

```c
// Opens a menu below a button: absolutely placed, so it takes no room
// in the tree, painted in the overlay layer above everything, and put
// beside its anchor after each layout. A press outside it or Escape
// dismisses it.
static muiResult OpenMenu(muiContext* context, muiNodeId menu, muiNodeId button)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.placement.position = mui_positionAbsolute;
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.layer = mui_layerOverlay;
    muiPopup popup = muiDefaultPopup();
    popup.anchor = button;
    popup.side = mui_popupBelow;
    popup.gap = 4.0f;
    muiResult result =
        muiNode_SetLayoutValues(context, menu, &layout, MUI_PROPERTY_BIT(mui_propertyPosition));
    if (result == mui_success)
    {
        result = muiNode_SetInteractionValues(context, menu, &interaction,
                                              MUI_PROPERTY_BIT(mui_propertyLayer));
    }
    return result == mui_success ? muiNode_SetPopup(context, menu, &popup) : result;
}
```

A menu opened from a button 40 units high sits 4 units below it. A press outside it, Escape, or the focus moved elsewhere dismisses it, which the context reports with `mui_notificationPopupDismissed`. The program closes it, with whatever exit it likes ([Styling](styling.md#transitions-and-exits)). A popup anchored inside another nests under it, and a press inside the inner one keeps both open. A manual popup is not dismissed by the library at all.
