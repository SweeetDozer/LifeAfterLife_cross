#include "app-window.h"
#include "app/app_state.h"
#include "models/person.h"
#include "models/relationship.h"
#include "ui_sync/sync.h"
#include "utils/formatters.h"

#include <algorithm>
#include <array>
#include <cmath>
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
    return { value.data(), value.size() };
}

std::string format_person_name(const Person &person)
{
    return format_person_name_parts(person.first_name, person.middle_name, person.last_name);
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

        return relationship_type_is_symmetric(relationship_type)
            && relationship.from_person_id == to_person_id
            && relationship.to_person_id == from_person_id;
    });
}

std::vector<Person> make_mock_persons(int tree_id)
{
    switch (tree_id) {
    case 1:
        return {
            { 110, "Ava", "Claire", "Stone", "1988", "", 120.f, 130.f },
            { 111, "Liam", "", "Brooks", "1986", "", 340.f, 150.f },
            { 112, "Mia", "", "Chen", "1990", "", 230.f, 320.f }
        };
    case 2:
        return {
            { 210, "Nora", "", "Fields", "1978", "", 140.f, 120.f },
            { 211, "Ethan", "", "Cole", "1981", "", 360.f, 140.f },
            { 212, "June", "", "Patel", "1992", "", 250.f, 300.f },
            { 213, "Iris", "", "Young", "1994", "", 500.f, 260.f }
        };
    case 3:
        return {
            { 310, "Helen", "", "Reed", "1928", "2004", 150.f, 120.f },
            { 311, "Robert", "", "Reed", "1925", "1998", 360.f, 120.f },
            { 312, "Anna", "", "Reed", "1954", "2016", 255.f, 290.f }
        };
    default:
        return {
            { 10, "Elena", "", "Hart", "1946", "2019", 130.f, 110.f },
            { 11, "David", "Alexander", "Hart", "1943", "2015", 360.f, 110.f },
            { 12, "Maya", "", "Hart", "1971", "", 120.f, 300.f },
            { 13, "Jonah", "", "Hart", "1974", "", 360.f, 300.f }
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

int main()
{
    auto app = AppWindow::create();
    AppState app_state {};
    const std::vector<Tree> trees = {
        { 0, "Family" },
        { 1, "Friends" },
        { 2, "Work" },
        { 3, "Archive" }
    };
    app_state.selected_tree_id = trees.empty() ? -1 : trees.front().id;
    std::map<int, std::vector<Person>> persons_by_tree;
    std::map<int, std::vector<Relationship>> relationships_by_tree;
    std::map<int, std::pair<float, float>> drag_start_positions;
    int next_person_id = 1;
    int next_relationship_id = 1;

    for (const auto &tree : trees) {
        auto tree_persons = make_mock_persons(tree.id);
        auto tree_relationships = make_mock_relationships(tree.id);

        for (const auto &person : tree_persons) {
            next_person_id = std::max(next_person_id, person.id + 1);
        }

        for (const auto &relationship : tree_relationships) {
            next_relationship_id = std::max(next_relationship_id, relationship.id + 1);
        }

        persons_by_tree.emplace(tree.id, std::move(tree_persons));
        relationships_by_tree.emplace(tree.id, std::move(tree_relationships));
    }

    std::vector<Person> &persons = persons_by_tree[app_state.selected_tree_id];
    RelationshipCreationStep relationship_creation_step = RelationshipCreationStep::Inactive;
    int relationship_first_person_id = -1;
    int relationship_second_person_id = -1;
    auto persons_model = make_person_model(persons);
    auto relationship_line_cache = build_relationship_line_data(
        persons,
        relationships_by_tree[app_state.selected_tree_id],
        app_state.canvas_offset_x,
        app_state.canvas_offset_y,
        app_state.canvas_zoom);
    auto relationship_lines_model = make_relationship_line_model(relationship_line_cache);

    app->set_tree_names(make_tree_name_model(trees));
    app->set_tree_ids(make_tree_id_model(trees));
    sync_selected_tree(*app, trees, app_state.selected_tree_id);
    app->set_persons(persons_model);
    app->set_relationship_lines(relationship_lines_model);
    sync_selected_person(*app, persons, app_state.selected_person_id);
    clear_selected_relationship(*app);
    sync_inspector_draft(*app, nullptr);
    app->set_inspector_edit_mode(app_state.inspector_edit_mode);
    app->set_is_edit_mode(app_state.is_edit_mode);
    sync_relationship_creation_ui(*app, relationship_creation_step);
    app->set_canvas_offset_x(app_state.canvas_offset_x);
    app->set_canvas_offset_y(app_state.canvas_offset_y);
    app->set_canvas_zoom(app_state.canvas_zoom);

    app->on_login_clicked([app](slint::SharedString email, slint::SharedString password) {
        const auto email_str = to_std_string(email);
        const auto password_str = to_std_string(password);

        std::cout << "login requested\n";

        if (email_str.empty() || password_str.empty()) {
            app->set_login_status_code(1);
            return;
        }

        app->set_login_status_code(2);
        app->set_current_page(2);
    });

    app->on_register_clicked([app](slint::SharedString email, slint::SharedString password) {
        const auto email_str = to_std_string(email);
        const auto password_str = to_std_string(password);

        std::cout << "register requested\n";

        if (email_str.empty() || password_str.empty()) {
            app->set_register_status_code(1);
            return;
        }

        app->set_register_status_code(2);
    });

    app->on_tree_selected([app, trees, &app_state, &persons_by_tree, &relationships_by_tree, &persons_model, &relationship_line_cache, &relationship_lines_model, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id](int tree_id) {
        for (const auto &tree : trees) {
            if (tree.id == tree_id) {
                app_state.selected_tree_id = tree.id;
                app_state.selected_person_id = -1;
                app_state.selected_relationship_id = -1;
                auto &persons = persons_by_tree[app_state.selected_tree_id];
                auto &relationships = relationships_by_tree[app_state.selected_tree_id];
                app_state.inspector_edit_mode = false;
                relationship_creation_step = RelationshipCreationStep::Inactive;
                relationship_first_person_id = -1;
                relationship_second_person_id = -1;

                sync_selected_tree(*app, trees, app_state.selected_tree_id);
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
                sync_relationship_creation_ui(*app, relationship_creation_step);
                return;
            }
        }
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

    app->on_person_move_finished([app, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model, &drag_start_positions](int person_id, float x, float y) {
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
    });

    app->on_add_person_requested([app, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model, &next_person_id]() {
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
            .id = next_person_id++,
            .first_name = "New",
            .middle_name = "",
            .last_name = "Person",
            .birth_date = "",
            .death_date = "",
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
    });

    app->on_relationship_creation_started([app, &app_state, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id]() {
        if (!app_state.is_edit_mode || app_state.selected_tree_id < 0) {
            if (!app_state.is_edit_mode) {
                std::cout << "relationship creation ignored (view mode)\n";
            }
            return;
        }

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

    app->on_relationship_creation_canceled([app, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id]() {
        relationship_creation_step = RelationshipCreationStep::Inactive;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;
        sync_relationship_creation_ui(*app, relationship_creation_step);
    });

    app->on_relationship_type_selected([app, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache, &relationship_lines_model, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id, &next_relationship_id](slint::SharedString relationship_type_value) {
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

        if (!relationship_type.has_value() || relationship_exists(relationships, relationship_first_person_id, relationship_second_person_id, *relationship_type)) {
            relationship_creation_step = RelationshipCreationStep::Inactive;
            relationship_first_person_id = -1;
            relationship_second_person_id = -1;
            sync_relationship_creation_ui(*app, relationship_creation_step);
            return;
        }

        relationships.push_back(Relationship {
            .id = next_relationship_id++,
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
    });

    app->on_auto_layout_requested([app, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model]() {
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

    app->on_inspector_edit_saved([app, &persons_by_tree, &app_state, &persons_model](void) {
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

        const auto row = static_cast<size_t>(std::distance(persons.begin(), it));
        it->first_name = to_std_string(app->get_inspector_draft_first_name());
        it->middle_name = to_std_string(app->get_inspector_draft_middle_name());
        it->last_name = to_std_string(app->get_inspector_draft_last_name());
        it->birth_date = to_std_string(app->get_inspector_draft_birth_date());
        it->death_date = to_std_string(app->get_inspector_draft_death_date());
        persons_model->set_row_data(row, to_person_node(*it));
        sync_selected_person(*app, persons, app_state.selected_person_id);
        sync_inspector_draft(*app, &(*it));
        app_state.inspector_edit_mode = false;
        app->set_inspector_edit_mode(app_state.inspector_edit_mode);
    });

    app->on_delete_person_requested([app, &persons_by_tree, &relationships_by_tree, &app_state, &persons_model, &relationship_line_cache, &relationship_lines_model]() {
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
    });

    app->on_delete_relationship_requested([app, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache, &relationship_lines_model]() {
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

    app->on_canvas_panned([app, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache, &relationship_lines_model](float offset_x, float offset_y) {
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
        }
    });

    app->on_canvas_zoom_changed([app, &persons_by_tree, &relationships_by_tree, &app_state, &relationship_line_cache, &relationship_lines_model](float zoom) {
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
        }
    });

    app->run();
    return 0;
}
