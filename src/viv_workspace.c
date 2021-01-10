#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "viv_cursor.h"
#include "viv_types.h"
#include "viv_view.h"
#include "viv_wl_list_utils.h"

void viv_workspace_focus_next_window(struct viv_workspace *workspace) {
    struct viv_view *active_view = workspace->active_view;
    if (active_view == NULL) {
        wlr_log(WLR_DEBUG, "Could not get next window, no active view");
        return;
    }

    struct viv_view *next_view = viv_view_next_in_workspace(workspace->active_view);

    viv_view_focus(next_view, viv_view_get_toplevel_surface(next_view));
}

void viv_workspace_focus_prev_window(struct viv_workspace *workspace) {
    struct viv_view *active_view = workspace->active_view;
    if (active_view == NULL) {
        wlr_log(WLR_DEBUG, "Could not get prev window, no active view");
        return;
    }

    struct viv_view *prev_view = viv_view_prev_in_workspace(workspace->active_view);

    viv_view_focus(prev_view, viv_view_get_toplevel_surface(prev_view));
}

void viv_workspace_shift_active_window_up(struct viv_workspace *workspace) {
    struct viv_view *active_view = workspace->active_view;
    if (active_view == NULL) {
        wlr_log(WLR_DEBUG, "Could not get next window, no active view");
        return;
    }

    struct wl_list *prev_link = active_view->workspace_link.prev;
    if (prev_link == &workspace->views) {
        prev_link = prev_link->prev;
    }

    if (prev_link == &workspace->views) {
        wlr_log(WLR_ERROR, "Next window couldn't be found\n");
        return;
    }

    viv_wl_list_swap(&active_view->workspace_link, prev_link);

    active_view->workspace->needs_layout = true;
}

void viv_workspace_shift_active_window_down(struct viv_workspace *workspace) {
    struct viv_view *active_view = workspace->active_view;
    if (active_view == NULL) {
        wlr_log(WLR_DEBUG, "Could not get next window, no active view");
        return;
    }

    struct wl_list *next_link = active_view->workspace_link.next;
    if (next_link == &workspace->views) {
        next_link = next_link->next;
    }

    if (next_link == &workspace->views) {
        wlr_log(WLR_ERROR, "Next window couldn't be found\n");
        return;
    }

    viv_wl_list_swap(&active_view->workspace_link, next_link);

    active_view->workspace->needs_layout = true;
}

void viv_workspace_increment_divide(struct viv_workspace *workspace, float increment) {
    workspace->active_layout->parameter += increment;
    if (workspace->active_layout->parameter > 1) {
        workspace->active_layout->parameter = 1;
    } else if (workspace->active_layout->parameter < 0) {
        workspace->active_layout->parameter = 0;
    }

    workspace->needs_layout = true;
}

void viv_workspace_swap_out(struct viv_output *output, struct wl_list *workspaces) {
    if (wl_list_length(workspaces) < 2) {
        wlr_log(WLR_DEBUG, "Not switching workspaces as only %d present\n", wl_list_length(workspaces));
        return;
    }

    struct wl_list *new_workspace_link = workspaces->prev;

    struct viv_workspace *new_workspace = wl_container_of(new_workspace_link, new_workspace, server_link);
    struct viv_workspace *old_workspace = wl_container_of(workspaces->next, old_workspace, server_link);

    wl_list_remove(new_workspace_link);
    wl_list_insert(workspaces, new_workspace_link);

    old_workspace->output = NULL;
    new_workspace->output = output;
    output->current_workspace = new_workspace;

    output->needs_layout = true;

    if (new_workspace->active_view != NULL) {
        viv_view_focus(new_workspace->active_view, viv_view_get_toplevel_surface(new_workspace->active_view));
    }

    wlr_log(WLR_INFO, "Workspace changed from %s to %s", old_workspace->name, new_workspace->name);
}

void viv_workspace_next_layout(struct viv_workspace *workspace) {
    struct wl_list *next_layout_link = workspace->active_layout->workspace_link.next;
    if (next_layout_link == &workspace->layouts) {
        next_layout_link = next_layout_link->next;
    }
    struct viv_layout *next_layout = wl_container_of(next_layout_link, next_layout, workspace_link);
    wlr_log(WLR_DEBUG, "Switching to layout with name %s", next_layout->name);
    workspace->active_layout = next_layout;
    workspace->needs_layout = true;
}

void viv_workspace_prev_layout(struct viv_workspace *workspace) {
    struct wl_list *prev_layout_link = workspace->active_layout->workspace_link.prev;
    if (prev_layout_link == &workspace->layouts) {
        prev_layout_link = prev_layout_link->prev;
    }
    struct viv_layout *prev_layout = wl_container_of(prev_layout_link, prev_layout, workspace_link);
    wlr_log(WLR_DEBUG, "Switching to layout with name %s", prev_layout->name);
    workspace->active_layout = prev_layout;
    workspace->needs_layout = true;
}

void viv_workspace_assign_to_output(struct viv_workspace *workspace, struct viv_output *output) {
    // TODO add workspace switching between outputs
    if (workspace->output != NULL) {
        wlr_log(WLR_ERROR, "Changed the output of a workspace that already has an output");
    }

    output->current_workspace = workspace;
    workspace->output = output;
}

struct viv_view *viv_workspace_main_view(struct viv_workspace *workspace) {
    struct viv_view *view = NULL;
    wl_list_for_each(view, &workspace->views, workspace_link) {
        if (!view->is_floating) {
            break;
        }
    }

    return view;
}

void viv_workspace_swap_active_and_main(struct viv_workspace *workspace) {
    struct viv_view *active_view = workspace->active_view;
    struct viv_view *main_view = viv_workspace_main_view(workspace);

    if (main_view == NULL || active_view == NULL) {
        wlr_log(WLR_ERROR, "Cannot swap active and main views, one or both is NULL");
        return;
    }

    viv_wl_list_swap(&active_view->workspace_link, &main_view->workspace_link);
    workspace->needs_layout = true;
}

void viv_workspace_do_layout(struct viv_workspace *workspace) {
    struct viv_output *output = workspace->output;

    int32_t width = output->wlr_output->width - output->excluded_margin.left - output->excluded_margin.right;
    int32_t height = output->wlr_output->height - output->excluded_margin.top - output->excluded_margin.bottom;

    workspace->active_layout->layout_function(workspace, width, height);

    workspace->needs_layout = false;
    workspace->output->needs_layout = false;

    // Reset cursor focus as the view under the cursor may have changed
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    viv_cursor_reset_focus(workspace->output->server, (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);

    workspace->was_laid_out = true;
}

uint32_t viv_workspace_num_tiled_views(struct viv_workspace *workspace) {
    uint32_t num_views = 0;
    struct viv_view *view;
    wl_list_for_each(view, &workspace->views, workspace_link) {
        if (view->is_floating) {
            continue;
        }
        num_views++;
    }
    return num_views;
}

void viv_workspace_add_view(struct viv_workspace *workspace, struct viv_view *view) {
    view->workspace = workspace;

    if (workspace->active_view != NULL) {
        wl_list_insert(workspace->active_view->workspace_link.prev, &view->workspace_link);
    } else {
        wl_list_insert(&workspace->views, &view->workspace_link);
    }

	viv_view_focus(view, viv_view_get_toplevel_surface(view));

    workspace->needs_layout = true;
}
