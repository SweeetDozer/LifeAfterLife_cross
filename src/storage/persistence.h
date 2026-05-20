#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "../models/relationship.h"

namespace storage {

struct SessionData {
    std::string access_token;
    std::string refresh_token;
    std::string email;
};

struct UiStateData {
    int last_tree_id = -1;
};

struct CanvasState {
    double zoom = 1.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
};

struct PersonLayoutPosition {
    double x = 0.0;
    double y = 0.0;
};

struct TreeLayoutData {
    CanvasState canvas;
    std::unordered_map<int, PersonLayoutPosition> persons;
    std::vector<Relationship> relationships;
};

std::filesystem::path default_app_data_directory();
std::filesystem::path session_file_path(const std::filesystem::path& app_data_directory);
std::filesystem::path ui_state_file_path(const std::filesystem::path& app_data_directory);
std::filesystem::path tree_layouts_directory(const std::filesystem::path& app_data_directory);
std::filesystem::path tree_layout_file_path(const std::filesystem::path& app_data_directory, int tree_id);

bool ensure_app_data_directories(const std::filesystem::path& app_data_directory);
std::optional<std::string> load_json_text(const std::filesystem::path& file_path);
bool save_json_text_atomic(const std::filesystem::path& file_path, std::string_view json_text);

SessionData load_session(const std::filesystem::path& app_data_directory = default_app_data_directory());
bool save_session(const SessionData& session, const std::filesystem::path& app_data_directory = default_app_data_directory());

UiStateData load_ui_state(const std::filesystem::path& app_data_directory = default_app_data_directory());
bool save_ui_state(const UiStateData& ui_state, const std::filesystem::path& app_data_directory = default_app_data_directory());

TreeLayoutData load_tree_layout(int tree_id, const std::filesystem::path& app_data_directory = default_app_data_directory());
bool save_tree_layout(int tree_id, const TreeLayoutData& tree_layout, const std::filesystem::path& app_data_directory = default_app_data_directory());

} // namespace storage
