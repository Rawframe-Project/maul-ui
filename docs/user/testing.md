# Testing a program's interface

Nothing in the core needs a window, a GPU or a thread, so a program's interface is tested by building its screen in a context and reading what layout and drawing give: rectangles (`muiNode_GetRect`), states, the focus, the draw list's commands, and the accessibility tree. The context counts its work (`muiContext_GetWorkCounts`), so a test can hold a frame to what it should cost:

```c
// A test a program can run with no window and no GPU: its screen laid
// out and drawn twice with nothing changed, the second frame doing no
// work at all.
static bool StillFrameIsFree(muiContext* context, muiNodeId root)
{
    const muiLayoutInput layout = {800.0f, 600.0f, NULL, NULL, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, NULL, NULL};
    if (muiNode_ComputeLayout(context, root, &layout) != mui_success ||
        muiNode_BuildDrawList(context, root, &draw) != mui_success)
    {
        return false;
    }
    muiWorkCounts before = muiContext_GetWorkCounts(context);
    if (muiNode_ComputeLayout(context, root, &layout) != mui_success ||
        muiNode_BuildDrawList(context, root, &draw) != mui_success)
    {
        return false;
    }
    muiWorkCounts after = muiContext_GetWorkCounts(context);
    return after.styled == before.styled && after.sized == before.sized &&
           after.measured == before.measured && after.painted == before.painted;
}
```

Some ways to test with it:

- **Layout:** lay a screen out at a phone's size and a desktop's, and check the rectangles that matter, such as a sidebar that collapses below a width.
- **Input:** feed `muiNode_RoutePointer`, `muiNode_RouteKey` and `muiNode_RouteNavigation` as a user would, and check the focus, the states and what the event function saw. The library keeps no clock, so a test passes the times itself and runs in no time.
- **Transitions:** lay out at chosen `timeNs` values to see a transition halfway and at its end.
- **Text:** the text service works without a window too; test fonts with exact metrics, such as Ahem, make measurements exact numbers.
- **Accessibility:** enable the root and read the update, or with the consumer tree the shown tree, to check the names and roles a screen reader will meet. On Maul Window's test backend the [window glue](maul-window.md#accessibility) gives that tree for a whole window.
- **Drawing:** the draw list is the same bytes on every machine, so a test may pin a scene's commands and catch a change in what is drawn.

A test that compares draw lists or rectangles should compare what the program promises, not every value: a small change in the library's rounding or a new command kind should not break a program's tests unless it changes what the user sees.
