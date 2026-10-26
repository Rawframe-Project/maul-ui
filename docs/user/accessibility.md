# Accessibility

Every node of a root is a node of its accessibility tree, with a role, texts, flags and actions. Most of it comes from what the library already holds: rectangles, scrolling, focus, states, value ranges and virtual lists. Text in the UI names itself through the text component (`muiReadAccessText`, set with `muiSetAccessTextFunction`), including an editing field's selection, its lines and words, and where its characters are. The program says the rest:

```c
// Tells assistive technology what a node is: a button, and its name.
static muiResult Describe(muiContext* context, muiNodeId button, const char* name)
{
    muiResult result = muiNode_SetAccessRole(context, button, mui_roleButton);
    return result == mui_success
               ? muiNode_SetAccessText(context, button, mui_accessLabel, name, strlen(name))
               : result;
}
```

A node without a role is generic, and a screen reader skips it and reads its children in its place, so layout-only nodes cost nothing. Give roles to what a user meets: buttons, checkboxes, text fields, lists and their items, headings, dialogs. A button's name comes from the label inside it, so a labelled button needs no name of its own.

## Updates and requests

Nothing is built until the program enables a root (`muiAccess_Enable`), as when Maul Window reports that an assistive technology asked for one. Then each frame gives an update in AccessKit's shape: the nodes that changed, each whole, the focus with every update, and the root with the first. The program hands it to the platform's adapter, which keeps its own copy and answers the screen reader from it. The adapter queues what assistive technology asks (a press, a focus, a value, a scroll), and the program applies it on its own thread:

```c
// A frame's accessibility, once the root is enabled: the update of what
// changed since the last, for the platform's adapter, then the requests
// it queued from assistive technology, applied on the program's thread.
static muiResult AccessFrame(muiContext* context, muiNodeId root, muiAccessUpdate* updateOut,
                             const muiAccessRequest* requests, uint32_t requestCount)
{
    muiResult result = muiBuildAccessUpdate(context, root, updateOut);
    for (uint32_t i = 0; result == mui_success && i < requestCount; i++)
    {
        // A node that does not take the action or is gone, and a client's
        // request that does not fit it, are skipped.
        muiResult applied = muiPerformAccessAction(context, &requests[i], NULL);
        result = applied == mui_errorInvalid ? applied : mui_success;
    }
    return result;
}
```

The first update after enabling holds the whole tree, with the button in it named "OK". A press requested by assistive technology reaches the program as the button's activation ([Input](input.md#routing)). A request to move a text's selection or change its text is not the context's, so this loop skips it. A program with an editing field gives those two to `muiTextPerformAccessAction` instead, as `samples/app.c` does.

Expanding and collapsing a node is the program's: a request for it arrives as `mui_notificationAccessAction`.

## The adapters

Each adapter is its own build option, on by default on its platform, with its header `maul-ui/access_<platform>.h`:

| Platform | Adapter | Notes |
|---|---|---|
| Windows | UI Automation | Call it on a thread in a single-threaded apartment, the window's thread. |
| Linux | AT-SPI | The program polls its D-Bus connection each frame; no thread is started. libdbus-1 is opened at run time. |
| macOS | NSAccessibility | The root's parent is the program's view. |
| iOS | UIAccessibility | |
| Android | Android accessibility | Partly Java, see below. |
| The web | ARIA elements | Mirrored into elements over the canvas, built when a screen reader user presses a hidden button or the program enables it. |

The [Maul Window](maul-window.md) glue connects the one for the window's platform.

Android's adapter is partly Java, [java/maul/ui](../../java/maul/ui), which the application compiles with its own. Its native code reaches that Java by name, so an application that shrinks its code, as a Gradle release build does, adds [java/proguard-rules.pro](../../java/proguard-rules.pro) to its rules. Without them a release build runs, and the screen reader hears nothing.

## Things to know

- Open a window with a control focused, as platform toolkits' windows do. A keyboard user then has a place to start, and Orca 46 announces nothing for the first focus of a window that opened with none.
- A widget should answer activation, not only pointer clicks: assistive technology and gamepads press with activation.
- A live region's new text is announced: set the live value on a node whose name changes, such as a status line.
- A password field is read as bullets, and its text is never sent to an adapter.
