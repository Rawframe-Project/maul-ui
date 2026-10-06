// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The accessibility tree's consumer (record mui-0008), the component
// MAUL_UI_ACCESS_TREE builds: the copy of a root's accessibility tree
// that platform adapters read, kept from the updates muiBuildAccessUpdate
// gives (maul-ui/access.h). Applying an update reports what it changed,
// so an adapter can raise its platform's events. A tree is used on one
// thread, the one its platform calls on: the window's, with UI
// Automation's COM threading on an STA thread.

#ifndef MAUL_UI_ACCESS_TREE_H
#define MAUL_UI_ACCESS_TREE_H

#include "maul-ui/access.h"
#include "maul-ui/base.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct muiAccessTree muiAccessTree;

    // How a tree is made. Build it with muiDefaultAccessTreeDef.
    typedef struct muiAccessTreeDef
    {
        uint32_t cookie;
        muiAllocator allocator;
        // The most nodes it holds at once, at least 1.
        uint32_t nodes;
    } muiAccessTreeDef;

    // What applying an update changed, reported once the tree holds all
    // of it: nodes added, in the update's order; nodes updated, with
    // what they were; nodes let go, with their subtrees, each with what
    // it was; then the focus, when it moved. Any of the functions may be
    // NULL. The records they are given are valid during the call.
    typedef struct muiAccessChanges
    {
        void* user;
        void (*added)(void* user, const muiAccessTree* tree, uint64_t id);
        void (*updated)(void* user, const muiAccessTree* tree, const muiAccessNode* old);
        void (*removed)(void* user, const muiAccessTree* tree, const muiAccessNode* old);
        void (*focusMoved)(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus);
    } muiAccessChanges;

    /// The default def: the C library's allocation, 4096 nodes, as many as
    /// a default context's.
    ///
    /// @return The def.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API muiAccessTreeDef muiDefaultAccessTreeDef(void);

    /// Creates an empty tree.
    ///
    /// @param def      The def, from muiDefaultAccessTreeDef.
    /// @param treeOut  Receives the tree; NULL on failure.
    /// @return `mui_success`; `mui_errorInvalid` for a NULL argument, a
    ///         def not from muiDefaultAccessTreeDef, a half-set allocator
    ///         or no nodes; `mui_errorCapacity` when memory runs out.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_NODISCARD MUI_API muiResult muiCreateAccessTree(const muiAccessTreeDef* def,
                                                        muiAccessTree** treeOut);

    /// Destroys a tree and all it holds; NULL is ignored.
    ///
    /// @param tree  The tree.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_API void muiDestroyAccessTree(muiAccessTree* tree);

    /// Applies an update whole, or nothing of it: the nodes sent replace
    /// those held, new ones join, and a node a parent sent no longer
    /// lists, which no other node sent lists, leaves with its subtree, as
    /// does a node sent with no parent that is not the root (told only as
    /// removed).
    /// The first update a tree takes, and any naming a new root, must be
    /// whole.
    ///
    /// @param tree     The tree.
    /// @param update   The update.
    /// @param changes  Told what changed; may be NULL.
    /// @return `mui_success`; `mui_errorCapacity` when the nodes would not
    ///         fit or memory runs out, which changes nothing;
    ///         `mui_errorInvalid` for a NULL tree or update, or an update
    ///         that does not fit the tree: no root for an empty tree, a
    ///         node with the id 0, a child neither held nor sent, or a
    ///         root, or focus, that is neither.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiAccessTree_Apply(muiAccessTree* tree,
                                                        const muiAccessUpdate* update,
                                                        const muiAccessChanges* changes);

    /// The root's id, or 0 for an empty tree.
    ///
    /// @param tree  The tree.
    /// @return The id.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_API uint64_t muiAccessTree_GetRoot(const muiAccessTree* tree);

    /// The focused node's id, or 0 for an empty tree.
    ///
    /// @param tree  The tree.
    /// @return The id.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_API uint64_t muiAccessTree_GetFocus(const muiAccessTree* tree);

    /// How many nodes the tree holds.
    ///
    /// @param tree  The tree.
    /// @return The count; 0 for NULL.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_API uint32_t muiAccessTree_Count(const muiAccessTree* tree);

    /// A node the tree holds: its texts and links are the tree's copies,
    /// valid until an update replaces or removes it; its firstChild is 0,
    /// and its children come from muiAccessTree_GetChildren.
    ///
    /// @param tree  The tree.
    /// @param id    The node's id.
    /// @return The node; NULL for one not held or a NULL tree.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_API const muiAccessNode* muiAccessTree_Find(const muiAccessTree* tree, uint64_t id);

    /// A node's parent.
    ///
    /// @param tree  The tree.
    /// @param id    The node's id.
    /// @return The parent's id; 0 for the root, a node not held, or a NULL
    ///         tree.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_API uint64_t muiAccessTree_GetParent(const muiAccessTree* tree, uint64_t id);

    /// A node's children, in order, valid until an update replaces it.
    ///
    /// @param tree      The tree.
    /// @param id        The node's id.
    /// @param countOut  Receives how many; 0 for a node not held.
    /// @return Their ids; NULL for none.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_API const uint64_t* muiAccessTree_GetChildren(const muiAccessTree* tree, uint64_t id,
                                                      uint32_t* countOut);

    /// Writes the tree as text, a node a line in tree order, indented by
    /// depth: its role's name, its id's index, and its flags, actions,
    /// size, place and texts where it has them. Tests compare it.
    ///
    /// @param tree       The tree.
    /// @param buffer     Receives the text, NUL-terminated; may be NULL
    ///                   when capacity is 0.
    /// @param capacity   Its size in bytes.
    /// @param lengthOut  Receives the text's length without its NUL.
    /// @return `mui_success`; `mui_errorCapacity` when it does not fit,
    ///         as much written as fits; `mui_errorInvalid` for a NULL tree
    ///         or length, or a NULL buffer with room.
    /// @par Thread safety
    /// Safe from any thread; the tree is used by one thread at a time.
    MUI_NODISCARD MUI_API muiResult muiAccessTree_Write(const muiAccessTree* tree, char* buffer,
                                                        size_t capacity, size_t* lengthOut);

    /// A role's name, as its constant's after mui_role with a lower-case
    /// first letter: "button" for mui_roleButton.
    ///
    /// @param role  The role.
    /// @return The name; "unknown" for a role past MUI_ROLE_LAST.
    /// @par Thread safety
    /// Safe from any thread.
    MUI_API const char* muiAccessRoleName(muiRole role);

#ifdef __cplusplus
}
#endif

#endif // MAUL_UI_ACCESS_TREE_H
