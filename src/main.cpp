#include "app-window.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <iostream>
#include <memory>
#include <vector>

namespace {

struct Tree {
    int id;
    std::string name;
};

struct Person {
    int id;
    std::string first_name;
    std::string middle_name;
    std::string last_name;
    std::string birth_date;
    std::string death_date;
    float x;
    float y;
};

struct Relationship {
    int id;
    int from_person_id;
    int to_person_id;
    std::string relationship_type;
};

enum class RelationshipCreationStep {
    Inactive,
    SelectFirstPerson,
    SelectSecondPerson,
    ChooseRelationshipType,
};

constexpr float kCardWidth = 232.0f;
constexpr float kCardHeight = 132.0f;
constexpr float kRelationshipHitPadding = 8.0f;

std::string to_std_string(const slint::SharedString &value)
{
    return { value.data(), value.size() };
}

std::string format_date_range(const std::string &birth_date, const std::string &death_date)
{
    if (birth_date.empty() && death_date.empty()) {
        return "";
    }

    if (birth_date.empty()) {
        return death_date;
    }

    if (!death_date.empty()) {
        return birth_date + " - " + death_date;
    }

    return birth_date;
}

std::string format_person_name(const Person &person)
{
    std::string result;

    auto append_part = [&result](const std::string &part) {
        if (part.empty()) {
            return;
        }

        if (!result.empty()) {
            result += ' ';
        }

        result += part;
    };

    append_part(person.first_name);
    append_part(person.middle_name);
    append_part(person.last_name);
    return result;
}

float clamp_canvas_zoom(float zoom)
{
    return std::clamp(zoom, 0.5f, 2.5f);
}

float point_to_segment_distance(float point_x,
                                float point_y,
                                float start_x,
                                float start_y,
                                float end_x,
                                float end_y)
{
    const float segment_dx = end_x - start_x;
    const float segment_dy = end_y - start_y;
    const float segment_length_squared = segment_dx * segment_dx + segment_dy * segment_dy;

    if (segment_length_squared <= 0.0001f) {
        const float dx = point_x - start_x;
        const float dy = point_y - start_y;
        return std::sqrt(dx * dx + dy * dy);
    }

    const float projection = ((point_x - start_x) * segment_dx + (point_y - start_y) * segment_dy) / segment_length_squared;
    const float clamped_projection = std::clamp(projection, 0.0f, 1.0f);
    const float closest_x = start_x + clamped_projection * segment_dx;
    const float closest_y = start_y + clamped_projection * segment_dy;
    const float dx = point_x - closest_x;
    const float dy = point_y - closest_y;
    return std::sqrt(dx * dx + dy * dy);
}

bool relationship_type_is_symmetric(const std::string &relationship_type)
{
    return relationship_type == "spouse" || relationship_type == "sibling" || relationship_type == "friend";
}

bool relationship_exists(const std::vector<Relationship> &relationships,
                         int from_person_id,
                         int to_person_id,
                         const std::string &relationship_type)
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
            { 1010, 110, 111, "friend" },
            { 1011, 111, 112, "sibling" }
        };
    case 2:
        return {
            { 2010, 210, 211, "spouse" },
            { 2011, 210, 212, "parent" },
            { 2012, 211, 212, "parent" },
            { 2013, 212, 213, "friend" }
        };
    case 3:
        return {
            { 3010, 310, 311, "spouse" },
            { 3011, 310, 312, "parent" },
            { 3012, 311, 312, "parent" }
        };
    default:
        return {
            { 10, 10, 11, "spouse" },
            { 11, 10, 12, "parent" },
            { 12, 11, 12, "parent" },
            { 13, 12, 13, "sibling" }
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
    const float start_x = (from_person.x + kCardWidth / 2.0f) * canvas_zoom + canvas_offset_x;
    const float start_y = (from_person.y + kCardHeight / 2.0f) * canvas_zoom + canvas_offset_y;
    const float end_x = (to_person.x + kCardWidth / 2.0f) * canvas_zoom + canvas_offset_x;
    const float end_y = (to_person.y + kCardHeight / 2.0f) * canvas_zoom + canvas_offset_y;
    const float hit_x = std::min(start_x, end_x) - kRelationshipHitPadding;
    const float hit_y = std::min(start_y, end_y) - kRelationshipHitPadding;
    const float hit_width = std::max(std::fabs(end_x - start_x), 1.0f) + kRelationshipHitPadding * 2.0f;
    const float hit_height = std::max(std::fabs(end_y - start_y), 1.0f) + kRelationshipHitPadding * 2.0f;
    const auto commands = "M " + std::to_string(start_x) + " " + std::to_string(start_y) +
                          " L " + std::to_string(end_x) + " " + std::to_string(end_y);

    return RelationshipLine {
        .id = relationship.id,
        .relationship_type = slint::SharedString(relationship.relationship_type),
        .start_x = start_x,
        .start_y = start_y,
        .end_x = end_x,
        .end_y = end_y,
        .hit_x = hit_x,
        .hit_y = hit_y,
        .hit_width = hit_width,
        .hit_height = hit_height,
        .commands = slint::SharedString(commands),
    };
}

std::shared_ptr<slint::VectorModel<RelationshipLine>> make_relationship_line_model(
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

    return std::make_shared<slint::VectorModel<RelationshipLine>>(std::move(lines));
}

void sync_relationship_creation_ui(const AppWindow &app, RelationshipCreationStep step)
{
    app.set_relationship_creation_active(step != RelationshipCreationStep::Inactive);
    app.set_relationship_type_selection_visible(step == RelationshipCreationStep::ChooseRelationshipType);

    switch (step) {
    case RelationshipCreationStep::SelectFirstPerson:
        app.set_relationship_creation_helper_text("Select first person");
        break;
    case RelationshipCreationStep::SelectSecondPerson:
        app.set_relationship_creation_helper_text("Select second person");
        break;
    case RelationshipCreationStep::ChooseRelationshipType:
        app.set_relationship_creation_helper_text("Choose relationship type");
        break;
    case RelationshipCreationStep::Inactive:
    default:
        app.set_relationship_creation_helper_text("");
        break;
    }
}

void sync_selected_person_details(const AppWindow &app, const Person &person)
{
    app.set_selected_person_first_name(slint::SharedString(person.first_name));
    app.set_selected_person_middle_name(slint::SharedString(person.middle_name));
    app.set_selected_person_last_name(slint::SharedString(person.last_name));
    app.set_selected_person_full_name(slint::SharedString(format_person_name(person)));
    app.set_selected_person_birth_date(slint::SharedString(person.birth_date));
    app.set_selected_person_death_date(slint::SharedString(person.death_date));
    app.set_selected_person_dates_text(slint::SharedString(format_date_range(person.birth_date, person.death_date)));
}

void sync_inspector_draft(const AppWindow &app, const Person *person)
{
    if (!person) {
        app.set_inspector_draft_first_name(slint::SharedString());
        app.set_inspector_draft_middle_name(slint::SharedString());
        app.set_inspector_draft_last_name(slint::SharedString());
        app.set_inspector_draft_birth_date(slint::SharedString());
        app.set_inspector_draft_death_date(slint::SharedString());
        return;
    }

    app.set_inspector_draft_first_name(slint::SharedString(person->first_name));
    app.set_inspector_draft_middle_name(slint::SharedString(person->middle_name));
    app.set_inspector_draft_last_name(slint::SharedString(person->last_name));
    app.set_inspector_draft_birth_date(slint::SharedString(person->birth_date));
    app.set_inspector_draft_death_date(slint::SharedString(person->death_date));
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

void sync_selected_person(const AppWindow &app, const std::vector<Person> &persons, int selected_person_id)
{
    app.set_selected_person_id(selected_person_id);

    for (const auto &person : persons) {
        if (person.id == selected_person_id) {
            sync_selected_person_details(app, person);
            return;
        }
    }

    app.set_selected_person_first_name(slint::SharedString());
    app.set_selected_person_middle_name(slint::SharedString());
    app.set_selected_person_last_name(slint::SharedString());
    app.set_selected_person_full_name(slint::SharedString());
    app.set_selected_person_birth_date(slint::SharedString());
    app.set_selected_person_death_date(slint::SharedString());
    app.set_selected_person_dates_text(slint::SharedString());
}

void clear_selected_person(const AppWindow &app)
{
    app.set_selected_person_id(-1);
    app.set_selected_person_first_name(slint::SharedString());
    app.set_selected_person_middle_name(slint::SharedString());
    app.set_selected_person_last_name(slint::SharedString());
    app.set_selected_person_full_name(slint::SharedString());
    app.set_selected_person_birth_date(slint::SharedString());
    app.set_selected_person_death_date(slint::SharedString());
    app.set_selected_person_dates_text(slint::SharedString());
    sync_inspector_draft(app, nullptr);
}

void clear_selected_relationship(const AppWindow &app)
{
    app.set_selected_relationship_id(-1);
    app.set_selected_relationship_type(slint::SharedString());
    app.set_selected_relationship_from_name(slint::SharedString());
    app.set_selected_relationship_to_name(slint::SharedString());
    app.set_selected_relationship_summary(slint::SharedString());
}

void sync_selected_relationship(const AppWindow &app,
                                const std::vector<Person> &persons,
                                const std::vector<Relationship> &relationships,
                                int selected_relationship_id)
{
    app.set_selected_relationship_id(selected_relationship_id);

    const auto relationship_it = std::find_if(relationships.begin(), relationships.end(), [selected_relationship_id](const Relationship &relationship) {
        return relationship.id == selected_relationship_id;
    });

    if (relationship_it == relationships.end()) {
        clear_selected_relationship(app);
        return;
    }

    const auto from_it = std::find_if(persons.begin(), persons.end(), [relationship_it](const Person &person) {
        return person.id == relationship_it->from_person_id;
    });
    const auto to_it = std::find_if(persons.begin(), persons.end(), [relationship_it](const Person &person) {
        return person.id == relationship_it->to_person_id;
    });

    const auto from_name = from_it == persons.end() ? std::string() : format_person_name(*from_it);
    const auto to_name = to_it == persons.end() ? std::string() : format_person_name(*to_it);
    const auto summary = from_name + " -> " + relationship_it->relationship_type + " -> " + to_name;

    app.set_selected_relationship_type(slint::SharedString(relationship_it->relationship_type));
    app.set_selected_relationship_from_name(slint::SharedString(from_name));
    app.set_selected_relationship_to_name(slint::SharedString(to_name));
    app.set_selected_relationship_summary(slint::SharedString(summary));
}

int find_closest_relationship_id(const std::vector<Person> &persons,
                                 const std::vector<Relationship> &relationships,
                                 float point_x,
                                 float point_y,
                                 float canvas_offset_x,
                                 float canvas_offset_y,
                                 float canvas_zoom)
{
    constexpr float max_hit_distance = 14.0f;
    int best_relationship_id = -1;
    float best_distance = max_hit_distance;

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

        const float start_x = (from_it->x + kCardWidth / 2.0f) * canvas_zoom + canvas_offset_x;
        const float start_y = (from_it->y + kCardHeight / 2.0f) * canvas_zoom + canvas_offset_y;
        const float end_x = (to_it->x + kCardWidth / 2.0f) * canvas_zoom + canvas_offset_x;
        const float end_y = (to_it->y + kCardHeight / 2.0f) * canvas_zoom + canvas_offset_y;
        const float distance = point_to_segment_distance(point_x, point_y, start_x, start_y, end_x, end_y);

        if (distance <= best_distance) {
            best_distance = distance;
            best_relationship_id = relationship.id;
        }
    }

    return best_relationship_id;
}

}

int main()
{
    auto app = AppWindow::create();
    const std::vector<Tree> trees = {
        { 0, "Family" },
        { 1, "Friends" },
        { 2, "Work" },
        { 3, "Archive" }
    };
    int selected_tree_id = trees.empty() ? -1 : trees.front().id;
    std::map<int, std::vector<Person>> persons_by_tree;
    std::map<int, std::vector<Relationship>> relationships_by_tree;
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

    std::vector<Person> &persons = persons_by_tree[selected_tree_id];
    int selected_person_id = -1;
    int selected_relationship_id = -1;
    bool inspector_edit_mode = false;
    bool is_edit_mode = false;
    RelationshipCreationStep relationship_creation_step = RelationshipCreationStep::Inactive;
    int relationship_first_person_id = -1;
    int relationship_second_person_id = -1;
    float canvas_offset_x = 0.0f;
    float canvas_offset_y = 0.0f;
    float canvas_zoom = 1.0f;
    auto persons_model = make_person_model(persons);
    auto relationship_lines_model = make_relationship_line_model(
        persons,
        relationships_by_tree[selected_tree_id],
        canvas_offset_x,
        canvas_offset_y,
        canvas_zoom);

    app->set_tree_names(make_tree_name_model(trees));
    app->set_tree_ids(make_tree_id_model(trees));
    sync_selected_tree(*app, trees, selected_tree_id);
    app->set_persons(persons_model);
    app->set_relationship_lines(relationship_lines_model);
    sync_selected_person(*app, persons, selected_person_id);
    clear_selected_relationship(*app);
    sync_inspector_draft(*app, nullptr);
    app->set_inspector_edit_mode(inspector_edit_mode);
    app->set_is_edit_mode(is_edit_mode);
    sync_relationship_creation_ui(*app, relationship_creation_step);
    app->set_canvas_offset_x(canvas_offset_x);
    app->set_canvas_offset_y(canvas_offset_y);
    app->set_canvas_zoom(canvas_zoom);

    app->on_login_clicked([app](slint::SharedString email, slint::SharedString password) {
        const auto email_str = to_std_string(email);
        const auto password_str = to_std_string(password);

        std::cout << "Login clicked\n";
        std::cout << "Email: " << email_str << "\n";
        std::cout << "Password: " << password_str << "\n";

        if (email_str.empty() || password_str.empty()) {
            app->set_login_status_text("Please fill in both email and password.");
            return;
        }

        app->set_login_status_text("Login request is ready.");
        app->set_current_page(2);
    });

    app->on_register_clicked([app](slint::SharedString email, slint::SharedString password) {
        const auto email_str = to_std_string(email);
        const auto password_str = to_std_string(password);

        std::cout << "Register clicked\n";
        std::cout << "Email: " << email_str << "\n";
        std::cout << "Password: " << password_str << "\n";

        if (email_str.empty() || password_str.empty()) {
            app->set_register_status_text("Please fill in both email and password.");
            return;
        }

        app->set_register_status_text("Registration request is ready.");
    });

    app->on_tree_selected([app, trees, &selected_tree_id, &persons_by_tree, &relationships_by_tree, &selected_person_id, &selected_relationship_id, &persons_model, &relationship_lines_model, &inspector_edit_mode, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id, &canvas_offset_x, &canvas_offset_y, &canvas_zoom](int tree_id) {
        for (const auto &tree : trees) {
            if (tree.id == tree_id) {
                selected_tree_id = tree.id;
                selected_person_id = -1;
                selected_relationship_id = -1;
                auto &persons = persons_by_tree[selected_tree_id];
                auto &relationships = relationships_by_tree[selected_tree_id];
                inspector_edit_mode = false;
                relationship_creation_step = RelationshipCreationStep::Inactive;
                relationship_first_person_id = -1;
                relationship_second_person_id = -1;

                sync_selected_tree(*app, trees, selected_tree_id);
                persons_model = make_person_model(persons);
                relationship_lines_model = make_relationship_line_model(
                    persons,
                    relationships,
                    canvas_offset_x,
                    canvas_offset_y,
                    canvas_zoom);
                app->set_persons(persons_model);
                app->set_relationship_lines(relationship_lines_model);
                sync_selected_person(*app, persons, selected_person_id);
                clear_selected_relationship(*app);
                sync_inspector_draft(*app, nullptr);
                app->set_inspector_edit_mode(inspector_edit_mode);
                sync_relationship_creation_ui(*app, relationship_creation_step);
                return;
            }
        }
    });

    app->on_person_selected([app, &persons_by_tree, &selected_tree_id, &selected_person_id, &selected_relationship_id, &inspector_edit_mode, &is_edit_mode, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id](int person_id) {
        auto &persons = persons_by_tree[selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [person_id](const Person &person) {
            return person.id == person_id;
        });

        if (it == persons.end()) {
            return;
        }

        selected_person_id = it->id;
        selected_relationship_id = -1;
        sync_selected_person(*app, persons, selected_person_id);
        clear_selected_relationship(*app);
        sync_inspector_draft(*app, &(*it));

        if (is_edit_mode && relationship_creation_step != RelationshipCreationStep::Inactive) {
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

        inspector_edit_mode = false;
        app->set_inspector_edit_mode(inspector_edit_mode);
    });

    app->on_relationship_selected([app, &persons_by_tree, &relationships_by_tree, &selected_tree_id, &selected_person_id, &selected_relationship_id, &inspector_edit_mode, &canvas_offset_x, &canvas_offset_y, &canvas_zoom](float click_x, float click_y) {
        auto &persons = persons_by_tree[selected_tree_id];
        auto &relationships = relationships_by_tree[selected_tree_id];
        const auto relationship_id = find_closest_relationship_id(
            persons,
            relationships,
            click_x,
            click_y,
            canvas_offset_x,
            canvas_offset_y,
            canvas_zoom);
        const auto it = std::find_if(relationships.begin(), relationships.end(), [relationship_id](const Relationship &relationship) {
            return relationship.id == relationship_id;
        });

        if (it == relationships.end()) {
            return;
        }

        selected_relationship_id = it->id;
        selected_person_id = -1;
        clear_selected_person(*app);
        sync_selected_relationship(*app, persons, relationships, selected_relationship_id);
        inspector_edit_mode = false;
        app->set_inspector_edit_mode(inspector_edit_mode);
    });

    app->on_person_moved([app, &persons_by_tree, &relationships_by_tree, &selected_tree_id, &persons_model, &relationship_lines_model, &selected_person_id, &is_edit_mode, &canvas_offset_x, &canvas_offset_y, &canvas_zoom](int person_id, float x, float y) {
        if (!is_edit_mode) {
            return;
        }

        auto &persons = persons_by_tree[selected_tree_id];
        auto &relationships = relationships_by_tree[selected_tree_id];
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
        relationship_lines_model = make_relationship_line_model(
            persons,
            relationships,
            canvas_offset_x,
            canvas_offset_y,
            canvas_zoom);
        app->set_relationship_lines(relationship_lines_model);

        if (selected_person_id == person_id) {
            sync_selected_person(*app, persons, selected_person_id);
        }
    });

    app->on_add_person_requested([app, &persons_by_tree, &relationships_by_tree, &selected_tree_id, &persons_model, &relationship_lines_model, &selected_person_id, &canvas_offset_x, &canvas_offset_y, &canvas_zoom, &next_person_id, &is_edit_mode, &inspector_edit_mode]() {
        if (selected_tree_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[selected_tree_id];
        auto &relationships = relationships_by_tree[selected_tree_id];
        const float default_x = (260.0f - canvas_offset_x) / canvas_zoom;
        const float default_y = (190.0f - canvas_offset_y) / canvas_zoom;

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
        relationship_lines_model = make_relationship_line_model(
            persons,
            relationships,
            canvas_offset_x,
            canvas_offset_y,
            canvas_zoom);
        app->set_relationship_lines(relationship_lines_model);
        selected_person_id = person.id;
        sync_selected_person(*app, persons, selected_person_id);
        sync_inspector_draft(*app, &person);
        inspector_edit_mode = false;
        app->set_inspector_edit_mode(inspector_edit_mode);

        if (is_edit_mode) {
            app->set_selected_person_id(selected_person_id);
        }
    });

    app->on_relationship_creation_started([app, &selected_tree_id, &is_edit_mode, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id]() {
        if (!is_edit_mode || selected_tree_id < 0) {
            return;
        }

        relationship_creation_step = RelationshipCreationStep::SelectFirstPerson;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;
        sync_relationship_creation_ui(*app, relationship_creation_step);
    });

    app->on_canvas_selection_cleared([app, &selected_person_id, &selected_relationship_id, &inspector_edit_mode]() {
        selected_person_id = -1;
        selected_relationship_id = -1;
        clear_selected_person(*app);
        clear_selected_relationship(*app);
        inspector_edit_mode = false;
        app->set_inspector_edit_mode(inspector_edit_mode);
    });

    app->on_relationship_creation_canceled([app, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id]() {
        relationship_creation_step = RelationshipCreationStep::Inactive;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;
        sync_relationship_creation_ui(*app, relationship_creation_step);
    });

    app->on_relationship_type_selected([app, &persons_by_tree, &relationships_by_tree, &selected_tree_id, &relationship_lines_model, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id, &next_relationship_id, &canvas_offset_x, &canvas_offset_y, &canvas_zoom](slint::SharedString relationship_type_value) {
        if (relationship_creation_step != RelationshipCreationStep::ChooseRelationshipType || selected_tree_id < 0) {
            return;
        }

        if (relationship_first_person_id < 0 || relationship_second_person_id < 0 || relationship_first_person_id == relationship_second_person_id) {
            return;
        }

        auto &persons = persons_by_tree[selected_tree_id];
        auto &relationships = relationships_by_tree[selected_tree_id];
        const auto relationship_type = to_std_string(relationship_type_value);

        if (relationship_type.empty() || relationship_exists(relationships, relationship_first_person_id, relationship_second_person_id, relationship_type)) {
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
            .relationship_type = relationship_type,
        });

        relationship_lines_model = make_relationship_line_model(
            persons,
            relationships,
            canvas_offset_x,
            canvas_offset_y,
            canvas_zoom);
        app->set_relationship_lines(relationship_lines_model);

        relationship_creation_step = RelationshipCreationStep::Inactive;
        relationship_first_person_id = -1;
        relationship_second_person_id = -1;
        sync_relationship_creation_ui(*app, relationship_creation_step);
    });

    app->on_inspector_edit_started([app, &persons_by_tree, &selected_tree_id, &selected_person_id, &inspector_edit_mode]() {
        if (selected_person_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [selected_person_id](const Person &person) {
            return person.id == selected_person_id;
        });

        if (it == persons.end()) {
            return;
        }

        sync_inspector_draft(*app, &(*it));
        inspector_edit_mode = true;
        app->set_inspector_edit_mode(inspector_edit_mode);
    });

    app->on_inspector_edit_canceled([app, &persons_by_tree, &selected_tree_id, &selected_person_id, &inspector_edit_mode]() {
        auto &persons = persons_by_tree[selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [selected_person_id](const Person &person) {
            return person.id == selected_person_id;
        });

        sync_inspector_draft(*app, it == persons.end() ? nullptr : &(*it));
        inspector_edit_mode = false;
        app->set_inspector_edit_mode(inspector_edit_mode);
    });

    app->on_inspector_edit_saved([app, &persons_by_tree, &selected_tree_id, &persons_model, &selected_person_id, &inspector_edit_mode](void) {
        if (selected_person_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[selected_tree_id];
        const auto it = std::find_if(persons.begin(), persons.end(), [selected_person_id](const Person &person) {
            return person.id == selected_person_id;
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
        sync_selected_person(*app, persons, selected_person_id);
        sync_inspector_draft(*app, &(*it));
        inspector_edit_mode = false;
        app->set_inspector_edit_mode(inspector_edit_mode);
    });

    app->on_delete_person_requested([app, &persons_by_tree, &relationships_by_tree, &selected_tree_id, &selected_person_id, &selected_relationship_id, &persons_model, &relationship_lines_model, &inspector_edit_mode, &canvas_offset_x, &canvas_offset_y, &canvas_zoom]() {
        if (selected_tree_id < 0 || selected_person_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[selected_tree_id];
        auto &relationships = relationships_by_tree[selected_tree_id];
        const auto person_it = std::find_if(persons.begin(), persons.end(), [selected_person_id](const Person &person) {
            return person.id == selected_person_id;
        });

        if (person_it == persons.end()) {
            return;
        }

        const auto row = static_cast<size_t>(std::distance(persons.begin(), person_it));
        persons.erase(person_it);
        persons_model->erase(row);

        relationships.erase(
            std::remove_if(relationships.begin(), relationships.end(), [selected_person_id](const Relationship &relationship) {
                return relationship.from_person_id == selected_person_id
                    || relationship.to_person_id == selected_person_id;
            }),
            relationships.end());

        relationship_lines_model = make_relationship_line_model(
            persons,
            relationships,
            canvas_offset_x,
            canvas_offset_y,
            canvas_zoom);
        app->set_relationship_lines(relationship_lines_model);

        selected_person_id = -1;
        selected_relationship_id = -1;
        clear_selected_person(*app);
        clear_selected_relationship(*app);
        inspector_edit_mode = false;
        app->set_inspector_edit_mode(inspector_edit_mode);
    });

    app->on_delete_relationship_requested([app, &persons_by_tree, &relationships_by_tree, &selected_tree_id, &selected_relationship_id, &relationship_lines_model, &canvas_offset_x, &canvas_offset_y, &canvas_zoom]() {
        if (selected_tree_id < 0 || selected_relationship_id < 0) {
            return;
        }

        auto &persons = persons_by_tree[selected_tree_id];
        auto &relationships = relationships_by_tree[selected_tree_id];
        const auto it = std::find_if(relationships.begin(), relationships.end(), [selected_relationship_id](const Relationship &relationship) {
            return relationship.id == selected_relationship_id;
        });

        if (it == relationships.end()) {
            return;
        }

        relationships.erase(it);
        relationship_lines_model = make_relationship_line_model(
            persons,
            relationships,
            canvas_offset_x,
            canvas_offset_y,
            canvas_zoom);
        app->set_relationship_lines(relationship_lines_model);
        selected_relationship_id = -1;
        clear_selected_relationship(*app);
    });

    app->on_inspector_draft_first_name_edited([app](slint::SharedString value) {
        app->set_inspector_draft_first_name(value);
    });

    app->on_inspector_draft_middle_name_edited([app](slint::SharedString value) {
        app->set_inspector_draft_middle_name(value);
    });

    app->on_inspector_draft_last_name_edited([app](slint::SharedString value) {
        app->set_inspector_draft_last_name(value);
    });

    app->on_inspector_draft_birth_date_edited([app](slint::SharedString value) {
        app->set_inspector_draft_birth_date(value);
    });

    app->on_inspector_draft_death_date_edited([app](slint::SharedString value) {
        app->set_inspector_draft_death_date(value);
    });

    app->on_toggle_edit_mode([app, &is_edit_mode, &relationship_creation_step, &relationship_first_person_id, &relationship_second_person_id]() {
        is_edit_mode = !is_edit_mode;
        app->set_is_edit_mode(is_edit_mode);

        if (!is_edit_mode) {
            relationship_creation_step = RelationshipCreationStep::Inactive;
            relationship_first_person_id = -1;
            relationship_second_person_id = -1;
            sync_relationship_creation_ui(*app, relationship_creation_step);
        }
    });

    app->on_canvas_panned([app, &persons_by_tree, &relationships_by_tree, &selected_tree_id, &relationship_lines_model, &canvas_offset_x, &canvas_offset_y, &canvas_zoom](float offset_x, float offset_y) {
        canvas_offset_x = offset_x;
        canvas_offset_y = offset_y;
        app->set_canvas_offset_x(canvas_offset_x);
        app->set_canvas_offset_y(canvas_offset_y);

        if (selected_tree_id >= 0) {
            relationship_lines_model = make_relationship_line_model(
                persons_by_tree[selected_tree_id],
                relationships_by_tree[selected_tree_id],
                canvas_offset_x,
                canvas_offset_y,
                canvas_zoom);
            app->set_relationship_lines(relationship_lines_model);
        }
    });

    app->on_canvas_zoom_changed([app, &persons_by_tree, &relationships_by_tree, &selected_tree_id, &relationship_lines_model, &canvas_offset_x, &canvas_offset_y, &canvas_zoom](float zoom) {
        canvas_zoom = clamp_canvas_zoom(zoom);
        app->set_canvas_zoom(canvas_zoom);

        if (selected_tree_id >= 0) {
            relationship_lines_model = make_relationship_line_model(
                persons_by_tree[selected_tree_id],
                relationships_by_tree[selected_tree_id],
                canvas_offset_x,
                canvas_offset_y,
                canvas_zoom);
            app->set_relationship_lines(relationship_lines_model);
        }
    });

    app->run();
    return 0;
}
