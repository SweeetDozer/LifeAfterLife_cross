#pragma once

struct AppState {
    int selected_tree_id = -1;
    int selected_person_id = -1;
    int selected_relationship_id = -1;
    bool is_edit_mode = false;
    bool inspector_edit_mode = false;
    float canvas_offset_x = 0.0f;
    float canvas_offset_y = 0.0f;
    float canvas_zoom = 1.0f;
};
