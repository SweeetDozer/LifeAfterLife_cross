#include "app-window.h"
#include "api/api_client.h"
#include "api/auth_api.h"
#include "api/tree_api.h"
#include "app/app_state.h"
#include "models/person.h"
#include "models/relationship.h"
#include "storage/persistence.h"
#include "ui_sync/sync.h"
#include "utils/formatters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <functional>
#include <filesystem>
#include <map>
#include <string>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

namespace {

struct Tree {
    int id;
    std::string name;
};

enum class RelationshipCreationStep {
    Inactive,
    SelectFirstPerson,
    SelectSecondPerson,
    ChooseRelationshipType,
};

constexpr float kCardWidth = 232.0f;
constexpr float kCardHeight = 132.0f;
constexpr float kRelationshipHitPadding = 4.0f;
constexpr float kRelationshipClickThreshold = 6.0f;
constexpr int kRelationshipCurveHitSegments = 12;
constexpr float kCardOverlapSpacing = 6.0f;
constexpr float kAutoLayoutOriginX = 120.0f;
constexpr float kAutoLayoutOriginY = 120.0f;
constexpr float kAutoLayoutHorizontalSpacing = kCardWidth + 60.0f;
constexpr float kAutoLayoutVerticalSpacing = kCardHeight + 80.0f;
constexpr float kDragResolveStep = 20.0f;
constexpr float kDragResolveMaxRadius = 280.0f;

std::string to_std_string(const slint::SharedString &value)
{
    std::string_view sv = value;  // SharedString неявно конвертируется
    return { sv.data(), sv.size() };
}

std::string format_person_name(const Person &person)
{
    return format_person_name_parts(person.first_name, person.middle_name, person.last_name);
}

std::string to_lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string login_error_message_from_result(const api::ApiError &error)
{
    if (error.type == api::ApiErrorType::Network) {
        return error.message.empty()
            ? "Could not reach the server. Check your connection."
            : "Network error: " + error.message;
    }

    if (error.http_status == 401 || error.http_status == 403) {
        return "Invalid email or password.";
    }

    if (error.http_status != 0) {
        return "HTTP " + std::to_string(error.http_status) + ": " + error.message;
    }

    return error.message.empty() ? "Could not complete the request. Please try again." : error.message;
}

std::string register_error_message_from_result(const api::ApiError &error)
{
    if (error.type == api::ApiErrorType::Network) {
        return "Could not reach the server. Check your connection.";
    }

    const std::string lowered = to_lower_copy(error.message);
    if (error.http_status == 409
        || lowered.find("already") != std::string::npos
        || lowered.find("exists") != std::string::npos) {
        return "An account with this email already exists.";
    }

    return "Could not complete the request. Please try again.";
}

bool has_persisted_session(const storage::SessionData &session)
{
    return !session.access_token.empty() && !session.refresh_token.empty();
}

bool tree_exists(const std::vector<Tree> &trees, int tree_id)
{
    return std::any_of(trees.begin(), trees.end(), [tree_id](const Tree &tree) {
        return tree.id == tree_id;
    });
}

Tree tree_from_summary(const api::TreeSummary &summary)
{
    return Tree {
        .id = summary.id,
        .name = summary.name,
    };
}

std::vector<Tree> trees_from_summaries(const std::vector<api::TreeSummary> &summaries)
{
    std::vector<Tree> trees;
    trees.reserve(summaries.size());
    for (const auto &summary : summaries) {
        trees.push_back(tree_from_summary(summary));
    }
    return trees;
}

std::string tree_error_message_from_result(const api::ApiError &error)
{
    if (error.type == api::ApiErrorType::Network) {
        return "Could not reach the server.";
    }

    if (error.http_status == 404) {
        return "Requested tree was not found.";
    }

    return error.message.empty() ? "Could not load tree data." : error.message;
}

std::string graph_mutation_error_message_from_result(const api::ApiError &error)
{
    if (error.type == api::ApiErrorType::Network) {
        return "Could not reach the server.";
    }

    if (error.http_status == 404) {
        return "The item no longer exists on the server.";
    }

    if (error.http_status == 422) {
        if (!error.message.empty()) {
            if (auto parsed = api::parse_json(error.message)) {
                if (const auto *object = api::as_object(&*parsed)) {
                    if (auto detail = api::json_get_string(object, "detail"); detail) {
                        if (*detail == "Parent relationship cannot point in both directions") {
                            return "Parent relationship must be created from parent to child, not the other way around.";
                        }
                        return *detail;
                    }
                }
            }
            return error.message;
        }
        return "The server rejected the graph change.";
    }

    return error.message.empty() ? "Could not save graph changes." : error.message;
}

std::pair<float, float> default_person_position_for_index(size_t index)
{
    constexpr size_t kDefaultColumns = 3;
    const size_t column = index % kDefaultColumns;
    const size_t row = index / kDefaultColumns;
    return {
        kAutoLayoutOriginX + static_cast<float>(column) * kAutoLayoutHorizontalSpacing,
        kAutoLayoutOriginY + static_cast<float>(row) * kAutoLayoutVerticalSpacing,
    };
}

Person person_from_record(const api::PersonRecord &record, size_t index)
{
    const auto [default_x, default_y] = default_person_position_for_index(index);
    return Person {
        .id = record.id,
        .first_name = record.first_name,
        .middle_name = record.middle_name,
        .last_name = record.last_name,
        .birth_date = record.birth_date,
        .death_date = record.death_date,
        .description = record.description,
        .x = default_x,
        .y = default_y,
    };
}

std::vector<Person> persons_from_records(const std::vector<api::PersonRecord> &records)
{
    std::vector<Person> persons;
    persons.reserve(records.size());
    for (size_t index = 0; index < records.size(); ++index) {
        persons.push_back(person_from_record(records[index], index));
    }
    return persons;
}

std::optional<std::string> optional_string_from_maybe_empty(const std::string &value)
{
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}

api::PersonMutationData person_mutation_data_from_person(const Person &person)
{
    return api::PersonMutationData {
        .first_name = person.first_name.empty() ? std::optional<std::optional<std::string>>() : std::optional<std::optional<std::string>>(person.first_name),
        .middle_name = person.middle_name.empty() ? std::optional<std::optional<std::string>>() : std::optional<std::optional<std::string>>(person.middle_name),
        .last_name = person.last_name.empty() ? std::optional<std::optional<std::string>>() : std::optional<std::optional<std::string>>(person.last_name),
        .birth_date = person.birth_date.empty() ? std::optional<std::optional<std::string>>() : std::optional<std::optional<std::string>>(person.birth_date),
        .death_date = person.death_date.empty() ? std::optional<std::optional<std::string>>() : std::optional<std::optional<std::string>>(person.death_date),
    };
}

api::PersonMutationData person_update_data_from_changes(const Person &previous_person, const Person &updated_person)
{
    api::PersonMutationData data;
    if (previous_person.first_name != updated_person.first_name) {
        if (updated_person.first_name.empty()) {
            data.first_name = std::optional<std::optional<std::string>>(std::optional<std::string>());
        } else {
            data.first_name = updated_person.first_name;
        }
    }
    if (previous_person.middle_name != updated_person.middle_name) {
        if (updated_person.middle_name.empty()) {
            data.middle_name = std::optional<std::optional<std::string>>(std::optional<std::string>());
        } else {
            data.middle_name = updated_person.middle_name;
        }
    }
    if (previous_person.last_name != updated_person.last_name) {
        if (updated_person.last_name.empty()) {
            data.last_name = std::optional<std::optional<std::string>>(std::optional<std::string>());
        } else {
            data.last_name = updated_person.last_name;
        }
    }
    if (previous_person.birth_date != updated_person.birth_date) {
        if (updated_person.birth_date.empty()) {
            data.birth_date = std::optional<std::optional<std::string>>(std::optional<std::string>());
        } else {
            data.birth_date = updated_person.birth_date;
        }
    }
    if (previous_person.death_date != updated_person.death_date) {
        if (updated_person.death_date.empty()) {
            data.death_date = std::optional<std::optional<std::string>>(std::optional<std::string>());
        } else {
            data.death_date = updated_person.death_date;
        }
    }
    if (previous_person.description != updated_person.description) {
        if (updated_person.description.empty()) {
            data.description = std::optional<std::optional<std::string>>(std::optional<std::string>());
        } else {
            data.description = updated_person.description;
        }
    }
    return data;
}

storage::TreeLayoutData make_canvas_only_tree_layout(const AppState &app_state)
{
    storage::TreeLayoutData layout;
    layout.canvas.zoom = app_state.canvas_zoom;
    layout.canvas.offset_x = app_state.canvas_offset_x;
    layout.canvas.offset_y = app_state.canvas_offset_y;
    return layout;
}

storage::TreeLayoutData make_full_tree_layout(const AppState &app_state, const std::vector<Person> &persons, const std::vector<Relationship> &relationships)
{
    storage::TreeLayoutData layout = make_canvas_only_tree_layout(app_state);
    for (const auto &person : persons) {
        layout.persons[person.id] = storage::PersonLayoutPosition {
            .x = person.x,
            .y = person.y,
        };
    }

    for (const auto &rel : relationships) {
        layout.relationships.push_back(rel);
    }

    return layout;
}

void apply_saved_person_positions(std::vector<Person> &persons, const storage::TreeLayoutData &tree_layout)
{
    for (auto &person : persons) {
        const auto position_it = tree_layout.persons.find(person.id);
        if (position_it == tree_layout.persons.end()) {
            continue;
        }

        person.x = static_cast<float>(position_it->second.x);
        person.y = static_cast<float>(position_it->second.y);
    }
}

float clamp_canvas_zoom(float zoom)
{
    return std::clamp(zoom, 0.5f, 2.5f);
}

std::string relationship_type_to_string(RelationshipType relationship_type)
{
    switch (relationship_type) {
    case RelationshipType::Parent:
        return "parent";
    case RelationshipType::Spouse:
        return "spouse";
    case RelationshipType::Sibling:
        return "sibling";
    case RelationshipType::Friend:
        return "friend";
    }

    return "parent";
}

std::optional<RelationshipType> relationship_type_from_string(const std::string &relationship_type)
{
    if (relationship_type == "parent") {
        return RelationshipType::Parent;
    }

    if (relationship_type == "spouse") {
        return RelationshipType::Spouse;
    }

    if (relationship_type == "sibling") {
        return RelationshipType::Sibling;
    }

    if (relationship_type == "friend") {
        return RelationshipType::Friend;
    }

    return std::nullopt;
}

bool relationship_type_is_symmetric(RelationshipType relationship_type)
{
    return relationship_type == RelationshipType::Spouse
        || relationship_type == RelationshipType::Sibling
        || relationship_type == RelationshipType::Friend;
}

bool relationship_exists(const std::vector<Relationship> &relationships,
                         int from_person_id,
                         int to_person_id,
                         RelationshipType relationship_type)
{
    return std::any_of(relationships.begin(), relationships.end(), [&](const Relationship &relationship) {
        if (relationship.relationship_type != relationship_type) {
            return false;
        }

        if (relationship.from_person_id == from_person_id && relationship.to_person_id == to_person_id) {
            return true;
        }

        if (relationship_type_is_symmetric(relationship_type)) {
            return relationship.from_person_id == to_person_id
                && relationship.to_person_id == from_person_id;
        }

        if (relationship.relationship_type == RelationshipType::Parent) {
            return relationship.from_person_id == to_person_id
                && relationship.to_person_id == from_person_id;
        }

        return false;
    });
}

std::vector<Person> make_mock_persons(int tree_id)
{
    switch (tree_id) {
    case 1:
        return {
            { 110, "Ava", "Claire", "Stone", "1988", "", "", 120.f, 130.f },
            { 111, "Liam", "", "Brooks", "1986", "", "", 340.f, 150.f },
            { 112, "Mia", "", "Chen", "1990", "", "", 230.f, 320.f }
        };
    case 2:
        return {
            { 210, "Nora", "", "Fields", "1978", "", "", 140.f, 120.f },
            { 211, "Ethan", "", "Cole", "1981", "", "", 360.f, 140.f },
            { 212, "June", "", "Patel", "1992", "", "", 250.f, 300.f },
            { 213, "Iris", "", "Young", "1994", "", "", 500.f, 260.f }
        };
    case 3:
        return {
            { 310, "Helen", "", "Reed", "1928", "2004", "", 150.f, 120.f },
            { 311, "Robert", "", "Reed", "1925", "1998", "", 360.f, 120.f },
            { 312, "Anna", "", "Reed", "1954", "2016", "", 255.f, 290.f }
        };
    default:
        return {
            { 10, "Elena", "", "Hart", "1946", "2019", "", 130.f, 110.f },
            { 11, "David", "Alexander", "Hart", "1943", "2015", "", 360.f, 110.f },
            { 12, "Maya", "", "Hart", "1971", "", "", 120.f, 300.f },
            { 13, "Jonah", "", "Hart", "1974", "", "", 360.f, 300.f }
        };
    }
}

std::vector<Relationship> make_mock_relationships(int tree_id)
{
    switch (tree_id) {
    case 1:
        return {
            { 1010, 110, 111, RelationshipType::Friend },
            { 1011, 111, 112, RelationshipType::Sibling }
        };
    case 2:
        return {
            { 2010, 210, 211, RelationshipType::Spouse },
            { 2011, 210, 212, RelationshipType::Parent },
            { 2012, 211, 212, RelationshipType::Parent },
            { 2013, 212, 213, RelationshipType::Friend }
        };
    case 3:
        return {
            { 3010, 310, 311, RelationshipType::Spouse },
            { 3011, 310, 312, RelationshipType::Parent },
            { 3012, 311, 312, RelationshipType::Parent }
        };
    default:
        return {
            { 10, 10, 11, RelationshipType::Spouse },
            { 11, 10, 12, RelationshipType::Parent },
            { 12, 11, 12, RelationshipType::Parent },
            { 13, 12, 13, RelationshipType::Sibling }
        };
    }
}

std::shared_ptr<slint::Model<slint::SharedString>> make_tree_name_model(const std::vector<Tree> &trees)
{
    std::vector<slint::SharedString> names;
    names.reserve(trees.size());

    for (const auto &tree : trees) {
        names.emplace_back(tree.name);
    }

    return std::make_shared<slint::VectorModel<slint::SharedString>>(std::move(names));
}

std::shared_ptr<slint::Model<int>> make_tree_id_model(const std::vector<Tree> &trees)
{
    std::vector<int> ids;
    ids.reserve(trees.size());

    for (const auto &tree : trees) {
        ids.push_back(tree.id);
    }

    return std::make_shared<slint::VectorModel<int>>(std::move(ids));
}

PersonNode to_person_node(const Person &person)
{
    return PersonNode {
        .id = person.id,
        .first_name = slint::SharedString(person.first_name),
        .middle_name = slint::SharedString(person.middle_name),
        .last_name = slint::SharedString(person.last_name),
        .full_name = slint::SharedString(format_person_name(person)),
        .birth_date = slint::SharedString(person.birth_date),
        .death_date = slint::SharedString(person.death_date),
        .dates_text = slint::SharedString(format_date_range(person.birth_date, person.death_date)),
        .x = person.x,
        .y = person.y,
    };
}

std::shared_ptr<slint::VectorModel<PersonNode>> make_person_model(const std::vector<Person> &persons)
{
    std::vector<PersonNode> nodes;
    nodes.reserve(persons.size());

    for (const auto &person : persons) {
        nodes.push_back(to_person_node(person));
    }

    return std::make_shared<slint::VectorModel<PersonNode>>(std::move(nodes));
}

RelationshipLine to_relationship_line(
    const Relationship &relationship,
    const Person &from_person,
    const Person &to_person,
    float canvas_offset_x,
    float canvas_offset_y,
    float canvas_zoom)
{
    const float connector_x = kCardWidth / 2.0f;
    const float top_connector_y = 0.0f;
    const float bottom_connector_y = kCardHeight;
    const bool is_parent_relationship = relationship.relationship_type == RelationshipType::Parent;
    const float start_world_x = from_person.x + connector_x;
    const float start_world_y = from_person.y + (is_parent_relationship ? bottom_connector_y : top_connector_y);
    const float end_world_x = to_person.x + connector_x;
    const float end_world_y = to_person.y + top_connector_y;
    const float control_1_world_x = start_world_x;
    const float control_2_world_x = end_world_x;
    const float control_1_world_y = is_parent_relationship
        ? start_world_y + std::max(32.0f, std::fabs(end_world_y - start_world_y) * 0.35f)
        : std::min(start_world_y, end_world_y) - std::max(28.0f, std::fabs(end_world_x - start_world_x) * 0.12f);
    const float control_2_world_y = is_parent_relationship
        ? end_world_y - std::max(32.0f, std::fabs(end_world_y - start_world_y) * 0.35f)
        : std::min(start_world_y, end_world_y) - std::max(28.0f, std::fabs(end_world_x - start_world_x) * 0.12f);
    const float start_x = start_world_x * canvas_zoom + canvas_offset_x;
    const float start_y = start_world_y * canvas_zoom + canvas_offset_y;
    const float control_1_x = control_1_world_x * canvas_zoom + canvas_offset_x;
    const float control_1_y = control_1_world_y * canvas_zoom + canvas_offset_y;
    const float control_2_x = control_2_world_x * canvas_zoom + canvas_offset_x;
    const float control_2_y = control_2_world_y * canvas_zoom + canvas_offset_y;
    const float end_x = end_world_x * canvas_zoom + canvas_offset_x;
    const float end_y = end_world_y * canvas_zoom + canvas_offset_y;
    const float min_x = std::min({ start_x, control_1_x, control_2_x, end_x });
    const float max_x = std::max({ start_x, control_1_x, control_2_x, end_x });
    const float min_y = std::min({ start_y, control_1_y, control_2_y, end_y });
    const float max_y = std::max({ start_y, control_1_y, control_2_y, end_y });
    const float hit_x = min_x - kRelationshipHitPadding;
    const float hit_y = min_y - kRelationshipHitPadding;
    const float hit_width = std::max(max_x - min_x, 1.0f) + kRelationshipHitPadding * 2.0f;
    const float hit_height = std::max(max_y - min_y, 1.0f) + kRelationshipHitPadding * 2.0f;
    const auto commands = "M " + std::to_string(start_x) + " " + std::to_string(start_y) +
                          " C " + std::to_string(control_1_x) + " " + std::to_string(control_1_y) +
                          " " + std::to_string(control_2_x) + " " + std::to_string(control_2_y) +
                          " " + std::to_string(end_x) + " " + std::to_string(end_y);

    return RelationshipLine {
        .id = relationship.id,
        .relationship_type = slint::SharedString(relationship_type_to_string(relationship.relationship_type)),
        .start_x = start_x,
        .start_y = start_y,
        .control_1_x = control_1_x,
        .control_1_y = control_1_y,
        .control_2_x = control_2_x,
        .control_2_y = control_2_y,
        .end_x = end_x,
        .end_y = end_y,
        .hit_x = hit_x,
        .hit_y = hit_y,
        .hit_width = hit_width,
        .hit_height = hit_height,
        .commands = slint::SharedString(commands),
    };
}

std::vector<RelationshipLine> build_relationship_line_data(
    const std::vector<Person> &persons,
    const std::vector<Relationship> &relationships,
    float canvas_offset_x,
    float canvas_offset_y,
    float canvas_zoom)
{
    std::vector<RelationshipLine> lines;
    lines.reserve(relationships.size());

    for (const auto &relationship : relationships) {
        const auto from_it = std::find_if(persons.begin(), persons.end(), [&relationship](const Person &person) {
            return person.id == relationship.from_person_id;
        });
        const auto to_it = std::find_if(persons.begin(), persons.end(), [&relationship](const Person &person) {
            return person.id == relationship.to_person_id;
        });

        if (from_it == persons.end() || to_it == persons.end()) {
            continue;
        }

        lines.push_back(to_relationship_line(relationship, *from_it, *to_it, canvas_offset_x, canvas_offset_y, canvas_zoom));
    }

    return lines;
}

std::shared_ptr<slint::VectorModel<RelationshipLine>> make_relationship_line_model(
    const std::vector<RelationshipLine> &relationship_lines)
{
    return std::make_shared<slint::VectorModel<RelationshipLine>>(relationship_lines);
}

float cubic_bezier_coordinate(float t, float p0, float p1, float p2, float p3)
{
    const float inverse_t = 1.0f - t;
    return inverse_t * inverse_t * inverse_t * p0
        + 3.0f * inverse_t * inverse_t * t * p1
        + 3.0f * inverse_t * t * t * p2
        + t * t * t * p3;
}

float point_to_segment_distance_sq(float click_x,
                                   float click_y,
                                   float start_x,
                                   float start_y,
                                   float end_x,
                                   float end_y)
{
    const float line_dx = end_x - start_x;
    const float line_dy = end_y - start_y;
    const float line_length_sq = line_dx * line_dx + line_dy * line_dy;

    if (line_length_sq < 1.0f) {
        const float midpoint_x = (start_x + end_x) * 0.5f;
        const float midpoint_y = (start_y + end_y) * 0.5f;
        const float delta_x = click_x - midpoint_x;
        const float delta_y = click_y - midpoint_y;
        return delta_x * delta_x + delta_y * delta_y;
    }

    const float projection = ((click_x - start_x) * line_dx + (click_y - start_y) * line_dy) / line_length_sq;
    const float projection_t = std::clamp(projection, 0.0f, 1.0f);
    const float nearest_x = start_x + projection_t * line_dx;
    const float nearest_y = start_y + projection_t * line_dy;
    const float delta_x = click_x - nearest_x;
    const float delta_y = click_y - nearest_y;
    return delta_x * delta_x + delta_y * delta_y;
}

float point_to_relationship_curve_distance_sq(float click_x,
                                              float click_y,
                                              const RelationshipLine &relationship_line)
{
    float best_distance_sq = std::numeric_limits<float>::max();
    float previous_x = relationship_line.start_x;
    float previous_y = relationship_line.start_y;

    for (int segment_index = 1; segment_index <= kRelationshipCurveHitSegments; ++segment_index) {
        const float t = static_cast<float>(segment_index) / static_cast<float>(kRelationshipCurveHitSegments);
        const float current_x = cubic_bezier_coordinate(
            t,
            relationship_line.start_x,
            relationship_line.control_1_x,
            relationship_line.control_2_x,
            relationship_line.end_x);
        const float current_y = cubic_bezier_coordinate(
            t,
            relationship_line.start_y,
            relationship_line.control_1_y,
            relationship_line.control_2_y,
            relationship_line.end_y);
        best_distance_sq = std::min(
            best_distance_sq,
            point_to_segment_distance_sq(click_x, click_y, previous_x, previous_y, current_x, current_y));
        previous_x = current_x;
        previous_y = current_y;
    }

    return best_distance_sq;
}

bool cards_overlap(float first_x,
                   float first_y,
                   float second_x,
                   float second_y,
                   float spacing)
{
    const float padded_first_left = first_x - spacing * 0.5f;
    const float padded_first_top = first_y - spacing * 0.5f;
    const float padded_first_right = first_x + kCardWidth + spacing * 0.5f;
    const float padded_first_bottom = first_y + kCardHeight + spacing * 0.5f;

    const float padded_second_left = second_x - spacing * 0.5f;
    const float padded_second_top = second_y - spacing * 0.5f;
    const float padded_second_right = second_x + kCardWidth + spacing * 0.5f;
    const float padded_second_bottom = second_y + kCardHeight + spacing * 0.5f;

    return padded_first_left < padded_second_right
        && padded_first_right > padded_second_left
        && padded_first_top < padded_second_bottom
        && padded_first_bottom > padded_second_top;
}

bool is_position_free(const std::vector<Person> &persons,
                      int moving_person_id,
                      float x,
                      float y,
                      float spacing)
{
    return std::none_of(persons.begin(), persons.end(), [&](const Person &other_person) {
        if (other_person.id == moving_person_id) {
            return false;
        }

        return cards_overlap(x, y, other_person.x, other_person.y, spacing);
    });
}

std::pair<float, float> find_nearest_free_position(const std::vector<Person> &persons,
                                                   int moving_person_id,
                                                   float attempted_x,
                                                   float attempted_y,
                                                   float fallback_x,
                                                   float fallback_y)
{
    if (is_position_free(persons, moving_person_id, attempted_x, attempted_y, kCardOverlapSpacing)) {
        return { attempted_x, attempted_y };
    }

    static constexpr std::array<std::pair<float, float>, 8> kSearchDirections = {{
        { 1.0f, 0.0f },
        { -1.0f, 0.0f },
        { 0.0f, 1.0f },
        { 0.0f, -1.0f },
        { 1.0f, 1.0f },
        { 1.0f, -1.0f },
        { -1.0f, 1.0f },
        { -1.0f, -1.0f },
    }};

    for (float radius = kDragResolveStep; radius <= kDragResolveMaxRadius; radius += kDragResolveStep) {
        for (const auto &[direction_x, direction_y] : kSearchDirections) {
            const float candidate_x = attempted_x + direction_x * radius;
            const float candidate_y = attempted_y + direction_y * radius;

            if (is_position_free(persons, moving_person_id, candidate_x, candidate_y, kCardOverlapSpacing)) {
                return { candidate_x, candidate_y };
            }
        }
    }

    return { fallback_x, fallback_y };
}

void apply_grid_layout(std::vector<Person> &persons)
{
    if (persons.empty()) {
        return;
    }

    const size_t column_count = static_cast<size_t>(std::ceil(std::sqrt(static_cast<float>(persons.size()))));

    for (size_t index = 0; index < persons.size(); ++index) {
        const size_t row = index / column_count;
        const size_t column = index % column_count;
        persons[index].x = kAutoLayoutOriginX + static_cast<float>(column) * kAutoLayoutHorizontalSpacing;
        persons[index].y = kAutoLayoutOriginY + static_cast<float>(row) * kAutoLayoutVerticalSpacing;
    }
}

void apply_parent_layer_layout(std::vector<Person> &persons, const std::vector<Relationship> &relationships)
{
    if (persons.empty()) {
        return;
    }

    std::map<int, size_t> person_index_by_id;
    for (size_t index = 0; index < persons.size(); ++index) {
        person_index_by_id.emplace(persons[index].id, index);
    }

    std::vector<int> incoming_parent_count(persons.size(), 0);
    std::vector<std::vector<size_t>> child_indices(persons.size());
    bool has_parent_relationship = false;

    for (const auto &relationship : relationships) {
        if (relationship.relationship_type != RelationshipType::Parent) {
            continue;
        }

        const auto from_it = person_index_by_id.find(relationship.from_person_id);
        const auto to_it = person_index_by_id.find(relationship.to_person_id);
        if (from_it == person_index_by_id.end() || to_it == person_index_by_id.end()) {
            continue;
        }

        has_parent_relationship = true;
        child_indices[from_it->second].push_back(to_it->second);
        incoming_parent_count[to_it->second] += 1;
    }

    if (!has_parent_relationship) {
        apply_grid_layout(persons);
        return;
    }

    std::queue<size_t> ready_indices;
    for (size_t index = 0; index < persons.size(); ++index) {
        if (incoming_parent_count[index] == 0) {
            ready_indices.push(index);
        }
    }

    if (ready_indices.empty()) {
        apply_grid_layout(persons);
        return;
    }

    std::vector<int> layer_by_index(persons.size(), 0);
    size_t visited_count = 0;

    while (!ready_indices.empty()) {
        const size_t current_index = ready_indices.front();
        ready_indices.pop();
        visited_count += 1;

        for (const size_t child_index : child_indices[current_index]) {
            layer_by_index[child_index] = std::max(layer_by_index[child_index], layer_by_index[current_index] + 1);
            incoming_parent_count[child_index] -= 1;
            if (incoming_parent_count[child_index] == 0) {
                ready_indices.push(child_index);
            }
        }
    }

    if (visited_count != persons.size()) {
        apply_grid_layout(persons);
        return;
    }

    int max_layer = 0;
    for (const int layer : layer_by_index) {
        max_layer = std::max(max_layer, layer);
    }

    std::vector<std::vector<size_t>> indices_by_layer(static_cast<size_t>(max_layer) + 1);
    for (size_t index = 0; index < persons.size(); ++index) {
        indices_by_layer[static_cast<size_t>(layer_by_index[index])].push_back(index);
    }

    size_t widest_layer_size = 0;
    for (const auto &layer_indices : indices_by_layer) {
        widest_layer_size = std::max(widest_layer_size, layer_indices.size());
    }

    for (size_t layer_index = 0; layer_index < indices_by_layer.size(); ++layer_index) {
        const auto &layer_indices = indices_by_layer[layer_index];
        const float start_x = kAutoLayoutOriginX
            + static_cast<float>(widest_layer_size - layer_indices.size()) * kAutoLayoutHorizontalSpacing * 0.5f;
        const float y = kAutoLayoutOriginY + static_cast<float>(layer_index) * kAutoLayoutVerticalSpacing;

        for (size_t column_index = 0; column_index < layer_indices.size(); ++column_index) {
            Person &person = persons[layer_indices[column_index]];
            person.x = start_x + static_cast<float>(column_index) * kAutoLayoutHorizontalSpacing;
            person.y = y;
        }
    }
}

void sync_relationship_creation_ui(const AppWindow &app, RelationshipCreationStep step)
{
    app.set_relationship_creation_active(step != RelationshipCreationStep::Inactive);
    app.set_relationship_type_selection_visible(step == RelationshipCreationStep::ChooseRelationshipType);
    app.set_relationship_creation_step(static_cast<int>(step));
}

void sync_selected_tree(const AppWindow &app, const std::vector<Tree> &trees, int selected_tree_id)
{
    app.set_selected_tree_id(selected_tree_id);

    for (const auto &tree : trees) {
        if (tree.id == selected_tree_id) {
            app.set_selected_tree_name(slint::SharedString(tree.name));
            return;
        }
    }

    app.set_selected_tree_name(slint::SharedString());
}

}

int lal_run_application()
{
    auto app = AppWindow::create();
    AppState app_state {};
    const storage::SessionData restored_session = storage::load_session();
    const storage::UiStateData restored_ui_state = storage::load_ui_state();
    api::ApiClient api_client;
    api::AuthApi auth_api(api_client);
    api::TreeApi tree_api(api_client);
    api_client.set_access_token(restored_session.access_token);
    std::vector<Tree> trees;
    app_state.selected_tree_id = -1;
    std::map<int, std::vector<Person>> persons_by_tree;
    std::map<int, std::vector<Relationship>> relationships_by_tree;
    std::map<int, std::pair<float, float>> drag_start_positions;
    slint::Timer layout_save_timer;
    int next_person_id = 1;
    int next_relationship_id = 1;
    int next_temporary_person_id = -1;
    int next_temporary_relationship_id = -1;
    RelationshipCreationStep relationship_creation_step = RelationshipCreationStep::Inactive;
    int relationship_first_person_id = -1;
    int relationship_second_person_id = -1;
    auto persons_model = make_person_model({});
    std::vector<RelationshipLine> relationship_line_cache;
    auto relationship_lines_model = make_relationship_line_model(relationship_line_cache);
    slint::Timer relationship_error_timer;
    std::string tree_rename_name;

    const std::vector<Person> empty_persons;
    bool relationship_load_notice_logged = false;

    const auto save_tree_layout_for_tree = [&](int tree_id) {
        if (tree_id < 0) {
            return;
        }

        const auto persons_it = persons_by_tree.find(tree_id);
        if (persons_it == persons_by_tree.end()) {
            return;
        }

        static const std::vector<Relationship> empty_relationships;
        const auto rels_it = relationships_by_tree.find(tree_id);
        const auto &relationships_for_save = rels_it != relationships_by_tree.end()
            ? rels_it->second
            : empty_relationships;
        storage::save_tree_layout(
            tree_id,
            make_full_tree_layout(app_state, persons_it->second, relationships_for_save));
    };

    const std::function<void()> schedule_tree_layout_save = [&, app_weak = slint::ComponentWeakHandle<AppWindow>(app)]() {
        if (app_state.selected_tree_id < 0) {
            return;
        }

        const int tree_id_to_save = app_state.selected_tree_id;
        layout_save_timer.start(
            slint::TimerMode::SingleShot,
            std::chrono::milliseconds(450),
            [&, app_weak, tree_id_to_save]() {
                if (!app_weak.lock()) {
                    return;
                }

                save_tree_layout_for_tree(tree_id_to_save);
            });
    };

    const auto apply_tree_list_to_ui = [&]() {
        app->set_tree_names(make_tree_name_model(trees));
        app->set_tree_ids(make_tree_id_model(trees));
        sync_selected_tree(*app, trees, app_state.selected_tree_id);

        for (const auto &tree : trees) {
            if (tree.id == app_state.selected_tree_id) {
                app->set_tree_rename_name(slint::SharedString(tree.name));
                return;
            }
        }

        app->set_tree_rename_name(slint::SharedString());
    };

    const auto apply_current_tree_to_ui = [&]() {
        const auto persons_it = persons_by_tree.find(app_state.selected_tree_id);
        const auto relationships_it = relationships_by_tree.find(app_state.selected_tree_id);
        const auto &persons = persons_it != persons_by_tree.end() ? persons_it->second : empty_persons;
        static const std::vector<Relationship> empty_relationships;
        const auto &relationships = relationships_it != relationships_by_tree.end() ? relationships_it->second : empty_relationships;

        persons_model = make_person_model(persons);
        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);

        app->set_persons(persons_model);
        app->set_relationship_lines(relationship_lines_model);
        sync_selected_person(*app, persons, app_state.selected_person_id);
        clear_selected_relationship(*app);
        sync_inspector_draft(*app, nullptr);
        app->set_inspector_edit_mode(app_state.inspector_edit_mode);
        app->set_canvas_offset_x(app_state.canvas_offset_x);
        app->set_canvas_offset_y(app_state.canvas_offset_y);
        app->set_canvas_zoom(app_state.canvas_zoom);
        sync_relationship_creation_ui(*app, relationship_creation_step);
    };

    const auto load_tree_cache_from_backend = [&](int tree_id) -> bool {
        const auto persons_result = tree_api.fetch_persons(tree_id);
        if (!persons_result.ok) {
            std::cerr << "Tree API warning: failed to fetch persons for tree " << tree_id
                      << ": " << tree_error_message_from_result(*persons_result.error) << "\n";
            return false;
        }

        auto tree_persons = persons_from_records(persons_result.value);
        for (const auto &person : tree_persons) {
            next_person_id = std::max(next_person_id, person.id + 1);
        }

        const storage::TreeLayoutData stored_tree_layout = storage::load_tree_layout(tree_id);
        apply_saved_person_positions(tree_persons, stored_tree_layout);

        persons_by_tree[tree_id] = std::move(tree_persons);

        // If layout file contains relationships, restore them locally; otherwise clear and log notice once.
        if (!stored_tree_layout.relationships.empty()) {
            relationships_by_tree[tree_id] = stored_tree_layout.relationships;
        } else {
            relationships_by_tree[tree_id].clear();

            if (!relationship_load_notice_logged) {
                std::cerr << "Tree API notice: current OpenAPI schema has no relationship list endpoint; "
                             "relationships are left empty on backend tree load.\n";
                relationship_load_notice_logged = true;
            }
        }

        return true;
    };

    std::function<bool(int, bool)> activate_tree;
    activate_tree = [&](int tree_id, bool persist_ui_state) -> bool {
        if (!tree_exists(trees, tree_id)) {
            return false;
        }

        save_tree_layout_for_tree(app_state.selected_tree_id);
        layout_save_timer.stop();

        if (!load_tree_cache_from_backend(tree_id)) {
            return false;
        }

        app_state.selected_tree_id = tree_id;
        app_state.selected_person_id = -1;
        app_state.selected_relationship_id = -1;
        app_state.inspector_edit_mode = false;
        relationship_creation_step = RelationshipCreationStep::Inactive;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;

        const storage::TreeLayoutData restored_tree_layout = storage::load_tree_layout(app_state.selected_tree_id);
        app_state.canvas_zoom = clamp_canvas_zoom(static_cast<float>(restored_tree_layout.canvas.zoom));
        app_state.canvas_offset_x = static_cast<float>(restored_tree_layout.canvas.offset_x);
        app_state.canvas_offset_y = static_cast<float>(restored_tree_layout.canvas.offset_y);

        if (persist_ui_state) {
            storage::save_ui_state(storage::UiStateData {
                .last_tree_id = app_state.selected_tree_id,
            });
        }

        apply_tree_list_to_ui();
        apply_current_tree_to_ui();
        return true;
    };

    const auto clear_tree_ui = [&]() {
        app_state.selected_tree_id = -1;
        app_state.selected_person_id = -1;
        app_state.selected_relationship_id = -1;
        app_state.inspector_edit_mode = false;
        app_state.canvas_offset_x = 0.0f;
        app_state.canvas_offset_y = 0.0f;
        app_state.canvas_zoom = 1.0f;
        relationship_creation_step = RelationshipCreationStep::Inactive;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;
        apply_tree_list_to_ui();
        apply_current_tree_to_ui();
    };

    const auto refresh_tree_list_from_backend = [&](std::optional<int> preferred_tree_id) -> bool {
        const auto tree_list_result = tree_api.fetch_tree_list();
        if (!tree_list_result.ok) {
            std::cerr << "Tree API warning: failed to fetch tree list: "
                      << tree_error_message_from_result(*tree_list_result.error) << "\n";
            trees.clear();
            clear_tree_ui();
            return false;
        }

        trees = trees_from_summaries(tree_list_result.value);
        if (trees.empty()) {
            clear_tree_ui();
            return true;
        }

        int target_tree_id = -1;
        if (preferred_tree_id.has_value() && tree_exists(trees, *preferred_tree_id)) {
            target_tree_id = *preferred_tree_id;
        } else if (tree_exists(trees, app_state.selected_tree_id)) {
            target_tree_id = app_state.selected_tree_id;
        } else if (tree_exists(trees, restored_ui_state.last_tree_id)) {
            target_tree_id = restored_ui_state.last_tree_id;
        } else {
            target_tree_id = trees.front().id;
        }

        if (!activate_tree(target_tree_id, false)) {
            if (target_tree_id != trees.front().id) {
                return activate_tree(trees.front().id, false);
            }
            clear_tree_ui();
            return false;
        }

        return true;
    };

    apply_tree_list_to_ui();
    apply_current_tree_to_ui();
    app->set_is_edit_mode(app_state.is_edit_mode);
    app->set_login_email_text(slint::SharedString(restored_session.email));
    app->set_login_status_message("");
    app->set_register_status_message("");

    if (has_persisted_session(restored_session)) {
        refresh_tree_list_from_backend(restored_ui_state.last_tree_id);
        app->set_current_page(2);
    }

    app->on_login_clicked([app, &api_client, &auth_api, &refresh_tree_list_from_backend, &restored_ui_state](slint::SharedString email, slint::SharedString password) {
        const auto email_str = to_std_string(email);
        const auto password_str = to_std_string(password);

        std::cout << "login requested\n";
        app->set_login_status_message("");

        if (email_str.empty() || password_str.empty()) {
            app->set_login_status_code(1);
            return;
        }

        const auto login_result = auth_api.login(email_str, password_str);
        if (!login_result.ok) {
            app->set_login_status_code(0);
            app->set_login_status_message(slint::SharedString(login_error_message_from_result(*login_result.error)));
            return;
        }

        api_client.set_access_token(login_result.value.access_token);
        app->set_login_status_code(2);
        app->set_login_status_message("");
        app->set_login_password_text("");
        const storage::SessionData session {
            .access_token = login_result.value.access_token,
            .refresh_token = login_result.value.refresh_token,
            .email = email_str,
        };
        storage::save_session(session);
        refresh_tree_list_from_backend(restored_ui_state.last_tree_id);
        app->set_current_page(2);
    });

    app->on_register_clicked([app, &auth_api](slint::SharedString email, slint::SharedString password) {
        const auto email_str = to_std_string(email);
        const auto password_str = to_std_string(password);

        std::cout << "register requested\n";
        app->set_register_status_message("");

        if (email_str.empty() || password_str.empty()) {
            app->set_register_status_code(1);
            return;
        }

        const auto register_result = auth_api.register_user(email_str, password_str);
        if (!register_result.ok) {
            app->set_register_status_code(0);
            app->set_register_status_message(slint::SharedString(register_error_message_from_result(*register_result.error)));
            return;
        }

        app->set_register_status_code(2);
        app->set_register_status_message("");
    });

    app->on_logout_requested([app, &api_client, &auth_api, &restored_session]() {
        std::cout << "logout requested\n";

        // Call logout API if we have a refresh token
        if (!restored_session.refresh_token.empty()) {
            const auto logout_result = auth_api.logout(restored_session.refresh_token);
            if (!logout_result.ok) {
                std::cerr << "Logout API warning: failed to logout: "
                          << tree_error_message_from_result(*logout_result.error) << "\n";
                // Continue with local logout even if API fails
            }
        }

        // Clear session
        api_client.clear_access_token();
        storage::save_session(storage::SessionData{});

        // Clear UI state
        storage::save_ui_state(storage::UiStateData{});

        // Reset to login page
        app->set_current_page(0);
        app->set_login_email_text(slint::SharedString(restored_session.email)); // Keep email for convenience
        app->set_login_password_text("");
        app->set_login_status_message("");
    });

    app->on_add_tree_requested([&tree_api, &refresh_tree_list_from_backend, &trees]() {
        const std::string default_tree_name = "New Tree " + std::to_string(trees.size() + 1);
        const auto create_result = tree_api.create_tree(default_tree_name);
        if (!create_result.ok) {
            std::cerr << "Tree API warning: failed to create tree: "
                      << tree_error_message_from_result(*create_result.error) << "\n";
            return;
        }

        refresh_tree_list_from_backend(create_result.value.tree_id);
    });

    app->on_tree_rename_name_edited([&tree_rename_name](slint::SharedString new_name) {
        tree_rename_name = to_std_string(new_name);
    });

    app->on_rename_tree_requested([&app_state, &tree_api, &refresh_tree_list_from_backend, &tree_rename_name]() {
        if (app_state.selected_tree_id < 0) {
            return;
        }
        if (tree_rename_name.empty()) {
            return;
        }

        const auto update_result = tree_api.update_tree(app_state.selected_tree_id, tree_rename_name);
        if (!update_result.ok) {
            std::cerr << "Tree API warning: failed to rename tree: "
                      << tree_error_message_from_result(*update_result.error) << "\n";
            return;
        }

        refresh_tree_list_from_backend(app_state.selected_tree_id);
    });

    app->on_delete_tree_requested([&app, &app_state, &tree_api, &refresh_tree_list_from_backend]() {
        if (app_state.selected_tree_id < 0) {
            return;
        }

        const int deleted_tree_id = app_state.selected_tree_id;
        const auto delete_result = tree_api.delete_tree(deleted_tree_id);
        if (!delete_result.ok) {
            std::cerr << "Tree API warning: failed to delete tree: "
                      << tree_error_message_from_result(*delete_result.error) << "\n";
            return;
        }

        const auto app_data_directory = storage::default_app_data_directory();
        const auto tree_layout_path = storage::tree_layout_file_path(app_data_directory, deleted_tree_id);
        std::error_code remove_error;
        std::filesystem::remove(tree_layout_path, remove_error);
        if (remove_error) {
            std::cerr << "Persistence warning: failed to remove stale tree layout '"
                      << tree_layout_path.string() << "': " << remove_error.message() << "\n";
        }

        refresh_tree_list_from_backend(std::nullopt);
    });

    app->on_tree_selected([&activate_tree](int tree_id) {
        activate_tree(tree_id, true);
    });

    app->on_person_selected([app, &persons_by_tree, &app_state, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id](int person_id) {
        auto &persons = persons_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [person_id](const Person &person) {
            return person.id == person_id;
        });

        if (it == persons.end()) {
            return;
        }

        app_state.selected_person_id = it->id;
        app_state.selected_relationship_id = -1;
        sync_selected_person(*app, persons, app_state.selected_person_id);
        clear_selected_relationship(*app);
        if (!app_state.inspector_edit_mode) {
            sync_inspector_draft(*app, &(*it));
        }

        if (app_state.is_edit_mode && relationship_creation_step != RelationshipCreationStep::Inactive) {
            if (relationship_creation_step == RelationshipCreationStep::SelectFirstPerson) {
                relationship_first_person_id = person_id;
                relationship_second_person_id = -1;
                relationship_creation_step = RelationshipCreationStep::SelectSecondPerson;
            } else if (relationship_creation_step == RelationshipCreationStep::SelectSecondPerson) {
                if (person_id != relationship_first_person_id) {
                    relationship_second_person_id = person_id;
                    relationship_creation_step = RelationshipCreationStep::ChooseRelationshipType;
                }
            }

            sync_relationship_creation_ui(*app, relationship_creation_step);
            return;
        }

        if (!app_state.inspector_edit_mode) {
            app->set_inspector_edit_mode(app_state.inspector_edit_mode);
        }
    });

    app->on_relationship_selected([app, &persons_by_tree, &relationships_by_tree, &app_state](int relationship_id) {
        auto &persons = persons_by_tree[app_state.selected_tree_id];
        auto &relationships = relationships_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(relationships.begin(), relationships.end(), [relationship_id](const Relationship &relationship) {
            return relationship.id == relationship_id;
        });

        if (it == relationships.end()) {
            return;
        }

        app_state.selected_relationship_id = it->id;
        app_state.selected_person_id = -1;
        clear_selected_person(*app, !app_state.inspector_edit_mode);
        sync_selected_relationship(*app, persons, relationships, app_state.selected_relationship_id);
        if (!app_state.inspector_edit_mode) {
            app->set_inspector_edit_mode(app_state.inspector_edit_mode);
        }
    });

    app->on_relationship_hit_test_requested([app, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache](float click_x, float click_y) {
        const float threshold_sq = kRelationshipClickThreshold * kRelationshipClickThreshold;
        int best_id = -1;
        float best_distance_sq = std::numeric_limits<float>::max();

        for (const auto &relationship_line : relationship_line_cache) {
            if (click_x < relationship_line.hit_x || click_x > relationship_line.hit_x + relationship_line.hit_width
                || click_y < relationship_line.hit_y || click_y > relationship_line.hit_y + relationship_line.hit_height) {
                continue;
            }

            const float distance_sq = point_to_relationship_curve_distance_sq(
                click_x,
                click_y,
                relationship_line);

            if (distance_sq <= threshold_sq && distance_sq < best_distance_sq) {
                best_distance_sq = distance_sq;
                best_id = relationship_line.id;
            }
        }

        if (best_id >= 0) {
            auto &persons = persons_by_tree[app_state.selected_tree_id];
            auto &relationships = relationships_by_tree[app_state.selected_tree_id];
            const auto it = std::find_if(relationships.begin(), relationships.end(), [best_id](const Relationship &relationship) {
                return relationship.id == best_id;
            });

            if (it == relationships.end()) {
                app_state.selected_person_id = -1;
                app_state.selected_relationship_id = -1;
                clear_selected_person(*app, !app_state.inspector_edit_mode);
                clear_selected_relationship(*app);
                if (!app_state.inspector_edit_mode) {
                    app->set_inspector_edit_mode(app_state.inspector_edit_mode);
                }
                return;
            }

            app_state.selected_relationship_id = it->id;
            app_state.selected_person_id = -1;
            clear_selected_person(*app, !app_state.inspector_edit_mode);
            sync_selected_relationship(*app, persons, relationships, app_state.selected_relationship_id);
            if (!app_state.inspector_edit_mode) {
                app->set_inspector_edit_mode(app_state.inspector_edit_mode);
            }
            return;
        }

        app_state.selected_person_id = -1;
        app_state.selected_relationship_id = -1;
        clear_selected_person(*app, !app_state.inspector_edit_mode);
        clear_selected_relationship(*app);
        if (!app_state.inspector_edit_mode) {
            app->set_inspector_edit_mode(app_state.inspector_edit_mode);
        }
    });

    app->on_person_move_started([&persons_by_tree, &app_state, &drag_start_positions](int person_id) {
        if (app_state.selected_tree_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [person_id](const Person &person) {
            return person.id == person_id;
        });

        if (it == persons.end()) {
            return;
        }

        drag_start_positions[person_id] = { it->x, it->y };
    });

    app->on_person_moved([app, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model](int person_id, float x, float y) {
        if (!app_state.is_edit_mode) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        auto &relationships = relationships_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [person_id](const Person &person) {
            return person.id == person_id;
        });

        if (it == persons.end()) {
            return;
        }

        const auto row = static_cast<size_t>(std::distance(persons.begin(), it));
        it->x = x;
        it->y = y;
        persons_model->set_row_data(row, to_person_node(*it));
        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);
        app->set_relationship_lines(relationship_lines_model);

        if (app_state.selected_person_id == person_id) {
            sync_selected_person(*app, persons, app_state.selected_person_id);
        }
    });

    app->on_person_move_finished([app, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model, &drag_start_positions, &schedule_tree_layout_save](int person_id, float x, float y) {
        if (!app_state.is_edit_mode || app_state.selected_tree_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        auto &relationships = relationships_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [person_id](const Person &person) {
            return person.id == person_id;
        });

        if (it == persons.end()) {
            drag_start_positions.erase(person_id);
            return;
        }

        const auto start_position_it = drag_start_positions.find(person_id);
        const float fallback_x = start_position_it != drag_start_positions.end() ? start_position_it->second.first : it->x;
        const float fallback_y = start_position_it != drag_start_positions.end() ? start_position_it->second.second : it->y;
        const auto [resolved_x, resolved_y] = find_nearest_free_position(
            persons,
            person_id,
            x,
            y,
            fallback_x,
            fallback_y);

        const auto row = static_cast<size_t>(std::distance(persons.begin(), it));
        it->x = resolved_x;
        it->y = resolved_y;
        persons_model->set_row_data(row, to_person_node(*it));
        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);
        app->set_relationship_lines(relationship_lines_model);

        if (app_state.selected_person_id == person_id) {
            sync_selected_person(*app, persons, app_state.selected_person_id);
        }

        drag_start_positions.erase(person_id);
        schedule_tree_layout_save();
    });

    app->on_add_person_requested([app, &tree_api, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model, &next_temporary_person_id, &schedule_tree_layout_save]() {
        if (!app_state.is_edit_mode) {
            std::cout << "add person ignored (view mode)\n";
            return;
        }

        if (app_state.selected_tree_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        auto &relationships = relationships_by_tree[app_state.selected_tree_id];
        const float default_x = (260.0f - app_state.canvas_offset_x) / app_state.canvas_zoom;
        const float default_y = (190.0f - app_state.canvas_offset_y) / app_state.canvas_zoom;

        Person person {
            .id = next_temporary_person_id--,
            .first_name = "New",
            .middle_name = "",
            .last_name = "Person",
            .birth_date = "",
            .death_date = "",
            .description = "",
            .x = default_x,
            .y = default_y,
        };

        persons.push_back(person);
        persons_model->push_back(to_person_node(person));
        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);
        app->set_relationship_lines(relationship_lines_model);
        app_state.selected_person_id = person.id;
        sync_selected_person(*app, persons, app_state.selected_person_id);
        sync_inspector_draft(*app, &person);
        app_state.inspector_edit_mode = false;
        app->set_inspector_edit_mode(app_state.inspector_edit_mode);

        if (app_state.is_edit_mode) {
            app->set_selected_person_id(app_state.selected_person_id);
        }

        const auto create_result = tree_api.create_person(app_state.selected_tree_id, person_mutation_data_from_person(person));
        if (!create_result.ok) {
            std::cerr << "Tree API warning: failed to create person: "
                      << graph_mutation_error_message_from_result(*create_result.error) << "\n";
            persons.pop_back();
            persons_model->erase(persons_model->row_count() - 1);
            relationship_line_cache = build_relationship_line_data(
                persons,
                relationships,
                app_state.canvas_offset_x,
                app_state.canvas_offset_y,
                app_state.canvas_zoom);
            relationship_lines_model = make_relationship_line_model(relationship_line_cache);
            app->set_relationship_lines(relationship_lines_model);
            app_state.selected_person_id = -1;
            clear_selected_person(*app);
            sync_inspector_draft(*app, nullptr);
            return;
        }

        persons.back().id = create_result.value.person_id;
        persons_model->set_row_data(persons.size() - 1, to_person_node(persons.back()));
        app_state.selected_person_id = create_result.value.person_id;
        sync_selected_person(*app, persons, app_state.selected_person_id);
        sync_inspector_draft(*app, &persons.back());
        schedule_tree_layout_save();
    });

    app->on_relationship_creation_started([app, &app_state, &relationship_error_timer, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id]() {
        if (!app_state.is_edit_mode || app_state.selected_tree_id < 0) {
            if (!app_state.is_edit_mode) {
                std::cout << "relationship creation ignored (view mode)\n";
            }
            return;
        }

        relationship_error_timer.stop();
        app->set_relationship_error_message(slint::SharedString());
        relationship_creation_step = RelationshipCreationStep::SelectFirstPerson;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;
        sync_relationship_creation_ui(*app, relationship_creation_step);
    });

    app->on_canvas_selection_cleared([app, &app_state]() {
        app_state.selected_person_id = -1;
        app_state.selected_relationship_id = -1;
        clear_selected_person(*app, !app_state.inspector_edit_mode);
        clear_selected_relationship(*app);
        if (!app_state.inspector_edit_mode) {
            app->set_inspector_edit_mode(app_state.inspector_edit_mode);
        }
    });

    app->on_relationship_creation_canceled([app, &relationship_error_timer, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id]() {
        relationship_error_timer.stop();
        app->set_relationship_error_message(slint::SharedString());
        relationship_creation_step = RelationshipCreationStep::Inactive;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;
        sync_relationship_creation_ui(*app, relationship_creation_step);
    });

    app->on_relationship_type_selected([app, &tree_api, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache, &relationship_lines_model, &relationship_error_timer, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id, &next_temporary_relationship_id, &save_tree_layout_for_tree, &schedule_tree_layout_save](slint::SharedString relationship_type_value) {
        if (!app_state.is_edit_mode) {
            std::cout << "relationship creation ignored (view mode)\n";
            return;
        }

        if (relationship_creation_step != RelationshipCreationStep::ChooseRelationshipType || app_state.selected_tree_id < 0) {
            return;
        }

        if (relationship_first_person_id < 0 || relationship_second_person_id < 0 || relationship_first_person_id == relationship_second_person_id) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        auto &relationships = relationships_by_tree[app_state.selected_tree_id];
        const auto relationship_type_string = to_std_string(relationship_type_value);
        const auto relationship_type = relationship_type_from_string(relationship_type_string);

        if (!relationship_type.has_value()) {
            app->set_relationship_error_message(slint::SharedString("Invalid relationship type."));
            relationship_creation_step = RelationshipCreationStep::Inactive;
            relationship_first_person_id = -1;
            relationship_second_person_id = -1;
            sync_relationship_creation_ui(*app, relationship_creation_step);
            return;
        }

        if (relationship_exists(relationships, relationship_first_person_id, relationship_second_person_id, *relationship_type)) {
            app->set_relationship_error_message(slint::SharedString("Relationship already exists."));
            relationship_creation_step = RelationshipCreationStep::Inactive;
            relationship_first_person_id = -1;
            relationship_second_person_id = -1;
            sync_relationship_creation_ui(*app, relationship_creation_step);
            return;
        }

        relationships.push_back(Relationship {
            .id = next_temporary_relationship_id--,
            .from_person_id = relationship_first_person_id,
            .to_person_id = relationship_second_person_id,
            .relationship_type = *relationship_type,
        });

        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);
        app->set_relationship_lines(relationship_lines_model);

        relationship_creation_step = RelationshipCreationStep::Inactive;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;
        sync_relationship_creation_ui(*app, relationship_creation_step);

        const auto create_result = tree_api.create_relationship(
            relationships.back().from_person_id,
            relationships.back().to_person_id,
            relationship_type_string);
        if (!create_result.ok) {
            const std::string error_text = "Failed to create relationship: " + graph_mutation_error_message_from_result(*create_result.error);
            std::cerr << "Tree API warning: " << error_text << "\n";
            app->set_relationship_error_message(slint::SharedString(error_text));
            relationship_error_timer.start(
                slint::TimerMode::SingleShot,
                std::chrono::milliseconds(4500),
                [app]() {
                    app->set_relationship_error_message(slint::SharedString());
                });
            relationships.pop_back();
            relationship_line_cache = build_relationship_line_data(
                persons,
                relationships,
                app_state.canvas_offset_x,
                app_state.canvas_offset_y,
                app_state.canvas_zoom);
            relationship_lines_model = make_relationship_line_model(relationship_line_cache);
            app->set_relationship_lines(relationship_lines_model);
            return;
        }

        app->set_relationship_error_message(slint::SharedString());
        relationships.back().id = create_result.value.relationship_id;
        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);
        app->set_relationship_lines(relationship_lines_model);
        save_tree_layout_for_tree(app_state.selected_tree_id);
        schedule_tree_layout_save();
    });

    app->on_auto_layout_requested([app, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model, &schedule_tree_layout_save]() {
        if (!app_state.is_edit_mode) {
            return;
        }

        if (app_state.selected_tree_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        auto &relationships = relationships_by_tree[app_state.selected_tree_id];

        apply_parent_layer_layout(persons, relationships);

        for (size_t index = 0; index < persons.size(); ++index) {
            persons_model->set_row_data(index, to_person_node(persons[index]));
        }

        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);
        app->set_relationship_lines(relationship_lines_model);

        if (app_state.selected_person_id >= 0) {
            sync_selected_person(*app, persons, app_state.selected_person_id);
        }

        schedule_tree_layout_save();
    });

    app->on_inspector_edit_started([app, &persons_by_tree, &app_state]() {
        if (app_state.selected_person_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [&app_state](const Person &person) {
            return person.id == app_state.selected_person_id;
        });

        if (it == persons.end()) {
            return;
        }

        sync_inspector_draft(*app, &(*it));
        app_state.inspector_edit_mode = true;
        app->set_inspector_edit_mode(app_state.inspector_edit_mode);
    });

    app->on_inspector_edit_canceled([app, &persons_by_tree, &app_state]() {
        auto &persons = persons_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [&app_state](const Person &person) {
            return person.id == app_state.selected_person_id;
        });

        sync_inspector_draft(*app, it == persons.end() ? nullptr : &(*it));
        app_state.inspector_edit_mode = false;
        app->set_inspector_edit_mode(app_state.inspector_edit_mode);
    });

    app->on_inspector_edit_saved([app, &tree_api, &persons_by_tree, &app_state, &persons_model](void) {
        if (!app_state.is_edit_mode) {
            std::cout << "save person ignored (view mode)\n";
            return;
        }

        if (app_state.selected_person_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [&app_state](const Person &person) {
            return person.id == app_state.selected_person_id;
        });

        if (it == persons.end()) {
            return;
        }

        const Person previous_person = *it;
        const auto row = static_cast<size_t>(std::distance(persons.begin(), it));
        it->first_name = to_std_string(app->get_inspector_draft_first_name());
        it->middle_name = to_std_string(app->get_inspector_draft_middle_name());
        it->last_name = to_std_string(app->get_inspector_draft_last_name());
        it->birth_date = validate_and_normalize_date(to_std_string(app->get_inspector_draft_birth_date()));
        it->death_date = validate_and_normalize_date(to_std_string(app->get_inspector_draft_death_date()));
        it->description = to_std_string(app->get_inspector_draft_description());
        persons_model->set_row_data(row, to_person_node(*it));
        sync_selected_person(*app, persons, app_state.selected_person_id);
        sync_inspector_draft(*app, &(*it));

        const auto update_data = person_update_data_from_changes(previous_person, *it);
        if (!update_data.first_name.has_value()
            && !update_data.middle_name.has_value()
            && !update_data.last_name.has_value()
            && !update_data.birth_date.has_value()
            && !update_data.death_date.has_value()
            && !update_data.description.has_value()) {
            app_state.inspector_edit_mode = false;
            app->set_inspector_edit_mode(app_state.inspector_edit_mode);
            return;
        }

        const auto update_result = tree_api.update_person(it->id, update_data);
        if (!update_result.ok) {
            std::cerr << "Tree API warning: failed to update person: "
                      << graph_mutation_error_message_from_result(*update_result.error) << "\n";
            *it = previous_person;
            persons_model->set_row_data(row, to_person_node(*it));
            sync_selected_person(*app, persons, app_state.selected_person_id);
            sync_inspector_draft(*app, &(*it));
            return;
        }

        app_state.inspector_edit_mode = false;
        app->set_inspector_edit_mode(app_state.inspector_edit_mode);
    });

    app->on_delete_person_requested([app, &tree_api, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model, &save_tree_layout_for_tree, &schedule_tree_layout_save]() {
        if (!app_state.is_edit_mode) {
            std::cout << "delete person ignored (view mode)\n";
            return;
        }

        if (app_state.selected_tree_id < 0 || app_state.selected_person_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        auto &relationships = relationships_by_tree[app_state.selected_tree_id];
        const auto person_it = std::find_if(persons.begin(), persons.end(), [&app_state](const Person &person) {
            return person.id == app_state.selected_person_id;
        });

        if (person_it == persons.end()) {
            return;
        }

        const Person deleted_person = *person_it;
        std::vector<Relationship> deleted_relationships;
        for (const auto &relationship : relationships) {
            if (relationship.from_person_id == app_state.selected_person_id
                || relationship.to_person_id == app_state.selected_person_id) {
                deleted_relationships.push_back(relationship);
            }
        }
        const auto row = static_cast<size_t>(std::distance(persons.begin(), person_it));
        persons.erase(person_it);
        persons_model->erase(row);

        relationships.erase(
            std::remove_if(relationships.begin(), relationships.end(), [&app_state](const Relationship &relationship) {
                return relationship.from_person_id == app_state.selected_person_id
                    || relationship.to_person_id == app_state.selected_person_id;
            }),
            relationships.end());

        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);
        app->set_relationship_lines(relationship_lines_model);

        app_state.selected_person_id = -1;
        app_state.selected_relationship_id = -1;
        clear_selected_person(*app);
        clear_selected_relationship(*app);
        app_state.inspector_edit_mode = false;
        app->set_inspector_edit_mode(app_state.inspector_edit_mode);

        const auto delete_result = tree_api.delete_person(deleted_person.id);
        if (!delete_result.ok) {
            std::cerr << "Tree API warning: failed to delete person: "
                      << graph_mutation_error_message_from_result(*delete_result.error) << "\n";
            persons.insert(persons.begin() + static_cast<std::ptrdiff_t>(row), deleted_person);
            persons_model = make_person_model(persons);
            app->set_persons(persons_model);
            relationships.insert(relationships.end(), deleted_relationships.begin(), deleted_relationships.end());
            relationship_line_cache = build_relationship_line_data(
                persons,
                relationships,
                app_state.canvas_offset_x,
                app_state.canvas_offset_y,
                app_state.canvas_zoom);
            relationship_lines_model = make_relationship_line_model(relationship_line_cache);
            app->set_relationship_lines(relationship_lines_model);
            app_state.selected_person_id = deleted_person.id;
            sync_selected_person(*app, persons, app_state.selected_person_id);
            sync_inspector_draft(*app, &persons[row]);
            return;
        }

        save_tree_layout_for_tree(app_state.selected_tree_id);
        schedule_tree_layout_save();
    });

    app->on_delete_relationship_requested([app, &tree_api, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache, &relationship_lines_model, &save_tree_layout_for_tree, &schedule_tree_layout_save]() {
        if (!app_state.is_edit_mode) {
            std::cout << "delete relationship ignored (view mode)\n";
            return;
        }

        if (app_state.selected_tree_id < 0 || app_state.selected_relationship_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[app_state.selected_tree_id];
        auto &relationships = relationships_by_tree[app_state.selected_tree_id];
        const auto it = std::find_if(relationships.begin(), relationships.end(), [&app_state](const Relationship &relationship) {
            return relationship.id == app_state.selected_relationship_id;
        });

        if (it == relationships.end()) {
            return;
        }

        const Relationship deleted_relationship = *it;
        const auto deleted_row = static_cast<size_t>(std::distance(relationships.begin(), it));
        relationships.erase(it);
        relationship_line_cache = build_relationship_line_data(
            persons,
            relationships,
            app_state.canvas_offset_x,
            app_state.canvas_offset_y,
            app_state.canvas_zoom);
        relationship_lines_model = make_relationship_line_model(relationship_line_cache);
        app->set_relationship_lines(relationship_lines_model);
        app_state.selected_relationship_id = -1;
        clear_selected_relationship(*app);

        const auto delete_result = tree_api.delete_relationship(deleted_relationship.id);
        if (!delete_result.ok) {
            std::cerr << "Tree API warning: failed to delete relationship: "
                      << graph_mutation_error_message_from_result(*delete_result.error) << "\n";
            relationships.insert(relationships.begin() + static_cast<std::ptrdiff_t>(deleted_row), deleted_relationship);
            relationship_line_cache = build_relationship_line_data(
                persons,
                relationships,
                app_state.canvas_offset_x,
                app_state.canvas_offset_y,
                app_state.canvas_zoom);
            relationship_lines_model = make_relationship_line_model(relationship_line_cache);
            app->set_relationship_lines(relationship_lines_model);
            app_state.selected_relationship_id = deleted_relationship.id;
            sync_selected_relationship(*app, persons, relationships, app_state.selected_relationship_id);
            return;
        }

        save_tree_layout_for_tree(app_state.selected_tree_id);
        schedule_tree_layout_save();
    });

    app->on_inspector_draft_first_name_edited([app, &app_state](slint::SharedString value) {
        if (!app_state.is_edit_mode) {
            std::cout << "edit person ignored (view mode)\n";
            return;
        }

        app->set_inspector_draft_first_name(value);
    });

    app->on_inspector_draft_middle_name_edited([app, &app_state](slint::SharedString value) {
        if (!app_state.is_edit_mode) {
            std::cout << "edit person ignored (view mode)\n";
            return;
        }

        app->set_inspector_draft_middle_name(value);
    });

    app->on_inspector_draft_last_name_edited([app, &app_state](slint::SharedString value) {
        if (!app_state.is_edit_mode) {
            std::cout << "edit person ignored (view mode)\n";
            return;
        }

        app->set_inspector_draft_last_name(value);
    });

    app->on_inspector_draft_birth_date_edited([app, &app_state](slint::SharedString value) {
        if (!app_state.is_edit_mode) {
            std::cout << "edit person ignored (view mode)\n";
            return;
        }

        app->set_inspector_draft_birth_date(value);
    });

    app->on_inspector_draft_death_date_edited([app, &app_state](slint::SharedString value) {
        if (!app_state.is_edit_mode) {
            std::cout << "edit person ignored (view mode)\n";
            return;
        }

        app->set_inspector_draft_death_date(value);
    });

    app->on_inspector_draft_description_edited([app, &app_state](slint::SharedString value) {
        if (!app_state.is_edit_mode) {
            std::cout << "edit person ignored (view mode)\n";
            return;
        }

        app->set_inspector_draft_description(value);
    });

    app->on_toggle_edit_mode([app, &app_state, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id]() {
        app_state.is_edit_mode = !app_state.is_edit_mode;
        app->set_is_edit_mode(app_state.is_edit_mode);

        if (!app_state.is_edit_mode) {
            relationship_creation_step = RelationshipCreationStep::Inactive;
            relationship_first_person_id = -1;
            relationship_second_person_id = -1;
            sync_relationship_creation_ui(*app, relationship_creation_step);
        }
    });

    app->on_canvas_panned([app, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache, &relationship_lines_model, &schedule_tree_layout_save](float offset_x, float offset_y) {
        app_state.canvas_offset_x = offset_x;
        app_state.canvas_offset_y = offset_y;
        app->set_canvas_offset_x(app_state.canvas_offset_x);
        app->set_canvas_offset_y(app_state.canvas_offset_y);

        if (app_state.selected_tree_id >= 0) {
            relationship_line_cache = build_relationship_line_data(
                persons_by_tree[app_state.selected_tree_id],
                relationships_by_tree[app_state.selected_tree_id],
                app_state.canvas_offset_x,
                app_state.canvas_offset_y,
                app_state.canvas_zoom);
            relationship_lines_model = make_relationship_line_model(relationship_line_cache);
            app->set_relationship_lines(relationship_lines_model);
            schedule_tree_layout_save();
        }
    });

    app->on_canvas_zoom_changed([app, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache, &relationship_lines_model, &schedule_tree_layout_save](float zoom) {
        app_state.canvas_zoom = clamp_canvas_zoom(zoom);
        app->set_canvas_zoom(app_state.canvas_zoom);

        if (app_state.selected_tree_id >= 0) {
            relationship_line_cache = build_relationship_line_data(
                persons_by_tree[app_state.selected_tree_id],
                relationships_by_tree[app_state.selected_tree_id],
                app_state.canvas_offset_x,
                app_state.canvas_offset_y,
                app_state.canvas_zoom);
            relationship_lines_model = make_relationship_line_model(relationship_line_cache);
            app->set_relationship_lines(relationship_lines_model);
            schedule_tree_layout_save();
        }
    });

    app->run();

    if (app_state.selected_tree_id >= 0) {
        const auto persons_it = persons_by_tree.find(app_state.selected_tree_id);
        if (persons_it != persons_by_tree.end()) {
            const auto rels_it = relationships_by_tree.find(app_state.selected_tree_id);
            const auto &rels_for_save = rels_it != relationships_by_tree.end() ? rels_it->second : std::vector<Relationship>{};
            storage::save_tree_layout(
                app_state.selected_tree_id,
                make_full_tree_layout(app_state, persons_it->second, rels_for_save));
        }
    }

    return 0;
}

#ifndef __ANDROID__
int main()
{
    return lal_run_application();
}
#else
extern "C" int lal_android_main()
{
    return lal_run_application();
}
#endif
