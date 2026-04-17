#pragma once

enum class Page {
    Login = 0,
    Register = 1,
    Main = 2
};

struct AppState {
    bool is_authenticated = false;

    Page current_page = Page::Login; // use Page enum instead of raw int

    int selected_tree_id = -1;
    int selected_person_id = -1;

    bool edit_mode = false;

    float zoom = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    // helpers for conversion to/from Slint (which uses int)
    inline int current_page_to_int() const { return static_cast<int>(current_page); }
    inline void set_current_page_from_int(int v) { current_page = static_cast<Page>(v); }
};