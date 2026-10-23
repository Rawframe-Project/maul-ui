# Styling

## Contents

- [Layers](#layers)
- [Classes and states](#classes-and-states)
- [Conditions](#conditions)
- [Tokens and themes](#tokens-and-themes)
- [Visual values](#visual-values)
- [Transitions and exits](#transitions-and-exits)

## Layers

A node's values resolve in layers, each later one winning:

1. the defaults;
2. the base values of each of its classes, in order;
3. the variants of the states it is in: checked, selected, focused, focus visible, hovered, pressed, disabled, exiting, each stronger than the one before;
4. the variants of conditions that hold;
5. the values set on the node itself.

There is no cascade and no specificity: a value's layer alone decides whether it wins, so a hovered value of the first class beats the base value of the last. A node's classes are its node type's, then its own (`muiNode_SetClasses`). A mask says which fields of a style struct a call sets; the rest keep coming from the layers below. Values set on the node win until `muiNode_ResetProperties` takes them back.

Style properties come in four groups, each a struct with its own calls: layout (`muiLayoutStyle`), visual (`muiVisualStyle`), text (`muiTextStyle`) and interaction (`muiInteractionStyle`). A changed visual value repaints a node without laying it out again.

## Classes and states

A class names the look of a kind of node once:

```c
// A button class: padded and blue, lighter while hovered, darker while
// pressed; and a node type of buttons, which lists it.
static muiResult MakeButtonType(muiContext* context, muiStyleId* classOut, muiNodeTypeId* typeOut)
{
    const muiStyleDef styleDef = muiDefaultStyleDef();
    muiResult result = muiCreateStyle(context, &styleDef, classOut);
    muiStyleId button = *classOut;
    if (result != mui_success)
    {
        return result;
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding = (muiEdges){12.0f, 12.0f, 6.0f, 6.0f};
    const muiPropertyMask padding =
        MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
        MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom);
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    muiVisualStyle normal = muiDefaultVisualStyle();
    normal.background = (muiColor){0.2f, 0.4f, 0.8f, 1.0f};
    muiVisualStyle hovered = normal;
    hovered.background = (muiColor){0.3f, 0.5f, 0.9f, 1.0f};
    muiVisualStyle pressed = normal;
    pressed.background = (muiColor){0.1f, 0.3f, 0.6f, 1.0f};
    if ((result = muiStyle_SetLayoutValues(context, button, mui_variantBase, &layout, padding)) !=
            mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantBase, &normal,
                                           background)) != mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantHovered, &hovered,
                                           background)) != mui_success ||
        (result = muiStyle_SetVisualValues(context, button, mui_variantPressed, &pressed,
                                           background)) != mui_success)
    {
        return result;
    }
    muiNodeTypeDef typeDef = muiDefaultNodeTypeDef();
    typeDef.classes = &button;
    typeDef.classCount = 1;
    return muiCreateNodeType(context, &typeDef, typeOut);
}
```

A node of that type is blue. `muiNode_SetStates(context, node, mui_stateHovered)` makes it lighter, and pressed as well, darker, as pressed is the stronger state. Pointer input sets hover and press itself, and focus sets the focus states ([Input](input.md)); a program sets the others. Nodes are styled again at the next `muiComputeLayout`.

## Conditions

A condition holds for a node in some environment or at some size: a viewport class, an input modality, a text scale, reduced motion, or the node's own size or direction from its last layout. Its values are a variant of the class, set as a state's are:

```c
// A class that stacks a row's children on small viewports, a phone's.
static muiResult MakeStacking(muiContext* context, muiStyleId* classOut)
{
    const muiStyleDef styleDef = muiDefaultStyleDef();
    muiResult result = muiCreateStyle(context, &styleDef, classOut);
    muiCondition small = muiDefaultCondition();
    small.viewports = mui_viewportSmall;
    muiVariant variant = mui_variantBase;
    if (result == mui_success)
    {
        result = muiStyle_AddCondition(context, *classOut, &small, &variant);
    }
    muiLayoutStyle stacked = muiDefaultLayoutStyle();
    stacked.container.direction = mui_flexColumn;
    return result == mui_success
               ? muiStyle_SetLayoutValues(context, *classOut, variant, &stacked,
                                          MUI_PROPERTY_BIT(mui_propertyFlexDirection))
               : result;
}
```

`muiSetContextEnvironment` tells the context where its interface is shown: on a small viewport the row stacks. A condition on the node's own size reads its last layout, so a frame that changes the size needs one more layout to show the result; `muiIsUpdatePending` says when. A condition may not set what it reads, such as the width when it reads the width.

A rule that feeds itself through layout, such as a narrow node made wider, would flicker. The context notices four runs alternating between two sizes, holds the node's conditions, and reports `mui_notificationOscillation` for the node.

## Tokens and themes

Tokens name values a theme can change: a class variant names a token for a property instead of a value, and every node using it follows it. A theme overrides tokens for the subtree it is set on, the nearest theme above a node winning:

```c
// An accent color token the button class paints with, and a theme that
// makes it orange in the subtree it is set on.
static muiResult UseAccent(muiContext* context, muiStyleId button, muiNodeId warning)
{
    muiTokenValue value = {.type = mui_tokenColor, .color = {0.2f, 0.4f, 0.8f, 1.0f}};
    muiTokenId accent = {0, 0};
    muiThemeId alert = {0, 0};
    muiTokenDef tokenDef = muiDefaultTokenDef();
    tokenDef.value = value;
    muiResult result = muiCreateToken(context, &tokenDef, &accent);
    if (result == mui_success)
    {
        result =
            muiStyle_SetToken(context, button, mui_variantBase, mui_propertyBackground, accent);
    }
    if (result == mui_success)
    {
        const muiThemeDef themeDef = muiDefaultThemeDef();
        result = muiCreateTheme(context, &themeDef, &alert);
    }
    value.color = (muiColor){0.9f, 0.5f, 0.1f, 1.0f};
    if (result == mui_success)
    {
        result = muiTheme_SetTokenValue(context, alert, accent, &value);
    }
    return result == mui_success ? muiNode_SetTheme(context, warning, alert) : result;
}
```

The buttons are blue, and orange under the node with the theme. A token may also be an alias for another token of its type, so a theme can re-point semantic tokens ("accent") at other primitives ("orange 500"). Changing a token restyles every node, and transitions move the change.

## Visual values

`muiVisualStyle` (`maul-ui/visual.h`) holds what a node looks like:

- a background color and a gradient of up to four stops, linear at an angle, radial or conic, as CSS's three kinds;
- corner radii, as a scale of the box's shorter side plus an offset, held to half of it, so a large radius makes a pill;
- border colors per side (the widths are layout values);
- an outer and an inner shadow;
- an image by host key, with nine-slice insets, a tint, whether it mirrors under right to left, and how it fills its middle: stretched, repeated, rounded or spaced, as CSS's `border-image-repeat`;
- opacity, clipping, and a local scale about an origin that scales a node and its subtree as drawn and hit, without changing layout.

Colors are sRGB with straight alpha, as a design tool gives them. The draw list carries them in linear light with premultiplied alpha.

## Transitions and exits

A transition says how a property moves to a new value: over a duration along an easing curve, as CSS names them, or as a spring that keeps its speed when the target changes. A class's variants name transitions for properties as they name values, and the spec for a change resolves through the same layers, from the state after the change. Numbers, dimensions, radii, colors and shadows move; enumerators, image keys and gradients change at once, as does everything under reduced motion. Transitions run against the time each frame hands to layout (`timeNs`, on a monotonic clock), so a program that renders only when something changes keeps drawing while `muiIsUpdatePending` says one runs.

```c
// A class whose nodes' opacity moves over 150 ms, easing out, and which
// fades them to nothing while they leave.
static muiResult MakeFading(muiContext* context, muiStyleId* classOut)
{
    muiTransitionDef def = muiDefaultTransitionDef();
    def.kind = mui_transitionTimed;
    def.durationNs = 150000000;
    def.easing = mui_easingEaseOut;
    muiTransitionId fade = {0, 0};
    muiVisualStyle gone = muiDefaultVisualStyle();
    gone.opacity = 0.0f;
    const muiPropertyMask opacity = MUI_PROPERTY_BIT(mui_propertyOpacity);
    const muiStyleDef styleDef = muiDefaultStyleDef();
    muiResult result = muiCreateStyle(context, &styleDef, classOut);
    if (result == mui_success)
    {
        result = muiCreateTransition(context, &def, &fade);
    }
    if (result == mui_success)
    {
        result = muiStyle_SetTransition(context, *classOut, mui_variantBase, fade, mui_groupVisual,
                                        opacity);
    }
    return result == mui_success
               ? muiStyle_SetVisualValues(context, *classOut, mui_variantExiting, &gone, opacity)
               : result;
}
```

A node that leaves plays its way out before it goes. `muiNode_BeginExit` puts it and its subtree in the exiting state, so its classes' exiting variants and their transitions apply, and it leaves hit testing, focus and navigation at once. When nothing moves in it any more, the context reports it, once:

```c
// Destroys the nodes whose exits finished, as a frame's notifications
// say. A node leaves with muiNode_BeginExit; the library never destroys
// it.
static void DestroyExited(muiContext* context)
{
    muiNotification notification;
    while (muiNextNotification(context, &notification) == mui_success)
    {
        if (notification.kind == mui_notificationExitFinished)
        {
            (void)muiDestroyNode(context, notification.node);
        }
    }
}
```

A toast of the fading class begins its exit fully opaque, is halfway out 75 ms later and still in the tree, and at 150 ms is transparent, its exit reported and the node destroyed. `muiNode_CancelExit` brings a leaving node back: the exiting state goes, it takes input again, and its transitions move it back. An exit can also take the node out of its parent's flow while it plays out, so its siblings close up at once (`exitLayout`).
