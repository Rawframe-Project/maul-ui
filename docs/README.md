# Documentation

## Using the library

- [Getting started](user/getting-started.md): requirements, building and installing, the build options, the model, a first screen, results, ids, defs and limits.
- [Nodes and layout](user/layout.md): the tree, flexbox, sizes, absolute placement, direction, the safe area and host content.
- [Styling](user/styling.md): layers, classes and states, conditions, tokens and themes, visual values, transitions and exits.
- [Text](user/text.md): fonts, labels, spans, fallback and families.
- [Text editing](user/text-editing.md): fields, the caret and selection, input methods.
- [Drawing](user/drawing.md): the draw list, content the program draws, glyphs, the reference renderer.
- [Input](user/input.md): routing, pointers, focus and keys, gamepads, drags, drops and ranges.
- [Scrolling, lists and popups](user/scrolling-and-lists.md).
- [Accessibility](user/accessibility.md): roles and names, updates and requests, the platform adapters.
- [Maul Window](user/maul-window.md): the glue to Maul Window, and the samples.
- [Testing a program's interface](user/testing.md) without a window or a GPU.
- [Licenses](user/licenses.md): what a program that ships the library carries.
- [API reference](https://rawframe-project.github.io/maul-ui/api.html), generated from the headers.

## How it works

- [Overview](internals/overview.md): the library's profile, the frame, and the host code it calls.
- [Glossary](internals/glossary.md).
- [Node tree](internals/node-tree.md), [Layout](internals/layout.md), [Style](internals/style.md), [Draw list](internals/draw-list.md).
- [Interaction](internals/interaction.md) and [Scrolling and lists](internals/scrolling.md).
- [Text](internals/text.md), [Glyphs](internals/glyphs.md) and [Text editing](internals/text-editing.md).
- [Accessibility](internals/accessibility.md).
- [Reference renderer](internals/renderer.md), [Window glue](internals/window-glue.md) and [The web](internals/web.md).
- [Maul conventions](internals/maul-conventions.md): the rules every Maul library follows.

## Maintaining it

- [Development](operations/development.md): toolchain, presets, generated files, layout fixtures, fuzz findings, benchmarks and size.
- [Test environments](operations/test-environments.md): lavapipe, the test backend, buses, platforms, browsers and screen readers.
