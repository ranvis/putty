// PuTTYrv

#include <stdlib.h>
#include "utils/dassert.h"

#define IS_CTRL_COMBOBOX(ctrl)  ((ctrl)->type == CTRL_EDITBOX && (ctrl)->editbox.has_list)
#define IS_CTRL_PURE_LISTBOX(ctrl)  ((ctrl)->type == CTRL_LISTBOX && (ctrl)->listbox.height != 0)

enum {
    CB_LAZY_DEL,
    CB_LAZY_ADD,
    CB_LAZY_ADDWITHID_U,
    CB_LAZY_ADDWITHID_N,
};

typedef struct {
    bool is_populated;
    bool is_first_dd_item;
    strbuf *items;
} lazy_items_data;

static void lazy_items_free_data(struct winctrl *c)
{
    lazy_items_data *ld = (lazy_items_data *)c->data;
    strbuf_free(ld->items);
    sfree(ld);
}

static lazy_items_data *lazy_items_init_ld(struct winctrl *c)
{
    lazy_items_data *ld;
    dassert(!c->data);
    c->data = ld = snew(lazy_items_data);
    c->data_freefn = lazy_items_free_data;
    ld->is_populated = false;
    ld->is_first_dd_item = c->ctrl->type == CTRL_LISTBOX;
    ld->items = strbuf_new();
    return ld;
}

static bool lazy_items_clear(struct winctrl *c, const dlgcontrol *ctrl, const dlgparam *dp)
{
    if (IS_CTRL_PURE_LISTBOX(c->ctrl)) return false;
    if (!c->data) return true;  // uninitialized: already empty
    lazy_items_data *ld = (lazy_items_data *)c->data;
    bool done = !ld->is_populated && (IS_CTRL_COMBOBOX(ctrl) || ld->is_first_dd_item);
    ld->is_populated = false;
    ld->is_first_dd_item = c->ctrl->type == CTRL_LISTBOX;
    strbuf_shrink_to(ld->items, 0);
    return done;
}

static bool lazy_items_del(struct winctrl *c, const dlgcontrol *ctrl, const dlgparam *dp, int index)
{
    if (IS_CTRL_PURE_LISTBOX(c->ctrl)) return false;
    if (!c->data) return true;  // uninitialized: already empty
    lazy_items_data *ld = (lazy_items_data *)c->data;
    if (ld->is_populated) return false;
    put_uint16(ld->items, CB_LAZY_DEL);
    put_uint32(ld->items, index);
    return true;
}

static bool lazy_items_is_delayed(const struct winctrl *c, const dlgcontrol *ctrl, const dlgparam *dp)
{
    if (!c->data) return false;
    lazy_items_data *ld = (lazy_items_data *)c->data;
    return !ld->is_populated;
}

static void lazy_items_populate(const struct winctrl *c, const dlgcontrol *ctrl, const dlgparam *dp)
{
    if (!c->data) return;
    lazy_items_data *ld = (lazy_items_data *)c->data;
    if (ld->is_populated) return;
    BinarySource bs[1];
    BinarySource_BARE_INIT_PL(bs, ptrlen_from_strbuf(ld->items));
    for (;;) {
        uint16_t type = get_uint16(bs);
        if (get_err(bs)) break;
        if (type == CB_LAZY_DEL) {
            int index = get_uint32(bs);
            SendDlgItemMessage(dp->hwnd, c->base_id+1, CB_DELETESTRING, index, 0);
        } else {
            ptrlen str = get_string(bs);
            int index = SendDlgItemMessage(dp->hwnd, c->base_id+1, CB_ADDSTRING, 0, (LPARAM)str.ptr);
            if (type != CB_LAZY_ADD) {
                int id = (int)get_uint32(bs);
                if (type == CB_LAZY_ADDWITHID_N) id = -id;
                SendDlgItemMessage(dp->hwnd, c->base_id+1, CB_SETITEMDATA, index, (LPARAM)id);
            }
        }
    }
    ld->is_populated = true;
}

static bool lazy_items_add(struct winctrl *c, const dlgcontrol *ctrl, const dlgparam *dp, const char *text)
{
    if (IS_CTRL_PURE_LISTBOX(c->ctrl)) return false;
    lazy_items_data *ld = (lazy_items_data *)c->data;
    if (!c->data) ld = lazy_items_init_ld(c);
    if (ld->is_populated) return false;
    if (ld->is_first_dd_item) {
        ld->is_first_dd_item = false;
        return false;
    }
    put_uint16(ld->items, CB_LAZY_ADD);
    put_string(ld->items, text, strlen(text)+1);
    return true;
}

static bool lazy_items_addwithid(struct winctrl *c, const dlgcontrol *ctrl, const dlgparam *dp, const char *text, int id)
{
    if (IS_CTRL_PURE_LISTBOX(c->ctrl)) return false;
    lazy_items_data *ld = (lazy_items_data *)c->data;
    if (!c->data) ld = lazy_items_init_ld(c);
    if (ld->is_populated) return false;
    if (ld->is_first_dd_item) {
        ld->is_first_dd_item = false;
        return false;
    }
    int abs_id = abs(id);
    if (abs_id > 0xffffffff) {
        lazy_items_populate(c, ctrl, dp);
        return false;
    }
    put_uint16(ld->items, id >= 0 ? CB_LAZY_ADDWITHID_U : CB_LAZY_ADDWITHID_N);
    put_string(ld->items, text, strlen(text)+1);
    put_uint32(ld->items, abs_id);
    return true;
}

static bool lazy_items_select(struct winctrl *c, const dlgcontrol *ctrl, const dlgparam *dp, int index)
{
     if (c->ctrl->type == CTRL_LISTBOX && index != 0)
        lazy_items_populate(c, ctrl, dp);
     return false;
}

static void lazy_items_handle_cmd(const struct winctrl *c, const dlgcontrol *ctrl, const dlgparam *dp, WORD event)
{
    if (event == CBN_SETFOCUS || event == CBN_DROPDOWN)
        lazy_items_populate(c, ctrl, dp);
}
