# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The tests' own Java that their native code reaches by name: the
# activity's native methods, bound by their exported names. The
# activity itself the manifest's rules keep.
-keepclasseswithmembernames class maul.ui.tests.TestActivity {
    native <methods>;
}
